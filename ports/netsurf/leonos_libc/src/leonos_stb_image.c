/*
 * LeonOS NetSurf image handlers backed by stb_image.
 *
 * This is intentionally a frontend-local bridge: NetSurf still owns fetching,
 * MIME sniffing, layout, and redraw. stb_image only decodes PNG/JPEG bytes into
 * NetSurf's normal 32-bit RGBA bitmap format.
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

	source = content__get_source_data(c, &source_size);
	if (source == NULL || source_size == 0 || source_size > INT_MAX) {
		content_broadcast_error(c, NSERROR_INVALID, "Invalid image data");
		return false;
	}

	decoded = stbi_load_from_memory(source, (int)source_size,
					&width, &height, &channels, 4);
	if (decoded == NULL || width <= 0 || height <= 0) {
		moutf(MOUT_GENERIC, "LEONOS STB IMAGE DECODE FAILED %s",
		      stbi_failure_reason() != NULL ?
		      stbi_failure_reason() : "unknown");
		content_broadcast_error(c, NSERROR_UNKNOWN, "Image decode failed");
		return false;
	}

	for (int y = 0; y < height; y++) {
		const unsigned char *src_row = decoded + ((size_t)y * (size_t)width * 4u);
		for (int x = 0; x < width; x++) {
			if (src_row[(size_t)x * 4u + 3u] != 255u) {
				opaque = false;
			}
		}
	}

	image->bitmap = monkey_bitmap_adopt_buffer(width, height, decoded, opaque);
	if (image->bitmap == NULL) {
		stbi_image_free(decoded);
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
	"image/pjpeg"
};

CONTENT_FACTORY_REGISTER_TYPES(leonos_stb_image,
			       leonos_stb_image_types,
			       leonos_stb_image_handler);
