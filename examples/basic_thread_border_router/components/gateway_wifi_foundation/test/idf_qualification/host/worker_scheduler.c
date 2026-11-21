#include "worker_scheduler.h"
#include <string.h>

static void *worker_main(void *arg)
{
    qual_worker_t *w=arg;
    pthread_mutex_lock(&w->mutex);
    for(;;) {
        while(!w->pending && !w->stopping) pthread_cond_wait(&w->cond,&w->mutex);
        if(w->stopping && !w->pending) break;
        wifi_storage_job_t job=w->job;
        w->pending=false; w->active=true;
        pthread_mutex_unlock(&w->mutex);
        bool accepted=wifi_recovery_worker_run(w->coordinator,&job);
        pthread_mutex_lock(&w->mutex);
        w->active=false; w->accepted=accepted; w->completed=true;
        pthread_cond_broadcast(&w->cond);
    }
    pthread_mutex_unlock(&w->mutex);
    return NULL;
}
bool qual_worker_start(qual_worker_t *w, wifi_recovery_t *c)
{
    if(!w || !c) return false;
    memset(w,0,sizeof(*w)); w->coordinator=c;
    if(pthread_mutex_init(&w->mutex,NULL)) return false;
    if(pthread_cond_init(&w->cond,NULL)) { pthread_mutex_destroy(&w->mutex); return false; }
    if(pthread_create(&w->thread,NULL,worker_main,w)) {
        pthread_cond_destroy(&w->cond); pthread_mutex_destroy(&w->mutex); return false;
    }
    w->started=true; return true;
}
bool qual_worker_submit(qual_worker_t *w, const wifi_storage_job_t *job)
{
    if(!w || !w->started || !job) return false;
    pthread_mutex_lock(&w->mutex);
    bool ok=!w->stopping && !w->pending && !w->active && !w->completed;
    if(ok) { w->job=*job; w->pending=true; pthread_cond_signal(&w->cond); }
    pthread_mutex_unlock(&w->mutex); return ok;
}
bool qual_worker_wait(qual_worker_t *w, bool *accepted)
{
    if(!w || !w->started || !accepted) return false;
    pthread_mutex_lock(&w->mutex);
    if(!w->pending && !w->active && !w->completed) { pthread_mutex_unlock(&w->mutex); return false; }
    while(!w->completed) pthread_cond_wait(&w->cond,&w->mutex);
    *accepted=w->accepted; w->completed=false; memset(&w->job,0,sizeof(w->job));
    pthread_mutex_unlock(&w->mutex); return true;
}
bool qual_worker_stop(qual_worker_t *w)
{
    if(!w || !w->started) return false;
    pthread_mutex_lock(&w->mutex); w->stopping=true; pthread_cond_signal(&w->cond);
    pthread_mutex_unlock(&w->mutex);
    if(pthread_join(w->thread,NULL)) return false;
    pthread_cond_destroy(&w->cond); pthread_mutex_destroy(&w->mutex);
    memset(w,0,sizeof(*w)); return true;
}
