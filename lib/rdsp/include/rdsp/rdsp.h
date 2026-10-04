/**
 * @file rdsp.h
 * @brief Umbrella header of the rdsp radar signal processing library.
 *
 * Processing chain (see docs/signal_chain.md):
 *   0 rdsp_agc       gain control from frame statistics
 *   1 rdsp_frame     ADC -> float, statistics, DC/trend/background removal
 *   2 rdsp_window    window functions + scaling sums
 *   3 rdsp_fft       real FFT
 *   4 rdsp_spectrum  PSD, Welch, averaging across ramps
 *   5 rdsp_cfar      CFAR detection (CA/GO/SO/OS)
 *   6 rdsp_peak      sub-bin interpolation
 *   7 rdsp_fmcw      beat <-> range, Doppler, up/down combination
 *   8 rdsp_track     alpha-beta tracker with gating
 */
#ifndef RDSP_H
#define RDSP_H

#include "rdsp_common.h"
#include "rdsp_agc.h"
#include "rdsp_frame.h"
#include "rdsp_window.h"
#include "rdsp_fft.h"
#include "rdsp_spectrum.h"
#include "rdsp_cfar.h"
#include "rdsp_peak.h"
#include "rdsp_fmcw.h"
#include "rdsp_track.h"

#endif /* RDSP_H */
