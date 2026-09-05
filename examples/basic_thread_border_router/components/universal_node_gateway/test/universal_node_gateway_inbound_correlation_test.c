#include <assert.h>
#include <string.h>

#include "universal_node_gateway_rx.h"
#include "universal_node_gateway_pending.h"
#include "mesh_auth.h"
#include "transport.h"
#include "request_completion.h"
#include "control_frame_build.h"
#include "synchronization_protocol.h"
#include "freertos/FreeRTOS.h"

static unsigned s_completions;
static uint16_t s_sender = 0x1001u;
static TickType_t s_ticks;
int64_t esp_timer_get_time(void) { return 0; }
void node_identity_init(void) {}
uint16_t node_identity_get(void) { return 0x72efu; }
void security_root_init(void) {}
bool security_root_has_root(void) { return true; }
bool mesh_auth_init(uint16_t node_id) { return node_id == 0x72efu; }
bool mesh_session_init(uint16_t node_id) { return node_id == 0x72efu; }
bool mesh_session_is_initialized(void) { return true; }
bool mesh_auth_protect_outbound(uint8_t *f, size_t n, size_t c, size_t *out) { (void)f; (void)c; *out=n; return true; }
transport_result_t transport_send_result(const delivery_descriptor_t *d,const uint8_t *f,uint16_t n){(void)d;(void)f;(void)n;return TRANSPORT_RESULT_ADMITTED;}
bool request_completion_expected_response_command(uint8_t command, uint8_t *response_command) { if (!response_command) return false; *response_command = command; return command == CMD_RELAY_SET || command == CMD_QUERY_RESOURCE; }

mesh_auth_verify_status_t mesh_auth_verify_inbound_result(const uint8_t *f, size_t n, mesh_auth_verification_t *out) { *out=(mesh_auth_verification_t){.status=MESH_AUTH_VERIFY_AUTHENTICATED,.protected_payload_length=n,.authenticated_sender_node_id=s_sender}; (void)f; return MESH_AUTH_VERIFY_AUTHENTICATED; }
bool mesh_auth_control_frame_consistent(const uint8_t *f,size_t n,const mesh_auth_verification_t *v){(void)f;(void)n;(void)v;return true;}
bool address_resolver_observe_authenticated_thread_endpoint(uint16_t n,const uint8_t p[16]){(void)n;(void)p;return true;}
device_event_udev_parse_result_t device_event_delivery_parse_udev(const uint8_t*a,uint16_t b,device_event_delivery_envelope_t*c){(void)a;(void)b;(void)c;return DEVICE_EVENT_UDEV_NOT_UDEV;}
synchronization_usyn_parse_result_t synchronization_parse_usyn(const uint8_t*a,uint16_t b,runtime_state_update_t*c){(void)a;(void)b;(void)c;return SYNCHRONIZATION_USYN_NOT_USYN;}
bool control_frame_parse(const uint8_t *f,uint16_t n,control_frame_parsed_t *o){ if(n<28)return false; *o=(control_frame_parsed_t){.flags=f[1],.command=f[6],.request_id=(uint16_t)(f[10]<<8|f[11]),.transfer_id=(uint32_t)(f[16]<<24|f[17]<<16|f[18]<<8|f[19]),.logical_payload_length=(uint32_t)(f[20]<<24|f[21]<<16|f[22]<<8|f[23]),.fragment_offset=(uint32_t)(f[24]<<24|f[25]<<16|f[26]<<8|f[27]),.payload=(uint8_t*)f+28,.payload_len=(uint16_t)(n-28)};return true; }
bool control_frame_has_complete_header(uint16_t n){return n>=28;}
uint16_t control_frame_empty_frame_length(void){return 28;}
bool control_frame_build_response_fragment(uint8_t*f,uint16_t n,const control_frame_parsed_t*r,uint32_t a,uint32_t b,uint32_t c,control_frame_payload_writer_t*w){(void)r;(void)a;(void)b;(void)c;if(n<28)return false;w->data=f+28;w->capacity=n-28;w->frame_capacity=n;return true;}
uint32_t xTaskGetTickCount(void){return s_ticks;}
static void complete(const universal_node_gateway_completed_response_t *r,void*c){(void)r;(void)c;++s_completions;}
static void frame(uint8_t *f,uint16_t id,uint8_t cmd,uint32_t tid,uint32_t len,uint32_t off,const char*p,uint16_t pn){memset(f,0,64);f[0]=1;f[1]=2;f[6]=cmd;f[10]=id>>8;f[11]=id;f[16]=tid>>24;f[17]=tid>>16;f[18]=tid>>8;f[19]=tid;f[20]=len>>24;f[21]=len>>16;f[22]=len>>8;f[23]=len;f[24]=off>>24;f[25]=off>>16;f[26]=off>>8;f[27]=off;memcpy(f+28,p,pn);}
int main(void){uint8_t p[16]={0},f[64];assert(universal_node_gateway_register_response_consumer(complete,0)); gateway_pending_reset(); frame(f,1,9,0,0,0,"",0);assert(gateway_pending_register(1,9,s_sender,0)==GATEWAY_PENDING_REGISTERED);universal_node_gateway_handle_thread_rx(f,28,p);assert(s_completions==1&&gateway_pending_active_count()==0); universal_node_gateway_handle_thread_rx(f,28,p);assert(s_completions==1); gateway_pending_reset();frame(f,2,9,7,4,0,"ab",2);assert(gateway_pending_register(2,9,s_sender,0)==GATEWAY_PENDING_REGISTERED);universal_node_gateway_handle_thread_rx(f,30,p);assert(s_completions==1&&gateway_pending_active_count()==1);frame(f,2,9,7,4,2,"cd",2);universal_node_gateway_handle_thread_rx(f,30,p);assert(s_completions==2&&gateway_pending_active_count()==0);frame(f,3,9,0,0,0,"",0);universal_node_gateway_handle_thread_rx(f,28,p);assert(s_completions==2); gateway_pending_reset(); assert(gateway_pending_register(4,9,s_sender,0)==GATEWAY_PENDING_REGISTERED);frame(f,4,10,0,0,0,"",0);universal_node_gateway_handle_thread_rx(f,28,p);assert(gateway_pending_active_count()==1);frame(f,4,9,0,0,0,"",0);s_sender=0x1002;universal_node_gateway_handle_thread_rx(f,28,p);assert(gateway_pending_active_count()==1);s_sender=0x1001;frame(f,5,9,0,0,0,"",0);universal_node_gateway_handle_thread_rx(f,28,p);assert(gateway_pending_active_count()==1);assert(gateway_pending_cancel(4,9,s_sender));frame(f,4,9,0,0,0,"",0);universal_node_gateway_handle_thread_rx(f,28,p);assert(s_completions==2); assert(gateway_pending_register(6,CMD_RELAY_SET,s_sender,0)==GATEWAY_PENDING_REGISTERED);frame(f,6,CMD_RELAY_SET,0,0,0,"",0);universal_node_gateway_handle_thread_rx(f,28,p);assert(s_completions==3&&gateway_pending_active_count()==0);for(uint16_t i=0;i<4;i++)assert(gateway_pending_register(10+i,9,s_sender,0)==GATEWAY_PENDING_REGISTERED);assert(gateway_pending_register(20,9,s_sender,0)==GATEWAY_PENDING_FULL);assert(gateway_pending_expire(GATEWAY_REQUEST_TIMEOUT_MS)==4);assert(gateway_pending_active_count()==0);return 0;}
