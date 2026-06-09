#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libwapcaplet/libwapcaplet.h>

#include "utils/errors.h"
#include "utils/nsurl.h"
#include "utils/ring.h"
#include "utils/utils.h"
#include "content/fetch.h"
#include "content/fetchers.h"

#ifdef LEONOS_USER_APP
#include "leonos_user.h"
int leonos_netsurf_fetch_finished_for_script;
int leonos_netsurf_active_fetches_for_script;
unsigned int leonos_netsurf_fetch_generation_for_script;
#endif

#define LEONOS_FETCH_MAX_BYTES (8u * 1024u * 1024u)
#define LEONOS_FETCH_CHUNK_BYTES 4096u
#define LEONOS_FETCH_STREAM_POLL_BUDGET 1024u
#define LEONOS_FETCH_STREAM_IDLE_STEP_LIMIT 16u
#define LEONOS_FETCH_POLL_CONTEXT_BUDGET 12u
#define LEONOS_FETCH_OPEN_RETRY_BUDGET 256u
#define LEONOS_FETCH_MAX_URL_BYTES 4095u
#define LEONOS_FETCH_CSS_SOFT_LIMIT 0u
#define LEONOS_FETCH_JS_SOFT_LIMIT 0u
#define LEONOS_FETCH_ROBLOX_SCRIPT_BUDGET 64u

struct leonos_fetch_context {
    struct fetch *parent_fetch;
    nsurl *url;
    uint8_t *data;
    size_t data_len;
    size_t callback_off;
    unsigned int status_code;
    unsigned int fetch_flags;
    char content_type[64];
    char location[512];
    bool aborted;
    bool begun;
    bool headers_sent;
    bool completed;
    bool data_log_started;
    bool locked;
    bool synthetic_empty;
    bool dependency_wait_logged;
    unsigned int open_retries;
    unsigned int order;
    bool open_wait_logged;
#ifdef LEONOS_USER_APP
    uint32_t stream_handle;
    uint32_t stream_state;
    uint32_t stream_polls;
    unsigned int stream_chunk_logs;
    bool chunk_decoder_decided;
    bool chunk_decoder_active;
    unsigned int chunk_state;
    bool chunk_seen_digit;
    bool chunk_skip_ext;
    uint32_t chunk_remaining;
#endif
    struct leonos_fetch_context *r_next;
    struct leonos_fetch_context *r_prev;
};

static struct leonos_fetch_context *leonos_fetch_ring;
static unsigned int leonos_fetch_css_count;
static unsigned int leonos_fetch_js_count;
static unsigned int leonos_fetch_js_completion_cooldown;
static unsigned int leonos_fetch_next_order;
static bool leonos_fetch_roblox_react_core_delivered;
static bool leonos_fetch_roblox_react_shared_delivered;
static bool leonos_fetch_roblox_react_utilities_seen;
static bool leonos_fetch_roblox_react_utilities_delivered;
static bool leonos_fetch_roblox_react_styleguide_seen;
static bool leonos_fetch_roblox_react_styleguide_delivered;

#ifdef LEONOS_USER_APP
static void leonos_fetch_copy_meta(struct leonos_fetch_context *ctx,
        const struct leonos_net_fetch_meta *meta);
#endif

static void leonos_fetch_log(const char *text)
{
#ifdef LEONOS_USER_APP
    leonos_write(text);
#else
    (void) text;
#endif
}

static void leonos_fetch_log_sample(const char *prefix, const char *data,
        size_t len)
{
#ifdef LEONOS_USER_APP
    char sample[81];
    size_t out = 0u;

    leonos_write(prefix);
    for (size_t i = 0u; i < len && out + 1u < sizeof(sample); i += 1u) {
        char ch = data[i];
        if (ch == '\r' || ch == '\n' || ch == '\t') {
            ch = ' ';
        }
        if ((unsigned char) ch < 32u || (unsigned char) ch > 126u) {
            ch = '.';
        }
        sample[out++] = ch;
    }
    sample[out] = 0;
    leonos_write(sample);
    leonos_write("\r\n");
#else
    (void) prefix;
    (void) data;
    (void) len;
#endif
}

#ifdef LEONOS_USER_APP
static bool leonos_fetch_hex_value(char ch, uint8_t *value)
{
    if (ch >= '0' && ch <= '9') {
        *value = (uint8_t) (ch - '0');
        return true;
    }
    if (ch >= 'A' && ch <= 'F') {
        ch = (char) (ch + ('a' - 'A'));
    }
    if (ch >= 'a' && ch <= 'f') {
        *value = (uint8_t) (10u + (uint8_t) (ch - 'a'));
        return true;
    }
    return false;
}

static bool leonos_fetch_data_looks_chunked(const uint8_t *data, size_t len,
        bool *need_more)
{
    bool seen_digit = false;

    *need_more = false;
    for (size_t i = 0u; i < len && i < 16u; i += 1u) {
        uint8_t value = 0u;
        char ch = (char) data[i];

        if (leonos_fetch_hex_value(ch, &value)) {
            seen_digit = true;
        } else if (ch == ';') {
            return seen_digit;
        } else if (ch == '\r' || ch == '\n') {
            return seen_digit;
        } else {
            return false;
        }
    }

    *need_more = seen_digit;
    return false;
}

static void leonos_fetch_append_stream_bytes(struct leonos_fetch_context *ctx,
        const uint8_t *data, size_t len)
{
    size_t room;

    if (ctx->data_len >= LEONOS_FETCH_MAX_BYTES || len == 0u) {
        return;
    }
    room = LEONOS_FETCH_MAX_BYTES - ctx->data_len;
    if (len > room) {
        len = room;
    }
    memcpy(ctx->data + ctx->data_len, data, len);
    ctx->data_len += len;
}

static void leonos_fetch_decode_chunked_bytes(struct leonos_fetch_context *ctx,
        const uint8_t *data, size_t len)
{
    for (size_t i = 0u; i < len; i += 1u) {
        char ch = (char) data[i];

        if (ctx->chunk_state == 0u) {
            uint8_t value = 0u;
            if (ctx->chunk_skip_ext) {
                if (ch == '\r') {
                    ctx->chunk_state = 1u;
                } else if (ch == '\n') {
                    ctx->chunk_state = ctx->chunk_remaining != 0u ? 2u : 5u;
                    ctx->chunk_seen_digit = false;
                    ctx->chunk_skip_ext = false;
                }
            } else if (leonos_fetch_hex_value(ch, &value)) {
                ctx->chunk_seen_digit = true;
                if (ctx->chunk_remaining <= 0x0FFFFFFFu) {
                    ctx->chunk_remaining =
                            (ctx->chunk_remaining << 4) | value;
                }
            } else if (ch == ';') {
                ctx->chunk_skip_ext = true;
            } else if (ch == '\r') {
                ctx->chunk_state = 1u;
            } else if (ch == '\n') {
                ctx->chunk_state = (ctx->chunk_seen_digit &&
                        ctx->chunk_remaining != 0u) ? 2u : 5u;
                ctx->chunk_seen_digit = false;
            }
        } else if (ctx->chunk_state == 1u) {
            if (ch == '\n') {
                ctx->chunk_state = (ctx->chunk_seen_digit &&
                        ctx->chunk_remaining != 0u) ? 2u : 5u;
                ctx->chunk_seen_digit = false;
                ctx->chunk_skip_ext = false;
            }
        } else if (ctx->chunk_state == 2u) {
            size_t take = len - i;
            if (take > ctx->chunk_remaining) {
                take = ctx->chunk_remaining;
            }
            if (take != 0u) {
                leonos_fetch_append_stream_bytes(ctx, data + i, take);
                i += take - 1u;
                ctx->chunk_remaining -= (uint32_t) take;
            }
            if (ctx->chunk_remaining == 0u) {
                ctx->chunk_state = 3u;
            }
        } else if (ctx->chunk_state == 3u) {
            if (ch == '\n') {
                ctx->chunk_state = 0u;
                ctx->chunk_remaining = 0u;
                ctx->chunk_seen_digit = false;
                ctx->chunk_skip_ext = false;
            } else if (ch == '\r') {
                ctx->chunk_state = 4u;
            }
        } else if (ctx->chunk_state == 4u) {
            if (ch == '\n') {
                ctx->chunk_state = 0u;
                ctx->chunk_remaining = 0u;
                ctx->chunk_seen_digit = false;
                ctx->chunk_skip_ext = false;
            }
        } else if (ctx->chunk_state == 5u) {
            return;
        }
    }
}

static void leonos_fetch_append_stream_data(struct leonos_fetch_context *ctx,
        const uint8_t *data, size_t len)
{
    bool need_more = false;
    size_t before = ctx->data_len;

    if (len == 0u) {
        return;
    }
    if ((ctx->fetch_flags & LEONOS_NET_FETCH_FLAG_CHUNKED) != 0u &&
        !ctx->chunk_decoder_decided) {
        if (leonos_fetch_data_looks_chunked(data, len, &need_more)) {
            ctx->chunk_decoder_active = true;
            ctx->chunk_decoder_decided = true;
            leonos_fetch_log("NETSURF LEO HTTPS decode chunked stream\r\n");
        } else if (!need_more) {
            ctx->chunk_decoder_decided = true;
        }
    }
    if (ctx->chunk_decoder_active) {
        leonos_fetch_decode_chunked_bytes(ctx, data, len);
    } else {
        leonos_fetch_append_stream_bytes(ctx, data, len);
    }
    if (before == 0u && ctx->data_len != 0u) {
        leonos_fetch_log_sample("NETSURF LEO HTTPS first data ",
                (const char *) ctx->data, ctx->data_len);
    }
}
#endif

static bool leonos_fetch_initialise(lwc_string *scheme)
{
    (void) scheme;
    leonos_fetch_log("NETSURF LEO HTTPS fetcher init\r\n");
    return true;
}

static void leonos_fetch_finalise(lwc_string *scheme)
{
    (void) scheme;
}

static bool leonos_fetch_can_fetch(const nsurl *url)
{
    const char *text = nsurl_access(url);
    bool ok = text != NULL && strncmp(text, "https://", 8) == 0;
    leonos_fetch_log("NETSURF LEO HTTPS fetcher acceptable ");
    leonos_fetch_log(ok ? "yes " : "no ");
    if (text != NULL) {
        leonos_fetch_log(text);
    }
    leonos_fetch_log("\r\n");
    return ok;
}

static const char *leonos_fetch_mime_from_url(const char *url)
{
    size_t len = strlen(url);
    for (size_t i = 0u; i < len; i += 1u) {
        if (url[i] == '?' || url[i] == '#') {
            len = i;
            break;
        }
    }

    if (len >= 4 && strncmp(url + len - 4, ".css", 4) == 0) {
        return "text/css";
    }
    if (len >= 3 && strncmp(url + len - 3, ".js", 3) == 0) {
        return "application/javascript";
    }
    if (len >= 4 && strncmp(url + len - 4, ".gif", 4) == 0) {
        return "image/gif";
    }
    if (len >= 4 && strncmp(url + len - 4, ".bmp", 4) == 0) {
        return "image/bmp";
    }
    if (len >= 4 && strncmp(url + len - 4, ".ico", 4) == 0) {
        return "image/x-icon";
    }
    if ((len >= 4 && strncmp(url + len - 4, ".jpg", 4) == 0) ||
        (len >= 5 && strncmp(url + len - 5, ".jpeg", 5) == 0)) {
        return "image/jpeg";
    }
    if (len >= 4 && strncmp(url + len - 4, ".png", 4) == 0) {
        return "image/png";
    }
    if (len >= 4 && strncmp(url + len - 4, ".svg", 4) == 0) {
        return "image/svg+xml";
    }
    if (len >= 5 && strncmp(url + len - 5, ".webp", 5) == 0) {
        return "image/webp";
    }

    return "text/html; charset=utf-8";
}

static bool leonos_fetch_url_looks_css(const char *url)
{
    size_t len;
    if (url == NULL) {
        return false;
    }
    len = strlen(url);
    for (size_t i = 0u; i + 4u <= len; i += 1u) {
        if (strncmp(url + i, ".css", 4) == 0 &&
            (url[i + 4u] == 0 || url[i + 4u] == '?' ||
             url[i + 4u] == '#')) {
            return true;
        }
    }
    return strstr(url, "/css/") != NULL ||
           strstr(url, "/css.") != NULL ||
           strstr(url, "/_/ss/") != NULL;
}

static bool leonos_fetch_url_looks_js(const char *url)
{
    size_t len;
    if (url == NULL) {
        return false;
    }
    len = strlen(url);
    for (size_t i = 0u; i + 3u <= len; i += 1u) {
        if (strncmp(url + i, ".js", 3) == 0 &&
            (url[i + 3u] == 0 || url[i + 3u] == '?' ||
             url[i + 3u] == '#')) {
            return true;
        }
    }
    return strstr(url, "/js/") != NULL ||
           strstr(url, "javascript") != NULL;
}

static bool leonos_fetch_url_is_roblox_deferred_bundle(const char *url)
{
    if (url == NULL || strstr(url, "rbxcdn.com/") == NULL) {
        return false;
    }

    return strstr(url, "Challenge.") != NULL ||
           strstr(url, "UserProfiles.") != NULL ||
           strstr(url, "Navigation.") != NULL ||
           strstr(url, "Sentry.") != NULL ||
           strstr(url, "f85ce090699c1c3962762b8a2f8b252f0f2a7d0424c146f41d6c5abbf0147a57") != NULL ||
           strstr(url, "Thumbnails.") != NULL ||
           strstr(url, "PresenceStatus.") != NULL ||
           strstr(url, "RealTime.") != NULL ||
           strstr(url, "AccountSwitcher.") != NULL ||
           strstr(url, "VerificationUpsell.") != NULL ||
           strstr(url, "EmailVerifyCodeModal.") != NULL ||
           strstr(url, "Captcha") != NULL ||
           strstr(url, "CookieBanner") != NULL ||
           strstr(url, "Footer.") != NULL ||
           (strstr(url, "-StyleGuide.") != NULL &&
            strstr(url, "ReactStyleGuide.") == NULL) ||
           strstr(url, "Builder.") != NULL ||
           strstr(url, "ItemPurchase") != NULL ||
           strstr(url, "ItemDetailsHydrationService") != NULL ||
           strstr(url, "IdVerification") != NULL ||
           strstr(url, "AccessManagement") != NULL ||
           strstr(url, "GameLaunch") != NULL;
}

static bool leonos_fetch_url_is_roblox_deferred_style(const char *url)
{
    if (url == NULL || strstr(url, "css.rbxcdn.com/") == NULL) {
        return false;
    }
    if (strstr(url, "FoundationCss.") != NULL ||
        strstr(url, "ReactStyleGuide.") != NULL ||
        strstr(url, "SearchLandingPage.") != NULL ||
        strstr(url, "ReactLanding.") != NULL) {
        return false;
    }
    return true;
}

static bool leonos_fetch_url_is_roblox_landing_script(const char *url)
{
    if (url == NULL) {
        return false;
    }

    if (strstr(url, "roblox.com/js/hsts.js") != NULL) {
        return true;
    }

    if (strstr(url, "js.rbxcdn.com/") == NULL) {
        return false;
    }

    return strstr(url, "EnvironmentUrls.") != NULL ||
           strstr(url, "0496306d944faeb488779bad1af0738e") != NULL ||
           strstr(url, "5c8a2ba3737908f693394045e81ebd71c77cde6f87550ea51f7833e8c98200ae") != NULL ||
           strstr(url, "2f0fd0c2760ff1898187af6df3b764f4b08f77a315d0a33654f105f61b0ea6d0") != NULL ||
           strstr(url, "HeaderScripts.") != NULL ||
           strstr(url, "f85ce090699c1c3962762b8a2f8b252f0f2a7d0424c146f41d6c5abbf0147a57") != NULL ||
           strstr(url, "Theme.") != NULL ||
           strstr(url, "1d87d1231072878a0f6164e84b8cfa6f90a1b31b18ba5d8f410b947d4b029fe8") != NULL ||
           strstr(url, "4bae454bf5dab3028073fea1e91b6f19") != NULL ||
           strstr(url, "730a5206adda5785166891801b1e0f9ae622a95558b260a144db60278b243f5f") != NULL ||
           strstr(url, "TranslationResources.") != NULL ||
           strstr(url, "9731c232fa99c58b0dafafc81cc7905b") != NULL ||
           strstr(url, "bfb3e7a7efed2f8ba4d12dc9fdb70dce1ff97ee13e988833a8638cf3ed8fd7f8") != NULL ||
           strstr(url, "8bb519e5ff19cfc45bbf0fb7bfcf333106922bf91abce540c3ff310336604cda") != NULL ||
           strstr(url, "63dacb1fad541c7c50a937cbab11cd0858b78187acc6c0af105381aa406e7147") != NULL ||
           strstr(url, "686bd55707be42af370b81d2f36f55b6e10c56edb105502b3f9f0a34f7412bf5") != NULL ||
           strstr(url, "04361e4d6c9352df1c5f06ed40061568c30d46dcd289a35f8135c60368d2877a") != NULL ||
           strstr(url, "5f2ee2913bf919a8576bbe4120a6d41b933e814a4779a0a3fc1d9d941c5ee368") != NULL ||
           strstr(url, "bbeb020bc9e9a383ce34c550733e07f784ca8e09e64a6b32ca309d19c7970e4b") != NULL ||
           strstr(url, "CoreUtilities.") != NULL ||
           strstr(url, "41bd9f2b3a9485661a0c9637526141c355311899d473b7a4ad2cca837f5e47f0") != NULL ||
           strstr(url, "c27f57f4a397dabc2fe3b74fec93c2401913bdf49373f9339c00b6f18b32d2ac") != NULL ||
           strstr(url, "63b59480fef503ff6648900d1051bae7531757a38ce24f77587552fca279d16c") != NULL ||
           strstr(url, "ReactUtilities.") != NULL ||
           strstr(url, "ReactStyleGuide.") != NULL ||
           strstr(url, "SearchLandingPage.") != NULL ||
           strstr(url, "ReactLanding.") != NULL;
}

static unsigned int leonos_fetch_roblox_pre_core_priority(const char *url)
{
    if (url == NULL) {
        return 0u;
    }

    if (strstr(url, "js.rbxcdn.com/") == NULL) {
        return strstr(url, "roblox.com/js/hsts.js") != NULL ? 969u : 0u;
    }

    if (strstr(url, "EnvironmentUrls.") != NULL) {
        return 983u;
    }
    if (strstr(url, "0496306d944faeb488779bad1af0738e") != NULL) {
        return 981u;
    }
    if (strstr(url, "5c8a2ba3737908f693394045e81ebd71c77cde6f87550ea51f7833e8c98200ae") != NULL) {
        return 979u;
    }
    if (strstr(url, "2f0fd0c2760ff1898187af6df3b764f4b08f77a315d0a33654f105f61b0ea6d0") != NULL) {
        return 977u;
    }
    if (strstr(url, "HeaderScripts.") != NULL) {
        return 975u;
    }
    if (strstr(url, "f85ce090699c1c3962762b8a2f8b252f0f2a7d0424c146f41d6c5abbf0147a57") != NULL) {
        return 973u;
    }
    if (strstr(url, "Theme.") != NULL) {
        return 971u;
    }
    if (strstr(url, "1d87d1231072878a0f6164e84b8cfa6f90a1b31b18ba5d8f410b947d4b029fe8") != NULL) {
        return 967u;
    }
    if (strstr(url, "4bae454bf5dab3028073fea1e91b6f19") != NULL) {
        return 965u;
    }
    if (strstr(url, "730a5206adda5785166891801b1e0f9ae622a95558b260a144db60278b243f5f") != NULL) {
        return 963u;
    }
    if (strstr(url, "TranslationResources.") != NULL) {
        return 961u;
    }
    if (strstr(url, "9731c232fa99c58b0dafafc81cc7905b") != NULL) {
        return 959u;
    }
    if (strstr(url, "bfb3e7a7efed2f8ba4d12dc9fdb70dce1ff97ee13e988833a8638cf3ed8fd7f8") != NULL) {
        return 957u;
    }
    return 0u;
}

static bool leonos_fetch_url_is_roblox_core_utilities_script(const char *url)
{
    return url != NULL &&
           strstr(url, "js.rbxcdn.com/") != NULL &&
           strstr(url, "CoreUtilities.") != NULL;
}

static bool leonos_fetch_url_is_roblox_landing_locale_script(const char *url)
{
    if (url == NULL || strstr(url, "js.rbxcdn.com/") == NULL) {
        return false;
    }

    return strstr(url, "8bb519e5ff19cfc45bbf0fb7bfcf333106922bf91abce540c3ff310336604cda") != NULL ||
           strstr(url, "63dacb1fad541c7c50a937cbab11cd0858b78187acc6c0af105381aa406e7147") != NULL ||
           strstr(url, "686bd55707be42af370b81d2f36f55b6e10c56edb105502b3f9f0a34f7412bf5") != NULL ||
           strstr(url, "04361e4d6c9352df1c5f06ed40061568c30d46dcd289a35f8135c60368d2877a") != NULL ||
           strstr(url, "5f2ee2913bf919a8576bbe4120a6d41b933e814a4779a0a3fc1d9d941c5ee368") != NULL ||
           strstr(url, "bbeb020bc9e9a383ce34c550733e07f784ca8e09e64a6b32ca309d19c7970e4b") != NULL;
}

static bool leonos_fetch_url_is_roblox_landing_route_script(const char *url)
{
    if (url == NULL || strstr(url, "js.rbxcdn.com/") == NULL) {
        return false;
    }

    return strstr(url, "SearchLandingPage.") != NULL ||
           strstr(url, "ReactLanding.") != NULL;
}

static bool leonos_fetch_url_is_roblox_react_core_script(const char *url)
{
    return url != NULL &&
           strstr(url, "js.rbxcdn.com/") != NULL &&
           strstr(url, "c27f57f4a397dabc2fe3b74fec93c2401913bdf49373f9339c00b6f18b32d2ac") != NULL;
}

static bool leonos_fetch_url_is_roblox_pre_react_core_script(const char *url)
{
    return url != NULL &&
           strstr(url, "js.rbxcdn.com/") != NULL &&
           strstr(url, "41bd9f2b3a9485661a0c9637526141c355311899d473b7a4ad2cca837f5e47f0") != NULL;
}

static bool leonos_fetch_url_is_roblox_react_shared_script(const char *url)
{
    return url != NULL &&
           strstr(url, "js.rbxcdn.com/") != NULL &&
           strstr(url, "63b59480fef503ff6648900d1051bae7531757a38ce24f77587552fca279d16c") != NULL;
}

static bool leonos_fetch_url_is_roblox_react_utilities_script(const char *url)
{
    return url != NULL &&
           strstr(url, "js.rbxcdn.com/") != NULL &&
           strstr(url, "ReactUtilities.") != NULL;
}

static bool leonos_fetch_url_is_roblox_react_styleguide_script(const char *url)
{
    return url != NULL &&
           strstr(url, "js.rbxcdn.com/") != NULL &&
           strstr(url, "ReactStyleGuide.") != NULL;
}

static bool leonos_fetch_url_is_roblox_post_react_dependency_script(const char *url)
{
    if (url == NULL || strstr(url, "js.rbxcdn.com/") == NULL) {
        return false;
    }

    return leonos_fetch_url_is_roblox_react_utilities_script(url) ||
           leonos_fetch_url_is_roblox_react_styleguide_script(url);
}

static bool leonos_fetch_url_is_roblox_landing_dependency_script(const char *url)
{
    if (url == NULL || strstr(url, "js.rbxcdn.com/") == NULL) {
        return false;
    }

    return strstr(url, "730a5206adda5785166891801b1e0f9ae622a95558b260a144db60278b243f5f") != NULL ||
           strstr(url, "HeaderScripts.") != NULL ||
           strstr(url, "TranslationResources.") != NULL ||
           strstr(url, "CoreUtilities.") != NULL ||
           strstr(url, "c27f57f4a397dabc2fe3b74fec93c2401913bdf49373f9339c00b6f18b32d2ac") != NULL ||
           strstr(url, "ReactUtilities.") != NULL ||
           strstr(url, "ReactStyleGuide.") != NULL;
}

static bool leonos_fetch_url_is_roblox_late_script(const char *url,
        unsigned int script_count)
{
    return url != NULL &&
           script_count > LEONOS_FETCH_ROBLOX_SCRIPT_BUDGET &&
           strstr(url, "js.rbxcdn.com/") != NULL &&
           !leonos_fetch_url_is_roblox_landing_script(url);
}

static bool leonos_fetch_url_is_roblox_deferred_media(const char *url)
{
    size_t len;
    if (url == NULL || strstr(url, "rbxcdn.com/") == NULL) {
        return false;
    }
    len = strlen(url);
    for (size_t i = 0u; i < len; i += 1u) {
        if (url[i] == '?' || url[i] == '#') {
            len = i;
            break;
        }
    }
    return strstr(url, "tr.rbxcdn.com/") != NULL ||
           strstr(url, "vignette") != NULL;
}

static void leonos_fetch_reset_roblox_dependencies(void)
{
    leonos_fetch_roblox_react_core_delivered = false;
    leonos_fetch_roblox_react_shared_delivered = false;
    leonos_fetch_roblox_react_utilities_seen = false;
    leonos_fetch_roblox_react_utilities_delivered = false;
    leonos_fetch_roblox_react_styleguide_seen = false;
    leonos_fetch_roblox_react_styleguide_delivered = false;
}

static void leonos_fetch_note_roblox_script_seen(const char *url)
{
    if (leonos_fetch_url_is_roblox_react_utilities_script(url)) {
        leonos_fetch_roblox_react_utilities_seen = true;
    }
    if (leonos_fetch_url_is_roblox_react_styleguide_script(url)) {
        leonos_fetch_roblox_react_styleguide_seen = true;
    }
}

static bool leonos_fetch_roblox_delivery_dependencies_ready(const char *url)
{
    if (leonos_fetch_url_is_roblox_react_shared_script(url)) {
        return leonos_fetch_roblox_react_core_delivered;
    }
    if (leonos_fetch_url_is_roblox_post_react_dependency_script(url)) {
        return leonos_fetch_roblox_react_shared_delivered;
    }
    if (leonos_fetch_url_is_roblox_landing_route_script(url)) {
        if (!leonos_fetch_roblox_react_shared_delivered) {
            return false;
        }
        if (leonos_fetch_roblox_react_utilities_seen &&
            !leonos_fetch_roblox_react_utilities_delivered) {
            return false;
        }
        if (leonos_fetch_roblox_react_styleguide_seen &&
            !leonos_fetch_roblox_react_styleguide_delivered) {
            return false;
        }
    }
    return true;
}

static bool leonos_fetch_waiting_for_delivery_dependency(
        struct leonos_fetch_context *ctx)
{
    const char *url = nsurl_access(ctx->url);

    if (leonos_fetch_roblox_delivery_dependencies_ready(url)) {
        return false;
    }

    if (!ctx->dependency_wait_logged) {
        ctx->dependency_wait_logged = true;
        leonos_fetch_log("NETSURF LEO HTTPS delivery dependency wait ");
        leonos_fetch_log(url);
        leonos_fetch_log("\r\n");
    }
    (void) leonos_yield();
    return true;
}

static void leonos_fetch_note_roblox_script_delivered(const char *url)
{
    if (leonos_fetch_url_is_roblox_react_core_script(url)) {
        leonos_fetch_roblox_react_core_delivered = true;
        leonos_fetch_log("NETSURF LEO HTTPS dependency delivered react-core\r\n");
    } else if (leonos_fetch_url_is_roblox_react_shared_script(url)) {
        leonos_fetch_roblox_react_shared_delivered = true;
        leonos_fetch_log("NETSURF LEO HTTPS dependency delivered react-shared\r\n");
    } else if (leonos_fetch_url_is_roblox_react_utilities_script(url)) {
        leonos_fetch_roblox_react_utilities_delivered = true;
        leonos_fetch_log("NETSURF LEO HTTPS dependency delivered react-utilities\r\n");
    } else if (leonos_fetch_url_is_roblox_react_styleguide_script(url)) {
        leonos_fetch_roblox_react_styleguide_delivered = true;
        leonos_fetch_log("NETSURF LEO HTTPS dependency delivered react-styleguide\r\n");
    }
}

static bool leonos_fetch_url_looks_subresource(const char *url)
{
    size_t len;
    if (url == NULL) {
        return false;
    }
    len = strlen(url);
    for (size_t i = 0u; i < len; i += 1u) {
        if (url[i] == '?' || url[i] == '#') {
            len = i;
            break;
        }
    }
    return (len >= 3u && strncmp(url + len - 3u, ".js", 3) == 0) ||
           (len >= 4u && strncmp(url + len - 4u, ".css", 4) == 0) ||
           (len >= 4u && strncmp(url + len - 4u, ".png", 4) == 0) ||
           (len >= 4u && strncmp(url + len - 4u, ".jpg", 4) == 0) ||
           (len >= 5u && strncmp(url + len - 5u, ".jpeg", 5) == 0) ||
           (len >= 4u && strncmp(url + len - 4u, ".gif", 4) == 0) ||
           (len >= 4u && strncmp(url + len - 4u, ".ico", 4) == 0) ||
           (len >= 4u && strncmp(url + len - 4u, ".svg", 4) == 0) ||
           (len >= 5u && strncmp(url + len - 5u, ".webp", 5) == 0) ||
           strstr(url, "/css/") != NULL ||
           strstr(url, "/js/") != NULL ||
           strstr(url, "/images/") != NULL ||
           strstr(url, "images.rbxcdn.com/") != NULL ||
           strstr(url, "tr.rbxcdn.com/") != NULL;
}

static void leonos_fetch_send_callback(const fetch_msg *msg,
        struct leonos_fetch_context *ctx)
{
    ctx->locked = true;
    fetch_send_callback(msg, ctx->parent_fetch);
    ctx->locked = false;
}

static void leonos_fetch_send_header(struct leonos_fetch_context *ctx,
        const char *fmt, ...)
{
    char header[96];
    fetch_msg msg;
    va_list ap;
    int len;

    va_start(ap, fmt);
    len = vsnprintf(header, sizeof(header), fmt, ap);
    va_end(ap);

    if (len < 0 || len >= (int) sizeof(header)) {
        return;
    }

    msg.type = FETCH_HEADER;
    msg.data.header_or_data.buf = (const uint8_t *) header;
    msg.data.header_or_data.len = (size_t) len;
    leonos_fetch_send_callback(&msg, ctx);
}

static void leonos_fetch_send_headers(struct leonos_fetch_context *ctx,
        bool include_length)
{
    const char *content_type;

    if (ctx->headers_sent) {
        return;
    }
    if (ctx->status_code < 100u || ctx->status_code > 999u) {
        ctx->status_code = 200u;
    }

    fetch_set_http_code(ctx->parent_fetch, ctx->status_code);
    content_type = ctx->content_type[0] != 0 ?
            ctx->content_type : leonos_fetch_mime_from_url(nsurl_access(ctx->url));
    leonos_fetch_send_header(ctx, "Content-Type: %s", content_type);
    if (!ctx->aborted && include_length) {
        leonos_fetch_send_header(ctx, "Content-Length: %u",
                (unsigned int) ctx->data_len);
    }
    ctx->headers_sent = true;
}

static bool leonos_fetch_is_redirect(const struct leonos_fetch_context *ctx)
{
    return ctx->status_code >= 300u && ctx->status_code < 400u &&
            ctx->location[0] != 0;
}

static void leonos_fetch_send_data_callbacks(struct leonos_fetch_context *ctx)
{
    fetch_msg msg;

    if (!ctx->data_log_started) {
        leonos_fetch_log("NETSURF LEO HTTPS fetch data callbacks begin\r\n");
        ctx->data_log_started = true;
    }
    while (!ctx->aborted && ctx->callback_off < ctx->data_len) {
        size_t chunk = ctx->data_len - ctx->callback_off;
        if (chunk > LEONOS_FETCH_CHUNK_BYTES) {
            chunk = LEONOS_FETCH_CHUNK_BYTES;
        }
        msg.type = FETCH_DATA;
        msg.data.header_or_data.buf = ctx->data + ctx->callback_off;
        msg.data.header_or_data.len = chunk;
        leonos_fetch_send_callback(&msg, ctx);
        ctx->callback_off += chunk;
#ifdef LEONOS_USER_APP
        if (ctx->stream_chunk_logs < 4u) {
            char lenbuf[16];
            int used;
            ctx->stream_chunk_logs += 1u;
            leonos_fetch_log("NETSURF LEO HTTPS stream callback bytes ");
            used = snprintf(lenbuf, sizeof(lenbuf), "%u",
                    (unsigned int) chunk);
            if (used > 0) {
                leonos_fetch_log(lenbuf);
            }
            leonos_fetch_log("\r\n");
        }
#endif
    }
}

#ifdef LEONOS_USER_APP
static void leonos_fetch_copy_meta(struct leonos_fetch_context *ctx,
        const struct leonos_net_fetch_meta *meta)
{
    ctx->status_code = meta->status_code;
    ctx->fetch_flags = meta->flags;
    ctx->content_type[0] = 0;
    for (size_t i = 0u; i + 1u < sizeof(ctx->content_type) &&
         meta->content_type[i] != 0; i += 1u) {
        ctx->content_type[i] = meta->content_type[i];
        ctx->content_type[i + 1u] = 0;
    }
    ctx->location[0] = 0;
    for (size_t i = 0u; i + 1u < sizeof(ctx->location) &&
         meta->location[i] != 0; i += 1u) {
        ctx->location[i] = meta->location[i];
        ctx->location[i + 1u] = 0;
    }
    leonos_fetch_log("NETSURF LEO HTTPS metadata status ");
    char statusbuf[16];
    int status_used = snprintf(statusbuf, sizeof(statusbuf), "%u",
            ctx->status_code);
    if (status_used > 0) {
        leonos_fetch_log(statusbuf);
    }
    leonos_fetch_log(" type ");
    leonos_fetch_log(ctx->content_type[0] != 0 ?
            ctx->content_type : "unknown");
    leonos_fetch_log("\r\n");
}

static void leonos_fetch_close_stream(struct leonos_fetch_context *ctx)
{
    if (ctx->stream_handle != 0u) {
        (void) leonos_net_stream_close(ctx->stream_handle);
        ctx->stream_handle = 0u;
    }
}

static bool leonos_fetch_open_stream(struct leonos_fetch_context *ctx,
        const char *url)
{
    ctx->stream_handle = leonos_net_stream_open(url);

    if (ctx->stream_handle == 0u) {
        return false;
    }
    if (ctx->open_retries != 0u) {
        leonos_fetch_log("NETSURF LEO HTTPS stream open after wait\r\n");
    }
    leonos_fetch_log("NETSURF LEO HTTPS stream open handle ");
    char hbuf[16];
    int hused = snprintf(hbuf, sizeof(hbuf), "%u", ctx->stream_handle);
    if (hused > 0) {
        leonos_fetch_log(hbuf);
    }
    leonos_fetch_log("\r\n");
    return true;
}

static bool leonos_fetch_refresh_meta(struct leonos_fetch_context *ctx)
{
    struct leonos_net_fetch_meta meta;

    if (ctx->stream_handle == 0u ||
        !leonos_net_stream_meta(ctx->stream_handle, &meta)) {
        return false;
    }
    if ((meta.flags & LEONOS_NET_FETCH_FLAG_HEADERS) == 0u &&
        meta.status_code == 0u && meta.content_type[0] == 0) {
        return false;
    }
    if (ctx->status_code != meta.status_code ||
        ctx->fetch_flags != meta.flags ||
        strcmp(ctx->content_type, meta.content_type) != 0 ||
        strcmp(ctx->location, meta.location) != 0) {
        leonos_fetch_copy_meta(ctx, &meta);
    }
    return true;
}

static bool leonos_fetch_stream_step(struct leonos_fetch_context *ctx)
{
    bool done = false;
    bool ok = false;
    unsigned int idle_steps = 0u;

    if (ctx->stream_handle == 0u) {
        return true;
    }

    for (unsigned int step = 0u;
         step < LEONOS_FETCH_STREAM_POLL_BUDGET && !ctx->aborted;
         step += 1u) {
        bool made_progress = false;
        ctx->stream_state = leonos_net_stream_poll(ctx->stream_handle);
        ctx->stream_polls += 1u;

        if ((ctx->stream_state &
             (LEONOS_NET_STREAM_HAS_DATA | LEONOS_NET_STREAM_DONE)) != 0u) {
            (void) leonos_fetch_refresh_meta(ctx);
        }
        if (!leonos_fetch_is_redirect(ctx) && !ctx->headers_sent &&
            (ctx->fetch_flags & LEONOS_NET_FETCH_FLAG_HEADERS) != 0u) {
            leonos_fetch_send_headers(ctx, false);
        }

        if (!leonos_fetch_is_redirect(ctx) &&
            (ctx->stream_state & LEONOS_NET_STREAM_HAS_DATA) != 0u) {
            if (ctx->data == NULL) {
                ctx->data = malloc(LEONOS_FETCH_MAX_BYTES);
                if (ctx->data == NULL) {
                    leonos_fetch_log("NETSURF LEO HTTPS stream buffer alloc failed\r\n");
                    leonos_fetch_close_stream(ctx);
                    return true;
                }
            }
            while (ctx->data_len < LEONOS_FETCH_MAX_BYTES) {
                uint8_t read_buf[LEONOS_FETCH_CHUNK_BYTES];
                uint32_t got = leonos_net_stream_read(ctx->stream_handle,
                        read_buf, (uint32_t) sizeof(read_buf));
                if (got == 0u) {
                    break;
                }
                made_progress = true;
                leonos_fetch_append_stream_data(ctx, read_buf, got);
            }
            if (ctx->headers_sent) {
                leonos_fetch_send_data_callbacks(ctx);
            }
        }

        if (ctx->data_len >= LEONOS_FETCH_MAX_BYTES &&
            (ctx->stream_state & LEONOS_NET_STREAM_DONE) == 0u) {
            leonos_fetch_log("NETSURF LEO HTTPS stream truncated at buffer cap\r\n");
            ctx->stream_state |= LEONOS_NET_STREAM_DONE | LEONOS_NET_STREAM_OK;
        }

        done = (ctx->stream_state & LEONOS_NET_STREAM_DONE) != 0u;
        ok = (ctx->stream_state & LEONOS_NET_STREAM_OK) != 0u;
        if (done || ctx->aborted) {
            break;
        }
        if (made_progress) {
            idle_steps = 0u;
        } else {
            idle_steps += 1u;
        }
        if (idle_steps >= LEONOS_FETCH_STREAM_IDLE_STEP_LIMIT) {
            break;
        }
    }

    if (ctx->aborted) {
        leonos_fetch_close_stream(ctx);
        return true;
    }
    if (!done) {
        (void) leonos_yield();
        return false;
    }
    if (!ok) {
        leonos_fetch_log("NETSURF LEO HTTPS stream failed\r\n");
        leonos_fetch_close_stream(ctx);
        return true;
    }

    (void) leonos_fetch_refresh_meta(ctx);
    leonos_fetch_close_stream(ctx);

    leonos_fetch_log("NETSURF LEO HTTPS stream completed polls ");
    char pollbuf[16];
    int poll_used = snprintf(pollbuf, sizeof(pollbuf), "%u",
            (unsigned int) ctx->stream_polls);
    if (poll_used > 0) {
        leonos_fetch_log(pollbuf);
    }
    leonos_fetch_log(" bytes ");
    char lenbuf[16];
    int len_used = snprintf(lenbuf, sizeof(lenbuf), "%u",
            (unsigned int) ctx->data_len);
    if (len_used > 0) {
        leonos_fetch_log(lenbuf);
    }
    leonos_fetch_log("\r\n");
    return true;
}
#else
static void leonos_fetch_close_stream(struct leonos_fetch_context *ctx)
{
    (void) ctx;
}

static bool leonos_fetch_open_stream(struct leonos_fetch_context *ctx,
        const char *url)
{
    (void) ctx;
    (void) url;
    return false;
}

static bool leonos_fetch_stream_step(struct leonos_fetch_context *ctx)
{
    (void) ctx;
    return true;
}
#endif

static void leonos_fetch_send_finished(struct leonos_fetch_context *ctx)
{
    fetch_msg msg;

    if (ctx->completed) {
        return;
    }
    if (!ctx->aborted) {
        msg.type = FETCH_FINISHED;
        leonos_fetch_send_callback(&msg, ctx);
        leonos_fetch_note_roblox_script_delivered(nsurl_access(ctx->url));
#ifdef LEONOS_USER_APP
        leonos_netsurf_fetch_finished_for_script = 1;
#endif
        leonos_fetch_log("NETSURF LEO HTTPS fetch finished callback done\r\n");
    }
    ctx->completed = true;
}

static void leonos_fetch_send_error(struct leonos_fetch_context *ctx,
        const char *error)
{
    fetch_msg msg;

    if (ctx->completed) {
        return;
    }
    leonos_fetch_log("NETSURF LEO HTTPS fetch failed\r\n");
    msg.type = FETCH_ERROR;
    msg.data.error = error;
    leonos_fetch_send_callback(&msg, ctx);
    ctx->completed = true;
}

static void leonos_fetch_send_redirect(struct leonos_fetch_context *ctx)
{
    fetch_msg msg;

    if (ctx->completed || ctx->location[0] == 0) {
        return;
    }
    leonos_fetch_log("NETSURF LEO HTTPS redirect ");
    leonos_fetch_log(ctx->location);
    leonos_fetch_log("\r\n");
    if (ctx->status_code >= 300u && ctx->status_code < 400u) {
        fetch_set_http_code(ctx->parent_fetch, ctx->status_code);
    }
    msg.type = FETCH_REDIRECT;
    msg.data.redirect = ctx->location;
    leonos_fetch_send_callback(&msg, ctx);
    ctx->completed = true;
    leonos_fetch_close_stream(ctx);
}

static void leonos_fetch_log_bytes(struct leonos_fetch_context *ctx)
{
    leonos_fetch_log("NETSURF LEO HTTPS fetch bytes ");
#ifdef LEONOS_USER_APP
    char lenbuf[16];
    int used = snprintf(lenbuf, sizeof(lenbuf), "%u",
            (unsigned int) ctx->data_len);
    if (used > 0) {
        leonos_fetch_log(lenbuf);
    }
#endif
    leonos_fetch_log("\r\n");
}

static void leonos_fetch_finish_buffered_body(struct leonos_fetch_context *ctx)
{
    leonos_fetch_log_bytes(ctx);
    leonos_fetch_send_headers(ctx, true);
    if (!ctx->aborted) {
        leonos_fetch_send_data_callbacks(ctx);
        leonos_fetch_log("NETSURF LEO HTTPS fetch data callbacks done\r\n");
    }
    leonos_fetch_send_finished(ctx);
}

static void *leonos_fetch_setup(struct fetch *parent_fetch, nsurl *url,
        bool only_2xx, bool downgrade_tls, const char *post_urlenc,
        const struct fetch_multipart_data *post_multipart,
        const char **headers)
{
    struct leonos_fetch_context *ctx;

    (void) only_2xx;
    (void) downgrade_tls;
    (void) post_urlenc;
    (void) post_multipart;
    (void) headers;

    ctx = calloc(1, sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    ctx->parent_fetch = parent_fetch;
    ctx->url = nsurl_ref(url);
    ctx->order = leonos_fetch_next_order++;
    if (leonos_fetch_next_order == 0u) {
        leonos_fetch_next_order = 1u;
    }
    if (!leonos_fetch_url_looks_subresource(nsurl_access(url))) {
        leonos_fetch_css_count = 0u;
        leonos_fetch_js_count = 0u;
        leonos_fetch_next_order = 1u;
        leonos_fetch_reset_roblox_dependencies();
    } else if (leonos_fetch_url_looks_css(nsurl_access(url))) {
        leonos_fetch_css_count += 1u;
        if (leonos_fetch_url_is_roblox_deferred_style(nsurl_access(url)) ||
            leonos_fetch_url_is_roblox_deferred_bundle(nsurl_access(url)) ||
            (LEONOS_FETCH_CSS_SOFT_LIMIT != 0u &&
            leonos_fetch_css_count > LEONOS_FETCH_CSS_SOFT_LIMIT)) {
            ctx->synthetic_empty = true;
            ctx->status_code = 200u;
            (void) snprintf(ctx->content_type, sizeof(ctx->content_type),
                    "text/css");
        }
    } else if (leonos_fetch_url_looks_js(nsurl_access(url))) {
        leonos_fetch_js_count += 1u;
        if (leonos_fetch_url_is_roblox_late_script(nsurl_access(url),
                leonos_fetch_js_count) ||
            leonos_fetch_url_is_roblox_deferred_bundle(nsurl_access(url)) ||
            (LEONOS_FETCH_JS_SOFT_LIMIT != 0u &&
            leonos_fetch_js_count > LEONOS_FETCH_JS_SOFT_LIMIT)) {
            ctx->synthetic_empty = true;
            ctx->status_code = 200u;
            (void) snprintf(ctx->content_type, sizeof(ctx->content_type),
                    "application/javascript");
        }
        leonos_fetch_note_roblox_script_seen(nsurl_access(url));
    } else if (leonos_fetch_url_is_roblox_deferred_media(nsurl_access(url))) {
        const char *media_type = leonos_fetch_mime_from_url(nsurl_access(url));
        if (strncmp(media_type, "text/html", 9) == 0) {
            media_type = "image/png";
        }
        ctx->synthetic_empty = true;
        ctx->status_code = 200u;
        (void) snprintf(ctx->content_type, sizeof(ctx->content_type), "%s",
                media_type);
    }
#ifdef LEONOS_USER_APP
    leonos_netsurf_active_fetches_for_script += 1;
    leonos_netsurf_fetch_generation_for_script += 1u;
    leonos_netsurf_fetch_finished_for_script = 0;
#endif
    leonos_fetch_log("NETSURF LEO HTTPS fetch setup ");
    leonos_fetch_log(nsurl_access(url));
    if (ctx->synthetic_empty) {
        leonos_fetch_log(" synthetic-empty");
    }
    leonos_fetch_log("\r\n");
    RING_INSERT(leonos_fetch_ring, ctx);
    return ctx;
}

static bool leonos_fetch_start(void *ctx)
{
    (void) ctx;
    return true;
}

static void leonos_fetch_abort(void *ctx)
{
    struct leonos_fetch_context *c = ctx;
    c->aborted = true;
    leonos_fetch_close_stream(c);
}

static void leonos_fetch_free(void *ctx)
{
    struct leonos_fetch_context *c = ctx;
    leonos_fetch_close_stream(c);
    nsurl_unref(c->url);
    free(c->data);
#ifdef LEONOS_USER_APP
    if (leonos_netsurf_active_fetches_for_script > 0) {
        leonos_netsurf_active_fetches_for_script -= 1;
    }
#endif
    free(c);
}

static bool leonos_fetch_process(struct leonos_fetch_context *ctx)
{
    const char *url = nsurl_access(ctx->url);

    if (!ctx->begun) {
        ctx->begun = true;
        leonos_fetch_log("NETSURF LEO HTTPS fetch begin ");
        leonos_fetch_log(url);
        leonos_fetch_log("\r\n");
    }

    if (strlen(url) > LEONOS_FETCH_MAX_URL_BYTES) {
        leonos_fetch_log("NETSURF LEO HTTPS fetch URL too long\r\n");
        leonos_fetch_send_error(ctx, "LeonOS HTTPS URL too long");
        return true;
    }

    if (ctx->synthetic_empty) {
        leonos_fetch_log("NETSURF LEO HTTPS synthetic empty resource\r\n");
        leonos_fetch_finish_buffered_body(ctx);
        return true;
    }

    if (ctx->stream_handle == 0u && ctx->data_len == 0u &&
        !ctx->completed) {
        if (!leonos_fetch_open_stream(ctx, url)) {
            if (!ctx->open_wait_logged) {
                leonos_fetch_log("NETSURF LEO HTTPS stream open waiting\r\n");
                ctx->open_wait_logged = true;
            }
            if (ctx->open_retries < LEONOS_FETCH_OPEN_RETRY_BUDGET) {
                ctx->open_retries += 1u;
                (void) leonos_yield();
                return false;
            }
            leonos_fetch_log("NETSURF LEO HTTPS stream open failed\r\n");
            leonos_fetch_send_error(ctx, "LeonOS HTTPS stream open failed");
            return true;
        }
    }

    if (!leonos_fetch_stream_step(ctx)) {
        if (leonos_fetch_is_redirect(ctx)) {
            leonos_fetch_send_redirect(ctx);
            return true;
        }
        return false;
    }

    if (ctx->completed || ctx->aborted) {
        return true;
    }
    if (leonos_fetch_is_redirect(ctx)) {
        leonos_fetch_send_redirect(ctx);
        return true;
    }

    if ((ctx->stream_state & LEONOS_NET_STREAM_DONE) != 0u &&
        (ctx->stream_state & LEONOS_NET_STREAM_OK) == 0u) {
        leonos_fetch_send_error(ctx, "LeonOS HTTPS stream failed");
        return true;
    }

    if (!ctx->headers_sent) {
        if (leonos_fetch_waiting_for_delivery_dependency(ctx)) {
            return false;
        }
        leonos_fetch_finish_buffered_body(ctx);
        return true;
    }

    if (leonos_fetch_waiting_for_delivery_dependency(ctx)) {
        return false;
    }
    if (!ctx->aborted) {
        leonos_fetch_log("NETSURF LEO HTTPS fetch data callbacks done\r\n");
    }
    leonos_fetch_log_bytes(ctx);
    leonos_fetch_send_finished(ctx);
    return true;
}

static bool leonos_fetch_priority_has_begun(
        const struct leonos_fetch_context *ctx)
{
#ifdef LEONOS_USER_APP
    if (ctx->begun && ctx->stream_handle == 0u && ctx->data_len == 0u &&
        !ctx->completed && !ctx->synthetic_empty) {
        return false;
    }
#endif
    return ctx->begun;
}

static unsigned int leonos_fetch_priority(
        const struct leonos_fetch_context *ctx)
{
    const char *url = nsurl_access(ctx->url);
    bool begun = leonos_fetch_priority_has_begun(ctx);
    unsigned int roblox_priority;

    if (ctx->locked) {
        return 0u;
    }
#ifdef LEONOS_USER_APP
    if (ctx->stream_handle != 0u) {
        return 1000u;
    }
#endif
    if (!leonos_fetch_url_looks_subresource(url)) {
        return 900u;
    }
    if (leonos_fetch_url_looks_css(url)) {
        if (ctx->synthetic_empty) {
            return begun ? 890u : 880u;
        }
        return begun ? 990u : 985u;
    }
    if (leonos_fetch_url_looks_js(url)) {
        roblox_priority = leonos_fetch_roblox_pre_core_priority(url);
        if (roblox_priority != 0u) {
            return begun ? roblox_priority + 1u : roblox_priority;
        }
        if (leonos_fetch_url_is_roblox_core_utilities_script(url)) {
            return begun ? 956u : 955u;
        }
        if (leonos_fetch_url_is_roblox_pre_react_core_script(url)) {
            return begun ? 954u : 953u;
        }
        if (leonos_fetch_url_is_roblox_react_core_script(url)) {
            return begun ? 952u : 951u;
        }
        if (leonos_fetch_url_is_roblox_react_shared_script(url)) {
            return begun ? 950u : 949u;
        }
        if (leonos_fetch_url_is_roblox_landing_locale_script(url)) {
            return begun ? 946u : 945u;
        }
        if (leonos_fetch_url_is_roblox_landing_dependency_script(url)) {
            if (leonos_fetch_url_is_roblox_post_react_dependency_script(url)) {
                return begun ? 948u : 947u;
            }
            return begun ? 944u : 943u;
        }
        if (leonos_fetch_url_is_roblox_landing_route_script(url)) {
            return begun ? 942u : 941u;
        }
        if (leonos_fetch_url_is_roblox_landing_script(url)) {
            return begun ? 940u : 939u;
        }
        return begun ? 740u : 730u;
    }
    return begun ? 260u : 250u;
}

static bool leonos_fetch_has_pending_css(void)
{
    struct leonos_fetch_context *ctx;

    if (leonos_fetch_ring == NULL) {
        return false;
    }

    ctx = leonos_fetch_ring;
    do {
        if (!ctx->locked && !ctx->aborted &&
            leonos_fetch_url_looks_css(nsurl_access(ctx->url))) {
            return true;
        }
        ctx = ctx->r_next;
    } while (ctx != leonos_fetch_ring);

    return false;
}

static struct leonos_fetch_context *leonos_fetch_pick_next(void)
{
    struct leonos_fetch_context *ctx;
    struct leonos_fetch_context *best = NULL;
    unsigned int best_priority = 0u;

    if (leonos_fetch_ring == NULL) {
        return NULL;
    }

    ctx = leonos_fetch_ring;
    do {
        unsigned int priority = leonos_fetch_priority(ctx);
        if (best == NULL || priority > best_priority ||
            (priority == best_priority && ctx->order < best->order)) {
            best = ctx;
            best_priority = priority;
        }
        ctx = ctx->r_next;
    } while (ctx != leonos_fetch_ring);

    return best;
}

static void leonos_fetch_poll(lwc_string *scheme)
{
    struct leonos_fetch_context *ctx;
    struct leonos_fetch_context *save_ring = NULL;
    static unsigned int poll_log_count;
    unsigned int processed = 0u;

    (void) scheme;

    if (poll_log_count < 2u) {
        poll_log_count += 1u;
        leonos_fetch_log("NETSURF LEO HTTPS fetcher poll\r\n");
    }

    if (leonos_fetch_js_completion_cooldown != 0u &&
        !leonos_fetch_has_pending_css()) {
        leonos_fetch_js_completion_cooldown -= 1u;
        return;
    }
    leonos_fetch_js_completion_cooldown = 0u;

    while (leonos_fetch_ring != NULL &&
           processed < LEONOS_FETCH_POLL_CONTEXT_BUDGET) {
        ctx = leonos_fetch_pick_next();
        if (ctx == NULL) {
            break;
        }
        RING_REMOVE(leonos_fetch_ring, ctx);
        bool finished = false;
        bool finished_js = leonos_fetch_url_looks_js(nsurl_access(ctx->url));
        processed += 1u;

        if (ctx->locked) {
            RING_INSERT(save_ring, ctx);
            continue;
        }

        if (!ctx->aborted) {
            finished = leonos_fetch_process(ctx);
        } else {
            leonos_fetch_close_stream(ctx);
            finished = true;
        }

        if (finished) {
            fetch_remove_from_queues(ctx->parent_fetch);
            fetch_free(ctx->parent_fetch);
            if (finished_js) {
                leonos_fetch_js_completion_cooldown = 3u;
                break;
            }
        } else {
            RING_INSERT(save_ring, ctx);
        }
    }

    while (leonos_fetch_ring != NULL) {
        ctx = leonos_fetch_ring;
        RING_REMOVE(leonos_fetch_ring, ctx);
        RING_INSERT(save_ring, ctx);
    }

    leonos_fetch_ring = save_ring;
}

nserror leonos_fetcher_register(void)
{
    lwc_string *scheme;
    const struct fetcher_operation_table fetcher_ops = {
        .initialise = leonos_fetch_initialise,
        .acceptable = leonos_fetch_can_fetch,
        .setup = leonos_fetch_setup,
        .start = leonos_fetch_start,
        .abort = leonos_fetch_abort,
        .free = leonos_fetch_free,
        .poll = leonos_fetch_poll,
        .finalise = leonos_fetch_finalise
    };

    if (lwc_intern_string("https", SLEN("https"), &scheme) !=
            lwc_error_ok) {
        return NSERROR_NOMEM;
    }

    leonos_fetch_log("NETSURF LEO HTTPS fetcher register\r\n");
    return fetcher_add(scheme, &fetcher_ops);
}
