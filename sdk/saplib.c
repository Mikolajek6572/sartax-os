// Sartax OS SDK - funkcje, ktorych kompilator moze potrzebowac w programie .sap
#include <stddef.h>

__attribute__((visibility("hidden"))) void *memset(void *d, int c, size_t n) {
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

__attribute__((visibility("hidden"))) void *memcpy(void *d, const void *s, size_t n) {
    unsigned char *a = d;
    const unsigned char *b = s;
    while (n--) *a++ = *b++;
    return d;
}
