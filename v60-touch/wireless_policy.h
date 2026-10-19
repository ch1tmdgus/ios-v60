/* SPDX-License-Identifier: GPL-2.0-only
 * gcc-kona.c wrap0: only S_AHB declares HWCG bit1.
 */
#pragma once
#include <stdint.h>
static inline int v60_wireless_clock_ready(unsigned offset,uint32_t value) {
    return (offset==0x17008u && (value&2u)) || !(value&0x80000000u);
}
