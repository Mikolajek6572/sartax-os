// Sartax OS - minimalny stdio.h (wyjscie idzie na port szeregowy)
#pragma once
#include <stddef.h>
#include <stdarg.h>
typedef struct sx_file FILE;
extern FILE *stdout, *stderr, *stdin;
#define EOF (-1)
int printf(const char *fmt, ...);
int fprintf(FILE *f, const char *fmt, ...);
int vfprintf(FILE *f, const char *fmt, va_list ap);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int sprintf(char *buf, const char *fmt, ...);
int putchar(int c);
int fputc(int c, FILE *f);
int fputs(const char *s, FILE *f);
int puts(const char *s);
size_t fwrite(const void *p, size_t sz, size_t n, FILE *f);
int fflush(FILE *f);
#define putc fputc
