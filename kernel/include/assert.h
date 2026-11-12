#pragma once
void __sx_assert_fail(const char *e, const char *f, int l) __attribute__((noreturn));
#ifdef NDEBUG
#define assert(e) ((void)0)
#else
#define assert(e) ((e) ? (void)0 : __sx_assert_fail(#e, __FILE__, __LINE__))
#endif
