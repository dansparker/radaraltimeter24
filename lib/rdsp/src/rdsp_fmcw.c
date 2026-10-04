/**
 * @file rdsp_fmcw.c
 * @brief Step 7 - FMCW conversions (see rdsp_fmcw.h).
 */
#include "rdsp/rdsp_fmcw.h"

float rdsp_fmcw_range(const rdsp_fmcw_t *f, float f_r)
{
    return RDSP_C0_MPS * f_r / (2.0f * f->slope_hz_s) + f->r_offset_m;
}

float rdsp_fmcw_beat(const rdsp_fmcw_t *f, float range_m)
{
    return (range_m - f->r_offset_m) * 2.0f * f->slope_hz_s / RDSP_C0_MPS;
}

float rdsp_fmcw_doppler(const rdsp_fmcw_t *f, float closing_mps)
{
    return 2.0f * closing_mps / f->lambda_m;
}

float rdsp_fmcw_closing_speed(const rdsp_fmcw_t *f, float f_d)
{
    return 0.5f * f_d * f->lambda_m;
}

int rdsp_fmcw_updown(float f_up, float f_dn, rdsp_ud_hyp_t h, float *f_r, float *f_d)
{
    float r, d;
    if ((f_up < 0.0f) || (f_dn < 0.0f)) { return -1; }
    switch (h) {
    case RDSP_UD_NORMAL:
        r = 0.5f * (f_up + f_dn);
        d = 0.5f * (f_dn - f_up);
        if (r < ((d < 0.0f) ? -d : d)) { return -1; }
        break;
    case RDSP_UD_UP_MIRRORED:
        r = 0.5f * (f_dn - f_up);
        d = 0.5f * (f_dn + f_up);
        if ((r < 0.0f) || (d <= r)) { return -1; }
        break;
    case RDSP_UD_DOWN_MIRRORED:
        r = 0.5f * (f_up - f_dn);
        d = -0.5f * (f_up + f_dn);
        if ((r < 0.0f) || (-d <= r)) { return -1; }
        break;
    default:
        return -1;
    }
    *f_r = r;
    *f_d = d;
    return 0;
}
