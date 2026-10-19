/* SPDX-License-Identifier: GPL-2.0-only
 * qcom-geni-se.h register fields, i2c-qcom-geni.c runtime_resume policy.
 * A FIFO-disabled engine needs a separate GSI transport, not a bypass.
 */
#pragma once
#include <stdint.h>
enum v60_engine_result {
    V60_ENGINE_OK, V60_ENGINE_PROTOCOL, V60_ENGINE_GSI,
    V60_ENGINE_WIDTH, V60_ENGINE_DEPTH, V60_ENGINE_BUSY
};
static inline unsigned v60_fifo_width(uint32_t v) { return (v>>24)&63u; }
static inline unsigned v60_fifo_depth(uint32_t v) { return (v>>16)&63u; }
static inline enum v60_engine_result v60_engine_check(
    uint32_t fw,uint32_t iface,uint32_t status,uint32_t tx,uint32_t rx) {
    if(((fw>>8)&255u)!=3) return V60_ENGINE_PROTOCOL;
    if(iface&1u) return V60_ENGINE_GSI;
    if(v60_fifo_width(tx)!=32 || v60_fifo_width(rx)!=32) return V60_ENGINE_WIDTH;
    if(v60_fifo_depth(tx)<2 || v60_fifo_depth(rx)<2) return V60_ENGINE_DEPTH;
    if(status&0x1001u) return V60_ENGINE_BUSY;
    return V60_ENGINE_OK;
}
