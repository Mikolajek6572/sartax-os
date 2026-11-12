// Sartax OS - matematyka dla JavaScriptu (koprocesor x87, 80-bit)
#include <math.h>
#include <stdint.h>

typedef long double ld;

static ld x_log2(ld x) { ld r; __asm__("fld1; fxch; fyl2x" : "=t"(r) : "0"(x)); return r; }

static ld x_exp2(ld t) {
    ld n, f, r;
    __asm__("frndint" : "=t"(n) : "0"(t));
    f = t - n;
    __asm__("f2xm1" : "=t"(r) : "0"(f));
    r += 1.0L;
    __asm__("fscale" : "=t"(r) : "0"(r), "u"(n));
    return r;
}

static ld x_atan2(ld y, ld x) { ld r; __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)"); return r; }
static ld x_sqrt(ld x) { ld r; __asm__("fsqrt" : "=t"(r) : "0"(x)); return r; }

// redukcja argumentu dla fsin/fcos (dziala do 2^63)
static ld reduce(ld x) {
    ld r;
    const ld twopi = 6.283185307179586476925286766559L;
    __asm__("1: fprem1\n\t"
            "fnstsw %%ax\n\t"
            "testw $0x400, %%ax\n\t"
            "jnz 1b"
            : "=t"(r) : "0"(x), "u"(twopi) : "ax");
    return r;
}

double fabs(double x) { return __builtin_fabs(x); }
float fabsf(float x) { return __builtin_fabsf(x); }
double copysign(double x, double y) { return __builtin_copysign(x, y); }

double sqrt(double x) {
    double r;
    __asm__("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}

double trunc(double x) {
    if (!(fabs(x) < 4503599627370496.0)) return x;    // 2^52, NaN, Inf
    double r = (double)(int64_t)x;
    return copysign(r, x);
}

double floor(double x) {
    double t = trunc(x);
    return t > x ? t - 1.0 : t;
}

double ceil(double x) {
    double t = trunc(x);
    return t < x ? t + 1.0 : (t == 0 ? copysign(0.0, x) : t);
}

double round(double x) {
    if (!(fabs(x) < 4503599627370496.0)) return x;
    double t = trunc(x);
    if (fabs(x - t) >= 0.5) t += copysign(1.0, x);
    return t;
}

long lrint(double x) {
    long r;
    __asm__("cvtsd2si %1, %0" : "=r"(r) : "x"(x));
    return r;
}

double rint(double x) {
    if (!(fabs(x) < 4503599627370496.0)) return x;
    return copysign((double)lrint(x), x);
}

double nearbyint(double x) { return rint(x); }

double fmod(double x, double y) {
    if (isnan(x) || isnan(y) || isinf(x) || y == 0) return NAN;
    if (isinf(y)) return x;
    ld r;
    __asm__("1: fprem\n\t"
            "fnstsw %%ax\n\t"
            "testw $0x400, %%ax\n\t"
            "jnz 1b"
            : "=t"(r) : "0"((ld)x), "u"((ld)y) : "ax");
    return (double)r;
}

double log2(double x) {
    if (isnan(x) || x < 0) return NAN;
    if (x == 0) return -INFINITY;
    if (isinf(x)) return x;
    return (double)x_log2(x);
}

double log(double x) {
    if (isnan(x) || x < 0) return NAN;
    if (x == 0) return -INFINITY;
    if (isinf(x)) return x;
    return (double)(x_log2(x) * 0.693147180559945309417232121458L);
}

double log10(double x) {
    if (isnan(x) || x < 0) return NAN;
    if (x == 0) return -INFINITY;
    if (isinf(x)) return x;
    return (double)(x_log2(x) * 0.301029995663981195213738894724L);
}

double log1p(double x) {
    if (fabs(x) < 1e-5) return x - x * x / 2 + x * x * x / 3;
    return log(1.0 + x);
}

static double exp2_ld(ld t) {
    if (t > 1100) return INFINITY;
    if (t < -1100) return 0;
    return (double)x_exp2(t);
}

double exp(double x) {
    if (isnan(x)) return x;
    if (isinf(x)) return x > 0 ? x : 0;
    return exp2_ld((ld)x * 1.44269504088896340735992468100L);
}

double expm1(double x) {
    if (fabs(x) < 1e-5) return x + x * x / 2 + x * x * x / 6;
    return exp(x) - 1.0;
}

static int odd_int(double y) {
    if (fabs(y) >= 9007199254740992.0 || y != trunc(y)) return 0;
    return ((int64_t)y) & 1;
}

double pow(double x, double y) {
    if (y == 0) return 1.0;
    if (isnan(x) || isnan(y)) return NAN;
    if (y == 1) return x;
    if (x == 0) {
        if (y > 0) return odd_int(y) ? x : 0.0;
        return odd_int(y) ? copysign(INFINITY, x) : INFINITY;
    }
    if (isinf(y)) {
        double ax = fabs(x);
        if (ax == 1) return NAN;
        return (ax > 1) == (y > 0) ? INFINITY : 0.0;
    }
    if (isinf(x)) {
        if (x > 0) return y > 0 ? INFINITY : 0.0;
        if (y > 0) return odd_int(y) ? -INFINITY : INFINITY;
        return odd_int(y) ? -0.0 : 0.0;
    }
    // male calkowite wykladniki - dokladne mnozenie
    if (y == trunc(y) && fabs(y) <= 64) {
        int n = (int)fabs(y);
        ld r = 1, b = x;
        while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
        return (double)(y < 0 ? 1 / r : r);
    }
    if (x < 0) {
        if (y != trunc(y)) return NAN;
        double r = exp2_ld((ld)y * x_log2(-(ld)x));
        return odd_int(y) ? -r : r;
    }
    return exp2_ld((ld)y * x_log2(x));
}

double sin(double x) {
    if (isnan(x) || isinf(x)) return NAN;
    ld r = reduce(x);
    __asm__("fsin" : "+t"(r));
    return (double)r;
}

double cos(double x) {
    if (isnan(x) || isinf(x)) return NAN;
    ld r = reduce(x);
    __asm__("fcos" : "+t"(r));
    return (double)r;
}

double tan(double x) {
    if (isnan(x) || isinf(x)) return NAN;
    ld r = reduce(x), s = r, c = r;
    __asm__("fsin" : "+t"(s));
    __asm__("fcos" : "+t"(c));
    return (double)(s / c);
}

double atan2(double y, double x) {
    if (isnan(x) || isnan(y)) return NAN;
    return (double)x_atan2(y, x);
}

double atan(double x) { return atan2(x, 1.0); }

double asin(double x) {
    if (fabs(x) > 1) return NAN;
    return (double)x_atan2(x, x_sqrt(1.0L - (ld)x * x));
}

double acos(double x) {
    if (fabs(x) > 1) return NAN;
    return (double)x_atan2(x_sqrt(1.0L - (ld)x * x), x);
}

double sinh(double x) {
    if (fabs(x) < 1e-5) return x;
    double e = exp(x);
    return (e - 1 / e) / 2;
}

double cosh(double x) {
    double e = exp(x);
    return (e + 1 / e) / 2;
}

double tanh(double x) {
    if (x > 20) return 1;
    if (x < -20) return -1;
    if (fabs(x) < 1e-5) return x;
    double e = exp(2 * x);
    return (e - 1) / (e + 1);
}

double asinh(double x) {
    if (isinf(x) || isnan(x)) return x;
    double ax = fabs(x);
    double r = log(ax + sqrt(ax * ax + 1));
    return copysign(r, x);
}

double acosh(double x) {
    if (x < 1) return NAN;
    return log(x + sqrt(x * x - 1));
}

double atanh(double x) {
    if (fabs(x) > 1) return NAN;
    return 0.5 * log((1 + x) / (1 - x));
}

double cbrt(double x) {
    if (x == 0 || isnan(x) || isinf(x)) return x;
    double ax = fabs(x);
    double r = exp2_ld(x_log2(ax) / 3.0L);
    r = r - (r * r * r - ax) / (3 * r * r);
    return copysign(r, x);
}

double hypot(double x, double y) {
    if (isinf(x) || isinf(y)) return INFINITY;
    if (isnan(x) || isnan(y)) return NAN;
    return (double)x_sqrt((ld)x * x + (ld)y * y);
}

double ldexp(double x, int e) {
    if (x == 0 || isnan(x) || isinf(x)) return x;
    ld r;
    __asm__("fscale" : "=t"(r) : "0"((ld)x), "u"((ld)e));
    return (double)r;
}

double scalbn(double x, int e) { return ldexp(x, e); }

double frexp(double x, int *e) {
    *e = 0;
    if (x == 0 || isnan(x) || isinf(x)) return x;
    union { double d; uint64_t u; } v = { x };
    int ex = (v.u >> 52) & 0x7FF;
    if (!ex) {           // liczba zdenormalizowana
        v.d *= 18014398509481984.0;   // 2^54
        ex = ((v.u >> 52) & 0x7FF) - 54;
    }
    *e = ex - 1022;
    v.u = (v.u & ~(0x7FFull << 52)) | (1022ull << 52);
    return v.d;
}

double modf(double x, double *ip) {
    double t = trunc(x);
    *ip = t;
    return isinf(x) ? copysign(0.0, x) : x - t;
}

double fmin(double a, double b) { return isnan(a) ? b : isnan(b) ? a : (a < b ? a : b); }
double fmax(double a, double b) { return isnan(a) ? b : isnan(b) ? a : (a > b ? a : b); }
