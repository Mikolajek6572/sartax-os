// Sartax OS - sterta (first-fit z laczeniem wolnych blokow)
#include "kernel.h"

typedef struct block {
    size_t size;            // rozmiar razem z naglowkiem
    struct block *next;     // tylko dla wolnych
} block_t;

#define HDR 16
#define USED_MAGIC ((struct block *)0x5A5A5A5A5A5A5A5Aull)

static block_t *free_list;
static u64 total_bytes, used_bytes;

static void add_region(u64 start, u64 end) {
    start = (start + 15) & ~15ull;
    end &= ~15ull;
    if (end <= start + 4096) return;
    block_t *b = (block_t *)start;
    b->size = end - start;
    b->next = NULL;
    // wstaw posortowane
    block_t **pp = &free_list;
    while (*pp && *pp < b) pp = &(*pp)->next;
    b->next = *pp;
    *pp = b;
    total_bytes += b->size;
}

void heap_init(void) {
    e820_t *e = (e820_t *)0x5100;
    u64 kend = (u64)pristine_image + (u64)(_image_end - (char *)0x100000);
    kend = (kend + 0xFFFF) & ~0xFFFFull;
    for (int i = 0; i < g_boot->e820_count; i++) {
        if (e[i].type != 1) continue;
        u64 s = e[i].base, t = e[i].base + e[i].len;
        if (t > 0x100000000ull) t = 0x100000000ull;     // mapujemy tylko 4 GB
        if (s < kend) s = kend;
        if (t > s) add_region(s, t);
    }
    if (!free_list) panic("Brak pamieci RAM");
}

void *kmalloc(size_t n) {
    if (!n) n = 1;
    size_t need = (n + HDR + 15) & ~15ull;
    u64 f = irq_save();
    block_t **pp = &free_list;
    while (*pp) {
        block_t *b = *pp;
        if (b->size >= need) {
            if (b->size - need >= 64) {
                block_t *rest = (block_t *)((u8 *)b + need);
                rest->size = b->size - need;
                rest->next = b->next;
                *pp = rest;
                b->size = need;
            } else {
                *pp = b->next;
            }
            b->next = USED_MAGIC;
            used_bytes += b->size;
            irq_restore(f);
            return (u8 *)b + HDR;
        }
        pp = &b->next;
    }
    irq_restore(f);
    panic("Zabraklo pamieci (kmalloc %u B)", (u32)n);
}

void *kzalloc(size_t n) {
    void *p = kmalloc(n);
    memset(p, 0, n);
    return p;
}

void kfree(void *p) {
    if (!p) return;
    block_t *b = (block_t *)((u8 *)p - HDR);
    if (b->next != USED_MAGIC) panic("kfree: uszkodzony blok %p", p);
    u64 f = irq_save();
    used_bytes -= b->size;
    block_t *prev = NULL, *cur = free_list;
    while (cur && cur < b) prev = cur, cur = cur->next;
    b->next = cur;
    if (prev) prev->next = b; else free_list = b;
    if (cur && (u8 *)b + b->size == (u8 *)cur) {
        b->size += cur->size;
        b->next = cur->next;
    }
    if (prev && (u8 *)prev + prev->size == (u8 *)b) {
        prev->size += b->size;
        prev->next = b->next;
    }
    irq_restore(f);
}

void *krealloc(void *p, size_t n) {
    if (!p) return kmalloc(n);
    block_t *b = (block_t *)((u8 *)p - HDR);
    size_t have = b->size - HDR;
    if (have >= n) return p;
    void *q = kmalloc(n);
    memcpy(q, p, have);
    kfree(p);
    return q;
}

u64 heap_free_bytes(void) { return total_bytes - used_bytes; }
u64 heap_total_bytes(void) { return total_bytes; }
