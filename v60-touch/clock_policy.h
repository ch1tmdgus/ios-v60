/* SPDX-License-Identifier: GPL-2.0-only
 * Mirror gcc-kona.c / clk-branch.c for the five branches this driver votes.
 * Only wrap1 S_AHB (0x18008) declares hwcg_reg=0x18008, hwcg_bit=1.
 * For that branch Linux skips HALT polling while hardware gating is enabled.
 */
#pragma once
#include <stdint.h>
static inline int v60_clock_hwcg(unsigned offset,uint32_t value) {
    return offset==0x18008u && (value&2u)!=0;
}
static inline int v60_clock_ready(unsigned offset,uint32_t value) {
    return v60_clock_hwcg(offset,value) || !(value&(1u<<31));
}
