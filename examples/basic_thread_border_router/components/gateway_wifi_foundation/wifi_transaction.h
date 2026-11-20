#ifndef GATEWAY_WIFI_TRANSACTION_H
#define GATEWAY_WIFI_TRANSACTION_H
#include "wifi_codec.h"
/* Serialized owner. ABORTED fences promotion; DURABLE means verified promotion
 * completed; UNKNOWN leaves the transaction pending until resolved. stage
 * must copy input synchronously and must not retain caller-owned pointers.
 * A state machine instance must outlive all callbacks; reinitialization requires
 * the owner to fence and drain all old callbacks first. */
typedef enum { WIFI_CANCEL_ABORTED, WIFI_CANCEL_DURABLE, WIFI_CANCEL_UNKNOWN } wifi_cancel_result_t;
typedef struct { void *ctx; bool (*load)(void *,wifi_config_t *,uint32_t *); bool (*stage)(void *,const wifi_config_t *,uint32_t); wifi_cancel_result_t (*cancel_commit)(void *); bool (*restore)(void *,const wifi_config_t *,uint32_t); void (*clear)(void *); } wifi_store_t;
typedef enum { WIFI_EVENT_ASSOCIATED, WIFI_EVENT_IP_READY, WIFI_EVENT_LINK_LOST, WIFI_EVENT_TIMEOUT, WIFI_EVENT_STABILITY_COMPLETE, WIFI_EVENT_COMMIT_SUCCESS, WIFI_EVENT_COMMIT_FAILED, WIFI_EVENT_ROLLBACK_SUCCESS, WIFI_EVENT_ROLLBACK_FAILED, WIFI_EVENT_CARRIER_LOST, WIFI_EVENT_CANCEL, WIFI_EVENT_REBOOT } wifi_event_t;
typedef enum { WIFI_TX_IDLE, WIFI_TX_CONNECTING, WIFI_TX_VALIDATING_IP, WIFI_TX_STABILITY, WIFI_TX_COMMITTED, WIFI_TX_FAILED, WIFI_TX_CANCELLED, WIFI_TX_ROLLBACK, WIFI_TX_COMMITTING } wifi_tx_state_t;
typedef struct { wifi_store_t store; wifi_config_t previous, candidate; uint8_t id[16], recent_ids[8][16], recent_count, reason; uint32_t generation; uint64_t started_ms, stable_ms, last_ms, ticket; wifi_tx_state_t state; bool previous_loaded; } wifi_tx_t;
bool wifi_tx_init(wifi_tx_t *, wifi_store_t);
bool wifi_tx_start(wifi_tx_t *, const wifi_request_t *, bool authorized, uint64_t now_ms);
bool wifi_tx_event(wifi_tx_t *, wifi_event_t, uint64_t now_ms);
/* Storage and rollback completions must carry immutable transaction ID and the
 * generation and ticket observed when that operation began. Never infer identity from the
 * currently active transaction. */
bool wifi_tx_complete(wifi_tx_t *, wifi_event_t, const uint8_t id[16], uint32_t prior_generation, uint64_t ticket, uint64_t now_ms);
#endif
