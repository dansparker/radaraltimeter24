/**
 * @file unit.h
 * @brief Minimal test framework.
 */
#ifndef UNIT_H
#define UNIT_H

#include <stdio.h>
#include <math.h>

extern int g_checks, g_fails;

#define CHECK(c) do { g_checks++; if (!(c)) { g_fails++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

#define CHECK_NEAR(a, b, tol) do { const double a_ = (double)(a), b_ = (double)(b); g_checks++; \
    if (!(fabs(a_ - b_) <= (double)(tol))) { g_fails++; \
    printf("  FAIL %s:%d: %s = %.6g, expected %.6g +- %.3g\n", __FILE__, __LINE__, #a, a_, b_, (double)(tol)); } } while (0)

#define RUN(fn) do { printf("%s\n", #fn); fn(); } while (0)

#endif /* UNIT_H */
