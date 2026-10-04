/**
 * @file test_main.c
 * @brief Host test runner.
 */
#include "unit.h"

int g_checks, g_fails;

void test_dsp_all(void);
void test_alt_all(void);

int main(void)
{
    test_dsp_all();
    test_alt_all();
    printf("\n%d checks, %d failures\n", g_checks, g_fails);
    return (g_fails == 0) ? 0 : 1;
}
