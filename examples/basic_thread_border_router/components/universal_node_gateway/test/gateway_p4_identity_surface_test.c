/* Existing production parser must continue rejecting the unapproved route. */
#include "universal_node_gateway_p4_http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    gateway_p4_http_request_t request;
    const char identity[] = "GET /un/p4/identity HTTP/1.1\r\nHost: gateway\r\nContent-Length: 0\r\n\r\n";
    const char auth[] = "POST /un/p4/auth HTTP/1.1\r\nHost: gateway\r\nContent-Length: 0\r\nContent-Type: application/octet-stream\r\n\r\n";
    const char query[] = "GET /un/p4/identity?x=1 HTTP/1.1\r\nHost: gateway\r\nContent-Length: 0\r\n\r\n";
    const char origin[] = "GET /un/p4/identity HTTP/1.1\r\nHost: gateway\r\nOrigin: https://example.invalid\r\nContent-Length: 0\r\n\r\n";
    assert(gateway_p4_http_headers((const uint8_t *)identity, strlen(identity), &request) == 404);
    assert(gateway_p4_http_validate(&request, 0) == 401);
    assert(gateway_p4_http_headers((const uint8_t *)auth, strlen(auth), &request) == 200);
    assert(gateway_p4_http_validate(&request, 0) == 401); /* valid framing is no grant */
    assert(gateway_p4_http_headers((const uint8_t *)query, strlen(query), &request) == 400);
    assert(gateway_p4_http_headers((const uint8_t *)origin, strlen(origin), &request) == 403);
    puts("Inactive identity surface: 6 checks PASS");
    return 0;
}
