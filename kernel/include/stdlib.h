// Sartax OS - minimalny stdlib.h
#pragma once
#include <stddef.h>
void *malloc(size_t n);
void *calloc(size_t n, size_t m);
void *realloc(void *p, size_t n);
void free(void *p);
void abort(void) __attribute__((noreturn));
void exit(int c) __attribute__((noreturn));
int atoi(const char *s);
long strtol(const char *s, char **end, int base);
unsigned long strtoul(const char *s, char **end, int base);
long long strtoll(const char *s, char **end, int base);
unsigned long long strtoull(const char *s, char **end, int base);
double strtod(const char *s, char **end);
void qsort(void *base, size_t n, size_t sz, int (*cmp)(const void *, const void *));
int abs(int x);
long labs(long x);
char *getenv(const char *n);
#define alloca(n) __builtin_alloca(n)
