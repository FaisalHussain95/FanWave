// Fixed-code remote protocols: decode captured pulse trains into bits, and
// encode bits back into pulse trains for transmission.
//
// Plain C with no ESP-IDF dependencies, so it builds and is tested on the host
// (see test/host).
//
// A pulse train is an array of durations in microseconds, alternating
// mark (carrier on) and space (carrier off), starting with a mark.

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FW_MOD_OOK = 0,
    FW_MOD_FSK = 1,
} fw_modulation_t;

/**
 * One symbol's two halves in multiples of the protocol's base pulse. For
 * normal protocols the first half is a mark and the second a space; for
 * inverted ones the first half is a space and the second a mark.
 */
typedef struct {
    uint8_t mark;
    uint8_t space;
} fw_ratio_t;

/**
 * A fixed-code protocol in the rc-switch style: every symbol is one mark and
 * one space whose lengths are small multiples of a base pulse, and frames are
 * separated by a sync symbol.
 */
typedef struct {
    const char *id;   // stable id used by the API and stored fans, e.g. "pt2262"
    const char *name; // human name, e.g. "Princeton PT2262 / EV1527"
    uint16_t base_us; // nominal base pulse
    fw_ratio_t sync;
    fw_ratio_t zero;
    fw_ratio_t one;
    bool inverted;    // symbols are space-then-mark instead of mark-then-space
    uint8_t min_bits;
    uint8_t max_bits;
    uint8_t address_bits; // leading bits that identify the remote, 0 if unknown
    fw_modulation_t modulation;
} fw_protocol_t;

typedef struct {
    const fw_protocol_t *protocol;
    uint64_t bits;      // first received bit is the most significant
    uint8_t bit_count;
    uint16_t repeats;   // identical frames seen in the capture
    uint16_t base_us;   // base pulse measured from the capture
    float confidence;   // 0..1
} fw_match_t;

/** Timing summary of a capture, shown in the RF Explorer. 0 means unknown. */
typedef struct {
    uint32_t short_us;
    uint32_t long_us;
    uint32_t gap_us;    // longest space, usually the sync gap
    uint32_t total_us;
    size_t pulse_count; // marks
} fw_timing_t;

/** The built-in protocol table. */
const fw_protocol_t *fw_protocols(size_t *count);

/** Look up a protocol by id, or NULL. */
const fw_protocol_t *fw_protocol_find(const char *id);

/**
 * Try every known protocol against a capture. Writes up to max_out matches,
 * best first, and returns how many were written.
 */
size_t fw_decode(const uint32_t *durations, size_t count, fw_match_t *out, size_t max_out);

/**
 * Try one protocol. Returns true and fills *out when at least one complete
 * frame decodes.
 */
bool fw_decode_with(const fw_protocol_t *p, const uint32_t *durations, size_t count, fw_match_t *out);

/**
 * Encode a frame repeated `repeats` times, each followed by the sync symbol.
 * base_us 0 uses the protocol's nominal base. Returns the number of
 * durations written, or 0 if they don't fit in max_out.
 */
size_t fw_encode(const fw_protocol_t *p, uint64_t bits, uint8_t bit_count, uint16_t repeats,
                 uint16_t base_us, uint32_t *out, size_t max_out);

/** Summarise short/long pulse widths and the sync gap. */
fw_timing_t fw_analyze(const uint32_t *durations, size_t count);

#ifdef __cplusplus
}
#endif
