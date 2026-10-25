#include "fw_protocol.h"

#include <stdlib.h>
#include <string.h>

// How far a measured duration may stray from the expected one: a fraction of
// the expected length plus a fixed allowance for receiver edge jitter.
#define TOL_REL 0.35f
#define TOL_ABS_US 80u

#define MAX_FRAMES 48
#define MAX_SYNCS 64
#define MAX_ANALYZE 256

// rc-switch-style protocols. Timings are the widely documented on-air values
// for each chip family; see docs/protocols.md for sources.
static const fw_protocol_t PROTOCOLS[] = {
    {
        .id = "pt2262", .name = "Princeton PT2262 / EV1527", .base_us = 350,
        .sync = {1, 31}, .zero = {1, 3}, .one = {3, 1},
        .min_bits = 12, .max_bits = 32, .address_bits = 20, .modulation = FW_MOD_OOK,
    },
    {
        .id = "rcs2", .name = "Fixed code, 650 µs (rc-switch 2)", .base_us = 650,
        .sync = {1, 10}, .zero = {1, 2}, .one = {2, 1},
        .min_bits = 12, .max_bits = 32, .modulation = FW_MOD_OOK,
    },
    {
        .id = "rcs3", .name = "Fixed code, 100 µs (rc-switch 3)", .base_us = 100,
        .sync = {30, 71}, .zero = {4, 11}, .one = {9, 6},
        .min_bits = 12, .max_bits = 32, .modulation = FW_MOD_OOK,
    },
    {
        .id = "rcs4", .name = "Fixed code, 380 µs (rc-switch 4)", .base_us = 380,
        .sync = {1, 6}, .zero = {1, 3}, .one = {3, 1},
        .min_bits = 12, .max_bits = 32, .modulation = FW_MOD_OOK,
    },
    {
        .id = "rcs5", .name = "Fixed code, 500 µs (rc-switch 5)", .base_us = 500,
        .sync = {6, 14}, .zero = {1, 2}, .one = {2, 1},
        .min_bits = 12, .max_bits = 32, .modulation = FW_MOD_OOK,
    },
    {
        .id = "ht6p20b", .name = "Holtek HT6P20B", .base_us = 450,
        .sync = {23, 1}, .zero = {1, 2}, .one = {2, 1}, .inverted = true,
        .min_bits = 24, .max_bits = 28, .address_bits = 22, .modulation = FW_MOD_OOK,
    },
    {
        .id = "hs2303", .name = "HS2303-PT", .base_us = 150,
        .sync = {2, 62}, .zero = {1, 6}, .one = {6, 1},
        .min_bits = 12, .max_bits = 32, .modulation = FW_MOD_OOK,
    },
    // Inverted long-sync family: Holtek HT12E and the DL/Chungear fan remotes
    // (Hampton Bay, Harbor Breeze, Minka, Fanimation, Craftmade), usually
    // around 303–305 MHz. Frames are 12–13 bits: DIP address then command.
    {
        .id = "ht12e", .name = "Holtek HT12E", .base_us = 270,
        .sync = {36, 1}, .zero = {1, 2}, .one = {2, 1}, .inverted = true,
        .min_bits = 12, .max_bits = 13, .address_bits = 6, .modulation = FW_MOD_OOK,
    },
    {
        .id = "sm5212", .name = "SM5212", .base_us = 320,
        .sync = {36, 1}, .zero = {1, 2}, .one = {2, 1}, .inverted = true,
        .min_bits = 12, .max_bits = 13, .address_bits = 6, .modulation = FW_MOD_OOK,
    },
    {
        .id = "dl-fanimation", .name = "DL fan remote (Fanimation)", .base_us = 346,
        .sync = {35, 1}, .zero = {1, 2}, .one = {2, 1}, .inverted = true,
        .min_bits = 12, .max_bits = 13, .address_bits = 6, .modulation = FW_MOD_OOK,
    },
    {
        .id = "dl-hamptonbay", .name = "DL fan remote (Hampton Bay A25-TX028)", .base_us = 400,
        .sync = {34, 1}, .zero = {1, 2}, .one = {2, 1}, .inverted = true,
        .min_bits = 12, .max_bits = 24, .address_bits = 6, .modulation = FW_MOD_OOK,
    },
    {
        .id = "dl-fanhd", .name = "DL fan remote (fan-hd 201946)", .base_us = 361,
        .sync = {35, 2}, .zero = {1, 2}, .one = {2, 1}, .inverted = true,
        .min_bits = 12, .max_bits = 13, .address_bits = 6, .modulation = FW_MOD_OOK,
    },
};

#define PROTOCOL_COUNT (sizeof(PROTOCOLS) / sizeof(PROTOCOLS[0]))

const fw_protocol_t *fw_protocols(size_t *count)
{
    if (count) *count = PROTOCOL_COUNT;
    return PROTOCOLS;
}

const fw_protocol_t *fw_protocol_find(const char *id)
{
    if (!id) return NULL;
    for (size_t i = 0; i < PROTOCOL_COUNT; i++) {
        if (strcmp(PROTOCOLS[i].id, id) == 0) return &PROTOCOLS[i];
    }
    return NULL;
}

static bool close_to(uint32_t measured, float expected)
{
    float tol = expected * TOL_REL + (float)TOL_ABS_US;
    float diff = (float)measured - expected;
    return diff <= tol && diff >= -tol;
}

static bool symbol_matches(uint32_t a, uint32_t b, fw_ratio_t r, float base)
{
    return close_to(a, r.mark * base) && close_to(b, r.space * base);
}

/** If (a, b) looks like the protocol's sync symbol, return the base it implies, else 0. */
static float sync_base(const fw_protocol_t *p, uint32_t a, uint32_t b)
{
    // Estimate the base from the longer half of the sync, which is measured
    // more accurately, then check both halves against it.
    float base = p->sync.space >= p->sync.mark ? (float)b / p->sync.space : (float)a / p->sync.mark;
    if (base < p->base_us * 0.5f || base > p->base_us * 1.8f) return 0;
    return symbol_matches(a, b, p->sync, base) ? base : 0;
}

typedef struct {
    uint64_t bits;
    uint8_t len;
    bool edge;
    float base;
} frame_t;

/** Decode symbols [from, to) with the given base. Returns false on any unrecognised symbol. */
static bool decode_frame(const fw_protocol_t *p, const uint32_t *s, size_t from, size_t to, float base,
                         frame_t *f)
{
    size_t n = to - from;
    if (n < p->min_bits || n > p->max_bits) return false;
    uint64_t bits = 0;
    float units = 0, total = 0;
    for (size_t i = from; i < to; i++) {
        uint32_t a = s[2 * i], b = s[2 * i + 1];
        bool zero = symbol_matches(a, b, p->zero, base);
        bool one = symbol_matches(a, b, p->one, base);
        if (!zero && !one) return false;
        if (zero && one) {
            // Ambiguous: pick whichever expected first half is closer.
            float dz = (float)a - p->zero.mark * base, d1 = (float)a - p->one.mark * base;
            zero = dz * dz <= d1 * d1;
        }
        fw_ratio_t r = zero ? p->zero : p->one;
        bits = (bits << 1) | (zero ? 0u : 1u);
        units += r.mark + r.space;
        total += a + b;
    }
    f->bits = bits;
    f->len = (uint8_t)n;
    f->base = total / units;
    return true;
}

bool fw_decode_with(const fw_protocol_t *p, const uint32_t *durations, size_t count, fw_match_t *out)
{
    // Pair durations into symbols. Inverted protocols start each symbol with a
    // space, so skip the leading mark (its preceding space was never captured).
    const uint32_t *s = durations + (p->inverted ? 1 : 0);
    size_t avail = count - (p->inverted && count ? 1 : 0);
    size_t nsym = avail / 2;
    if (nsym < (size_t)p->min_bits + 1) return false;

    size_t syncs[MAX_SYNCS];
    float sync_bases[MAX_SYNCS];
    size_t nsync = 0;
    for (size_t i = 0; i < nsym && nsync < MAX_SYNCS; i++) {
        float base = sync_base(p, s[2 * i], s[2 * i + 1]);
        if (base > 0) {
            syncs[nsync] = i;
            sync_bases[nsync] = base;
            nsync++;
        }
    }
    if (nsync == 0) return false;

    frame_t frames[MAX_FRAMES];
    size_t nframes = 0, attempted = 0;

    // A frame is the run of symbols between two syncs. The run before the
    // first sync counts too (the capture may begin on a frame boundary), as
    // does the run after the last one.
    for (size_t k = 0; k <= nsync && nframes < MAX_FRAMES; k++) {
        size_t from = k == 0 ? 0 : syncs[k - 1] + 1;
        size_t to = k == nsync ? nsym : syncs[k];
        float base = sync_bases[k == nsync ? nsync - 1 : k];
        if (to <= from) continue;
        // A capture usually starts and ends mid-frame, so a run at either edge
        // that fails to decode doesn't count against the protocol.
        bool edge = k == 0 || k == nsync;
        frame_t f;
        bool ok = decode_frame(p, s, from, to, base, &f);
        if (ok) {
            f.edge = edge;
            frames[nframes++] = f;
        }
        if (ok || !edge) attempted++;
    }

    // An edge run can also be a cut-off frame that happens to decode as a
    // shorter one. When complete frames exist, keep only edge frames of the
    // same length.
    int interior_len = -1;
    for (size_t i = 0; i < nframes; i++) {
        if (!frames[i].edge) {
            interior_len = frames[i].len;
            break;
        }
    }
    if (interior_len >= 0) {
        size_t kept = 0;
        for (size_t i = 0; i < nframes; i++) {
            if (frames[i].edge && frames[i].len != interior_len) {
                attempted--;
                continue;
            }
            frames[kept++] = frames[i];
        }
        nframes = kept;
    }
    if (nframes == 0) return false;

    // The most frequent frame wins.
    size_t best = 0, best_count = 0;
    for (size_t i = 0; i < nframes; i++) {
        size_t c = 0;
        for (size_t j = 0; j < nframes; j++) {
            if (frames[j].bits == frames[i].bits && frames[j].len == frames[i].len) c++;
        }
        if (c > best_count) {
            best = i;
            best_count = c;
        }
    }

    float base_sum = 0;
    for (size_t j = 0; j < nframes; j++) {
        if (frames[j].bits == frames[best].bits && frames[j].len == frames[best].len) base_sum += frames[j].base;
    }

    float repeat_factor = best_count >= 3 ? 0.95f : best_count == 2 ? 0.8f : 0.55f;
    float valid_share = attempted ? (float)nframes / (float)attempted : 1.0f;
    float agreement = (float)best_count / (float)nframes;

    out->protocol = p;
    out->bits = frames[best].bits;
    out->bit_count = frames[best].len;
    out->repeats = (uint16_t)best_count;
    out->base_us = (uint16_t)(base_sum / best_count + 0.5f);
    out->confidence = repeat_factor * valid_share * agreement;
    return true;
}

static int by_confidence(const void *a, const void *b)
{
    const fw_match_t *x = a, *y = b;
    if (x->confidence != y->confidence) return x->confidence < y->confidence ? 1 : -1;
    return (int)y->repeats - (int)x->repeats;
}

size_t fw_decode(const uint32_t *durations, size_t count, fw_match_t *out, size_t max_out)
{
    fw_match_t found[PROTOCOL_COUNT];
    size_t n = 0;
    for (size_t i = 0; i < PROTOCOL_COUNT; i++) {
        if (fw_decode_with(&PROTOCOLS[i], durations, count, &found[n])) n++;
    }
    qsort(found, n, sizeof(found[0]), by_confidence);
    if (n > max_out) n = max_out;
    memcpy(out, found, n * sizeof(found[0]));
    return n;
}

size_t fw_encode(const fw_protocol_t *p, uint64_t bits, uint8_t bit_count, uint16_t repeats,
                 uint16_t base_us, uint32_t *out, size_t max_out)
{
    if (!p || bit_count == 0 || bit_count > 64 || repeats == 0) return 0;
    uint32_t base = base_us ? base_us : p->base_us;
    size_t need = (size_t)repeats * ((size_t)bit_count + 1) * 2;
    // Inverted frames drop the leading space: there's nothing to transmit before the first mark.
    size_t skip = p->inverted ? 1 : 0;
    if (need - skip > max_out) return 0;

    size_t w = 0, pos = 0;
#define EMIT(v)                              \
    do {                                     \
        if (pos++ >= skip) out[w++] = (v);   \
    } while (0)
    for (uint16_t r = 0; r < repeats; r++) {
        for (int i = bit_count - 1; i >= 0; i--) {
            fw_ratio_t sym = (bits >> i) & 1 ? p->one : p->zero;
            EMIT(sym.mark * base);
            EMIT(sym.space * base);
        }
        EMIT(p->sync.mark * base);
        EMIT(p->sync.space * base);
    }
#undef EMIT
    return w;
}

bool fw_frame_address(const fw_protocol_t *p, uint64_t bits, uint8_t bit_count, uint64_t *address)
{
    if (!p || !p->address_bits || p->address_bits >= bit_count || bit_count > 64) return false;
    *address = bits >> (bit_count - p->address_bits);
    return true;
}

bool fw_frame_with_address(const fw_protocol_t *p, uint64_t bits, uint8_t bit_count, uint64_t address,
                           uint64_t *out)
{
    uint64_t old;
    if (!fw_frame_address(p, bits, bit_count, &old)) return false;
    if (p->address_bits < 64 && address >> p->address_bits) return false;
    uint8_t command_bits = bit_count - p->address_bits;
    uint64_t command = bits & ((1ULL << command_bits) - 1);
    *out = (address << command_bits) | command;
    return true;
}

size_t fw_repeat_train(const uint32_t *press, size_t count, uint32_t gap_us, uint32_t total_us, uint32_t *out,
                       size_t max)
{
    if (!count) return 0;
    bool ends_on_mark = count % 2 == 1;
    size_t per = count + (ends_on_mark ? 1 : 0);
    uint64_t press_us = ends_on_mark ? gap_us : 0;
    for (size_t i = 0; i < count; i++) press_us += press[i];
    size_t n = 0;
    uint64_t total = 0;
    do {
        if (n + per > max) break;
        for (size_t i = 0; i < count; i++) out[n++] = press[i];
        if (ends_on_mark) out[n++] = gap_us;
        total += press_us;
    } while (total < total_us && press_us > 0);
    return n;
}

static int cmp_u32(const void *a, const void *b)
{
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

fw_timing_t fw_analyze(const uint32_t *durations, size_t count)
{
    fw_timing_t t = {0};
    uint32_t v[MAX_ANALYZE];
    size_t nv = 0;

    for (size_t i = 0; i < count; i++) {
        t.total_us += durations[i];
        if (i % 2 == 0) t.pulse_count++;
        else if (durations[i] > t.gap_us) t.gap_us = durations[i];
    }
    // Short and long widths come from symbols, not sync gaps: leave out anything
    // over four times the median.
    for (size_t i = 0; i < count && nv < MAX_ANALYZE; i++) v[nv++] = durations[i];
    if (nv < 4) return t;
    qsort(v, nv, sizeof(v[0]), cmp_u32);
    uint32_t median = v[nv / 2];
    while (nv > 2 && v[nv - 1] > median * 4) nv--;

    // Split at the largest ratio jump between neighbouring widths.
    size_t split = 0;
    float best = 1.6f;
    for (size_t i = 0; i + 1 < nv; i++) {
        float ratio = (float)v[i + 1] / (float)(v[i] ? v[i] : 1);
        if (ratio > best) {
            best = ratio;
            split = i + 1;
        }
    }
    if (split == 0) {
        t.short_us = median;
        return t;
    }
    t.short_us = v[split / 2];
    t.long_us = v[split + (nv - split) / 2];
    return t;
}
