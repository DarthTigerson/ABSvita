/* Minimal host test harness. Each test_*.c defines TEST(name) functions and a
 * suite function registered in tests/main.c. */
#ifndef ABSVITA_TEST_H
#define ABSVITA_TEST_H
#include <math.h>
#include <stdio.h>
#include <string.h>

extern int t_failures, t_checks;
extern const char *t_current;

#define TEST(name) static void name(void)
#define RUN(name) do { t_current = #name; name(); } while (0)

#define CHECK(cond) do { t_checks++; if (!(cond)) { t_failures++; \
    printf("  FAIL %s:%d [%s] %s\n", __FILE__, __LINE__, t_current, #cond); } } while (0)
#define CHECK_EQ_INT(a, b) do { long long _a = (long long)(a), _b = (long long)(b); t_checks++; \
    if (_a != _b) { t_failures++; printf("  FAIL %s:%d [%s] %s == %lld, expected %lld\n", \
    __FILE__, __LINE__, t_current, #a, _a, _b); } } while (0)
#define CHECK_NEAR(a, b, eps) do { double _a = (a), _b = (b); t_checks++; \
    if (fabs(_a - _b) > (eps)) { t_failures++; printf("  FAIL %s:%d [%s] %s == %f, expected %f\n", \
    __FILE__, __LINE__, t_current, #a, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { const char *_a = (a), *_b = (b); t_checks++; \
    if (strcmp(_a ? _a : "(null)", _b) != 0) { t_failures++; printf("  FAIL %s:%d [%s] %s == \"%s\", expected \"%s\"\n", \
    __FILE__, __LINE__, t_current, #a, _a ? _a : "(null)", _b); } } while (0)

#endif
