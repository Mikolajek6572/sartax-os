// Sartax OS - stos sieciowy: e1000, Ethernet, ARP, IPv4, ICMP, UDP, DHCP, DNS, TCP
#include "../kernel.h"
#include "../net.h"
#include "../ui.h"

static inline u16 htons(u16 v) { return (v >> 8) | (v << 8); }
static inline u32 htonl(u32 v) { return __builtin_bswap32(v); }
#define ntohs htons
#define ntohl htonl

// ================= e1000 =================
#define E_CTRL 0x0000
#define E_STATUS 0x0008
#define E_EERD 0x0014
#define E_ICR 0x00C0
#define E_IMC 0x00D8
#define E_RCTL 0x0100
#define E_TCTL 0x0400
#define E_TIPG 0x0410
#define E_RDBAL 0x2800
#define E_RDBAH 0x2804
#define E_RDLEN 0x2808
#define E_RDH 0x2810
#define E_RDT 0x2818
#define E_TDBAL 0x3800
#define E_TDBAH 0x3804
#define E_TDLEN 0x3808
#define E_TDH 0x3810
#define E_TDT 0x3818
#define E_MTA 0x5200
#define E_RAL 0x5400
#define E_RAH 0x5404

#define NRX 64
#define NTX 32
#define BUFSZ 2048

typedef struct PACKED { u64 addr; u16 len, csum; u8 status, errors; u16 special; } rxd_t;
typedef struct PACKED { u64 addr; u16 len; u8 cso, cmd, status, css; u16 special; } txd_t;

static u64 mmio;
static rxd_t *rxr;
static txd_t *txr;
static u8 *rxbuf, *txbuf;
static int rx_cur, tx_cur;
static u8 mac[6];
static bool have_card;

static u32 er(u32 r) { return mmio_r32(mmio + r); }
static void ew(u32 r, u32 v) { mmio_w32(mmio + r, v); }

static void *alloc_aligned(size_t n, size_t al) {
    u8 *p = kzalloc(n + al);
    return (void *)(((u64)p + al - 1) & ~(al - 1));
}

static bool e1000_init(void) {
    pci_dev_t *d = NULL;
    static const u16 ids[] = { 0x100E, 0x100F, 0x1004, 0x10D3, 0x153A };
    for (int i = 0; i < (int)ARRAY_LEN(ids) && !d; i++) d = pci_find(0x8086, ids[i]);
    if (!d) return false;
    pci_enable_busmaster(d);
    mmio = d->bar[0] & ~0xFull;
    ew(E_IMC, 0xFFFFFFFF);
    ew(E_CTRL, er(E_CTRL) | (1u << 26));       // reset
    sleep_ms(20);
    ew(E_IMC, 0xFFFFFFFF);
    er(E_ICR);
    ew(E_CTRL, (er(E_CTRL) | (1 << 6) | (1 << 5)) & ~((1u << 3) | (1u << 31) | (1u << 7)));  // SLU, ASDE
    u32 ral = er(E_RAL), rah = er(E_RAH);
    if (ral || (rah & 0xFFFF)) {
        for (int i = 0; i < 4; i++) mac[i] = ral >> (i * 8);
        mac[4] = rah, mac[5] = rah >> 8;
    } else {
        for (int i = 0; i < 3; i++) {
            ew(E_EERD, 1 | (i << 8));
            u32 v;
            int t = 0;
            while (!((v = er(E_EERD)) & 0x10) && t++ < 100000) {}
            mac[i * 2] = v >> 16;
            mac[i * 2 + 1] = v >> 24;
        }
    }
    for (int i = 0; i < 128; i++) ew(E_MTA + i * 4, 0);
    rxr = alloc_aligned(NRX * sizeof(rxd_t), 128);
    txr = alloc_aligned(NTX * sizeof(txd_t), 128);
    rxbuf = alloc_aligned(NRX * BUFSZ, 16);
    txbuf = alloc_aligned(NTX * BUFSZ, 16);
    for (int i = 0; i < NRX; i++) rxr[i].addr = (u64)(rxbuf + i * BUFSZ);
    for (int i = 0; i < NTX; i++) { txr[i].addr = (u64)(txbuf + i * BUFSZ); txr[i].status = 1; }
    ew(E_RDBAL, (u32)(u64)rxr);
    ew(E_RDBAH, 0);
    ew(E_RDLEN, NRX * sizeof(rxd_t));
    ew(E_RDH, 0);
    ew(E_RDT, NRX - 1);
    ew(E_RCTL, (1 << 1) | (1 << 15) | (1 << 26));     // EN, BAM, SECRC, 2048 B
    ew(E_TDBAL, (u32)(u64)txr);
    ew(E_TDBAH, 0);
    ew(E_TDLEN, NTX * sizeof(txd_t));
    ew(E_TDH, 0);
    ew(E_TDT, 0);
    ew(E_TCTL, (1 << 1) | (1 << 3) | (15 << 4) | (64 << 12));
    ew(E_TIPG, 0x0060200A);
    rx_cur = 0;
    tx_cur = 0;
    kprintf("[net] e1000 %04x MAC %02x:%02x:%02x:%02x:%02x:%02x\n", d->device, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return true;
}

static void nic_send(const void *data, u16 len) {
    if (!have_card) return;
    txd_t *t = &txr[tx_cur];
    for (int i = 0; i < 100000 && !(t->status & 1); i++) {}
    memcpy(txbuf + tx_cur * BUFSZ, data, len);
    t->len = len < 60 ? 60 : len;
    if (len < 60) memset(txbuf + tx_cur * BUFSZ + len, 0, 60 - len);
    t->cmd = (1 << 0) | (1 << 1) | (1 << 3);          // EOP, IFCS, RS
    t->status = 0;
    tx_cur = (tx_cur + 1) % NTX;
    ew(E_TDT, tx_cur);
}

// ================= wspolne =================
static u32 my_ip, my_mask, my_gw, my_dns;
static u8 gw_mac[6];
static bool gw_mac_ok;
static char status_text[128] = "Siec wylaczona";
static enum { DHCP_OFF, DHCP_DISCOVER, DHCP_REQUEST, DHCP_DONE } dhcp_state;
static u32 dhcp_xid, dhcp_offer_ip, dhcp_server;
static u64 dhcp_timer;
static int dhcp_tries;

bool net_ready(void) { return dhcp_state == DHCP_DONE; }
bool net_has_card(void) { return have_card; }
const char *net_status_text(void) { return status_text; }

static void ip_str(u32 ip, char *b, size_t n) {
    snprintf(b, n, "%u.%u.%u.%u", ip >> 24, (ip >> 16) & 255, (ip >> 8) & 255, ip & 255);
}
void net_ip_str(char *b, size_t n) { ip_str(my_ip, b, n); }

static u16 csum(const void *data, int len, u32 sum) {
    const u8 *p = data;
    while (len > 1) { sum += (p[0] << 8) | p[1]; p += 2; len -= 2; }
    if (len) sum += p[0] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum & 0xFFFF;
}

typedef struct PACKED { u8 dst[6], src[6]; u16 type; } eth_t;
typedef struct PACKED { u8 vihl, tos; u16 len, id, frag; u8 ttl, proto; u16 csum; u32 src, dst; } ip_t;
typedef struct PACKED { u16 htype, ptype; u8 hlen, plen; u16 op; u8 sha[6]; u32 spa; u8 tha[6]; u32 tpa; } arp_t;
typedef struct PACKED { u16 sport, dport, len, csum; } udp_t;
typedef struct PACKED { u16 sport, dport; u32 seq, ack; u8 off, flags; u16 win, csum, urg; } tcp_t;

static const u8 bcast[6] = { 255, 255, 255, 255, 255, 255 };

// ================= ARP =================
static struct { u32 ip; u8 mac[6]; } arp_cache[16];
static int arp_n;

static bool arp_lookup(u32 ip, u8 *out) {
    for (int i = 0; i < arp_n; i++)
        if (arp_cache[i].ip == ip) { memcpy(out, arp_cache[i].mac, 6); return true; }
    return false;
}

static void arp_store(u32 ip, const u8 *m) {
    for (int i = 0; i < arp_n; i++)
        if (arp_cache[i].ip == ip) { memcpy(arp_cache[i].mac, m, 6); return; }
    int i = arp_n < 16 ? arp_n++ : (int)(rng_u32() % 16);
    arp_cache[i].ip = ip;
    memcpy(arp_cache[i].mac, m, 6);
}

static void arp_send(u16 op, const u8 *tmac, u32 tip) {
    u8 pkt[sizeof(eth_t) + sizeof(arp_t)];
    eth_t *e = (eth_t *)pkt;
    arp_t *a = (arp_t *)(pkt + sizeof(eth_t));
    memcpy(e->dst, op == 1 ? bcast : tmac, 6);
    memcpy(e->src, mac, 6);
    e->type = htons(0x0806);
    a->htype = htons(1);
    a->ptype = htons(0x0800);
    a->hlen = 6, a->plen = 4;
    a->op = htons(op);
    memcpy(a->sha, mac, 6);
    a->spa = htonl(my_ip);
    if (op == 1) memset(a->tha, 0, 6); else memcpy(a->tha, tmac, 6);
    a->tpa = htonl(tip);
    nic_send(pkt, sizeof pkt);
}

static void arp_rx(arp_t *a) {
    u32 spa = ntohl(a->spa), tpa = ntohl(a->tpa);
    if (spa) arp_store(spa, a->sha);
    if (spa == my_gw) { memcpy(gw_mac, a->sha, 6); gw_mac_ok = true; }
    if (ntohs(a->op) == 1 && tpa == my_ip && my_ip) arp_send(2, a->sha, spa);
}

// ================= IP =================
static u16 ip_id;

static bool resolve_mac(u32 dst, u8 *out) {
    if (dst == 0xFFFFFFFF) { memcpy(out, bcast, 6); return true; }
    u32 hop = ((dst & my_mask) == (my_ip & my_mask)) ? dst : my_gw;
    if (hop == my_gw && gw_mac_ok) { memcpy(out, gw_mac, 6); return true; }
    if (arp_lookup(hop, out)) return true;
    for (int t = 0; t < 3; t++) {
        arp_send(1, NULL, hop);
        u64 end = ticks_ms() + 500;
        while (ticks_ms() < end) {
            net_poll();
            if (arp_lookup(hop, out)) return true;
            sys_idle();
            hlt();
        }
    }
    return false;
}

static int ip_send(u32 dst, u8 proto, const void *payload, int plen) {
    static u8 pkt[1600];
    u8 dmac[6];
    if (!resolve_mac(dst, dmac)) return -1;
    eth_t *e = (eth_t *)pkt;
    ip_t *ip = (ip_t *)(pkt + sizeof(eth_t));
    memcpy(e->dst, dmac, 6);
    memcpy(e->src, mac, 6);
    e->type = htons(0x0800);
    ip->vihl = 0x45;
    ip->tos = 0;
    ip->len = htons(sizeof(ip_t) + plen);
    ip->id = htons(ip_id++);
    ip->frag = htons(0x4000);
    ip->ttl = 64;
    ip->proto = proto;
    ip->csum = 0;
    ip->src = htonl(my_ip);
    ip->dst = htonl(dst);
    ip->csum = htons(csum(ip, sizeof(ip_t), 0));
    memcpy(pkt + sizeof(eth_t) + sizeof(ip_t), payload, plen);
    nic_send(pkt, sizeof(eth_t) + sizeof(ip_t) + plen);
    return 0;
}

static u32 pseudo_sum(u32 src, u32 dst, u8 proto, u16 len) {
    return (src >> 16) + (src & 0xFFFF) + (dst >> 16) + (dst & 0xFFFF) + proto + len;
}

// ================= UDP =================
static int udp_send(u32 dst, u16 sport, u16 dport, const void *data, int len) {
    u8 buf[1500];
    udp_t *u = (udp_t *)buf;
    u->sport = htons(sport);
    u->dport = htons(dport);
    u->len = htons(sizeof(udp_t) + len);
    u->csum = 0;
    memcpy(buf + sizeof(udp_t), data, len);
    u16 c = csum(buf, sizeof(udp_t) + len, pseudo_sum(my_ip, dst, 17, sizeof(udp_t) + len));
    u->csum = htons(c ? c : 0xFFFF);
    return ip_send(dst, 17, buf, sizeof(udp_t) + len);
}

// ================= DHCP =================
typedef struct PACKED {
    u8 op, htype, hlen, hops;
    u32 xid;
    u16 secs, flags;
    u32 ciaddr, yiaddr, siaddr, giaddr;
    u8 chaddr[16], sname[64], file[128];
    u32 magic;
    u8 opts[312];
} dhcp_t;

static void dhcp_send(int type) {
    dhcp_t d;
    memset(&d, 0, sizeof d);
    d.op = 1, d.htype = 1, d.hlen = 6;
    d.xid = htonl(dhcp_xid);
    d.flags = htons(0x8000);
    memcpy(d.chaddr, mac, 6);
    d.magic = htonl(0x63825363);
    u8 *o = d.opts;
    *o++ = 53; *o++ = 1; *o++ = type;
    if (type == 3) {
        *o++ = 50; *o++ = 4; u32 ip = htonl(dhcp_offer_ip); memcpy(o, &ip, 4); o += 4;
        *o++ = 54; *o++ = 4; u32 sv = htonl(dhcp_server); memcpy(o, &sv, 4); o += 4;
    }
    *o++ = 12; *o++ = 6; memcpy(o, "sartax", 6); o += 6;
    *o++ = 55; *o++ = 3; *o++ = 1; *o++ = 3; *o++ = 6;
    *o++ = 255;
    u32 save = my_ip;
    my_ip = 0;
    udp_send(0xFFFFFFFF, 68, 67, &d, sizeof d);
    my_ip = save;
}

static void dhcp_rx(dhcp_t *d, int len) {
    if (ntohl(d->xid) != dhcp_xid || d->op != 2) return;
    int type = 0;
    u32 mask = 0, gw = 0, dns = 0, server = 0;
    u8 *o = d->opts, *end = (u8 *)d + len;
    while (o < end && *o != 255) {
        if (*o == 0) { o++; continue; }
        u8 code = o[0], l = o[1];
        u8 *v = o + 2;
        if (code == 53) type = v[0];
        else if (code == 1 && l >= 4) mask = ntohl(*(u32 *)v);
        else if (code == 3 && l >= 4) gw = ntohl(*(u32 *)v);
        else if (code == 6 && l >= 4) dns = ntohl(*(u32 *)v);
        else if (code == 54 && l >= 4) server = ntohl(*(u32 *)v);
        o += 2 + l;
    }
    if (type == 2 && dhcp_state == DHCP_DISCOVER) {
        dhcp_offer_ip = ntohl(d->yiaddr);
        dhcp_server = server;
        dhcp_state = DHCP_REQUEST;
        dhcp_send(3);
        dhcp_timer = ticks_ms();
    } else if (type == 5 && dhcp_state == DHCP_REQUEST) {
        my_ip = ntohl(d->yiaddr);
        my_mask = mask ? mask : 0xFFFFFF00;
        my_gw = gw;
        my_dns = dns ? dns : gw;
        dhcp_state = DHCP_DONE;
        char a[20], b[20], c[20];
        ip_str(my_ip, a, sizeof a);
        ip_str(my_gw, b, sizeof b);
        ip_str(my_dns, c, sizeof c);
        snprintf(status_text, sizeof status_text, "IP %s, brama %s, DNS %s", a, b, c);
        kprintf("[net] DHCP: %s\n", status_text);
        arp_send(1, NULL, my_gw);
        wm_invalidate();
    }
}

// ================= DNS =================
static volatile u32 dns_answer;
static volatile bool dns_done;
static u16 dns_id;

static void dns_rx(u8 *p, int len) {
    if (len < 12 || ((p[0] << 8) | p[1]) != dns_id) return;
    int qd = (p[4] << 8) | p[5], an = (p[6] << 8) | p[7];
    int i = 12;
    for (int q = 0; q < qd; q++) {
        while (i < len && p[i]) { if ((p[i] & 0xC0) == 0xC0) { i++; break; } i += p[i] + 1; }
        i += 5;
    }
    for (int a = 0; a < an && i < len; a++) {
        if ((p[i] & 0xC0) == 0xC0) i += 2;
        else { while (i < len && p[i]) i += p[i] + 1; i++; }
        if (i + 10 > len) break;
        int type = (p[i] << 8) | p[i + 1];
        int rdl = (p[i + 8] << 8) | p[i + 9];
        i += 10;
        if (type == 1 && rdl == 4 && i + 4 <= len) {
            dns_answer = ((u32)p[i] << 24) | (p[i + 1] << 16) | (p[i + 2] << 8) | p[i + 3];
            dns_done = true;
            return;
        }
        i += rdl;
    }
    dns_done = true;
}

static struct { char host[96]; u32 ip; u64 t; } dns_cache[8];

int dns_resolve(const char *host, u32 *ip) {
    // adres IP w postaci tekstu
    int parts[4], n = 0;
    const char *p = host;
    while (n < 4 && *p >= '0' && *p <= '9') {
        parts[n++] = katoi(p);
        while (*p >= '0' && *p <= '9') p++;
        if (*p == '.') p++;
        else break;
    }
    if (n == 4 && !*p) { *ip = (parts[0] << 24) | (parts[1] << 16) | (parts[2] << 8) | parts[3]; return 0; }
    for (int i = 0; i < 8; i++)
        if (!strcmp(dns_cache[i].host, host) && ticks_ms() - dns_cache[i].t < 300000) { *ip = dns_cache[i].ip; return 0; }
    if (!net_ready()) return -1;
    u8 q[300];
    int l = 0;
    dns_id = rng_u32();
    q[l++] = dns_id >> 8; q[l++] = dns_id;
    q[l++] = 1; q[l++] = 0;          // RD
    q[l++] = 0; q[l++] = 1;
    memset(q + l, 0, 6); l += 6;
    const char *s = host;
    while (*s && l < 260) {
        const char *dot = strchr(s, '.');
        int sl = dot ? dot - s : (int)strlen(s);
        q[l++] = sl;
        memcpy(q + l, s, sl);
        l += sl;
        s += sl;
        if (*s == '.') s++;
    }
    q[l++] = 0;
    q[l++] = 0; q[l++] = 1; q[l++] = 0; q[l++] = 1;
    for (int t = 0; t < 4; t++) {
        dns_done = false;
        dns_answer = 0;
        udp_send(my_dns, 5353 + t, 53, q, l);
        u64 end = ticks_ms() + 1500;
        while (!dns_done && ticks_ms() < end) { net_poll(); sys_idle(); hlt(); }
        if (dns_done) break;
    }
    if (!dns_answer) return -1;
    *ip = dns_answer;
    static int ci;
    strlcpy(dns_cache[ci].host, host, sizeof dns_cache[0].host);
    dns_cache[ci].ip = dns_answer;
    dns_cache[ci].t = ticks_ms();
    ci = (ci + 1) % 8;
    return 0;
}

// ================= TCP =================
#define TCP_RXCAP 65535
#define TCP_MSS 1460
enum { T_CLOSED, T_SYN_SENT, T_EST, T_FIN_WAIT, T_DONE };
#define F_FIN 1
#define F_SYN 2
#define F_RST 4
#define F_PSH 8
#define F_ACK 16

struct tcp_sock {
    int state;
    u32 rip;
    u16 rport, lport;
    u32 snd_una, snd_nxt, rcv_nxt, iss;
    u16 rwnd;
    u8 *rx;
    u32 rx_head, rx_len;         // bufor cykliczny
    u8 *tx;
    u32 tx_len, tx_cap;          // dane od snd_una
    u64 rto_at;
    u32 rto;
    int retries;
    bool fin_rx, reset;
    struct tcp_sock *next;
};

static tcp_sock_t *socks;
static u16 next_port;

static void tcp_out(tcp_sock_t *s, u8 flags, u32 seq, const u8 *data, int len) {
    u8 buf[sizeof(tcp_t) + 4 + TCP_MSS];
    tcp_t *t = (tcp_t *)buf;
    int hl = sizeof(tcp_t) + ((flags & F_SYN) ? 4 : 0);
    t->sport = htons(s->lport);
    t->dport = htons(s->rport);
    t->seq = htonl(seq);
    t->ack = htonl((flags & F_ACK) ? s->rcv_nxt : 0);
    t->off = (hl / 4) << 4;
    t->flags = flags;
    u32 space = TCP_RXCAP - s->rx_len;
    t->win = htons(space > 65535 ? 65535 : space);
    t->csum = 0;
    t->urg = 0;
    if (flags & F_SYN) {
        u8 *o = buf + sizeof(tcp_t);
        o[0] = 2; o[1] = 4; o[2] = TCP_MSS >> 8; o[3] = TCP_MSS & 0xFF;
    }
    if (len) memcpy(buf + hl, data, len);
    t->csum = htons(csum(buf, hl + len, pseudo_sum(my_ip, s->rip, 6, hl + len)));
    ip_send(s->rip, 6, buf, hl + len);
}

static void tcp_ack_now(tcp_sock_t *s) { tcp_out(s, F_ACK, s->snd_nxt, NULL, 0); }

// wyslij nowe dane mieszczace sie w oknie
static void tcp_push(tcp_sock_t *s) {
    if (s->state != T_EST && s->state != T_FIN_WAIT) return;
    for (;;) {
        u32 inflight = s->snd_nxt - s->snd_una;
        if (inflight >= s->tx_len) break;
        u32 wnd = s->rwnd ? s->rwnd : 1;
        if (inflight >= wnd || inflight >= 32 * 1024) break;
        u32 n = MIN(s->tx_len - inflight, TCP_MSS);
        n = MIN(n, wnd - inflight);
        if (!n) break;
        tcp_out(s, F_ACK | F_PSH, s->snd_nxt, s->tx + inflight, n);
        s->snd_nxt += n;
        if (!s->rto_at) s->rto_at = ticks_ms() + s->rto;
    }
}

static void tcp_rx(u32 src, tcp_t *t, int len) {
    u16 dport = ntohs(t->dport), sport = ntohs(t->sport);
    tcp_sock_t *s = socks;
    while (s && !(s->lport == dport && s->rport == sport && s->rip == src)) s = s->next;
    if (!s) return;
    int hl = (t->off >> 4) * 4;
    if (hl < 20 || hl > len) return;
    u8 *data = (u8 *)t + hl;
    int dlen = len - hl;
    u32 seq = ntohl(t->seq), ack = ntohl(t->ack);
    u8 fl = t->flags;
    if (fl & F_RST) { s->reset = true; s->state = T_DONE; return; }
    if (s->state == T_SYN_SENT) {
        if ((fl & (F_SYN | F_ACK)) == (F_SYN | F_ACK) && ack == s->iss + 1) {
            s->rcv_nxt = seq + 1;
            s->snd_una = ack;
            s->snd_nxt = ack;
            s->rwnd = ntohs(t->win);
            s->state = T_EST;
            s->rto_at = 0;
            s->retries = 0;
            tcp_ack_now(s);
        }
        return;
    }
    if (fl & F_ACK) {
        if ((i32)(ack - s->snd_una) > 0 && (i32)(ack - s->snd_nxt) <= 0) {
            u32 acked = ack - s->snd_una;
            u32 drop = MIN(acked, s->tx_len);
            memmove(s->tx, s->tx + drop, s->tx_len - drop);
            s->tx_len -= drop;
            s->snd_una = ack;
            s->retries = 0;
            s->rto = 600;
            s->rto_at = s->snd_una != s->snd_nxt ? ticks_ms() + s->rto : 0;
        }
        s->rwnd = ntohs(t->win);
    }
    if (dlen > 0) {
        if (seq == s->rcv_nxt) {
            u32 space = TCP_RXCAP - s->rx_len;
            u32 n = MIN((u32)dlen, space);
            for (u32 i = 0; i < n; i++) s->rx[(s->rx_head + s->rx_len + i) % TCP_RXCAP] = data[i];
            s->rx_len += n;
            s->rcv_nxt += n;
            if (n == (u32)dlen && (fl & F_FIN)) { s->rcv_nxt++; s->fin_rx = true; }
        }
        tcp_ack_now(s);
    } else if ((fl & F_FIN) && seq == s->rcv_nxt) {
        s->rcv_nxt++;
        s->fin_rx = true;
        tcp_ack_now(s);
    }
    tcp_push(s);
}

static void tcp_timers(void) {
    u64 now = ticks_ms();
    for (tcp_sock_t *s = socks; s; s = s->next) {
        if (!s->rto_at || now < s->rto_at) continue;
        if (++s->retries > 8) { s->reset = true; s->state = T_DONE; s->rto_at = 0; continue; }
        s->rto = MIN(s->rto * 2, 8000);
        s->rto_at = now + s->rto;
        if (s->state == T_SYN_SENT) tcp_out(s, F_SYN, s->iss, NULL, 0);
        else {
            // go-back-N: wyslij od snd_una
            s->snd_nxt = s->snd_una;
            s->rto_at = 0;
            tcp_push(s);
            if (!s->rto_at) s->rto_at = now + s->rto;
        }
    }
}

tcp_sock_t *tcp_connect(u32 ip, u16 port, u32 timeout_ms) {
    if (!net_ready()) return NULL;
    tcp_sock_t *s = kzalloc(sizeof *s);
    s->rip = ip;
    s->rport = port;
    if (!next_port) next_port = 40000 + rng_u32() % 20000;
    s->lport = next_port++;
    if (next_port > 65000) next_port = 40000;
    s->iss = rng_u32();
    s->snd_una = s->snd_nxt = s->iss;
    s->rx = kmalloc(TCP_RXCAP);
    s->tx_cap = 65536;
    s->tx = kmalloc(s->tx_cap);
    s->rto = 1000;
    s->state = T_SYN_SENT;
    s->next = socks;
    socks = s;
    tcp_out(s, F_SYN, s->iss, NULL, 0);
    s->rto_at = ticks_ms() + s->rto;
    u64 end = ticks_ms() + timeout_ms;
    while (s->state == T_SYN_SENT && ticks_ms() < end && !busy_cancelled()) { net_poll(); sys_idle(); hlt(); }
    if (s->state != T_EST) { tcp_close(s); return NULL; }
    return s;
}

int tcp_send(tcp_sock_t *s, const void *data, size_t len) {
    const u8 *p = data;
    size_t left = len;
    while (left) {
        if (s->reset || s->state != T_EST) return -1;
        if (busy_cancelled()) return -2;
        u32 room = s->tx_cap - s->tx_len;
        if (room) {
            u32 n = MIN(room, left);
            memcpy(s->tx + s->tx_len, p, n);
            s->tx_len += n;
            p += n;
            left -= n;
            tcp_push(s);
        } else {
            net_poll();
            sys_idle();
            hlt();
        }
    }
    return len;
}

int tcp_recv(tcp_sock_t *s, void *buf, size_t len, u32 timeout_ms) {
    u64 end = ticks_ms() + timeout_ms;
    while (!s->rx_len) {
        if (s->fin_rx) return 0;
        if (s->reset) return -1;
        if (busy_cancelled()) return -2;
        if (ticks_ms() > end) return -3;
        net_poll();
        sys_idle();
        hlt();
    }
    u32 n = MIN((u32)len, s->rx_len);
    bool was_full = TCP_RXCAP - s->rx_len < TCP_MSS * 2;
    for (u32 i = 0; i < n; i++) ((u8 *)buf)[i] = s->rx[(s->rx_head + i) % TCP_RXCAP];
    s->rx_head = (s->rx_head + n) % TCP_RXCAP;
    s->rx_len -= n;
    if (was_full) tcp_ack_now(s);    // aktualizacja okna
    return n;
}

void tcp_close(tcp_sock_t *s) {
    if (!s) return;
    if (s->state == T_EST && !s->reset) tcp_out(s, F_FIN | F_ACK, s->snd_nxt, NULL, 0);
    tcp_sock_t **pp = &socks;
    while (*pp && *pp != s) pp = &(*pp)->next;
    if (*pp) *pp = s->next;
    kfree(s->rx);
    kfree(s->tx);
    kfree(s);
}

// ================= odbior =================
static void ip_rx(u8 *pkt, int len) {
    ip_t *ip = (ip_t *)pkt;
    if (len < (int)sizeof(ip_t) || (ip->vihl >> 4) != 4) return;
    int hl = (ip->vihl & 15) * 4;
    int tl = ntohs(ip->len);
    if (tl > len) return;
    if (ntohs(ip->frag) & 0x3FFF) return;      // fragmenty nieobslugiwane
    u32 src = ntohl(ip->src), dst = ntohl(ip->dst);
    u8 *pl = pkt + hl;
    int plen = tl - hl;
    if (my_ip && dst != my_ip && dst != 0xFFFFFFFF) return;
    if (ip->proto == 17 && plen >= (int)sizeof(udp_t)) {
        udp_t *u = (udp_t *)pl;
        u16 dp = ntohs(u->dport);
        u8 *d = pl + sizeof(udp_t);
        int dl = ntohs(u->len) - sizeof(udp_t);
        if (dp == 68) dhcp_rx((dhcp_t *)d, dl);
        else if (dp >= 5353 && dp < 5360 && ntohs(u->sport) == 53) dns_rx(d, dl);
    } else if (ip->proto == 6) {
        tcp_rx(src, (tcp_t *)pl, plen);
    } else if (ip->proto == 1 && plen >= 8 && pl[0] == 8) {
        // ping - odpowiedz
        static u8 rep[1500];
        memcpy(rep, pl, plen);
        rep[0] = 0;
        rep[2] = rep[3] = 0;
        u16 c = csum(rep, plen, 0);
        rep[2] = c >> 8;
        rep[3] = c;
        ip_send(src, 1, rep, plen);
    }
}

static bool polling;
void net_poll(void) {
    if (!have_card || polling) return;
    polling = true;
    for (int n = 0; n < NRX; n++) {
        rxd_t *d = &rxr[rx_cur];
        if (!(d->status & 1)) break;
        u8 *pkt = rxbuf + rx_cur * BUFSZ;
        int len = d->len;
        if ((d->status & 2) && len >= (int)sizeof(eth_t)) {
            eth_t *e = (eth_t *)pkt;
            u16 type = ntohs(e->type);
            if (type == 0x0806) arp_rx((arp_t *)(pkt + sizeof(eth_t)));
            else if (type == 0x0800) ip_rx(pkt + sizeof(eth_t), len - sizeof(eth_t));
        }
        d->status = 0;
        ew(E_RDT, rx_cur);
        rx_cur = (rx_cur + 1) % NRX;
    }
    tcp_timers();
    // DHCP
    if (dhcp_state != DHCP_DONE && dhcp_state != DHCP_OFF && ticks_ms() - dhcp_timer > 2000) {
        dhcp_tries++;
        dhcp_state = DHCP_DISCOVER;
        dhcp_xid = rng_u32();
        dhcp_send(1);
        dhcp_timer = ticks_ms();
        if (dhcp_tries > 3) snprintf(status_text, sizeof status_text, "Brak odpowiedzi DHCP (kabel podlaczony? NAT?)");
    }
    polling = false;
}

void net_init(void) {
    have_card = e1000_init();
    if (!have_card) {
        strlcpy(status_text, "Nie znaleziono obslugiwanej karty sieciowej (Intel PRO/1000)", sizeof status_text);
        return;
    }
    strlcpy(status_text, "Pobieranie adresu IP (DHCP)...", sizeof status_text);
    dhcp_state = DHCP_DISCOVER;
    dhcp_xid = rng_u32();
    dhcp_send(1);
    dhcp_timer = ticks_ms();
}
