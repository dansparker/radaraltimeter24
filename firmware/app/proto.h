/**
 * @file proto.h
 * @brief Output protocols: UART text sentences and CAN frames (no printf).
 *
 * UART (NMEA-like, XOR checksum over the characters between '$' and '*'):
 *   $RALT,<seq>,<alt_m>,<vs_mps>,<snr_db>,<gain>,<status_hex>*CS\r\n
 *   $RDBG,<seq>,<raw_m>,<f_rise>,<f_fall>,<f_r>,<track_state>,<L|S ramp>*CS\r\n
 * Empty fields mean "not available".
 *
 * CAN (standard id, DLC 8, little endian):
 *   id+0: int32 altitude [mm] | int16 vertical speed [cm/s] | uint8 status (low byte)
 *         | uint8 (counter 0..15) | (gain << 4) | (degraded << 6) | (busy << 7)
 *   id+1: uint16 status | uint8 SNR [dB] | int8 track state | uint32 seq
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include <stddef.h>
#include "altimeter.h"

/** Format a float with fixed decimals; NAN/Inf gives an empty string. */
size_t proto_fmt_fixed(char *dst, float v, unsigned decimals);

size_t proto_ralt(char *buf, size_t cap, const alt_output_t *o);
size_t proto_rdbg(char *buf, size_t cap, const alt_output_t *o);

/** XOR checksum of the text between '$' and '*' */
uint8_t proto_nmea_cs(const char *s, size_t len);

void proto_can_alt(uint8_t d[8], const alt_output_t *o, uint8_t counter);
void proto_can_diag(uint8_t d[8], const alt_output_t *o);

#endif /* PROTO_H */
