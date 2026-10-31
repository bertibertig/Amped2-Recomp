/* The XBE's dashboard title image as a window icon (title_image.h).
 *
 * The $$XTIMAGE section holds an XPR0 resource: "XPR0", total size, header
 * size, then one D3D texture header (common, data, lock, format, size) and the
 * pixels after the header. Amped 2's is 128x128 DXT1 (format 0x07710C29:
 * colour 0x0C, log2 sizes 7/7 in bits 20-27). Only that layout is decoded;
 * anything else gives no icon rather than a wrong one. */
#include "title_image.h"

#include <string.h>

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t rgb565(uint32_t c)
{
    uint32_t r = (c >> 11 & 31) * 255 / 31, g = (c >> 5 & 63) * 255 / 63, b = (c & 31) * 255 / 31;
    return 0xFF000000u | r << 16 | g << 8 | b;
}

static uint32_t mix(uint32_t a, uint32_t b, int wa, int wb)
{
    uint32_t out = 0xFF000000u;
    int s;
    for (s = 0; s < 24; s += 8)
        out |= (((a >> s & 255) * wa + (b >> s & 255) * wb) / (wa + wb)) << s;
    return out;
}

int amped2_title_image(const void *xbe, size_t xbe_size, uint32_t *argb,
                       uint32_t *w, uint32_t *h)
{
    const uint8_t *d = (const uint8_t *)xbe;
    uint32_t base, nsec, shdr, i;

    if (!d || xbe_size < 0x180 || memcmp(d, "XBEH", 4))
        return 0;
    base = rd32(d + 0x104);
    nsec = rd32(d + 0x11C);
    shdr = rd32(d + 0x120) - base;
    for (i = 0; i < nsec && shdr + (i + 1) * 56 <= xbe_size; i++) {
        const uint8_t *s = d + shdr + i * 56;
        uint32_t raw = rd32(s + 12), rsize = rd32(s + 16), name = rd32(s + 20) - base;
        const uint8_t *x;
        uint32_t hdr, fmt, tw, th, bx, by, k;

        if (name + 10 > xbe_size || memcmp(d + name, "$$XTIMAGE", 10))
            continue;
        if ((uint64_t)raw + rsize > xbe_size || rsize < 32)
            return 0;
        x = d + raw;
        hdr = rd32(x + 8);
        fmt = rd32(x + 12 + 12);
        if (memcmp(x, "XPR0", 4) || ((fmt >> 8) & 0xFF) != 0x0C)
            return 0;                       /* not a DXT1 texture */
        tw = 1u << ((fmt >> 20) & 15);
        th = 1u << ((fmt >> 24) & 15);
        if (tw < 4 || th < 4 || tw > AMPED2_TITLE_IMAGE_MAX || th > AMPED2_TITLE_IMAGE_MAX
                || hdr + (uint64_t)tw * th / 2 > rsize)
            return 0;
        x += hdr;
        for (by = 0; by < th / 4; by++)
            for (bx = 0; bx < tw / 4; bx++, x += 8) {
                uint32_t c0 = (uint32_t)x[0] | x[1] << 8, c1 = (uint32_t)x[2] | x[3] << 8;
                uint32_t bits = rd32(x + 4), pal[4];
                pal[0] = rgb565(c0);
                pal[1] = rgb565(c1);
                if (c0 > c1) {
                    pal[2] = mix(pal[0], pal[1], 2, 1);
                    pal[3] = mix(pal[0], pal[1], 1, 2);
                } else {
                    pal[2] = mix(pal[0], pal[1], 1, 1);
                    pal[3] = 0;             /* transparent */
                }
                for (k = 0; k < 16; k++)
                    argb[(by * 4 + k / 4) * tw + bx * 4 + k % 4] = pal[bits >> (2 * k) & 3];
            }
        *w = tw;
        *h = th;
        return 1;
    }
    return 0;
}
