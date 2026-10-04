/**
 * @file rdsp_common.h
 * @brief Common definitions of the rdsp (radar DSP) library.
 *
 * rdsp is a small, portable C99 library for FMCW radar signal processing.
 * Design rules:
 *  - no dynamic memory, all buffers are provided by the caller
 *  - single precision float only (fast on Cortex-M4F), no double in hot paths
 *  - every module is independent and documented in docs/signal_chain.md
 */
#ifndef RDSP_COMMON_H
#define RDSP_COMMON_H

#include <stdint.h>
#include <stddef.h>

#define RDSP_C0_MPS   299792458.0f   /**< speed of light [m/s] */
#define RDSP_PI       3.14159265358979f
#define RDSP_EPS      1.0e-30f       /**< floor to avoid log(0) / div by 0 */

#define RDSP_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define RDSP_MAX(a, b) (((a) > (b)) ? (a) : (b))

#endif /* RDSP_COMMON_H */
