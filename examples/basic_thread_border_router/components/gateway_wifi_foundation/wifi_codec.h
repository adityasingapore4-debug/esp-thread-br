#ifndef GATEWAY_WIFI_CODEC_H
#define GATEWAY_WIFI_CODEC_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define WIFI_SSID_MAX 32u
#define WIFI_SECRET_MAX 63u
#define WIFI_ID_LEN 16u
typedef struct { uint8_t mode, ssid_len, secret_len, ssid[32], secret[63]; } wifi_config_t;
typedef struct { uint8_t op, id[16]; wifi_config_t config; } wifi_request_t;
typedef struct { uint8_t ssid_len, security; int8_t rssi; uint8_t channel, ssid[32]; } wifi_scan_entry_t;
typedef struct { uint8_t op, phase; union { struct { uint32_t generation; uint8_t flags, state, mode, ssid_len, ssid[32]; } status; struct { uint8_t count, truncated; wifi_scan_entry_t entries[8]; } scan; struct { uint8_t id[16], state, reason; uint32_t generation; } tx; } u; } wifi_response_t;
void wifi_zero(void *p, size_t n);
bool wifi_request_decode(const uint8_t *in, size_t len, wifi_request_t *out);
bool wifi_response_encode(const wifi_response_t *r, uint8_t *out, size_t cap, size_t *written);
#endif
