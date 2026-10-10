#include "mp4.h"

#include <math.h>
#include <string.h>

#define FOURCC(a, b, c, d) ((uint32_t)(a) << 24 | (uint32_t)(b) << 16 | (uint32_t)(c) << 8 | (uint32_t)(d))

typedef struct {
    uint32_t type;
    uint64_t off;  /* start of the box */
    uint64_t body; /* start of payload */
    uint64_t end;
} Box;

/* Reads a box header at off, bounded by limit. 0 ok. */
static int box_at(const Reader *r, uint64_t off, uint64_t limit, Box *b) {
    uint8_t h[16];
    if (off + 8 > limit) return -1;
    if (reader_read_full(r, off, h, 8) != 0) return -1;
    uint64_t size = rd_be32(h);
    b->type = rd_be32(h + 4);
    b->off = off;
    b->body = off + 8;
    if (size == 1) {
        if (reader_read_full(r, off + 8, h + 8, 8) != 0) return -1;
        size = rd_be64(h + 8);
        b->body = off + 16;
        if (size < 16) return -1;
    } else if (size == 0) {
        size = limit - off;
    } else if (size < 8) {
        return -1;
    }
    b->end = off + size;
    if (b->end > limit) return -1;
    return 0;
}

/* Finds the first child of type within [start, end). */
static int find_child(const Reader *r, uint64_t start, uint64_t end, uint32_t type, Box *out) {
    uint64_t off = start;
    Box b;
    while (off + 8 <= end) {
        if (box_at(r, off, end, &b) != 0) return -1;
        if (b.type == type) { *out = b; return 0; }
        off = b.end;
    }
    return -1;
}

int mp4_find_moov(const Reader *r, uint64_t *off, uint64_t *size) {
    Box b;
    if (find_child(r, 0, r->size, FOURCC('m', 'o', 'o', 'v'), &b) != 0) return -1;
    *off = b.off;
    *size = b.end - b.off;
    return 0;
}

/* ---- tables ---------------------------------------------------------- */

static int table_init(const Reader *r, Mp4Table *t, const Box *b, uint32_t header, uint32_t esize) {
    uint8_t h[16];
    /* body: version/flags, `header` extra bytes, entry count, entries */
    if (reader_read_full(r, b->body, h, 8 + header) != 0) return -1;
    t->count = rd_be32(h + 4 + header);
    t->esize = esize;
    t->off = b->body + 8 + header;
    t->cfirst = t->cn = 0;
    if (t->off + (uint64_t)t->count * esize > b->end) return -1;
    return 0;
}

/* Pointer to entry i (valid until the next call). */
static const uint8_t *table_get(const Reader *r, Mp4Table *t, uint32_t i) {
    if (i >= t->count) return NULL;
    if (i < t->cfirst || i >= t->cfirst + t->cn) {
        uint32_t per = sizeof t->cache / t->esize;
        uint32_t first = i;
        uint32_t n = t->count - first;
        if (n > per) n = per;
        if (reader_read_full(r, t->off + (uint64_t)first * t->esize, t->cache, n * t->esize) != 0) return NULL;
        t->cfirst = first;
        t->cn = n;
    }
    return t->cache + (i - t->cfirst) * t->esize;
}

static uint32_t sample_size(Mp4 *m, uint32_t i) {
    if (m->const_size) return m->const_size;
    const uint8_t *p = table_get(m->r, &m->stsz, i);
    return p ? rd_be32(p) : 0;
}

static int chunk_offset(Mp4 *m, uint32_t chunk1, uint64_t *off) {
    const uint8_t *p = table_get(m->r, &m->stco, chunk1 - 1);
    if (!p) return -1;
    *off = m->stco.esize == 8 ? rd_be64(p) : rd_be32(p);
    return 0;
}

/* ---- esds / sample entry ------------------------------------------------ */

static int desc_len(const uint8_t *p, const uint8_t *end, uint32_t *len) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        if (p + i >= end) return -1;
        v = v << 7 | (p[i] & 0x7f);
        if (!(p[i] & 0x80)) { *len = v; return i + 1; }
    }
    return -1;
}

static void parse_esds(Mp4 *m, const uint8_t *p, const uint8_t *end) {
    p += 4; /* version/flags */
    while (p + 2 <= end) {
        uint8_t tag = *p++;
        uint32_t len;
        int k = desc_len(p, end, &len);
        if (k < 0) return;
        p += k;
        const uint8_t *body = p;
        if (body + len > end) len = (uint32_t)(end - body);
        if (tag == 0x03) { /* ES_Descriptor: descend */
            if (len < 3) return;
            uint8_t flags = body[2];
            const uint8_t *q = body + 3;
            if (flags & 0x80) q += 2;
            if (flags & 0x40) { if (q >= end) return; q += 1 + *q; }
            if (flags & 0x20) q += 2;
            p = q;
            continue;
        }
        if (tag == 0x04) { /* DecoderConfigDescriptor: descend after 13 bytes */
            if (len < 13) return;
            uint8_t oti = body[0];
            if (oti == 0x40 || oti == 0x66 || oti == 0x67 || oti == 0x68) m->codec = MP4_AAC;
            else if (oti == 0x69 || oti == 0x6B) m->codec = MP4_MP3;
            p = body + 13;
            continue;
        }
        if (tag == 0x05) { /* DecoderSpecificInfo = AudioSpecificConfig */
            m->asc_len = len > sizeof m->asc ? (int)sizeof m->asc : (int)len;
            memcpy(m->asc, body, m->asc_len);
            return;
        }
        p = body + len;
    }
}

static int parse_stsd(Mp4 *m, const Box *stsd) {
    uint8_t buf[512];
    Box e;
    if (box_at(m->r, stsd->body + 8, stsd->end, &e) != 0) return -1;
    uint64_t n = e.end - e.body;
    if (n > sizeof buf) n = sizeof buf;
    if (n < 28 || reader_read_full(m->r, e.body, buf, (uint32_t)n) != 0) return -1;
    uint16_t version = rd_be16(buf + 8);
    m->channels = rd_be16(buf + 16);
    m->sample_rate = rd_be32(buf + 24) >> 16;
    uint32_t child = 28 + (version == 1 ? 16 : version == 2 ? 36 : 0);
    if (e.type == FOURCC('.', 'm', 'p', '3') || e.type == FOURCC('m', 'p', '3', ' ')) {
        m->codec = MP4_MP3;
        return 0;
    }
    if (e.type != FOURCC('m', 'p', '4', 'a')) return -1;
    /* scan children of the sample entry for esds (may be nested in 'wave') */
    for (uint32_t off = child; off + 8 <= n;) {
        uint32_t sz = rd_be32(buf + off), ty = rd_be32(buf + off + 4);
        if (sz < 8 || off + sz > n) break;
        if (ty == FOURCC('e', 's', 'd', 's')) { parse_esds(m, buf + off + 8, buf + off + sz); break; }
        if (ty == FOURCC('w', 'a', 'v', 'e')) { off += 8; continue; }
        off += sz;
    }
    return m->codec ? 0 : -1;
}

/* ---- open ---------------------------------------------------------------- */

static int open_trak(Mp4 *m, const Box *trak) {
    const Reader *r = m->r;
    Box mdia, hdlr, mdhd, minf, stbl, b;
    uint8_t h[32];
    if (find_child(r, trak->body, trak->end, FOURCC('m', 'd', 'i', 'a'), &mdia) != 0) return -1;
    if (find_child(r, mdia.body, mdia.end, FOURCC('h', 'd', 'l', 'r'), &hdlr) != 0) return -1;
    if (reader_read_full(r, hdlr.body, h, 12) != 0 || rd_be32(h + 8) != FOURCC('s', 'o', 'u', 'n')) return -1;
    if (find_child(r, mdia.body, mdia.end, FOURCC('m', 'd', 'h', 'd'), &mdhd) != 0) return -1;
    if (reader_read_full(r, mdhd.body, h, 32) != 0) return -1;
    if (h[0] == 1) { m->timescale = rd_be32(h + 20); m->duration_ts = rd_be64(h + 24); }
    else { m->timescale = rd_be32(h + 12); m->duration_ts = rd_be32(h + 16); }
    if (!m->timescale) return -1;
    if (find_child(r, mdia.body, mdia.end, FOURCC('m', 'i', 'n', 'f'), &minf) != 0) return -1;
    if (find_child(r, minf.body, minf.end, FOURCC('s', 't', 'b', 'l'), &stbl) != 0) return -1;

    if (find_child(r, stbl.body, stbl.end, FOURCC('s', 't', 's', 'd'), &b) != 0 || parse_stsd(m, &b) != 0) return -1;
    if (find_child(r, stbl.body, stbl.end, FOURCC('s', 't', 't', 's'), &b) != 0 || table_init(r, &m->stts, &b, 0, 8) != 0) return -1;
    if (find_child(r, stbl.body, stbl.end, FOURCC('s', 't', 's', 'c'), &b) != 0 || table_init(r, &m->stsc, &b, 0, 12) != 0) return -1;
    if (find_child(r, stbl.body, stbl.end, FOURCC('s', 't', 's', 'z'), &b) != 0) return -1;
    if (reader_read_full(r, b.body + 4, h, 4) != 0) return -1;
    m->const_size = rd_be32(h);
    if (table_init(r, &m->stsz, &b, 4, 4) != 0) {
        if (!m->const_size) return -1;
        /* constant size: no entries follow, only the count */
        uint8_t c[4];
        if (reader_read_full(r, b.body + 8, c, 4) != 0) return -1;
        m->stsz.count = rd_be32(c);
    }
    m->nsamples = m->stsz.count;
    if (find_child(r, stbl.body, stbl.end, FOURCC('s', 't', 'c', 'o'), &b) == 0) {
        if (table_init(r, &m->stco, &b, 0, 4) != 0) return -1;
    } else if (find_child(r, stbl.body, stbl.end, FOURCC('c', 'o', '6', '4'), &b) == 0) {
        if (table_init(r, &m->stco, &b, 0, 8) != 0) return -1;
    } else {
        return -1;
    }
    m->nchunks = m->stco.count;
    if (!m->nsamples || !m->nchunks || !m->stsc.count || !m->stts.count) return -1;
    return 0;
}

int mp4_open(Mp4 *m, const Reader *r) {
    uint64_t off, size;
    if (mp4_find_moov(r, &off, &size) != 0) return -1;
    return mp4_open_at(m, r, off, size);
}

int mp4_open_at(Mp4 *m, const Reader *r, uint64_t moov_off, uint64_t moov_size) {
    memset(m, 0, sizeof *m);
    m->r = r;
    m->moov_off = moov_off;
    m->moov_size = moov_size;
    Box moov, b;
    if (box_at(r, m->moov_off, m->moov_off + m->moov_size, &moov) != 0) return -1;
    uint64_t off = moov.body;
    while (off + 8 <= moov.end) {
        if (box_at(r, off, moov.end, &b) != 0) return -1;
        if (b.type == FOURCC('t', 'r', 'a', 'k')) {
            const Reader *keep = m->r;
            uint64_t mo = m->moov_off, ms = m->moov_size;
            if (open_trak(m, &b) == 0) {
                if (m->codec == MP4_AAC && m->asc_len >= 2) {
                    int aot, rate, ch;
                    if (aac_parse_asc(m->asc, m->asc_len, &aot, &rate, &ch) == 0) {
                        if (rate) m->sample_rate = rate;
                        if (ch) m->channels = ch;
                    }
                }
                return mp4_seek(m, 0, NULL);
            }
            memset(m, 0, sizeof *m);
            m->r = keep;
            m->moov_off = mo;
            m->moov_size = ms;
        }
        off = b.end;
    }
    return -1;
}

double mp4_duration(const Mp4 *m) {
    return m->timescale ? (double)m->duration_ts / m->timescale : 0;
}

/* ---- cursor ---------------------------------------------------------------- */

static int stsc_entry(Mp4 *m, uint32_t i, uint32_t *first, uint32_t *spc) {
    const uint8_t *p = table_get(m->r, &m->stsc, i);
    if (!p) return -1;
    *first = rd_be32(p);
    *spc = rd_be32(p + 4);
    return 0;
}

static void load_run(Mp4 *m) {
    uint32_t first, spc;
    stsc_entry(m, m->stsc_i, &first, &spc);
    m->spc = spc ? spc : 1;
    if (m->stsc_i + 1 < m->stsc.count && stsc_entry(m, m->stsc_i + 1, &first, &spc) == 0) m->next_run = first;
    else m->next_run = UINT32_MAX;
}

int mp4_seek(Mp4 *m, double seconds, double *actual) {
    if (!m->nsamples) return -1;
    if (seconds < 0) seconds = 0;
    uint64_t target = (uint64_t)llround(seconds * m->timescale);
    /* time -> sample via stts */
    uint64_t acc_t = 0;
    uint32_t base = 0, s = m->nsamples - 1;
    uint64_t s_time = 0;
    int found = 0;
    for (uint32_t i = 0; i < m->stts.count; i++) {
        const uint8_t *p = table_get(m->r, &m->stts, i);
        if (!p) return -1;
        uint32_t cnt = rd_be32(p), delta = rd_be32(p + 4);
        uint64_t span = (uint64_t)cnt * delta;
        if (cnt && target < acc_t + span) {
            uint32_t k = delta ? (uint32_t)((target - acc_t) / delta) : 0;
            s = base + k;
            s_time = acc_t + (uint64_t)k * delta;
            found = 1;
            break;
        }
        acc_t += span;
        base += cnt;
    }
    if (!found) {
        /* past the end: last sample; its start time is total - last delta */
        const uint8_t *p = table_get(m->r, &m->stts, m->stts.count - 1);
        uint32_t delta = p ? rd_be32(p + 4) : 0;
        s_time = acc_t >= delta ? acc_t - delta : 0;
    }
    if (s >= m->nsamples) s = m->nsamples - 1;

    /* sample -> chunk via stsc */
    uint32_t first_s = 0;
    m->stsc_i = 0;
    for (;;) {
        load_run(m);
        uint32_t first, spc;
        if (stsc_entry(m, m->stsc_i, &first, &spc) != 0) return -1;
        uint32_t run_end_chunk = m->next_run == UINT32_MAX ? m->nchunks + 1 : m->next_run;
        uint64_t run_samples = (uint64_t)(run_end_chunk - first) * m->spc;
        if (m->next_run == UINT32_MAX || s < first_s + run_samples) {
            uint32_t k = s - first_s;
            m->chunk = first + k / m->spc;
            m->sic = k % m->spc;
            break;
        }
        first_s += (uint32_t)run_samples;
        m->stsc_i++;
    }
    if (m->chunk > m->nchunks) return -1;
    uint64_t off;
    if (chunk_offset(m, m->chunk, &off) != 0) return -1;
    for (uint32_t i = s - m->sic; i < s; i++) off += sample_size(m, i);
    m->off = off;
    m->s = s;
    if (actual) *actual = (double)s_time / m->timescale;
    return 0;
}

Mp4Cursor mp4_tell(const Mp4 *m) {
    Mp4Cursor c = {m->s, m->chunk, m->sic, m->spc, m->stsc_i, m->next_run, m->off};
    return c;
}

void mp4_restore(Mp4 *m, const Mp4Cursor *c) {
    m->s = c->s;
    m->chunk = c->chunk;
    m->sic = c->sic;
    m->spc = c->spc;
    m->stsc_i = c->stsc_i;
    m->next_run = c->next_run;
    m->off = c->off;
}

int mp4_next(Mp4 *m, uint64_t *off, uint32_t *size) {
    if (m->s >= m->nsamples) return 1;
    uint32_t sz = sample_size(m, m->s);
    if (!sz) return -1;
    *off = m->off;
    *size = sz;
    m->off += sz;
    m->s++;
    if (++m->sic >= m->spc) {
        m->sic = 0;
        m->chunk++;
        if (m->chunk >= m->next_run) {
            m->stsc_i++;
            load_run(m);
        }
        if (m->s < m->nsamples) {
            if (m->chunk > m->nchunks || chunk_offset(m, m->chunk, &m->off) != 0) return -1;
        }
    }
    return 0;
}

/* ---- AudioSpecificConfig ------------------------------------------------- */

static const int kRates[16] = {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050,
                               16000, 12000, 11025, 8000, 7350, 0, 0, 0};

static uint32_t getbits(const uint8_t *p, int len, int *pos, int n) {
    uint32_t v = 0;
    for (int i = 0; i < n; i++, (*pos)++) {
        int byte = *pos >> 3;
        int bit = byte < len ? (p[byte] >> (7 - (*pos & 7))) & 1 : 0;
        v = v << 1 | (uint32_t)bit;
    }
    return v;
}

int aac_parse_asc(const uint8_t *asc, int len, int *aot, int *rate, int *chans) {
    if (len < 2) return -1;
    int pos = 0;
    int a = (int)getbits(asc, len, &pos, 5);
    if (a == 31) a = 32 + (int)getbits(asc, len, &pos, 6);
    int sfi = (int)getbits(asc, len, &pos, 4);
    int r = sfi == 15 ? (int)getbits(asc, len, &pos, 24) : kRates[sfi];
    int ch = (int)getbits(asc, len, &pos, 4);
    if (!r || pos > len * 8) return -1;
    *aot = a;
    *rate = r;
    *chans = ch;
    return 0;
}
