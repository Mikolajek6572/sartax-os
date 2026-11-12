#pragma once
typedef unsigned long jmp_buf[8];
int setjmp(jmp_buf b) __attribute__((returns_twice));
void longjmp(jmp_buf b, int v) __attribute__((noreturn));
