/* The title image an XBE carries for the Xbox dashboard ($$XTIMAGE section):
 * the game's icon, taken from the player's own disc at run time. */
#ifndef AMPED2_TITLE_IMAGE_H
#define AMPED2_TITLE_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define AMPED2_TITLE_IMAGE_MAX 256          /* width/height accepted */

/* Decodes the title image of the XBE in memory into argb (0xAARRGGBB, rows
 * top-down, room for AMPED2_TITLE_IMAGE_MAX^2 pixels). Returns 1 and the size
 * in *w, *h; 0 when the XBE has none or it is not a DXT1 XPR texture. */
int amped2_title_image(const void *xbe, size_t xbe_size, uint32_t *argb,
                       uint32_t *w, uint32_t *h);

#endif
