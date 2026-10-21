// Sartax OS - siec (e1000, IPv4, TCP, DNS, DHCP, TLS, HTTP)
#pragma once
#include "kernel.h"

void net_init(void);
void net_poll(void);
bool net_ready(void);                 // mamy adres IP
bool net_has_card(void);
const char *net_status_text(void);
void net_ip_str(char *buf, size_t n);

int dns_resolve(const char *host, u32 *ip);

typedef struct tcp_sock tcp_sock_t;
tcp_sock_t *tcp_connect(u32 ip, u16 port, u32 timeout_ms);
int tcp_send(tcp_sock_t *s, const void *data, size_t len);     // blokujaco, zwraca len lub <0
int tcp_recv(tcp_sock_t *s, void *buf, size_t len, u32 timeout_ms); // 0 = koniec, <0 blad
void tcp_close(tcp_sock_t *s);

// HTTP(S) GET - pobiera cala odpowiedz do pamieci
typedef struct {
    int status;
    u8 *body;
    size_t len;
    char error[128];
} http_resp_t;
typedef void (*http_progress_fn)(void *ctx, size_t done, size_t total);
int http_get(const char *url, http_resp_t *resp, http_progress_fn progress, void *ctx);
void http_free(http_resp_t *r);
