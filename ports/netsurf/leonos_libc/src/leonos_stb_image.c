/*
 * LeonOS NetSurf image handlers backed by stb_image.
 *
 * This is intentionally a frontend-local bridge: NetSurf still owns fetching,
 * MIME sniffing, layout, and redraw. stb_image decodes PNG/JPEG bytes; a small
 * ICO path handles common 32-bit favicon DIB entries into NetSurf's normal
 * 32-bit RGBA bitmap format.
 */

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "utils/errors.h"
#include "utils/utils.h"
#include "netsurf/bitmap.h"
#include "netsurf/content.h"
#include "netsurf/plotters.h"
#include "content/llcache.h"
#include "content/content_factory.h"
#include "content/content_protected.h"
#include "desktop/bitmap.h"
#include "desktop/gui_internal.h"
#include "image/image.h"
#include "monkey/bitmap.h"
#include "monkey/output.h"

#define STBI_NO_STDIO
#define STBI_NO_THREAD_LOCALS
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

typedef struct leonos_stb_image_content {
	struct content base;
	struct bitmap *bitmap;
} leonos_stb_image_content;

static uint16_t leonos_read_le16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t leonos_read_le32(const uint8_t *p)
{
	return (uint32_t)p[0] |
	       ((uint32_t)p[1] << 8) |
	       ((uint32_t)p[2] << 16) |
	       ((uint32_t)p[3] << 24);
}

static unsigned char *
leonos_decode_ico_dib32(const uint8_t *source, size_t source_size,
			int *width_out, int *height_out, bool *opaque_out)
{
	uint16_t count;
	uint32_t best_size = 0u;
	uint32_t best_offset = 0u;
	uint8_t best_width = 0u;
	uint8_t best_height = 0u;
	const uint8_t *dib;
	uint32_t header_size;
	int32_t dib_width;
	int32_t dib_height_raw;
	int width;
	int height;
	uint16_t planes;
	uint16_t bit_count;
	uint32_t compression;
	size_t pixel_offset;
	size_t row_stride;
	unsigned char *decoded;
	bool opaque = true;

	if (source_size < 22u ||
	    leonos_read_le16(source) != 0u ||
	    leonos_read_le16(source + 2u) != 1u) {
		return NULL;
	}
	count = leonos_read_le16(source + 4u);
	if (count == 0u || 6u + (size_t)count * 16u > source_size) {
		return NULL;
	}
	for (uint16_t i = 0u; i < count; i++) {
		const uint8_t *entry = source + 6u + (size_t)i * 16u;
		uint16_t bpp = leonos_read_le16(entry + 6u);
		uint32_t bytes = leonos_read_le32(entry + 8u);
		uint32_t offset = leonos_read_le32(entry + 12u);
		if (bpp != 32u || bytes < 40u || offset >= source_size ||
		    bytes > source_size - offset) {
			continue;
		}
		if (best_size == 0u || bytes > best_size) {
			best_width = entry[0];
			best_height = entry[1];
			best_size = bytes;
			best_offset = offset;
		}
	}
	if (best_size == 0u) {
		return NULL;
	}
	dib = source + best_offset;
	header_size = leonos_read_le32(dib);
	if (header_size < 40u || header_size > best_size) {
		return NULL;
	}
	dib_width = (int32_t)leonos_read_le32(dib + 4u);
	dib_height_raw = (int32_t)leonos_read_le32(dib + 8u);
	planes = leonos_read_le16(dib + 12u);
	bit_count = leonos_read_le16(dib + 14u);
	compression = leonos_read_le32(dib + 16u);
	if (dib_width <= 0 || dib_height_raw == 0 || planes != 1u ||
	    bit_count != 32u || compression != 0u) {
		return NULL;
	}
	width = dib_width;
	height = dib_height_raw > 0 ? dib_height_raw / 2 : -dib_height_raw;
	if (best_width != 0u && width != (int)best_width) {
		return NULL;
	}
	if (best_height != 0u && height != (int)best_height) {
		return NULL;
	}
	if (width <= 0 || height <= 0 ||
	    (size_t)width > SIZE_MAX / 4u ||
	    (size_t)width * 4u > SIZE_MAX / (size_t)height) {
		return NULL;
	}
	pixel_offset = header_size;
	row_stride = (size_t)width * 4u;
	if (pixel_offset + row_stride * (size_t)height > best_size) {
		return NULL;
	}
	decoded = malloc(row_stride * (size_t)height);
	if (decoded == NULL) {
		return NULL;
	}
	for (int y = 0; y < height; y++) {
		int src_y = dib_height_raw > 0 ? (height - 1 - y) : y;
		const uint8_t *src_row = dib + pixel_offset +
			(size_t)src_y * row_stride;
		unsigned char *dst_row = decoded + (size_t)y * row_stride;
		for (int x = 0; x < width; x++) {
			const uint8_t *src_px = src_row + (size_t)x * 4u;
			unsigned char *dst_px = dst_row + (size_t)x * 4u;
			dst_px[0] = src_px[2];
			dst_px[1] = src_px[1];
			dst_px[2] = src_px[0];
			dst_px[3] = src_px[3];
			if (dst_px[3] != 255u) {
				opaque = false;
			}
		}
	}
	*width_out = width;
	*height_out = height;
	*opaque_out = opaque;
	return decoded;
}

static nserror
leonos_stb_image_create(const content_handler *handler,
			lwc_string *imime_type,
			const struct http_parameter *params,
			llcache_handle *llcache,
			const char *fallback_charset,
			bool quirks,
			struct content **c)
{
	leonos_stb_image_content *image;
	nserror error;

	image = calloc(1, sizeof(*image));
	if (image == NULL) {
		return NSERROR_NOMEM;
	}

	error = content__init(&image->base, handler, imime_type, params,
			      llcache, fallback_charset, quirks);
	if (error != NSERROR_OK) {
		free(image);
		return error;
	}

	*c = (struct content *)image;
	return NSERROR_OK;
}

static bool leonos_stb_image_convert(struct content *c)
{
	leonos_stb_image_content *image = (leonos_stb_image_content *)c;
	const uint8_t *source;
	unsigned char *decoded;
	size_t source_size;
	int width;
	int height;
	int channels;
	bool opaque = true;
	bool opaque_known = false;
	bool decoded_with_stbi = false;

	source = content__get_source_data(c, &source_size);
	if (source == NULL || source_size == 0 || source_size > INT_MAX) {
		content_broadcast_error(c, NSERROR_INVALID, "Invalid image data");
		return false;
	}

	decoded = leonos_decode_ico_dib32(source, source_size,
					  &width, &height, &opaque);
	opaque_known = decoded != NULL;
	if (decoded == NULL) {
		decoded = stbi_load_from_memory(source, (int)source_size,
						&width, &height, &channels, 4);
		decoded_with_stbi = decoded != NULL;
	}
	if (decoded == NULL || width <= 0 || height <= 0) {
		moutf(MOUT_GENERIC, "LEONOS STB IMAGE DECODE FAILED %s",
		      stbi_failure_reason() != NULL ?
		      stbi_failure_reason() : "unknown");
		content_broadcast_error(c, NSERROR_UNKNOWN, "Image decode failed");
		return false;
	}

	if (!opaque_known) {
		for (int y = 0; y < height; y++) {
			const unsigned char *src_row = decoded +
				((size_t)y * (size_t)width * 4u);
			for (int x = 0; x < width; x++) {
				if (src_row[(size_t)x * 4u + 3u] != 255u) {
					opaque = false;
				}
			}
		}
	}

	image->bitmap = monkey_bitmap_adopt_buffer(width, height, decoded, opaque);
	if (image->bitmap == NULL) {
		if (decoded_with_stbi) {
			stbi_image_free(decoded);
		} else {
			free(decoded);
		}
		content_broadcast_error(c, NSERROR_NOMEM, NULL);
		return false;
	}
	guit->bitmap->modified(image->bitmap);
	moutf(MOUT_GENERIC, "LEONOS STB IMAGE DECODED WIDTH %d HEIGHT %d",
	      width, height);

	c->width = width;
	c->height = height;
	c->size += (unsigned int)((size_t)width * (size_t)height * 4u);
	content__set_title(c, "Image");
	content_set_ready(c);
	content_set_done(c);
	content_set_status(c, "");
	return true;
}

static bool
leonos_stb_image_redraw(struct content *c,
			struct content_redraw_data *data,
			const struct rect *clip,
			const struct redraw_context *ctx)
{
	leonos_stb_image_content *image = (leonos_stb_image_content *)c;

	if (image->bitmap == NULL) {
		return false;
	}

	return image_bitmap_plot(image->bitmap, data, clip, ctx);
}

static void leonos_stb_image_destroy(struct content *c)
{
	leonos_stb_image_content *image = (leonos_stb_image_content *)c;

	if (image->bitmap != NULL) {
		guit->bitmap->destroy(image->bitmap);
		image->bitmap = NULL;
	}
}

static nserror
leonos_stb_image_clone(const struct content *old, struct content **newc)
{
	leonos_stb_image_content *image;
	nserror error;

	image = calloc(1, sizeof(*image));
	if (image == NULL) {
		return NSERROR_NOMEM;
	}

	error = content__clone(old, &image->base);
	if (error != NSERROR_OK) {
		content_destroy(&image->base);
		return error;
	}

	if (old->status == CONTENT_STATUS_READY ||
	    old->status == CONTENT_STATUS_DONE) {
		if (!leonos_stb_image_convert(&image->base)) {
			content_destroy(&image->base);
			return NSERROR_CLONE_FAILED;
		}
	}

	*newc = (struct content *)image;
	return NSERROR_OK;
}

static void *
leonos_stb_image_get_internal(const struct content *c, void *context)
{
	const leonos_stb_image_content *image =
		(const leonos_stb_image_content *)c;

	(void)context;
	return image->bitmap;
}

static content_type leonos_stb_image_type(void)
{
	return CONTENT_IMAGE;
}

static bool leonos_stb_image_is_opaque(struct content *c)
{
	leonos_stb_image_content *image = (leonos_stb_image_content *)c;

	if (image->bitmap == NULL) {
		return false;
	}
	return guit->bitmap->get_opaque(image->bitmap);
}

static const content_handler leonos_stb_image_handler = {
	.create = leonos_stb_image_create,
	.data_complete = leonos_stb_image_convert,
	.destroy = leonos_stb_image_destroy,
	.redraw = leonos_stb_image_redraw,
	.clone = leonos_stb_image_clone,
	.get_internal = leonos_stb_image_get_internal,
	.type = leonos_stb_image_type,
	.is_opaque = leonos_stb_image_is_opaque,
	.no_share = false,
};

static const char *leonos_stb_image_types[] = {
	"image/png",
	"image/x-png",
	"image/jpeg",
	"image/jpg",
	"image/pjpeg",
	"image/x-icon",
	"image/vnd.microsoft.icon"
};

CONTENT_FACTORY_REGISTER_TYPES(leonos_stb_image,
			       leonos_stb_image_types,
			       leonos_stb_image_handler);
