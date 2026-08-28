// abc802/emu/src/png.c - a minimal PNG writer, so a rendered ABC802 screen
// can leave the emulator as a real image file.
//
// Written by hand rather than pulled in as a dependency, for the same
// reason abc80/emu/src/sound.c writes its own WAV header: the format's
// uncompressed path is small and completely specified, and the default
// build of this project has no third-party libraries at all.
//
// PNG requires the pixel data to be zlib-wrapped DEFLATE, but DEFLATE
// permits *stored* (uncompressed) blocks - a 5-byte header per block
// carrying up to 65535 literal bytes. That is what this emits, so no
// compressor is needed and the output is still a completely standard PNG
// any viewer will open. The cost is file size, which for a 480x240 screen
// is around 340KB - irrelevant for a screenshot, and the reason this is
// not offered as a way to record video.

#include <stdio.h>
#include <string.h>

#include "png.h"

static uint32_t crc_table[256];
static bool crc_table_ready = false;

static void make_crc_table(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        crc_table[n] = c;
    }
    crc_table_ready = true;
}

static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len) {
    if (!crc_table_ready) make_crc_table();
    for (size_t n = 0; n < len; n++) {
        crc = crc_table[(crc ^ buf[n]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static void put_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

// One PNG chunk: length, type, data, CRC over type+data.
static bool write_chunk(FILE *f, const char *type, const uint8_t *data, size_t len) {
    uint8_t hdr[8];
    put_be32(hdr, (uint32_t)len);
    memcpy(hdr + 4, type, 4);
    if (fwrite(hdr, 1, 8, f) != 8) return false;
    if (len && fwrite(data, 1, len, f) != len) return false;

    uint32_t crc = crc32_update(0xFFFFFFFFu, hdr + 4, 4);
    if (len) crc = crc32_update(crc, data, len);
    crc ^= 0xFFFFFFFFu;

    uint8_t crcbuf[4];
    put_be32(crcbuf, crc);
    return fwrite(crcbuf, 1, 4, f) == 4;
}

bool abc802_write_png(const char *path, const uint8_t *pixels, int width, int height,
                      const uint8_t fg[3], const uint8_t bg[3]) {
    if (!path || !pixels || width <= 0 || height <= 0) return false;

    // Raw image data: each row is a filter byte (0 = None) followed by
    // width RGB triples. Built in full before writing, since the adler32
    // and the stored-block framing both need to see it as one stream.
    size_t stride = 1 + (size_t)width * 3;
    size_t raw_len = stride * (size_t)height;
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) return false;

    for (int y = 0; y < height; y++) {
        uint8_t *row = raw + (size_t)y * stride;
        row[0] = 0; // filter: None
        for (int x = 0; x < width; x++) {
            const uint8_t *c = pixels[(size_t)y * (size_t)width + (size_t)x] ? fg : bg;
            row[1 + (size_t)x * 3 + 0] = c[0];
            row[1 + (size_t)x * 3 + 1] = c[1];
            row[1 + (size_t)x * 3 + 2] = c[2];
        }
    }

    // zlib stream: 2-byte header, DEFLATE stored blocks, adler32 trailer.
    size_t max_block = 65535;
    size_t nblocks = (raw_len + max_block - 1) / max_block;
    if (nblocks == 0) nblocks = 1;
    size_t z_len = 2 + nblocks * 5 + raw_len + 4;
    uint8_t *z = (uint8_t *)malloc(z_len);
    if (!z) {
        free(raw);
        return false;
    }

    size_t zi = 0;
    z[zi++] = 0x78; // CMF: deflate, 32K window
    z[zi++] = 0x01; // FLG: no dictionary, fastest - checksum-valid pair

    size_t offset = 0;
    while (offset < raw_len) {
        size_t n = raw_len - offset;
        if (n > max_block) n = max_block;
        int final = (offset + n >= raw_len) ? 1 : 0;
        z[zi++] = (uint8_t)final;             // BFINAL, BTYPE = 00 (stored)
        z[zi++] = (uint8_t)(n & 0xFF);        // LEN, little-endian
        z[zi++] = (uint8_t)(n >> 8);
        z[zi++] = (uint8_t)(~n & 0xFF);       // NLEN, the one's complement
        z[zi++] = (uint8_t)((~n >> 8) & 0xFF);
        memcpy(z + zi, raw + offset, n);
        zi += n;
        offset += n;
    }

    // adler32 over the uncompressed data.
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521;
        b = (b + a) % 65521;
    }
    put_be32(z + zi, (b << 16) | a);
    zi += 4;

    FILE *f = fopen(path, "wb");
    if (!f) {
        free(raw);
        free(z);
        return false;
    }

    static const uint8_t signature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    bool ok = fwrite(signature, 1, 8, f) == 8;

    uint8_t ihdr[13];
    put_be32(ihdr + 0, (uint32_t)width);
    put_be32(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;   // bit depth
    ihdr[9] = 2;   // color type 2: truecolor RGB
    ihdr[10] = 0;  // compression: deflate
    ihdr[11] = 0;  // filter method 0
    ihdr[12] = 0;  // no interlace
    ok = ok && write_chunk(f, "IHDR", ihdr, sizeof(ihdr));
    ok = ok && write_chunk(f, "IDAT", z, zi);
    ok = ok && write_chunk(f, "IEND", NULL, 0);

    if (fclose(f) != 0) ok = false;
    free(raw);
    free(z);
    return ok;
}
