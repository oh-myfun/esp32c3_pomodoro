/* stb_image_write.h - 极简 PNG 编码（仅 PC 模拟器用，不烧入设备）。
 * 用 zlib stored blocks，无压缩。PNG 兼容性好但文件偏大（可接受）。 */
#pragma once

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

extern int stbi_write_png(const char *filename, int w, int h, int comp,
                          const void *data, int stride_in_bytes);

/* --- impl --- */

static uint32_t sim_crc_table[256];
static int sim_crc_init = 0;

static void sim_crc_init_fn(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xedb88320U ^ (c >> 1)) : (c >> 1);
        sim_crc_table[i] = c;
    }
    sim_crc_init = 1;
}

static uint32_t sim_crc_update(uint32_t crc, const uint8_t *buf, int len) {
    if (!sim_crc_init) sim_crc_init_fn();
    crc = crc ^ 0xFFFFFFFFU;
    for (int i = 0; i < len; i++) crc = sim_crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

static uint32_t sim_adler32(const uint8_t *buf, int len) {
    uint32_t a = 1, b = 0;
    for (int i = 0; i < len; i++) { a = (a + buf[i]) % 65521; b = (b + a) % 65521; }
    return (b << 16) | a;
}

static void sim_put32(FILE *f, uint32_t v) {
    fputc((v >> 24) & 0xFF, f); fputc((v >> 16) & 0xFF, f);
    fputc((v >> 8) & 0xFF, f); fputc(v & 0xFF, f);
}

static void sim_write_chunk(FILE *f, const char *type, const uint8_t *data, int len) {
    sim_put32(f, (uint32_t)len);
    fwrite(type, 1, 4, f);
    if (data && len) fwrite(data, 1, len, f);
    uint32_t crc = sim_crc_update(0, (const uint8_t *)type, 4);
    if (data && len) crc = sim_crc_update(crc, data, len);
    sim_put32(f, crc);
}

int stbi_write_png(const char *filename, int w, int h, int comp,
                   const void *data, int stride_in_bytes) {
    int row_bytes = w * comp;
    if (stride_in_bytes == 0) stride_in_bytes = row_bytes;

    /* Filtered raw scanlines (filter byte 0 = None per scanline). */
    int raw_len = h * (1 + row_bytes);
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    if (!raw) return 0;
    const uint8_t *src = (const uint8_t *)data;
    for (int y = 0; y < h; y++) {
        raw[y * (1 + row_bytes)] = 0;
        memcpy(raw + y * (1 + row_bytes) + 1, src + y * stride_in_bytes, row_bytes);
    }

    /* zlib stream: 2-byte header + stored deflate blocks + 4-byte adler32. */
    int max_block = 65535;
    int n_blocks = (raw_len + max_block - 1) / max_block;
    int zlib_len = 2 + raw_len + n_blocks * 5 + 4;
    uint8_t *zlib = (uint8_t *)malloc(zlib_len);
    if (!zlib) { free(raw); return 0; }
    int p = 0;
    zlib[p++] = 0x78; zlib[p++] = 0x01;
    int remaining = raw_len;
    const uint8_t *r = raw;
    while (remaining > 0) {
        int blk = remaining > max_block ? max_block : remaining;
        zlib[p++] = (remaining == blk) ? 1 : 0;  /* BFINAL */
        zlib[p++] = blk & 0xFF; zlib[p++] = (blk >> 8) & 0xFF;
        zlib[p++] = (~blk) & 0xFF; zlib[p++] = ((~blk) >> 8) & 0xFF;
        memcpy(zlib + p, r, blk); p += blk; r += blk;
        remaining -= blk;
    }
    uint32_t adler = sim_adler32(raw, raw_len);
    zlib[p++] = (adler >> 24) & 0xFF;
    zlib[p++] = (adler >> 16) & 0xFF;
    zlib[p++] = (adler >> 8) & 0xFF;
    zlib[p++] = adler & 0xFF;

    FILE *f = fopen(filename, "wb");
    if (!f) { free(raw); free(zlib); return 0; }
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    ihdr[0] = (w >> 24) & 0xFF; ihdr[1] = (w >> 16) & 0xFF;
    ihdr[2] = (w >> 8) & 0xFF; ihdr[3] = w & 0xFF;
    ihdr[4] = (h >> 24) & 0xFF; ihdr[5] = (h >> 16) & 0xFF;
    ihdr[6] = (h >> 8) & 0xFF; ihdr[7] = h & 0xFF;
    ihdr[8] = 8;
    ihdr[9] = (comp == 1) ? 0 : (comp == 2 ? 4 : comp == 3 ? 2 : 6);
    ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    sim_write_chunk(f, "IHDR", ihdr, 13);
    sim_write_chunk(f, "IDAT", zlib, p);
    sim_write_chunk(f, "IEND", NULL, 0);
    fclose(f);

    free(raw);
    free(zlib);
    return 1;
}

#ifdef __cplusplus
}
#endif
