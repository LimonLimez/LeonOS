#include <stddef.h>
#include <stdint.h>

#include "utils/errors.h"
#include "utils/nsurl.h"
#include "content/backing_store.h"
#include "desktop/save_pdf.h"

static nserror leonos_llcache_initialise(
        const struct llcache_store_parameters *parameters)
{
    (void) parameters;
    return NSERROR_OK;
}

static nserror leonos_llcache_finalise(void)
{
    return NSERROR_OK;
}

static nserror leonos_llcache_store(nsurl *url,
        enum backing_store_flags flags,
        uint8_t *data,
        const size_t datalen)
{
    (void) url;
    (void) flags;
    (void) data;
    (void) datalen;
    return NSERROR_SAVE_FAILED;
}

static nserror leonos_llcache_fetch(nsurl *url,
        enum backing_store_flags flags,
        uint8_t **data_out,
        size_t *datalen_out)
{
    (void) url;
    (void) flags;
    (void) data_out;
    (void) datalen_out;
    return NSERROR_NOT_FOUND;
}

static nserror leonos_llcache_release(nsurl *url,
        enum backing_store_flags flags)
{
    (void) url;
    (void) flags;
    return NSERROR_NOT_FOUND;
}

static nserror leonos_llcache_invalidate(nsurl *url)
{
    (void) url;
    return NSERROR_NOT_FOUND;
}

static struct gui_llcache_table leonos_no_fs_llcache_table = {
    .initialise = leonos_llcache_initialise,
    .finalise = leonos_llcache_finalise,
    .store = leonos_llcache_store,
    .fetch = leonos_llcache_fetch,
    .release = leonos_llcache_release,
    .invalidate = leonos_llcache_invalidate,
};

struct gui_llcache_table *filesystem_llcache_table =
    &leonos_no_fs_llcache_table;

nserror save_pdf(const char *path)
{
    (void) path;
    return NSERROR_NOT_IMPLEMENTED;
}
