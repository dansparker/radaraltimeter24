/**
 * @file rdsp_window.c
 * @brief Step 2 - window functions (see rdsp_window.h).
 */
#include "rdsp/rdsp_window.h"
#include <math.h>

int rdsp_window_make(float *w, uint32_t n, rdsp_win_type_t type, rdsp_win_info_t *info)
{
    if ((w == NULL) || (n == 0u)) { return -1; }

    double s1 = 0.0, s2 = 0.0;
    for (uint32_t i = 0; i < n; i++) {
        const double a = 2.0 * 3.14159265358979323846 * (double)i / (double)n;
        double v;
        switch (type) {
        case RDSP_WIN_HANN:
            v = 0.5 - 0.5 * cos(a);
            break;
        case RDSP_WIN_HAMMING:
            v = 0.54 - 0.46 * cos(a);
            break;
        case RDSP_WIN_BLACKMAN_HARRIS:
            v = 0.35875 - 0.48829 * cos(a) + 0.14128 * cos(2.0 * a) - 0.01168 * cos(3.0 * a);
            break;
        case RDSP_WIN_RECT:
        default:
            v = 1.0;
            break;
        }
        w[i] = (float)v;
        s1 += v;
        s2 += v * v;
    }
    if (info != NULL) {
        info->s1 = (float)s1;
        info->s2 = (float)s2;
        info->cg = (float)(s1 / (double)n);
        info->enbw_bins = (float)((double)n * s2 / (s1 * s1));
    }
    return 0;
}

void rdsp_window_apply(float *x, const float *w, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        x[i] *= w[i];
    }
}
