#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "javascript/js.h"
#include "javascript/content.h"
#include "html/private.h"
#include "utils/nsurl.h"
#include "utils/errors.h"
#include "utils/nsoption.h"

#include <dom/dom.h>

#include "quickjs.h"

#ifdef LEONOS_USER_APP
#include "leonos_user.h"
#endif

struct jsheap {
	JSRuntime *runtime;
	int timeout;
	uint32_t interrupt_budget;
	uint32_t interrupt_limit;
	bool interrupt_enabled;
	bool interrupt_hit;
	unsigned int live_threads;
	bool pending_destroy;
};

struct jsthread {
	struct jsheap *heap;
	JSContext *ctx;
	html_content *htmlc;
	bool closed;
};

struct qjs_native_node {
	dom_node *node;
};

static JSClassID qjs_dom_node_class_id;

static JSValue qjs_new_dom_node(JSContext *ctx, dom_node *node);
static JSValue qjs_new_dom_element(JSContext *ctx, dom_element *element);
static JSValue qjs_new_dom_node_limited(JSContext *ctx, dom_node *node,
				       unsigned int ancestor_depth,
				       bool populate_children);
static uint32_t qjs_array_length(JSContext *ctx, JSValueConst array);

static int qjs_interrupt_handler(JSRuntime *rt, void *opaque)
{
	jsheap *heap = opaque;
	(void) rt;
	if (heap == NULL || !heap->interrupt_enabled) {
		return 0;
	}
	if (heap->interrupt_budget == 0u) {
		heap->interrupt_hit = true;
		return 1;
	}
	heap->interrupt_budget -= 1u;
	return 0;
}

static uint32_t qjs_script_interrupt_limit(const jsheap *heap, size_t bytes,
					   const char *name)
{
	uint32_t timeout = 10u;
	uint32_t limit;
	if (heap != NULL && heap->timeout > 0) {
		timeout = (uint32_t) heap->timeout;
	}
	limit = 120u + (uint32_t) (bytes / 1024u) * 3u;
	if (limit < timeout * 35u) {
		limit = timeout * 35u;
	}
	if (limit > timeout * 100u) {
		limit = timeout * 100u;
	}
	if (name != NULL &&
	    (strstr(name, "ReactLanding.") != NULL ||
	     strstr(name, "SearchLandingPage.") != NULL) &&
	    strstr(name, "js.rbxcdn.com/") != NULL &&
	    limit < timeout * 5000u) {
		limit = timeout * 5000u;
	}
	return limit;
}

static void qjs_begin_script_interrupt(jsheap *heap, size_t bytes,
				       const char *name)
{
	if (heap == NULL) {
		return;
	}
	heap->interrupt_limit = qjs_script_interrupt_limit(heap, bytes, name);
	heap->interrupt_budget = heap->interrupt_limit;
	heap->interrupt_hit = false;
	heap->interrupt_enabled = true;
}

static bool qjs_end_script_interrupt(jsheap *heap)
{
	bool hit;
	if (heap == NULL) {
		return false;
	}
	hit = heap->interrupt_hit;
	heap->interrupt_enabled = false;
	heap->interrupt_budget = 0u;
	heap->interrupt_limit = 0u;
	heap->interrupt_hit = false;
	return hit;
}

static bool qjs_mem_contains(const char *text, size_t text_len,
			     const char *needle)
{
	size_t needle_len;
	if (text == NULL || needle == NULL) {
		return false;
	}
	needle_len = strlen(needle);
	if (needle_len == 0u || needle_len > text_len) {
		return false;
	}
	for (size_t i = 0u; i + needle_len <= text_len; i++) {
		if (memcmp(text + i, needle, needle_len) == 0) {
			return true;
		}
	}
	return false;
}

static bool qjs_should_skip_blocking_script(const char *name,
					    const char *source,
					    size_t bytes)
{
	/* These Roblox route bundles can monopolize QuickJS compile pre-paint. */
	if (name != NULL &&
	    (strstr(name, "Challenge.js") != NULL ||
	     strstr(name, "UserProfiles.js") != NULL ||
	     strstr(name, "Navigation.js") != NULL ||
	     strstr(name, "Sentry.js") != NULL ||
	     strstr(name, "SearchLandingPage.js") != NULL ||
	     strstr(name, "63b59480fef503ff6648900d1051bae7531757a38ce24f77587552fca279d16c") != NULL ||
	     strstr(name, "CoreUtilities.js") != NULL ||
	     strstr(name, "ReactStyleGuide.js") != NULL ||
	     strstr(name, "Thumbnails.js") != NULL ||
	     strstr(name, "PresenceStatus.js") != NULL ||
	     strstr(name, "RealTime.js") != NULL ||
	     strstr(name, "AccountSwitcher.js") != NULL ||
	     strstr(name, "VerificationUpsell.js") != NULL ||
	     strstr(name, "EmailVerifyCodeModal.js") != NULL ||
	     strstr(name, "Captcha") != NULL ||
	     strstr(name, "CookieBanner") != NULL ||
	     strstr(name, "Footer.js") != NULL ||
	     (strstr(name, "-StyleGuide.js") != NULL &&
	      strstr(name, "ReactStyleGuide.js") == NULL) ||
	     strstr(name, "Builder.js") != NULL ||
	     strstr(name, "ItemPurchase") != NULL ||
	     strstr(name, "ItemDetailsHydrationService") != NULL ||
	     strstr(name, "IdVerification") != NULL ||
	     strstr(name, "AccessManagement") != NULL ||
	     strstr(name, "GameLaunch") != NULL)) {
		return true;
	}
	if (bytes < 64u * 1024u) {
		return false;
	}
	return qjs_mem_contains(source, bytes, "bundleDetected(\"UserProfiles\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"Navigation\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"Sentry\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"SearchLandingPage\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"WebBlox\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"CoreUtilities\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"ReactStyleGuide\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"Thumbnails\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"PresenceStatus\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"RealTime\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"AccountSwitcher\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"VerificationUpsell\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"EmailVerifyCodeModal\")") ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"Captcha\")") ||
	       (qjs_mem_contains(source, bytes, "bundleDetected(\"StyleGuide\")") &&
		!qjs_mem_contains(source, bytes, "bundleDetected(\"ReactStyleGuide\")")) ||
	       qjs_mem_contains(source, bytes, "bundleDetected(\"Builder\")") ||
	       qjs_mem_contains(source, bytes,
				"bundleDetected(\"ItemDetailsHydrationService\")");
}

static unsigned int qjs_cstr_len(const char *text)
{
	unsigned int len = 0;
	while (text[len] != 0) {
		len += 1;
	}
	return len;
}

static void qjs_log(const char *text)
{
#ifdef LEONOS_USER_APP
	leonos_write(text);
	leonos_write("\r\n");
#else
	(void) printf("%s\n", text);
#endif
}

static JSValue qjs_console_log(JSContext *ctx, JSValueConst this_val,
			       int argc, JSValueConst *argv)
{
	(void) this_val;
	for (int i = 0; i < argc; i++) {
		const char *text = JS_ToCString(ctx, argv[i]);
		if (text != NULL) {
			qjs_log(text);
			JS_FreeCString(ctx, text);
		}
	}
	return JS_UNDEFINED;
}

static JSValue qjs_dom_unsupported(JSContext *ctx, JSValueConst this_val,
				   int argc, JSValueConst *argv)
{
	(void) this_val;
	(void) argc;
	(void) argv;
	return JS_ThrowTypeError(ctx,
		"LeonOS QuickJS core backend has no DOM bindings yet");
}

static JSValue qjs_dom_noop(JSContext *ctx, JSValueConst this_val,
			    int argc, JSValueConst *argv)
{
	(void) ctx;
	(void) this_val;
	(void) argc;
	(void) argv;
	return JS_UNDEFINED;
}

static JSValue qjs_dom_true(JSContext *ctx, JSValueConst this_val,
			    int argc, JSValueConst *argv)
{
	(void) this_val;
	(void) argc;
	(void) argv;
	return JS_NewBool(ctx, true);
}

static JSValue qjs_dom_arg0(JSContext *ctx, JSValueConst this_val,
			    int argc, JSValueConst *argv)
{
	(void) this_val;
	if (argc < 1) {
		return JS_UNDEFINED;
	}
	return JS_DupValue(ctx, argv[0]);
}

static bool qjs_js_node_is_element(JSContext *ctx, JSValueConst value)
{
	JSValue node_type = JS_GetPropertyStr(ctx, value, "nodeType");
	int32_t type = 0;
	bool is_element = false;
	if (!JS_IsUndefined(node_type) && !JS_IsNull(node_type) &&
	    JS_ToInt32(ctx, &type, node_type) == 0) {
		is_element = type == DOM_ELEMENT_NODE;
	}
	JS_FreeValue(ctx, node_type);
	return is_element;
}

static bool qjs_js_node_is_document_fragment(JSContext *ctx,
					     JSValueConst value)
{
	JSValue node_type = JS_GetPropertyStr(ctx, value, "nodeType");
	int32_t type = 0;
	bool is_fragment = false;
	if (!JS_IsUndefined(node_type) && !JS_IsNull(node_type) &&
	    JS_ToInt32(ctx, &type, node_type) == 0) {
		is_fragment = type == DOM_DOCUMENT_FRAGMENT_NODE;
	}
	JS_FreeValue(ctx, node_type);
	return is_fragment;
}

static JSValue qjs_array_item(JSContext *ctx, JSValueConst this_val,
			      int argc, JSValueConst *argv)
{
	uint32_t index = 0u;
	JSValue value;
	if (argc < 1 || JS_ToUint32(ctx, &index, argv[0]) < 0) {
		return JS_NULL;
	}
	value = JS_GetPropertyUint32(ctx, this_val, index);
	if (JS_IsUndefined(value)) {
		JS_FreeValue(ctx, value);
		return JS_NULL;
	}
	return value;
}

static void qjs_set_string(JSContext *ctx, JSValueConst obj,
			   const char *name, const char *value)
{
	JS_SetPropertyStr(ctx, obj, name, JS_NewString(ctx, value));
}

static void qjs_install_function(JSContext *ctx, JSValueConst obj,
				 const char *name, JSCFunction *func,
				 int argc)
{
	JS_SetPropertyStr(ctx, obj, name, JS_NewCFunction(ctx, func, name, argc));
}

static void qjs_set_nullable_object(JSContext *ctx, JSValueConst obj,
				    const char *name, JSValueConst value)
{
	if (JS_IsObject(value)) {
		JS_SetPropertyStr(ctx, obj, name, JS_DupValue(ctx, value));
	} else {
		JS_SetPropertyStr(ctx, obj, name, JS_NULL);
	}
}

static void qjs_dump_exception(JSContext *ctx, const char *name);

static void qjs_dom_node_finalizer(JSRuntime *rt, JSValue val)
{
	struct qjs_native_node *native =
		(struct qjs_native_node *) JS_GetOpaque(val,
							qjs_dom_node_class_id);
	(void) rt;
	if (native != NULL) {
		if (native->node != NULL) {
			dom_node_unref(native->node);
		}
		free(native);
	}
}

static bool qjs_register_dom_node_class(JSRuntime *rt)
{
	JSClassDef class_def = {
		.class_name = "LeonOSNativeDOMNode",
		.finalizer = qjs_dom_node_finalizer,
	};
	if (qjs_dom_node_class_id == JS_INVALID_CLASS_ID) {
		JS_NewClassID(&qjs_dom_node_class_id);
	}
	if (JS_IsRegisteredClass(rt, qjs_dom_node_class_id)) {
		return true;
	}
	return JS_NewClass(rt, qjs_dom_node_class_id, &class_def) == 0;
}

static struct qjs_native_node *qjs_get_native_node(JSValueConst value)
{
	if (qjs_dom_node_class_id == JS_INVALID_CLASS_ID) {
		return NULL;
	}
	return (struct qjs_native_node *) JS_GetOpaque(value,
						       qjs_dom_node_class_id);
}

static bool qjs_is_identifier_char(uint8_t ch)
{
	return ((ch >= (uint8_t) 'A' && ch <= (uint8_t) 'Z') ||
		(ch >= (uint8_t) 'a' && ch <= (uint8_t) 'z') ||
		(ch >= (uint8_t) '0' && ch <= (uint8_t) '9') ||
		ch == (uint8_t) '_' || ch == (uint8_t) '$');
}

static void qjs_set_dom_string(JSContext *ctx, JSValueConst obj,
			       const char *name, dom_string *value)
{
	if (value == NULL) {
		qjs_set_string(ctx, obj, name, "");
		return;
	}
	JS_SetPropertyStr(ctx, obj, name,
			JS_NewStringLen(ctx,
				dom_string_data(value),
				dom_string_byte_length(value)));
}

static void qjs_read_dom_attribute(dom_element *element, const char *name,
				   dom_string **value)
{
	dom_string *attr_name = NULL;
	*value = NULL;
	if (dom_string_create((const uint8_t *) name, strlen(name),
			      &attr_name) != DOM_NO_ERR) {
		return;
	}
	(void) dom_element_get_attribute(element, attr_name, value);
	dom_string_unref(attr_name);
}

static void qjs_set_url_component(JSContext *ctx, JSValueConst obj,
				  struct nsurl *url, const char *name,
				  nsurl_component part, const char *prefix)
{
	char *text = NULL;
	size_t len = 0u;
	char buffer[384];
	if (url == NULL ||
	    nsurl_get(url, part, &text, &len) != NSERROR_OK ||
	    text == NULL) {
		qjs_set_string(ctx, obj, name, "");
		return;
	}
	if (prefix != NULL && prefix[0] != 0) {
		(void) snprintf(buffer, sizeof(buffer), "%s%s", prefix, text);
		qjs_set_string(ctx, obj, name, buffer);
	} else {
		qjs_set_string(ctx, obj, name, text);
	}
	free(text);
}

static void qjs_set_url_protocol(JSContext *ctx, JSValueConst obj,
				 struct nsurl *url)
{
	char *text = NULL;
	size_t len = 0u;
	char buffer[64];
	if (url == NULL ||
	    nsurl_get(url, NSURL_SCHEME, &text, &len) != NSERROR_OK ||
	    text == NULL) {
		qjs_set_string(ctx, obj, "protocol", "");
		return;
	}
	(void) snprintf(buffer, sizeof(buffer), "%s:", text);
	qjs_set_string(ctx, obj, "protocol", buffer);
	free(text);
}

static JSValue qjs_location_to_string(JSContext *ctx, JSValueConst this_val,
				      int argc, JSValueConst *argv)
{
	JSValue href;
	(void) argc;
	(void) argv;
	href = JS_GetPropertyStr(ctx, this_val, "href");
	if (JS_IsUndefined(href) || JS_IsNull(href)) {
		JS_FreeValue(ctx, href);
		return JS_NewString(ctx, "");
	}
	return href;
}

static void qjs_set_location_from_thread(JSContext *ctx, JSValueConst location)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	struct nsurl *url = NULL;
	char origin[384];
	char *scheme = NULL;
	char *host = NULL;
	size_t scheme_len = 0u;
	size_t host_len = 0u;
	if (thread != NULL && thread->htmlc != NULL) {
		url = thread->htmlc->base_url;
	}
	if (url == NULL) {
		qjs_set_string(ctx, location, "href", "");
		qjs_set_string(ctx, location, "protocol", "");
		qjs_set_string(ctx, location, "host", "");
		qjs_set_string(ctx, location, "hostname", "");
		qjs_set_string(ctx, location, "pathname", "/");
		qjs_set_string(ctx, location, "search", "");
		qjs_set_string(ctx, location, "hash", "");
		qjs_set_string(ctx, location, "origin", "");
		return;
	}
	qjs_set_string(ctx, location, "href", nsurl_access(url));
	qjs_set_url_protocol(ctx, location, url);
	qjs_set_url_component(ctx, location, url, "host", NSURL_HOST, NULL);
	qjs_set_url_component(ctx, location, url, "hostname",
			      NSURL_HOST, NULL);
	qjs_set_url_component(ctx, location, url, "pathname",
			      NSURL_PATH, NULL);
	qjs_set_url_component(ctx, location, url, "search", NSURL_QUERY, "?");
	qjs_set_url_component(ctx, location, url, "hash", NSURL_FRAGMENT, "#");
	if (nsurl_get(url, NSURL_SCHEME, &scheme, &scheme_len) == NSERROR_OK &&
	    nsurl_get(url, NSURL_HOST, &host, &host_len) == NSERROR_OK &&
	    scheme != NULL && host != NULL) {
		(void) snprintf(origin, sizeof(origin), "%s://%s",
				scheme, host);
		qjs_set_string(ctx, location, "origin", origin);
	} else {
		qjs_set_string(ctx, location, "origin", "");
	}
	free(scheme);
	free(host);
}

static char qjs_ascii_lower_char(char ch)
{
	if (ch >= 'A' && ch <= 'Z') {
		return (char) (ch + ('a' - 'A'));
	}
	return ch;
}

static bool qjs_ascii_equals_ci(const char *a, const char *b)
{
	if (a == NULL || b == NULL) {
		return false;
	}
	while (*a != 0 && *b != 0) {
		if (qjs_ascii_lower_char(*a) != qjs_ascii_lower_char(*b)) {
			return false;
		}
		a++;
		b++;
	}
	return *a == 0 && *b == 0;
}

static void qjs_copy_cstr(char *dst, size_t dst_len, const char *src)
{
	size_t len;
	if (dst_len == 0u) {
		return;
	}
	dst[0] = 0;
	if (src == NULL) {
		return;
	}
	len = strlen(src);
	if (len + 1u > dst_len) {
		len = dst_len - 1u;
	}
	if (len != 0u) {
		memcpy(dst, src, len);
	}
	dst[len] = 0;
}

static void qjs_copy_slice(char *dst, size_t dst_len,
			   const char *start, const char *end)
{
	size_t len;
	if (dst_len == 0u) {
		return;
	}
	dst[0] = 0;
	if (start == NULL || end == NULL || end < start) {
		return;
	}
	len = (size_t) (end - start);
	if (len + 1u > dst_len) {
		len = dst_len - 1u;
	}
	if (len != 0u) {
		memcpy(dst, start, len);
	}
	dst[len] = 0;
}

static const char *qjs_url_scheme_end(const char *url)
{
	if (url == NULL ||
	    !((*url >= 'A' && *url <= 'Z') || (*url >= 'a' && *url <= 'z'))) {
		return NULL;
	}
	for (const char *p = url + 1; *p != 0; p++) {
		if (*p == ':') {
			return p;
		}
		if (*p == '/' || *p == '?' || *p == '#') {
			return NULL;
		}
		if (!((*p >= 'A' && *p <= 'Z') ||
		      (*p >= 'a' && *p <= 'z') ||
		      (*p >= '0' && *p <= '9') ||
		      *p == '+' || *p == '-' || *p == '.')) {
			return NULL;
		}
	}
	return NULL;
}

static const char *qjs_first_of_url_tail(const char *text)
{
	const char *p = text;
	while (p != NULL && *p != 0) {
		if (*p == '/' || *p == '?' || *p == '#') {
			return p;
		}
		p++;
	}
	return p;
}

static void qjs_get_location_string(JSContext *ctx, const char *name,
				    char *dst, size_t dst_len)
{
	JSValue global;
	JSValue location;
	JSValue value;
	const char *text;
	if (dst_len == 0u) {
		return;
	}
	dst[0] = 0;
	global = JS_GetGlobalObject(ctx);
	location = JS_GetPropertyStr(ctx, global, "location");
	value = JS_GetPropertyStr(ctx, location, name);
	text = JS_ToCString(ctx, value);
	if (text != NULL) {
		qjs_copy_cstr(dst, dst_len, text);
		JS_FreeCString(ctx, text);
	}
	JS_FreeValue(ctx, value);
	JS_FreeValue(ctx, location);
	JS_FreeValue(ctx, global);
}

static bool qjs_element_tag_is(JSContext *ctx, JSValueConst obj,
			       const char *tag)
{
	JSValue value = JS_GetPropertyStr(ctx, obj, "tagName");
	const char *text = JS_ToCString(ctx, value);
	bool matches = qjs_ascii_equals_ci(text, tag);
	if (text != NULL) {
		JS_FreeCString(ctx, text);
	}
	JS_FreeValue(ctx, value);
	if (matches) {
		return true;
	}
	value = JS_GetPropertyStr(ctx, obj, "nodeName");
	text = JS_ToCString(ctx, value);
	matches = qjs_ascii_equals_ci(text, tag);
	if (text != NULL) {
		JS_FreeCString(ctx, text);
	}
	JS_FreeValue(ctx, value);
	return matches;
}

static void qjs_anchor_reset_url_properties(JSContext *ctx, JSValueConst obj)
{
	qjs_set_string(ctx, obj, "href", "");
	qjs_set_string(ctx, obj, "protocol", "");
	qjs_set_string(ctx, obj, "host", "");
	qjs_set_string(ctx, obj, "hostname", "");
	qjs_set_string(ctx, obj, "port", "");
	qjs_set_string(ctx, obj, "pathname", "");
	qjs_set_string(ctx, obj, "search", "");
	qjs_set_string(ctx, obj, "hash", "");
	qjs_set_string(ctx, obj, "origin", "");
}

static void qjs_make_anchor_absolute(JSContext *ctx, const char *href,
				     char *out, size_t out_len)
{
	char protocol[64];
	char host[384];
	char pathname[512];
	char base_href[1536];
	char dir[512];
	size_t dir_len;
	const char *scheme_end;
	const char *last_slash;
	if (out_len == 0u) {
		return;
	}
	out[0] = 0;
	if (href == NULL) {
		href = "";
	}
	if (href[0] == 0) {
		qjs_get_location_string(ctx, "href", base_href, sizeof(base_href));
		qjs_copy_cstr(out, out_len, base_href);
		return;
	}
	scheme_end = qjs_url_scheme_end(href);
	if (scheme_end != NULL) {
		qjs_copy_cstr(out, out_len, href);
		return;
	}
	qjs_get_location_string(ctx, "protocol", protocol, sizeof(protocol));
	qjs_get_location_string(ctx, "host", host, sizeof(host));
	qjs_get_location_string(ctx, "pathname", pathname, sizeof(pathname));
	if (protocol[0] == 0) {
		qjs_copy_cstr(protocol, sizeof(protocol), "https:");
	}
	if (host[0] == 0) {
		qjs_copy_cstr(out, out_len, href);
		return;
	}
	if (href[0] == '/' && href[1] == '/') {
		(void) snprintf(out, out_len, "%s%s", protocol, href);
		return;
	}
	if (href[0] == '/') {
		(void) snprintf(out, out_len, "%s//%s%s", protocol, host, href);
		return;
	}
	if (pathname[0] == 0) {
		qjs_copy_cstr(pathname, sizeof(pathname), "/");
	}
	last_slash = strrchr(pathname, '/');
	if (last_slash == NULL) {
		qjs_copy_cstr(dir, sizeof(dir), "/");
	} else {
		dir_len = (size_t) (last_slash - pathname) + 1u;
		if (dir_len + 1u > sizeof(dir)) {
			dir_len = sizeof(dir) - 1u;
		}
		memcpy(dir, pathname, dir_len);
		dir[dir_len] = 0;
	}
	(void) snprintf(out, out_len, "%s//%s%s%s",
			protocol, host, dir, href);
}

static void qjs_update_anchor_url_properties(JSContext *ctx, JSValueConst obj,
					     const char *href)
{
	char absolute[1536];
	char protocol[64] = "";
	char host[384] = "";
	char hostname[384] = "";
	char port[64] = "";
	char pathname[768] = "";
	char search[384] = "";
	char hash[384] = "";
	char origin[512] = "";
	const char *scheme_end;
	const char *cursor;
	const char *authority_start = NULL;
	const char *authority_end = NULL;
	const char *path_start;
	const char *path_end;
	const char *query;
	const char *fragment;
	qjs_make_anchor_absolute(ctx, href, absolute, sizeof(absolute));
	scheme_end = qjs_url_scheme_end(absolute);
	cursor = absolute;
	if (scheme_end != NULL) {
		qjs_copy_slice(protocol, sizeof(protocol),
			       absolute, scheme_end + 1);
		cursor = scheme_end + 1;
	}
	if (cursor[0] == '/' && cursor[1] == '/') {
		authority_start = cursor + 2;
		authority_end = qjs_first_of_url_tail(authority_start);
		qjs_copy_slice(host, sizeof(host),
			       authority_start, authority_end);
		cursor = authority_end;
	}
	if (host[0] != 0) {
		const char *host_start = host;
		const char *at = strrchr(host, '@');
		const char *colon = NULL;
		if (at != NULL) {
			host_start = at + 1;
		}
		if (host_start[0] == '[') {
			const char *close = strchr(host_start, ']');
			if (close != NULL) {
				qjs_copy_slice(hostname, sizeof(hostname),
					       host_start, close + 1);
				if (close[1] == ':') {
					qjs_copy_cstr(port, sizeof(port),
						      close + 2);
				}
			}
		} else {
			colon = strchr(host_start, ':');
			if (colon != NULL) {
				qjs_copy_slice(hostname, sizeof(hostname),
					       host_start, colon);
				qjs_copy_cstr(port, sizeof(port), colon + 1);
			} else {
				qjs_copy_cstr(hostname, sizeof(hostname),
					      host_start);
			}
		}
		if (hostname[0] == 0) {
			qjs_copy_cstr(hostname, sizeof(hostname), host_start);
		}
	}
	path_start = cursor;
	fragment = strchr(path_start, '#');
	query = strchr(path_start, '?');
	if (query != NULL && fragment != NULL && query > fragment) {
		query = NULL;
	}
	path_end = path_start + strlen(path_start);
	if (query != NULL && query < path_end) {
		path_end = query;
	}
	if (fragment != NULL && fragment < path_end) {
		path_end = fragment;
	}
	if (path_start == path_end) {
		qjs_copy_cstr(pathname, sizeof(pathname),
			      host[0] != 0 ? "/" : "");
	} else if (host[0] != 0 && *path_start != '/') {
		pathname[0] = '/';
		qjs_copy_slice(pathname + 1, sizeof(pathname) - 1u,
			       path_start, path_end);
	} else {
		qjs_copy_slice(pathname, sizeof(pathname),
			       path_start, path_end);
	}
	if (query != NULL) {
		const char *query_end = fragment != NULL ? fragment :
			path_start + strlen(path_start);
		qjs_copy_slice(search, sizeof(search), query, query_end);
	}
	if (fragment != NULL) {
		qjs_copy_cstr(hash, sizeof(hash), fragment);
	}
	if (protocol[0] != 0 && host[0] != 0) {
		(void) snprintf(origin, sizeof(origin), "%s//%s",
				protocol, host);
	}
	qjs_set_string(ctx, obj, "href", absolute);
	qjs_set_string(ctx, obj, "protocol", protocol);
	qjs_set_string(ctx, obj, "host", host);
	qjs_set_string(ctx, obj, "hostname", hostname);
	qjs_set_string(ctx, obj, "port", port);
	qjs_set_string(ctx, obj, "pathname", pathname);
	qjs_set_string(ctx, obj, "search", search);
	qjs_set_string(ctx, obj, "hash", hash);
	qjs_set_string(ctx, obj, "origin", origin);
}

static void qjs_maybe_update_anchor_from_attr(JSContext *ctx,
					      JSValueConst obj,
					      const char *name,
					      const char *value)
{
	if (qjs_ascii_equals_ci(name, "href") &&
	    qjs_element_tag_is(ctx, obj, "A")) {
		qjs_update_anchor_url_properties(ctx, obj, value);
	}
}

static void qjs_apply_named_prototype(JSContext *ctx, JSValueConst obj,
				      const char *name)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue ctor = JS_GetPropertyStr(ctx, global, name);
	JSValue proto;
	if (!JS_IsObject(ctor)) {
		JS_FreeValue(ctx, ctor);
		JS_FreeValue(ctx, global);
		return;
	}
	proto = JS_GetPropertyStr(ctx, ctor, "prototype");
	if (JS_IsObject(proto)) {
		(void) JS_SetPrototype(ctx, obj, proto);
	}
	JS_FreeValue(ctx, proto);
	JS_FreeValue(ctx, ctor);
	JS_FreeValue(ctx, global);
}

static void qjs_apply_node_prototype(JSContext *ctx, JSValueConst obj)
{
	qjs_apply_named_prototype(ctx, obj, "Node");
}

static void qjs_apply_element_prototype(JSContext *ctx, JSValueConst obj)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue ctor = JS_GetPropertyStr(ctx, global, "HTMLElement");
	JS_FreeValue(ctx, global);
	if (JS_IsObject(ctor)) {
		JS_FreeValue(ctx, ctor);
		qjs_apply_named_prototype(ctx, obj, "HTMLElement");
		return;
	}
	JS_FreeValue(ctx, ctor);
	qjs_apply_named_prototype(ctx, obj, "Element");
}

static void qjs_set_owner_document(JSContext *ctx, JSValueConst obj)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue document = JS_GetPropertyStr(ctx, global, "document");
	if (JS_IsObject(document)) {
		JS_SetPropertyStr(ctx, obj, "ownerDocument",
				  JS_DupValue(ctx, document));
	}
	JS_FreeValue(ctx, document);
	JS_FreeValue(ctx, global);
}

static JSValue qjs_new_basic_element(JSContext *ctx,
				     const char *tag,
				     const char *id,
				     const char *class_name)
{
	JSValue obj = JS_NewObject(ctx);
	JSValue style = JS_NewObject(ctx);
	JSValue attributes = JS_NewObject(ctx);
	JSValue children = JS_NewArray(ctx);
	qjs_apply_element_prototype(ctx, obj);
	JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, 1));
	qjs_set_string(ctx, obj, "tagName", tag != NULL ? tag : "DIV");
	qjs_set_string(ctx, obj, "nodeName", tag != NULL ? tag : "DIV");
	qjs_set_string(ctx, obj, "id", id != NULL ? id : "");
	qjs_set_string(ctx, obj, "className", class_name != NULL ? class_name : "");
	qjs_set_string(ctx, obj, "value", "");
	qjs_set_string(ctx, obj, "textContent", "");
	JS_SetPropertyStr(ctx, obj, "style", style);
	if (id != NULL && id[0] != 0) {
		qjs_set_string(ctx, attributes, "id", id);
	}
	if (class_name != NULL && class_name[0] != 0) {
		qjs_set_string(ctx, attributes, "class", class_name);
	}
	JS_SetPropertyStr(ctx, obj, "attributes", attributes);
	if (qjs_ascii_equals_ci(tag, "A")) {
		qjs_anchor_reset_url_properties(ctx, obj);
	}
	JS_SetPropertyStr(ctx, obj, "children", JS_DupValue(ctx, children));
	JS_SetPropertyStr(ctx, obj, "childNodes", children);
	JS_SetPropertyStr(ctx, obj, "parentNode", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "parentElement", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "firstChild", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "firstElementChild", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "lastChild", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "lastElementChild", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "previousSibling", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "nextSibling", JS_NULL);
	qjs_set_string(ctx, obj, "namespaceURI",
		       "http://www.w3.org/1999/xhtml");
	qjs_set_owner_document(ctx, obj);
	qjs_install_function(ctx, obj, "addEventListener", qjs_dom_noop, 2);
	qjs_install_function(ctx, obj, "removeEventListener", qjs_dom_noop, 2);
	qjs_install_function(ctx, obj, "dispatchEvent", qjs_dom_true, 1);
	qjs_install_function(ctx, obj, "focus", qjs_dom_noop, 0);
	qjs_install_function(ctx, obj, "blur", qjs_dom_noop, 0);
	qjs_install_function(ctx, obj, "appendChild", qjs_dom_arg0, 1);
	qjs_install_function(ctx, obj, "insertBefore", qjs_dom_arg0, 2);
	qjs_install_function(ctx, obj, "removeChild", qjs_dom_arg0, 1);
	return obj;
}

static void qjs_copy_dom_string_cstr(char *dst, size_t dst_len,
				     dom_string *value)
{
	size_t len;
	if (dst_len == 0u) {
		return;
	}
	dst[0] = 0;
	if (value == NULL) {
		return;
	}
	len = dom_string_byte_length(value);
	if (len + 1u > dst_len) {
		len = dst_len - 1u;
	}
	if (len != 0u) {
		memcpy(dst, dom_string_data(value), len);
	}
	dst[len] = 0;
}

static bool qjs_dataset_name_from_attr(char *dst, size_t dst_len,
				       const char *attr_name)
{
	size_t out = 0u;
	bool upper_next = false;
	if (dst_len == 0u) {
		return false;
	}
	dst[0] = 0;
	if (attr_name == NULL || strncmp(attr_name, "data-", 5u) != 0 ||
	    attr_name[5] == 0) {
		return false;
	}
	for (size_t i = 5u; attr_name[i] != 0 && out + 1u < dst_len; i++) {
		char ch = attr_name[i];
		if (ch == '-') {
			upper_next = true;
			continue;
		}
		if (upper_next && ch >= 'a' && ch <= 'z') {
			ch = (char) (ch - ('a' - 'A'));
		}
		upper_next = false;
		dst[out++] = ch;
	}
	dst[out] = 0;
	return out != 0u;
}

static void qjs_populate_dom_element_attributes(JSContext *ctx,
						JSValueConst obj,
						dom_element *element)
{
	dom_namednodemap *attrs = NULL;
	dom_ulong length = 0u;
	JSValue attributes;
	JSValue dataset;
	if (element == NULL ||
	    dom_node_get_attributes((dom_node *) element, &attrs) != DOM_NO_ERR ||
	    attrs == NULL) {
		JS_SetPropertyStr(ctx, obj, "dataset", JS_NewObject(ctx));
		return;
	}
	attributes = JS_GetPropertyStr(ctx, obj, "attributes");
	if (!JS_IsObject(attributes)) {
		JS_FreeValue(ctx, attributes);
		attributes = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, obj, "attributes",
				  JS_DupValue(ctx, attributes));
	}
	dataset = JS_NewObject(ctx);
	if (dom_namednodemap_get_length(attrs, &length) == DOM_NO_ERR) {
		for (dom_ulong i = 0u; i < length; i++) {
			dom_node *node = NULL;
			dom_string *name = NULL;
			dom_string *value = NULL;
			char name_buf[128];
			char dataset_name[128];
			if (dom_namednodemap_item(attrs, i, &node) != DOM_NO_ERR ||
			    node == NULL) {
				continue;
			}
			if (dom_attr_get_name((dom_attr *) node, &name) == DOM_NO_ERR &&
			    dom_attr_get_value((dom_attr *) node, &value) == DOM_NO_ERR &&
			    name != NULL) {
				qjs_copy_dom_string_cstr(name_buf,
							 sizeof(name_buf),
							 name);
				if (name_buf[0] != 0) {
					char value_buf[1024];
					qjs_set_dom_string(ctx, attributes,
							   name_buf, value);
					qjs_set_dom_string(ctx, obj,
							   name_buf, value);
					qjs_copy_dom_string_cstr(value_buf,
								 sizeof(value_buf),
								 value);
					qjs_maybe_update_anchor_from_attr(
						ctx, obj, name_buf, value_buf);
					if (qjs_dataset_name_from_attr(
						    dataset_name,
						    sizeof(dataset_name),
						    name_buf)) {
						qjs_set_dom_string(ctx,
								   dataset,
								   dataset_name,
								   value);
					}
				}
			}
			if (name != NULL) {
				dom_string_unref(name);
			}
			if (value != NULL) {
				dom_string_unref(value);
			}
			dom_node_unref(node);
		}
	}
	JS_SetPropertyStr(ctx, obj, "dataset", dataset);
	JS_FreeValue(ctx, attributes);
	dom_namednodemap_unref(attrs);
}

static bool qjs_dom_string_from_js(JSContext *ctx, JSValueConst value,
				   dom_string **out)
{
	const char *text;
	*out = NULL;
	text = JS_ToCString(ctx, value);
	if (text == NULL) {
		return false;
	}
	if (dom_string_create((const uint8_t *) text, strlen(text),
			      out) != DOM_NO_ERR) {
		JS_FreeCString(ctx, text);
		return false;
	}
	JS_FreeCString(ctx, text);
	return true;
}

static dom_node_type qjs_native_node_type(dom_node *node)
{
	dom_node_type type = DOM_NODE_TYPE_COUNT;
	if (node != NULL) {
		(void) dom_node_get_node_type(node, &type);
	}
	return type;
}

static void qjs_set_native_attribute_cstr(dom_node *node,
					  const char *name,
					  const char *value)
{
	dom_string *attr_name = NULL;
	dom_string *attr_value = NULL;
	if (node == NULL || name == NULL || name[0] == 0 || value == NULL ||
	    qjs_native_node_type(node) != DOM_ELEMENT_NODE) {
		return;
	}
	if (dom_string_create((const uint8_t *) name, strlen(name),
			      &attr_name) != DOM_NO_ERR) {
		return;
	}
	if (dom_string_create((const uint8_t *) value, strlen(value),
			      &attr_value) == DOM_NO_ERR) {
		(void) dom_element_set_attribute((dom_element *) node,
						 attr_name, attr_value);
		dom_string_unref(attr_value);
	}
	dom_string_unref(attr_name);
}

static void qjs_copy_js_string(char *dst, size_t dst_len,
			       JSContext *ctx, JSValueConst value)
{
	const char *text;
	size_t len;
	if (dst_len == 0u) {
		return;
	}
	dst[0] = 0;
	if (JS_IsUndefined(value) || JS_IsNull(value) ||
	    (JS_IsObject(value) && !JS_IsString(value))) {
		return;
	}
	text = JS_ToCString(ctx, value);
	if (text == NULL) {
		return;
	}
	len = strlen(text);
	if (len + 1u > dst_len) {
		len = dst_len - 1u;
	}
	if (len != 0u) {
		memcpy(dst, text, len);
	}
	dst[len] = 0;
	JS_FreeCString(ctx, text);
}

static void qjs_css_property_name(char *dst, size_t dst_len, const char *name)
{
	size_t out = 0u;
	if (dst_len == 0u) {
		return;
	}
	dst[0] = 0;
	if (name == NULL || name[0] == 0) {
		return;
	}
	if (name[0] == '-' && name[1] == '-') {
		qjs_copy_cstr(dst, dst_len, name);
		return;
	}
	for (size_t i = 0u; name[i] != 0 && out + 1u < dst_len; i++) {
		char ch = name[i];
		if (ch >= 'A' && ch <= 'Z') {
			if (out != 0u && dst[out - 1u] != '-' &&
			    out + 1u < dst_len) {
				dst[out++] = '-';
			}
			ch = qjs_ascii_lower_char(ch);
		} else if (ch == '_') {
			ch = '-';
		}
		dst[out++] = ch;
	}
	dst[out] = 0;
}

static void qjs_append_css_decl(char *dst, size_t dst_len,
				const char *name, const char *value)
{
	size_t used;
	size_t need;
	if (dst_len == 0u || name == NULL || name[0] == 0 ||
	    value == NULL || value[0] == 0) {
		return;
	}
	used = strlen(dst);
	if (used + 1u >= dst_len) {
		return;
	}
	need = strlen(name) + strlen(value) + 4u;
	if (used + need + 1u > dst_len) {
		return;
	}
	(void) snprintf(dst + used, dst_len - used, "%s:%s;", name, value);
}

static bool qjs_style_key_is_helper(const char *name)
{
	return name == NULL ||
	       strcmp(name, "cssText") == 0 ||
	       strcmp(name, "getPropertyValue") == 0 ||
	       strcmp(name, "setProperty") == 0 ||
	       strcmp(name, "removeProperty") == 0 ||
	       strcmp(name, "constructor") == 0;
}

static void qjs_sync_style_attribute(JSContext *ctx, JSValueConst obj,
				     dom_node *node)
{
	JSValue style;
	JSValue css_text;
	JSPropertyEnum *props = NULL;
	uint32_t prop_count = 0u;
	char css[4096];
	if (node == NULL || qjs_native_node_type(node) != DOM_ELEMENT_NODE) {
		return;
	}
	style = JS_GetPropertyStr(ctx, obj, "style");
	if (!JS_IsObject(style)) {
		JS_FreeValue(ctx, style);
		return;
	}
	css[0] = 0;
	css_text = JS_GetPropertyStr(ctx, style, "cssText");
	qjs_copy_js_string(css, sizeof(css), ctx, css_text);
	JS_FreeValue(ctx, css_text);
	if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, style,
				   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
		for (uint32_t i = 0u; i < prop_count; i++) {
			const char *raw_name =
				JS_AtomToCString(ctx, props[i].atom);
			JSValue value;
			char css_name[96];
			char css_value[512];
			if (raw_name == NULL ||
			    qjs_style_key_is_helper(raw_name)) {
				JS_FreeCString(ctx, raw_name);
				continue;
			}
			value = JS_GetProperty(ctx, style, props[i].atom);
			qjs_copy_js_string(css_value, sizeof(css_value),
					   ctx, value);
			JS_FreeValue(ctx, value);
			if (css_value[0] != 0) {
				qjs_css_property_name(css_name,
						      sizeof(css_name),
						      raw_name);
				qjs_append_css_decl(css, sizeof(css),
						    css_name, css_value);
			}
			JS_FreeCString(ctx, raw_name);
		}
		JS_FreePropertyEnum(ctx, props, prop_count);
	}
	if (css[0] != 0) {
		qjs_set_native_attribute_cstr(node, "style", css);
		qjs_set_string(ctx, obj, "styleText", css);
	}
	JS_FreeValue(ctx, style);
}

static void qjs_sync_native_element_props(JSContext *ctx, JSValueConst obj)
{
	struct qjs_native_node *native = qjs_get_native_node(obj);
	JSValue value;
	char text[1024];
	if (native == NULL || native->node == NULL ||
	    qjs_native_node_type(native->node) != DOM_ELEMENT_NODE) {
		return;
	}
	value = JS_GetPropertyStr(ctx, obj, "id");
	qjs_copy_js_string(text, sizeof(text), ctx, value);
	JS_FreeValue(ctx, value);
	if (text[0] != 0) {
		qjs_set_native_attribute_cstr(native->node, "id", text);
	}
	value = JS_GetPropertyStr(ctx, obj, "className");
	qjs_copy_js_string(text, sizeof(text), ctx, value);
	JS_FreeValue(ctx, value);
	if (text[0] != 0) {
		qjs_set_native_attribute_cstr(native->node, "class", text);
	}
	qjs_sync_style_attribute(ctx, obj, native->node);
}

static void qjs_sync_native_subtree_props(JSContext *ctx, JSValueConst obj)
{
	JSValue child_nodes;
	uint32_t length;
	if (!JS_IsObject(obj)) {
		return;
	}
	qjs_sync_native_element_props(ctx, obj);
	child_nodes = JS_GetPropertyStr(ctx, obj, "childNodes");
	if (!JS_IsObject(child_nodes)) {
		JS_FreeValue(ctx, child_nodes);
		return;
	}
	length = qjs_array_length(ctx, child_nodes);
	for (uint32_t i = 0u; i < length; i++) {
		JSValue child = JS_GetPropertyUint32(ctx, child_nodes, i);
		if (JS_IsObject(child)) {
			qjs_sync_native_subtree_props(ctx, child);
		}
		JS_FreeValue(ctx, child);
	}
	JS_FreeValue(ctx, child_nodes);
}

static JSValue qjs_dom_string_value(JSContext *ctx, dom_string *value)
{
	JSValue result;
	if (value == NULL) {
		return JS_NewString(ctx, "");
	}
	result = JS_NewStringLen(ctx, dom_string_data(value),
				 dom_string_byte_length(value));
	dom_string_unref(value);
	return result;
}

static JSValue qjs_native_text_content_get(JSContext *ctx, JSValueConst this_val)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	dom_string *text = NULL;
	if (native == NULL || native->node == NULL ||
	    dom_node_get_text_content(native->node, &text) != DOM_NO_ERR) {
		return JS_NewString(ctx, "");
	}
	return qjs_dom_string_value(ctx, text);
}

static JSValue qjs_native_text_content_set(JSContext *ctx,
					   JSValueConst this_val,
					   JSValueConst value)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	dom_string *text = NULL;
	if (native != NULL && native->node != NULL &&
	    qjs_dom_string_from_js(ctx, value, &text)) {
		(void) dom_node_set_text_content(native->node, text);
		dom_string_unref(text);
	}
	return JS_UNDEFINED;
}

static JSValue qjs_native_node_value_get(JSContext *ctx, JSValueConst this_val)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	dom_string *text = NULL;
	if (native == NULL || native->node == NULL ||
	    dom_node_get_node_value(native->node, &text) != DOM_NO_ERR) {
		return JS_NewString(ctx, "");
	}
	return qjs_dom_string_value(ctx, text);
}

static JSValue qjs_native_node_value_set(JSContext *ctx,
					 JSValueConst this_val,
					 JSValueConst value)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	dom_string *text = NULL;
	if (native != NULL && native->node != NULL &&
	    qjs_dom_string_from_js(ctx, value, &text)) {
		(void) dom_node_set_node_value(native->node, text);
		dom_string_unref(text);
	}
	return JS_UNDEFINED;
}

static void qjs_define_native_text_accessors(JSContext *ctx, JSValueConst obj)
{
	JSValue getter;
	JSValue setter;
	JSAtom atom;
	getter = JS_NewCFunction2(ctx, (JSCFunction *) qjs_native_text_content_get,
				  "get textContent", 0, JS_CFUNC_getter, 0);
	setter = JS_NewCFunction2(ctx, (JSCFunction *) qjs_native_text_content_set,
				  "set textContent", 1, JS_CFUNC_setter, 0);
	atom = JS_NewAtom(ctx, "textContent");
	JS_DefinePropertyGetSet(ctx, obj, atom, getter, setter,
				JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
	JS_FreeAtom(ctx, atom);
	getter = JS_NewCFunction2(ctx, (JSCFunction *) qjs_native_node_value_get,
				  "get nodeValue", 0, JS_CFUNC_getter, 0);
	setter = JS_NewCFunction2(ctx, (JSCFunction *) qjs_native_node_value_set,
				  "set nodeValue", 1, JS_CFUNC_setter, 0);
	atom = JS_NewAtom(ctx, "nodeValue");
	JS_DefinePropertyGetSet(ctx, obj, atom, getter, setter,
				JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
	JS_FreeAtom(ctx, atom);
}

static JSValue qjs_get_child_nodes_array(JSContext *ctx, JSValueConst parent)
{
	JSValue child_nodes = JS_GetPropertyStr(ctx, parent, "childNodes");
	if (!JS_IsObject(child_nodes)) {
		JS_FreeValue(ctx, child_nodes);
		child_nodes = JS_NewArray(ctx);
		JS_SetPropertyStr(ctx, parent, "childNodes",
				  JS_DupValue(ctx, child_nodes));
		JS_SetPropertyStr(ctx, parent, "children",
				  JS_DupValue(ctx, child_nodes));
	}
	return child_nodes;
}

static uint32_t qjs_array_length(JSContext *ctx, JSValueConst array)
{
	JSValue length_value = JS_GetPropertyStr(ctx, array, "length");
	uint32_t length = 0u;
	(void) JS_ToUint32(ctx, &length, length_value);
	JS_FreeValue(ctx, length_value);
	return length;
}

static bool qjs_find_child_index(JSContext *ctx, JSValueConst child_nodes,
				 JSValueConst child, uint32_t length,
				 uint32_t *index_out)
{
	for (uint32_t i = 0u; i < length; i++) {
		JSValue item = JS_GetPropertyUint32(ctx, child_nodes, i);
		bool match = JS_StrictEq(ctx, item, child);
		JS_FreeValue(ctx, item);
		if (match) {
			*index_out = i;
			return true;
		}
	}
	return false;
}

static void qjs_refresh_js_child_links(JSContext *ctx, JSValueConst parent,
				       JSValueConst child_nodes)
{
	uint32_t length = qjs_array_length(ctx, child_nodes);
	bool parent_is_element = qjs_js_node_is_element(ctx, parent);
	JSValue first = JS_NULL;
	JSValue last = JS_NULL;
	JSValue first_element = JS_NULL;
	JSValue last_element = JS_NULL;
	for (uint32_t i = 0u; i < length; i++) {
		JSValue child = JS_GetPropertyUint32(ctx, child_nodes, i);
		JSValue previous = JS_NULL;
		JSValue next = JS_NULL;
		if (!JS_IsObject(child)) {
			JS_FreeValue(ctx, child);
			continue;
		}
		if (i > 0u) {
			previous = JS_GetPropertyUint32(ctx, child_nodes, i - 1u);
		}
		if (i + 1u < length) {
			next = JS_GetPropertyUint32(ctx, child_nodes, i + 1u);
		}
		JS_SetPropertyStr(ctx, child, "parentNode",
				  JS_DupValue(ctx, parent));
		if (parent_is_element) {
			JS_SetPropertyStr(ctx, child, "parentElement",
					  JS_DupValue(ctx, parent));
		} else {
			JS_SetPropertyStr(ctx, child, "parentElement", JS_NULL);
		}
		qjs_set_nullable_object(ctx, child, "previousSibling", previous);
		qjs_set_nullable_object(ctx, child, "nextSibling", next);
		if (JS_IsObject(previous)) {
			JS_SetPropertyStr(ctx, previous, "nextSibling",
					  JS_DupValue(ctx, child));
		}
		if (JS_IsObject(next)) {
			JS_SetPropertyStr(ctx, next, "previousSibling",
					  JS_DupValue(ctx, child));
		}
		if (!JS_IsObject(first)) {
			JS_FreeValue(ctx, first);
			first = JS_DupValue(ctx, child);
		}
		JS_FreeValue(ctx, last);
		last = JS_DupValue(ctx, child);
		if (qjs_js_node_is_element(ctx, child)) {
			if (!JS_IsObject(first_element)) {
				JS_FreeValue(ctx, first_element);
				first_element = JS_DupValue(ctx, child);
			}
			JS_FreeValue(ctx, last_element);
			last_element = JS_DupValue(ctx, child);
		}
		JS_FreeValue(ctx, previous);
		JS_FreeValue(ctx, next);
		JS_FreeValue(ctx, child);
	}
	qjs_set_nullable_object(ctx, parent, "firstChild", first);
	qjs_set_nullable_object(ctx, parent, "lastChild", last);
	qjs_set_nullable_object(ctx, parent, "firstElementChild", first_element);
	qjs_set_nullable_object(ctx, parent, "lastElementChild", last_element);
	JS_FreeValue(ctx, first);
	JS_FreeValue(ctx, last);
	JS_FreeValue(ctx, first_element);
	JS_FreeValue(ctx, last_element);
}

static void qjs_sync_js_child_insert_at(JSContext *ctx, JSValueConst parent,
					JSValueConst child, uint32_t index)
{
	JSValue child_nodes = qjs_get_child_nodes_array(ctx, parent);
	uint32_t length = qjs_array_length(ctx, child_nodes);
	if (index > length) {
		index = length;
	}
	for (uint32_t i = length; i > index; i--) {
		JSValue item = JS_GetPropertyUint32(ctx, child_nodes, i - 1u);
		JS_DefinePropertyValueUint32(ctx, child_nodes, i, item,
					     JS_PROP_C_W_E);
	}
	JS_DefinePropertyValueUint32(ctx, child_nodes, index,
				     JS_DupValue(ctx, child), JS_PROP_C_W_E);
	qjs_refresh_js_child_links(ctx, parent, child_nodes);
	JS_FreeValue(ctx, child_nodes);
}

static void qjs_sync_js_child_append(JSContext *ctx, JSValueConst parent,
				     JSValueConst child)
{
	JSValue child_nodes = qjs_get_child_nodes_array(ctx, parent);
	uint32_t length = qjs_array_length(ctx, child_nodes);
	JS_FreeValue(ctx, child_nodes);
	qjs_sync_js_child_insert_at(ctx, parent, child, length);
}

static void qjs_sync_js_child_insert_before(JSContext *ctx, JSValueConst parent,
					   JSValueConst child,
					   JSValueConst ref)
{
	JSValue child_nodes = qjs_get_child_nodes_array(ctx, parent);
	uint32_t length = qjs_array_length(ctx, child_nodes);
	uint32_t index = length;
	if (JS_IsObject(ref)) {
		(void) qjs_find_child_index(ctx, child_nodes, ref, length, &index);
	}
	JS_FreeValue(ctx, child_nodes);
	qjs_sync_js_child_insert_at(ctx, parent, child, index);
}

static void qjs_sync_js_fragment_insert_before(JSContext *ctx,
					       JSValueConst parent,
					       JSValueConst fragment,
					       JSValueConst ref)
{
	JSValue fragment_children = qjs_get_child_nodes_array(ctx, fragment);
	uint32_t length = qjs_array_length(ctx, fragment_children);
	for (uint32_t i = 0u; i < length; i++) {
		JSValue child = JS_GetPropertyUint32(ctx, fragment_children, i);
		if (JS_IsObject(child)) {
			qjs_sync_js_child_insert_before(ctx, parent, child, ref);
		}
		JS_FreeValue(ctx, child);
	}
	JS_SetPropertyStr(ctx, fragment_children, "length", JS_NewUint32(ctx, 0u));
	qjs_refresh_js_child_links(ctx, fragment, fragment_children);
	JS_FreeValue(ctx, fragment_children);
}

static void qjs_sync_js_child_remove(JSContext *ctx, JSValueConst parent,
				     JSValueConst child)
{
	JSValue child_nodes = qjs_get_child_nodes_array(ctx, parent);
	uint32_t length = qjs_array_length(ctx, child_nodes);
	uint32_t index = 0u;
	if (qjs_find_child_index(ctx, child_nodes, child, length, &index)) {
		for (uint32_t i = index; i + 1u < length; i++) {
			JSValue item = JS_GetPropertyUint32(ctx, child_nodes,
							    i + 1u);
			JS_DefinePropertyValueUint32(ctx, child_nodes, i, item,
						     JS_PROP_C_W_E);
		}
		if (length > 0u) {
			JS_SetPropertyStr(ctx, child_nodes, "length",
					  JS_NewUint32(ctx, length - 1u));
		}
	}
	JS_SetPropertyStr(ctx, child, "parentNode", JS_NULL);
	JS_SetPropertyStr(ctx, child, "parentElement", JS_NULL);
	JS_SetPropertyStr(ctx, child, "previousSibling", JS_NULL);
	JS_SetPropertyStr(ctx, child, "nextSibling", JS_NULL);
	qjs_refresh_js_child_links(ctx, parent, child_nodes);
	JS_FreeValue(ctx, child_nodes);
}

static void qjs_note_native_node_inserted(JSContext *ctx, dom_node *node)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	if (thread == NULL || thread->htmlc == NULL || node == NULL) {
		return;
	}
	html_leonos_dom_node_inserted(thread->htmlc, node);
}

static JSValue qjs_native_append_child(JSContext *ctx, JSValueConst this_val,
				       int argc, JSValueConst *argv)
{
	struct qjs_native_node *parent = qjs_get_native_node(this_val);
	struct qjs_native_node *child;
	dom_node *result = NULL;
	if (argc < 1) {
		return JS_UNDEFINED;
	}
	child = qjs_get_native_node(argv[0]);
	qjs_sync_native_subtree_props(ctx, argv[0]);
	if (parent != NULL && parent->node != NULL &&
	    child != NULL && child->node != NULL) {
		if (dom_node_append_child(parent->node, child->node,
					  &result) == DOM_NO_ERR) {
			if (result != NULL) {
				dom_node_unref(result);
			}
#ifdef LEONOS_USER_APP
			leonos_write("NETSURF QUICKJS DOM appendChild native\r\n");
#endif
			qjs_note_native_node_inserted(ctx, child->node);
		}
	}
	if (qjs_js_node_is_document_fragment(ctx, argv[0])) {
		qjs_sync_js_fragment_insert_before(ctx, this_val, argv[0],
						   JS_NULL);
	} else {
		qjs_sync_js_child_append(ctx, this_val, argv[0]);
	}
	return JS_DupValue(ctx, argv[0]);
}

static JSValue qjs_native_insert_before(JSContext *ctx, JSValueConst this_val,
					int argc, JSValueConst *argv)
{
	struct qjs_native_node *parent = qjs_get_native_node(this_val);
	struct qjs_native_node *child;
	struct qjs_native_node *ref = NULL;
	dom_node *result = NULL;
	if (argc < 1) {
		return JS_UNDEFINED;
	}
	child = qjs_get_native_node(argv[0]);
	if (argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
		ref = qjs_get_native_node(argv[1]);
	}
	qjs_sync_native_subtree_props(ctx, argv[0]);
	if (parent != NULL && parent->node != NULL &&
	    child != NULL && child->node != NULL) {
		if (dom_node_insert_before(parent->node, child->node,
					   ref != NULL ? ref->node : NULL,
					   &result) == DOM_NO_ERR &&
		    result != NULL) {
			dom_node_unref(result);
			qjs_note_native_node_inserted(ctx, child->node);
		}
	}
	if (qjs_js_node_is_document_fragment(ctx, argv[0])) {
		qjs_sync_js_fragment_insert_before(ctx, this_val, argv[0],
						   ref != NULL ? argv[1] :
						   JS_NULL);
	} else {
		qjs_sync_js_child_insert_before(ctx, this_val, argv[0],
						ref != NULL ? argv[1] :
						JS_NULL);
	}
	return JS_DupValue(ctx, argv[0]);
}

static JSValue qjs_native_remove_child(JSContext *ctx, JSValueConst this_val,
				       int argc, JSValueConst *argv)
{
	struct qjs_native_node *parent = qjs_get_native_node(this_val);
	struct qjs_native_node *child;
	dom_node *result = NULL;
	if (argc < 1) {
		return JS_UNDEFINED;
	}
	child = qjs_get_native_node(argv[0]);
	if (parent != NULL && parent->node != NULL &&
	    child != NULL && child->node != NULL &&
	    dom_node_remove_child(parent->node, child->node,
				  &result) == DOM_NO_ERR &&
	    result != NULL) {
		dom_node_unref(result);
	}
	qjs_sync_js_child_remove(ctx, this_val, argv[0]);
	return JS_DupValue(ctx, argv[0]);
}

static JSValue qjs_native_replace_child(JSContext *ctx, JSValueConst this_val,
					int argc, JSValueConst *argv)
{
	struct qjs_native_node *parent = qjs_get_native_node(this_val);
	struct qjs_native_node *new_child;
	struct qjs_native_node *old_child;
	dom_node *result = NULL;
	if (argc < 2) {
		return JS_UNDEFINED;
	}
	new_child = qjs_get_native_node(argv[0]);
	old_child = qjs_get_native_node(argv[1]);
	qjs_sync_native_subtree_props(ctx, argv[0]);
	if (parent != NULL && parent->node != NULL &&
	    new_child != NULL && new_child->node != NULL &&
	    old_child != NULL && old_child->node != NULL &&
	    dom_node_replace_child(parent->node, new_child->node,
				   old_child->node, &result) == DOM_NO_ERR) {
		if (result != NULL) {
			dom_node_unref(result);
		}
		qjs_note_native_node_inserted(ctx, new_child->node);
	}
	if (qjs_js_node_is_document_fragment(ctx, argv[0])) {
		qjs_sync_js_fragment_insert_before(ctx, this_val, argv[0],
						   argv[1]);
	} else {
		qjs_sync_js_child_insert_before(ctx, this_val, argv[0],
						argv[1]);
	}
	qjs_sync_js_child_remove(ctx, this_val, argv[1]);
	return JS_DupValue(ctx, argv[1]);
}

static JSValue qjs_native_contains(JSContext *ctx, JSValueConst this_val,
				   int argc, JSValueConst *argv)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	struct qjs_native_node *other;
	bool contains = false;
	if (argc < 1 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
		return JS_NewBool(ctx, false);
	}
	other = qjs_get_native_node(argv[0]);
	if (native != NULL && native->node != NULL &&
	    other != NULL && other->node != NULL) {
		(void) dom_node_contains(native->node, other->node, &contains);
		return JS_NewBool(ctx, contains);
	}
	{
		JSValue n = JS_DupValue(ctx, argv[0]);
		while (JS_IsObject(n)) {
			JSValue parent;
			bool match = JS_StrictEq(ctx, n, this_val);
			if (match) {
				JS_FreeValue(ctx, n);
				return JS_NewBool(ctx, true);
			}
			parent = JS_GetPropertyStr(ctx, n, "parentNode");
			JS_FreeValue(ctx, n);
			n = parent;
		}
		JS_FreeValue(ctx, n);
	}
	return JS_NewBool(ctx, false);
}

static JSValue qjs_native_clone_node(JSContext *ctx, JSValueConst this_val,
				     int argc, JSValueConst *argv)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	dom_node *clone = NULL;
	bool deep = false;
	if (argc > 0) {
		deep = JS_ToBool(ctx, argv[0]) != 0;
	}
	if (native == NULL || native->node == NULL ||
	    dom_node_clone_node(native->node, deep, &clone) != DOM_NO_ERR ||
	    clone == NULL) {
		return JS_NULL;
	}
	JSValue wrapped = qjs_new_dom_node(ctx, clone);
	if (deep) {
		JSValue first = JS_GetPropertyStr(ctx, this_val, "firstChild");
		JSValue last = JS_GetPropertyStr(ctx, this_val, "lastChild");
		if (!JS_IsUndefined(first) && !JS_IsNull(first)) {
			JS_SetPropertyStr(ctx, wrapped, "firstChild",
					  JS_DupValue(ctx, first));
			JS_SetPropertyStr(ctx, wrapped, "firstElementChild",
					  JS_DupValue(ctx, first));
		}
		if (!JS_IsUndefined(last) && !JS_IsNull(last)) {
			JS_SetPropertyStr(ctx, wrapped, "lastChild",
					  JS_DupValue(ctx, last));
			JS_SetPropertyStr(ctx, wrapped, "lastElementChild",
					  JS_DupValue(ctx, last));
		}
		JS_FreeValue(ctx, first);
		JS_FreeValue(ctx, last);
	}
	dom_node_unref(clone);
	return wrapped;
}

static JSValue qjs_native_get_attribute(JSContext *ctx, JSValueConst this_val,
					int argc, JSValueConst *argv)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	dom_string *name = NULL;
	dom_string *value = NULL;
	if (argc < 1 || native == NULL || native->node == NULL) {
		return JS_NULL;
	}
	if (!qjs_dom_string_from_js(ctx, argv[0], &name)) {
		return JS_NULL;
	}
	if (dom_element_get_attribute((dom_element *) native->node, name,
				      &value) != DOM_NO_ERR ||
	    value == NULL) {
		dom_string_unref(name);
		return JS_NULL;
	}
	dom_string_unref(name);
	return qjs_dom_string_value(ctx, value);
}

static JSValue qjs_native_set_attribute(JSContext *ctx, JSValueConst this_val,
					int argc, JSValueConst *argv)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	dom_string *name = NULL;
	dom_string *value = NULL;
	const char *name_text;
	const char *value_text;
	if (argc < 2 || native == NULL || native->node == NULL) {
		return JS_UNDEFINED;
	}
	name_text = JS_ToCString(ctx, argv[0]);
	value_text = JS_ToCString(ctx, argv[1]);
	if (name_text == NULL || value_text == NULL) {
		JS_FreeCString(ctx, name_text);
		JS_FreeCString(ctx, value_text);
		return JS_UNDEFINED;
	}
	if (dom_string_create((const uint8_t *) name_text, strlen(name_text),
			      &name) == DOM_NO_ERR &&
	    dom_string_create((const uint8_t *) value_text, strlen(value_text),
			      &value) == DOM_NO_ERR) {
		JSValue attrs = JS_GetPropertyStr(ctx, this_val, "attributes");
		(void) dom_element_set_attribute((dom_element *) native->node,
						 name, value);
		if (!JS_IsObject(attrs)) {
			JS_FreeValue(ctx, attrs);
			attrs = JS_NewObject(ctx);
			JS_SetPropertyStr(ctx, this_val, "attributes",
					  JS_DupValue(ctx, attrs));
		}
		qjs_set_string(ctx, attrs, name_text, value_text);
		JS_FreeValue(ctx, attrs);
		qjs_set_string(ctx, this_val, name_text, value_text);
		if (strcmp(name_text, "id") == 0) {
			qjs_set_string(ctx, this_val, "id", value_text);
		} else if (strcmp(name_text, "class") == 0) {
			qjs_set_string(ctx, this_val, "className", value_text);
		}
		qjs_maybe_update_anchor_from_attr(ctx, this_val,
						  name_text, value_text);
	}
	if (name != NULL) {
		dom_string_unref(name);
	}
	if (value != NULL) {
		dom_string_unref(value);
	}
	JS_FreeCString(ctx, name_text);
	JS_FreeCString(ctx, value_text);
	return JS_UNDEFINED;
}

static JSValue qjs_native_remove_attribute(JSContext *ctx, JSValueConst this_val,
					   int argc, JSValueConst *argv)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	dom_string *name = NULL;
	if (argc < 1 || native == NULL || native->node == NULL) {
		return JS_UNDEFINED;
	}
	if (qjs_dom_string_from_js(ctx, argv[0], &name)) {
		(void) dom_element_remove_attribute((dom_element *) native->node,
						    name);
		dom_string_unref(name);
	}
	return JS_UNDEFINED;
}

static void qjs_populate_native_child_edges(JSContext *ctx, JSValueConst obj,
					    dom_node *node)
{
	dom_node *child = NULL;
	JSValue child_nodes;
	uint32_t index = 0u;
	if (node == NULL) {
		return;
	}
	child_nodes = qjs_get_child_nodes_array(ctx, obj);
	JS_SetPropertyStr(ctx, child_nodes, "length", JS_NewUint32(ctx, 0u));
	if (dom_node_get_first_child(node, &child) == DOM_NO_ERR) {
		while (child != NULL) {
			dom_node *next = NULL;
			JSValue wrapped = qjs_new_dom_node_limited(ctx, child,
								  0u, true);
			JS_DefinePropertyValueUint32(ctx, child_nodes, index++,
						     wrapped, JS_PROP_C_W_E);
			if (dom_node_get_next_sibling(child, &next) !=
			    DOM_NO_ERR) {
				next = NULL;
			}
			dom_node_unref(child);
			child = next;
		}
	}
	qjs_refresh_js_child_links(ctx, obj, child_nodes);
	JS_FreeValue(ctx, child_nodes);
}

static void qjs_populate_native_parent_edges(JSContext *ctx, JSValueConst obj,
					     dom_node *node,
					     unsigned int ancestor_depth)
{
	dom_node *parent = NULL;
	dom_node_type parent_type = DOM_NODE_TYPE_COUNT;
	JSValue wrapped_parent;
	if (ancestor_depth == 0u || node == NULL ||
	    dom_node_get_parent_node(node, &parent) != DOM_NO_ERR ||
	    parent == NULL) {
		return;
	}
	wrapped_parent = qjs_new_dom_node_limited(ctx, parent,
						 ancestor_depth - 1u, false);
	JS_SetPropertyStr(ctx, obj, "parentNode",
			  JS_DupValue(ctx, wrapped_parent));
	if (dom_node_get_node_type(parent, &parent_type) == DOM_NO_ERR &&
	    parent_type == DOM_ELEMENT_NODE) {
		JS_SetPropertyStr(ctx, obj, "parentElement",
				  JS_DupValue(ctx, wrapped_parent));
	} else {
		JS_SetPropertyStr(ctx, obj, "parentElement", JS_NULL);
	}
	JS_FreeValue(ctx, wrapped_parent);
	dom_node_unref(parent);
}

static JSValue qjs_new_dom_node_limited(JSContext *ctx, dom_node *node,
				       unsigned int ancestor_depth,
				       bool populate_children)
{
	JSValue obj;
	JSValue style;
	JSValue children;
	struct qjs_native_node *native;
	dom_node_type type = DOM_NODE_TYPE_COUNT;
	dom_string *tag = NULL;
	dom_string *id = NULL;
	dom_string *class_name = NULL;
	dom_string *node_name = NULL;
	dom_string *namespace_uri = NULL;
	if (node == NULL || qjs_dom_node_class_id == JS_INVALID_CLASS_ID) {
		return qjs_new_basic_element(ctx, "DIV", "", "");
	}
	native = calloc(1, sizeof(*native));
	if (native == NULL) {
		return JS_NULL;
	}
	obj = JS_NewObjectClass(ctx, qjs_dom_node_class_id);
	if (JS_IsException(obj)) {
		free(native);
		return obj;
	}
	native->node = dom_node_ref(node);
	JS_SetOpaque(obj, native);
	(void) dom_node_get_node_type(node, &type);
	if (type == DOM_ELEMENT_NODE) {
		qjs_apply_element_prototype(ctx, obj);
	} else {
		qjs_apply_node_prototype(ctx, obj);
	}
	JS_SetPropertyStr(ctx, obj, "nodeType", JS_NewInt32(ctx, (int32_t) type));
	if (dom_node_get_node_name(node, &node_name) == DOM_NO_ERR &&
	    node_name != NULL) {
		qjs_set_dom_string(ctx, obj, "nodeName", node_name);
		dom_string_unref(node_name);
	} else {
		qjs_set_string(ctx, obj, "nodeName",
			       type == DOM_TEXT_NODE ? "#text" : "");
	}
	style = JS_NewObject(ctx);
	children = JS_NewArray(ctx);
	JS_SetPropertyStr(ctx, obj, "style", style);
	JS_SetPropertyStr(ctx, obj, "children", JS_DupValue(ctx, children));
	JS_SetPropertyStr(ctx, obj, "childNodes", children);
	JS_SetPropertyStr(ctx, obj, "parentNode", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "parentElement", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "firstChild", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "lastChild", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "firstElementChild", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "lastElementChild", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "previousSibling", JS_NULL);
	JS_SetPropertyStr(ctx, obj, "nextSibling", JS_NULL);
	if (dom_node_get_namespace(node, &namespace_uri) == DOM_NO_ERR &&
	    namespace_uri != NULL) {
		qjs_set_dom_string(ctx, obj, "namespaceURI", namespace_uri);
		dom_string_unref(namespace_uri);
	} else if (type == DOM_ELEMENT_NODE) {
		qjs_set_string(ctx, obj, "namespaceURI",
			       "http://www.w3.org/1999/xhtml");
	} else {
		JS_SetPropertyStr(ctx, obj, "namespaceURI", JS_NULL);
	}
	qjs_set_owner_document(ctx, obj);
	qjs_define_native_text_accessors(ctx, obj);
	qjs_install_function(ctx, obj, "appendChild", qjs_native_append_child, 1);
	qjs_install_function(ctx, obj, "insertBefore", qjs_native_insert_before, 2);
	qjs_install_function(ctx, obj, "removeChild", qjs_native_remove_child, 1);
	qjs_install_function(ctx, obj, "replaceChild", qjs_native_replace_child, 2);
	qjs_install_function(ctx, obj, "contains", qjs_native_contains, 1);
	qjs_install_function(ctx, obj, "cloneNode", qjs_native_clone_node, 1);
	qjs_install_function(ctx, obj, "addEventListener", qjs_dom_noop, 2);
	qjs_install_function(ctx, obj, "removeEventListener", qjs_dom_noop, 2);
	qjs_install_function(ctx, obj, "dispatchEvent", qjs_dom_true, 1);
	qjs_install_function(ctx, obj, "focus", qjs_dom_noop, 0);
	qjs_install_function(ctx, obj, "blur", qjs_dom_noop, 0);
	if (type != DOM_ELEMENT_NODE) {
		if (populate_children) {
			qjs_populate_native_child_edges(ctx, obj, node);
		}
		qjs_populate_native_parent_edges(ctx, obj, node, ancestor_depth);
		return obj;
	}
	(void) dom_element_get_tag_name((dom_element *) node, &tag);
	qjs_read_dom_attribute((dom_element *) node, "id", &id);
	qjs_read_dom_attribute((dom_element *) node, "class", &class_name);
	if (tag != NULL) {
		qjs_set_dom_string(ctx, obj, "tagName", tag);
		qjs_set_dom_string(ctx, obj, "nodeName", tag);
	} else {
		qjs_set_string(ctx, obj, "tagName", "DIV");
	}
	if (qjs_element_tag_is(ctx, obj, "IFRAME")) {
		qjs_apply_named_prototype(ctx, obj, "HTMLIFrameElement");
	} else if (qjs_element_tag_is(ctx, obj, "INPUT")) {
		qjs_apply_named_prototype(ctx, obj, "HTMLInputElement");
	} else if (qjs_element_tag_is(ctx, obj, "TEXTAREA")) {
		qjs_apply_named_prototype(ctx, obj, "HTMLTextAreaElement");
	} else if (qjs_element_tag_is(ctx, obj, "SELECT")) {
		qjs_apply_named_prototype(ctx, obj, "HTMLSelectElement");
	} else if (qjs_element_tag_is(ctx, obj, "BUTTON")) {
		qjs_apply_named_prototype(ctx, obj, "HTMLButtonElement");
	} else if (qjs_element_tag_is(ctx, obj, "IMG")) {
		qjs_apply_named_prototype(ctx, obj, "HTMLImageElement");
	} else if (qjs_element_tag_is(ctx, obj, "A")) {
		qjs_apply_named_prototype(ctx, obj, "HTMLAnchorElement");
	} else if (qjs_element_tag_is(ctx, obj, "FORM")) {
		qjs_apply_named_prototype(ctx, obj, "HTMLFormElement");
	} else if (qjs_element_tag_is(ctx, obj, "SVG")) {
		qjs_apply_named_prototype(ctx, obj, "SVGElement");
	}
	if (id != NULL) {
		qjs_set_dom_string(ctx, obj, "id", id);
	} else {
		qjs_set_string(ctx, obj, "id", "");
	}
	if (class_name != NULL) {
		qjs_set_dom_string(ctx, obj, "className", class_name);
	} else {
		qjs_set_string(ctx, obj, "className", "");
	}
	if (qjs_element_tag_is(ctx, obj, "A")) {
		qjs_anchor_reset_url_properties(ctx, obj);
	}
	qjs_set_string(ctx, obj, "value", "");
	qjs_set_string(ctx, obj, "defaultValue", "");
	JS_SetPropertyStr(ctx, obj, "checked", JS_NewBool(ctx, false));
	JS_SetPropertyStr(ctx, obj, "selected", JS_NewBool(ctx, false));
	qjs_populate_dom_element_attributes(ctx, obj, (dom_element *) node);
	qjs_install_function(ctx, obj, "getAttribute", qjs_native_get_attribute, 1);
	qjs_install_function(ctx, obj, "setAttribute", qjs_native_set_attribute, 2);
	qjs_install_function(ctx, obj, "removeAttribute",
			     qjs_native_remove_attribute, 1);
	if (populate_children) {
		qjs_populate_native_child_edges(ctx, obj, node);
	}
	qjs_populate_native_parent_edges(ctx, obj, node, ancestor_depth);
	if (tag != NULL) {
		dom_string_unref(tag);
	}
	if (id != NULL) {
		dom_string_unref(id);
	}
	if (class_name != NULL) {
		dom_string_unref(class_name);
	}
	return obj;
}

static JSValue qjs_new_dom_node(JSContext *ctx, dom_node *node)
{
	return qjs_new_dom_node_limited(ctx, node, 2u, true);
}

static JSValue qjs_new_dom_element(JSContext *ctx, dom_element *element)
{
	return qjs_new_dom_node(ctx, (dom_node *) element);
}

struct qjs_simple_selector {
	char tag[32];
	char id[96];
	char class_name[96];
	char attr[64];
	char attr_value[128];
	char not_attr[64];
	bool has_tag;
	bool has_id;
	bool has_class;
	bool has_attr;
	bool has_not_attr;
	bool attr_has_value;
	bool attr_dash_match;
	bool attr_prefix_match;
	bool unsupported;
};

struct qjs_selector_chain {
	struct qjs_simple_selector parts[4];
	unsigned int count;
	bool unsupported;
};

static bool qjs_ascii_space(char ch)
{
	return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n' ||
	       ch == '\f';
}

static bool qjs_selector_special(char ch)
{
	return ch == 0 || ch == '#' || ch == '.' || ch == '[' ||
	       ch == ']' || ch == '>' || ch == '+' || ch == '~' ||
	       qjs_ascii_space(ch) || ch == ',';
}

static char qjs_ascii_lower(char ch)
{
	if (ch >= 'A' && ch <= 'Z') {
		return (char) (ch + ('a' - 'A'));
	}
	return ch;
}

static bool qjs_ascii_equal_ci(const char *a, const char *b)
{
	size_t i = 0u;
	while (a[i] != 0 && b[i] != 0) {
		if (qjs_ascii_lower(a[i]) != qjs_ascii_lower(b[i])) {
			return false;
		}
		i += 1u;
	}
	return a[i] == 0 && b[i] == 0;
}

static void qjs_copy_selector_text(char *dst, size_t dst_len,
				   const char *src, size_t start,
				   size_t end)
{
	size_t out = 0u;
	while (start < end && qjs_ascii_space(src[start])) {
		start += 1u;
	}
	while (end > start && qjs_ascii_space(src[end - 1u])) {
		end -= 1u;
	}
	while (start < end && out + 1u < dst_len) {
		dst[out++] = src[start++];
	}
	dst[out] = 0;
}

static bool qjs_parse_selector_part(const char *text,
				    size_t start,
				    size_t end,
				    struct qjs_simple_selector *selector)
{
	size_t i;
	memset(selector, 0, sizeof(*selector));
	while (start < end && qjs_ascii_space(text[start])) {
		start += 1u;
	}
	while (end > start && qjs_ascii_space(text[end - 1u])) {
		end -= 1u;
	}
	if (start >= end) {
		selector->unsupported = true;
		return false;
	}
	unsigned int bracket_depth = 0u;
	char guard_quote = 0;
	for (i = start; i < end; i++) {
		if (guard_quote != 0) {
			if (text[i] == guard_quote) {
				guard_quote = 0;
			}
			continue;
		}
		if (text[i] == '\'' || text[i] == '"') {
			guard_quote = text[i];
			continue;
		}
		if (text[i] == '[') {
			bracket_depth += 1u;
			continue;
		}
		if (text[i] == ']' && bracket_depth != 0u) {
			bracket_depth -= 1u;
			continue;
		}
		if (bracket_depth == 0u &&
		    (text[i] == '>' || text[i] == '+' ||
		     text[i] == '~' || qjs_ascii_space(text[i]))) {
			selector->unsupported = true;
			return false;
		}
	}
	i = start;
	if (text[i] == '*') {
		selector->has_tag = true;
		selector->tag[0] = '*';
		selector->tag[1] = 0;
		i += 1u;
	} else if ((text[i] >= 'A' && text[i] <= 'Z') ||
		   (text[i] >= 'a' && text[i] <= 'z')) {
		size_t tag_start = i;
		while (i < end && !qjs_selector_special(text[i])) {
			i += 1u;
		}
		qjs_copy_selector_text(selector->tag, sizeof(selector->tag),
				       text, tag_start, i);
		selector->has_tag = selector->tag[0] != 0;
	}
	while (i < end) {
		if (text[i] == '#') {
			size_t token_start = ++i;
			while (i < end && !qjs_selector_special(text[i])) {
				i += 1u;
			}
			qjs_copy_selector_text(selector->id,
					       sizeof(selector->id),
					       text, token_start, i);
			selector->has_id = selector->id[0] != 0;
		} else if (text[i] == '.') {
			size_t token_start = ++i;
			while (i < end && !qjs_selector_special(text[i])) {
				i += 1u;
			}
			qjs_copy_selector_text(selector->class_name,
					       sizeof(selector->class_name),
					       text, token_start, i);
			selector->has_class = selector->class_name[0] != 0;
		} else if (text[i] == '[') {
			size_t attr_start;
			size_t value_start;
			char quote = 0;
			char attr_operator = 0;
			i += 1u;
			while (i < end && qjs_ascii_space(text[i])) {
				i += 1u;
			}
			attr_start = i;
			while (i < end && text[i] != ']' && text[i] != '=' &&
			       text[i] != '|' && text[i] != '~' &&
			       text[i] != '^' && text[i] != '$' &&
			       text[i] != '*') {
				i += 1u;
			}
			qjs_copy_selector_text(selector->attr,
					       sizeof(selector->attr),
					       text, attr_start, i);
			selector->has_attr = selector->attr[0] != 0;
			while (i < end && qjs_ascii_space(text[i])) {
				i += 1u;
			}
			if (i < end && text[i] != ']') {
				if (text[i] == '|') {
					selector->attr_dash_match = true;
					attr_operator = text[i];
					i += 1u;
				} else if (text[i] == '~' || text[i] == '^' ||
					   text[i] == '$' || text[i] == '*') {
					attr_operator = text[i];
					i += 1u;
				}
				if (i < end && text[i] == '=') {
					i += 1u;
				}
				while (i < end && qjs_ascii_space(text[i])) {
					i += 1u;
				}
				if (i < end && (text[i] == '\'' || text[i] == '"')) {
					quote = text[i++];
				}
				value_start = i;
				while (i < end &&
				       ((quote != 0 && text[i] != quote) ||
					(quote == 0 && text[i] != ']'))) {
					i += 1u;
				}
				qjs_copy_selector_text(selector->attr_value,
					sizeof(selector->attr_value),
					text, value_start, i);
				selector->attr_has_value = true;
				selector->attr_prefix_match = attr_operator == '^';
				if (quote != 0 && i < end && text[i] == quote) {
					i += 1u;
				}
			}
			while (i < end && text[i] != ']') {
				i += 1u;
			}
			if (i < end && text[i] == ']') {
				i += 1u;
			}
		} else if (text[i] == ':' && i + 5u < end &&
			   strncmp(text + i, ":not(", 5u) == 0) {
			size_t attr_start;
			i += 5u;
			while (i < end && qjs_ascii_space(text[i])) {
				i += 1u;
			}
			if (i >= end || text[i] != '[') {
				selector->unsupported = true;
				return false;
			}
			i += 1u;
			while (i < end && qjs_ascii_space(text[i])) {
				i += 1u;
			}
			attr_start = i;
			while (i < end && text[i] != ']' &&
			       !qjs_ascii_space(text[i]) &&
			       text[i] != '=' && text[i] != '|' &&
			       text[i] != '~' && text[i] != '^' &&
			       text[i] != '$' && text[i] != '*') {
				i += 1u;
			}
			qjs_copy_selector_text(selector->not_attr,
					       sizeof(selector->not_attr),
					       text, attr_start, i);
			selector->has_not_attr = selector->not_attr[0] != 0;
			while (i < end && text[i] != ']') {
				i += 1u;
			}
			if (i < end && text[i] == ']') {
				i += 1u;
			}
			while (i < end && qjs_ascii_space(text[i])) {
				i += 1u;
			}
			if (i >= end || text[i] != ')') {
				selector->unsupported = true;
				return false;
			}
			i += 1u;
		} else {
			selector->unsupported = true;
			return false;
		}
	}
	return !selector->unsupported;
}

static bool qjs_parse_selector_chain(const char *text,
				     struct qjs_selector_chain *chain)
{
	size_t len = strlen(text);
	size_t start = 0u;
	size_t end = 0u;
	size_t part_start;
	unsigned int bracket_depth = 0u;
	char quote = 0;
	memset(chain, 0, sizeof(*chain));
	while (start < len && qjs_ascii_space(text[start])) {
		start += 1u;
	}
	end = start;
	while (end < len &&
	       (text[end] != ',' || bracket_depth != 0u || quote != 0)) {
		if (quote != 0) {
			if (text[end] == quote) {
				quote = 0;
			}
			end += 1u;
			continue;
		}
		if (text[end] == '\'' || text[end] == '"') {
			quote = text[end++];
			continue;
		}
		if (text[end] == '[') {
			bracket_depth += 1u;
			end += 1u;
			continue;
		}
		if (text[end] == ']' && bracket_depth != 0u) {
			bracket_depth -= 1u;
			end += 1u;
			continue;
		}
		if (bracket_depth == 0u &&
		    (text[end] == '>' || text[end] == '+' ||
		     text[end] == '~')) {
			chain->unsupported = true;
			return false;
		}
		end += 1u;
	}
	while (end > start && qjs_ascii_space(text[end - 1u])) {
		end -= 1u;
	}
	if (start >= end) {
		chain->unsupported = true;
		return false;
	}
	while (start < end) {
		while (start < end && qjs_ascii_space(text[start])) {
			start += 1u;
		}
		part_start = start;
		bracket_depth = 0u;
		quote = 0;
		while (start < end) {
			if (quote != 0) {
				if (text[start] == quote) {
					quote = 0;
				}
				start += 1u;
				continue;
			}
			if (text[start] == '\'' || text[start] == '"') {
				quote = text[start++];
				continue;
			}
			if (text[start] == '[') {
				bracket_depth += 1u;
				start += 1u;
				continue;
			}
			if (text[start] == ']' && bracket_depth != 0u) {
				bracket_depth -= 1u;
				start += 1u;
				continue;
			}
			if (bracket_depth == 0u && qjs_ascii_space(text[start])) {
				break;
			}
			start += 1u;
		}
		if (part_start == start) {
			continue;
		}
		if (chain->count >= 4u) {
			chain->unsupported = true;
			return false;
		}
		if (!qjs_parse_selector_part(text, part_start, start,
					     &chain->parts[chain->count])) {
			chain->unsupported = true;
			return false;
		}
		chain->count += 1u;
	}
	if (chain->count == 0u) {
		chain->unsupported = true;
		return false;
	}
	return true;
}

static bool qjs_dom_string_equals_cstr(dom_string *value, const char *text)
{
	size_t len;
	if (value == NULL || text == NULL) {
		return false;
	}
	len = strlen(text);
	return dom_string_byte_length(value) == len &&
	       memcmp(dom_string_data(value), text, len) == 0;
}

static bool qjs_dom_class_contains(dom_string *value, const char *class_name)
{
	const char *data;
	size_t len;
	size_t needle_len;
	size_t i = 0u;
	if (value == NULL || class_name == NULL || class_name[0] == 0) {
		return false;
	}
	data = dom_string_data(value);
	len = dom_string_byte_length(value);
	needle_len = strlen(class_name);
	while (i < len) {
		while (i < len && qjs_ascii_space(data[i])) {
			i += 1u;
		}
		if (i + needle_len <= len &&
		    memcmp(data + i, class_name, needle_len) == 0 &&
		    (i + needle_len == len ||
		     qjs_ascii_space(data[i + needle_len]))) {
			return true;
		}
		while (i < len && !qjs_ascii_space(data[i])) {
			i += 1u;
		}
	}
	return false;
}

static bool qjs_dom_attr_matches(dom_string *value,
				 const struct qjs_simple_selector *selector)
{
	size_t value_len;
	size_t wanted_len;
	if (value == NULL) {
		return false;
	}
	if (!selector->attr_has_value) {
		return true;
	}
	value_len = dom_string_byte_length(value);
	wanted_len = strlen(selector->attr_value);
	if (value_len == wanted_len &&
	    memcmp(dom_string_data(value), selector->attr_value, wanted_len) == 0) {
		return true;
	}
	if (selector->attr_prefix_match && value_len >= wanted_len &&
	    memcmp(dom_string_data(value), selector->attr_value, wanted_len) == 0) {
		return true;
	}
	if (selector->attr_dash_match && value_len > wanted_len &&
	    memcmp(dom_string_data(value), selector->attr_value, wanted_len) == 0 &&
	    dom_string_data(value)[wanted_len] == '-') {
		return true;
	}
	return false;
}

static bool qjs_dom_element_matches(dom_element *element,
				    const struct qjs_simple_selector *selector)
{
	dom_string *tag = NULL;
	dom_string *id = NULL;
	dom_string *class_name = NULL;
	dom_string *attr = NULL;
	bool matched = true;
	if (element == NULL) {
		return false;
	}
	if (selector->has_tag && selector->tag[0] != '*') {
		if (dom_element_get_tag_name(element, &tag) != DOM_NO_ERR ||
		    tag == NULL ||
		    !qjs_ascii_equal_ci(dom_string_data(tag), selector->tag)) {
			matched = false;
		}
	}
	if (matched && selector->has_id) {
		qjs_read_dom_attribute(element, "id", &id);
		matched = qjs_dom_string_equals_cstr(id, selector->id);
	}
	if (matched && selector->has_class) {
		qjs_read_dom_attribute(element, "class", &class_name);
		matched = qjs_dom_class_contains(class_name,
						 selector->class_name);
	}
	if (matched && selector->has_attr) {
		qjs_read_dom_attribute(element, selector->attr, &attr);
		matched = qjs_dom_attr_matches(attr, selector);
	}
	if (matched && selector->has_not_attr) {
		dom_string *not_attr = NULL;
		qjs_read_dom_attribute(element, selector->not_attr, &not_attr);
		matched = not_attr == NULL;
		if (not_attr != NULL) {
			dom_string_unref(not_attr);
		}
	}
	if (tag != NULL) {
		dom_string_unref(tag);
	}
	if (id != NULL) {
		dom_string_unref(id);
	}
	if (class_name != NULL) {
		dom_string_unref(class_name);
	}
	if (attr != NULL) {
		dom_string_unref(attr);
	}
	return matched;
}

static dom_node *qjs_find_matching_ancestor(dom_node *start,
					   const struct qjs_simple_selector *selector)
{
	dom_node *cursor = NULL;
	dom_node *parent = NULL;
	if (start == NULL || selector == NULL ||
	    dom_node_get_parent_node(start, &cursor) != DOM_NO_ERR) {
		return NULL;
	}
	while (cursor != NULL) {
		dom_node_type type = DOM_NODE_TYPE_COUNT;
		if (dom_node_get_node_type(cursor, &type) == DOM_NO_ERR &&
		    type == DOM_ELEMENT_NODE &&
		    qjs_dom_element_matches((dom_element *) cursor, selector)) {
			return cursor;
		}
		if (dom_node_get_parent_node(cursor, &parent) != DOM_NO_ERR) {
			dom_node_unref(cursor);
			return NULL;
		}
		dom_node_unref(cursor);
		cursor = parent;
		parent = NULL;
	}
	return NULL;
}

static bool qjs_dom_element_matches_chain(dom_element *element,
					  const struct qjs_selector_chain *chain)
{
	dom_node *scope;
	dom_node *owned_scope = NULL;
	if (element == NULL || chain == NULL || chain->count == 0u) {
		return false;
	}
	if (!qjs_dom_element_matches(element, &chain->parts[chain->count - 1u])) {
		return false;
	}
	scope = (dom_node *) element;
	for (unsigned int part = chain->count - 1u; part > 0u; part--) {
		dom_node *found = qjs_find_matching_ancestor(scope,
			&chain->parts[part - 1u]);
		if (owned_scope != NULL) {
			dom_node_unref(owned_scope);
			owned_scope = NULL;
		}
		if (found == NULL) {
			return false;
		}
		owned_scope = found;
		scope = found;
	}
	if (owned_scope != NULL) {
		dom_node_unref(owned_scope);
	}
	return true;
}

static JSValue qjs_new_dom_node_array(JSContext *ctx)
{
	JSValue list = JS_NewArray(ctx);
	qjs_install_function(ctx, list, "item", qjs_array_item, 1);
	return list;
}

static JSValue qjs_new_roblox_account_experience_meta(JSContext *ctx)
{
	JSValue obj = qjs_new_basic_element(ctx, "META", "", "");
	JSValue dataset = JS_NewObject(ctx);
	qjs_set_string(ctx, obj, "name", "account-experience-revamp-data");
	qjs_set_string(ctx, dataset, "isAccountExperienceRevampEnabled", "true");
	JS_SetPropertyStr(ctx, obj, "dataset", dataset);
	return obj;
}

static JSValue qjs_document_query_selector_all(JSContext *ctx,
					       JSValueConst this_val,
					       int argc,
					       JSValueConst *argv)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	const char *selector_text;
	struct qjs_selector_chain chain;
	const struct qjs_simple_selector *leaf;
	dom_string *tag_name = NULL;
	dom_nodelist *nodes = NULL;
	uint32_t length = 0u;
	uint32_t out_index = 0u;
	JSValue list = qjs_new_dom_node_array(ctx);
	(void) this_val;
	if (thread == NULL || thread->htmlc == NULL ||
	    thread->htmlc->document == NULL || argc < 1) {
		return list;
	}
	selector_text = JS_ToCString(ctx, argv[0]);
	if (selector_text == NULL) {
		return list;
	}
	if (strcmp(selector_text,
		   "meta[name=\"account-experience-revamp-data\"]") == 0 ||
	    strcmp(selector_text,
		   "meta[name='account-experience-revamp-data']") == 0) {
		JS_DefinePropertyValueUint32(ctx, list, 0u,
			qjs_new_roblox_account_experience_meta(ctx),
			JS_PROP_C_W_E);
#ifdef LEONOS_USER_APP
		leonos_write("NETSURF QUICKJS QUERYALL ");
		leonos_write(selector_text);
		leonos_write(" nodes=synthetic count=1\r\n");
#endif
		JS_FreeCString(ctx, selector_text);
		return list;
	}
	if (!qjs_parse_selector_chain(selector_text, &chain)) {
#ifdef LEONOS_USER_APP
		leonos_write("NETSURF QUICKJS QUERYALL unsupported ");
		leonos_write(selector_text);
		leonos_write("\r\n");
#endif
		JS_FreeCString(ctx, selector_text);
		return list;
	}
#ifdef LEONOS_USER_APP
	static unsigned int query_log_count;
	bool log_selector = false;
	if (query_log_count < 64u ||
	    strstr(selector_text, "banner") != NULL) {
		query_log_count += 1u;
		log_selector = true;
	}
#endif
	leaf = &chain.parts[chain.count - 1u];
	if (dom_string_create((const uint8_t *)
			      (leaf->has_tag ? leaf->tag : "*"),
			      strlen(leaf->has_tag ? leaf->tag : "*"),
			      &tag_name) != DOM_NO_ERR) {
		JS_FreeCString(ctx, selector_text);
		return list;
	}
	if (dom_document_get_elements_by_tag_name(thread->htmlc->document,
						  tag_name,
						  &nodes) == DOM_NO_ERR &&
	    nodes != NULL &&
	    dom_nodelist_get_length(nodes, &length) == DOM_NO_ERR) {
		for (uint32_t i = 0u; i < length && out_index < 512u; i++) {
			dom_node *node = NULL;
			dom_node_type type = DOM_NODE_TYPE_COUNT;
			if (dom_nodelist_item(nodes, i, &node) != DOM_NO_ERR ||
			    node == NULL) {
				continue;
			}
			if (dom_node_get_node_type(node, &type) == DOM_NO_ERR &&
			    type == DOM_ELEMENT_NODE &&
			    qjs_dom_element_matches_chain((dom_element *) node,
							  &chain)) {
				JS_DefinePropertyValueUint32(ctx, list,
					out_index++,
					qjs_new_dom_element(ctx,
							    (dom_element *) node),
					JS_PROP_C_W_E);
			}
			dom_node_unref(node);
		}
	}
	if (nodes != NULL) {
		dom_nodelist_unref(nodes);
	}
#ifdef LEONOS_USER_APP
	if (log_selector) {
		char detail[128];
		int detail_len = snprintf(detail, sizeof(detail),
			"NETSURF QUICKJS QUERYALL %s nodes=%u count=%u\r\n",
			selector_text,
			(unsigned int) length,
			(unsigned int) out_index);
		if (detail_len > 0) {
			leonos_write(detail);
		}
	}
#endif
	dom_string_unref(tag_name);
	JS_FreeCString(ctx, selector_text);
	return list;
}

static JSValue qjs_document_query_selector(JSContext *ctx,
					   JSValueConst this_val,
					   int argc,
					   JSValueConst *argv)
{
	JSValue list = qjs_document_query_selector_all(ctx, this_val,
						       argc, argv);
	JSValue first = JS_GetPropertyUint32(ctx, list, 0u);
	JS_FreeValue(ctx, list);
	if (JS_IsUndefined(first)) {
		JS_FreeValue(ctx, first);
		return JS_NULL;
	}
	return first;
}

static JSValue qjs_document_get_elements_by_tag_name(JSContext *ctx,
						     JSValueConst this_val,
						     int argc,
						     JSValueConst *argv)
{
	const char *tag_text;
	char selector_text[40];
	JSValue selector_arg;
	JSValue result;
	(void) this_val;
	if (argc < 1) {
		return qjs_new_dom_node_array(ctx);
	}
	tag_text = JS_ToCString(ctx, argv[0]);
	if (tag_text == NULL) {
		return qjs_new_dom_node_array(ctx);
	}
	(void) snprintf(selector_text, sizeof(selector_text), "%s", tag_text);
	JS_FreeCString(ctx, tag_text);
	selector_arg = JS_NewString(ctx, selector_text);
	result = qjs_document_query_selector_all(ctx, this_val, 1,
						 &selector_arg);
	JS_FreeValue(ctx, selector_arg);
	return result;
}

static JSValue qjs_document_get_elements_by_class_name(JSContext *ctx,
						       JSValueConst this_val,
						       int argc,
						       JSValueConst *argv)
{
	const char *class_text;
	char selector_text[112];
	JSValue selector_arg;
	JSValue result;
	(void) this_val;
	if (argc < 1) {
		return qjs_new_dom_node_array(ctx);
	}
	class_text = JS_ToCString(ctx, argv[0]);
	if (class_text == NULL) {
		return qjs_new_dom_node_array(ctx);
	}
	(void) snprintf(selector_text, sizeof(selector_text), ".%s",
			class_text);
	JS_FreeCString(ctx, class_text);
	selector_arg = JS_NewString(ctx, selector_text);
	result = qjs_document_query_selector_all(ctx, this_val, 1,
						 &selector_arg);
	JS_FreeValue(ctx, selector_arg);
	return result;
}

static JSValue qjs_document_get_element_by_id(JSContext *ctx,
					      JSValueConst this_val,
					      int argc,
					      JSValueConst *argv)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	const char *id_text;
	dom_string *id = NULL;
	dom_element *element = NULL;
	(void) this_val;
	if (thread == NULL || thread->htmlc == NULL ||
	    thread->htmlc->document == NULL || argc < 1) {
		return JS_NULL;
	}
	id_text = JS_ToCString(ctx, argv[0]);
	if (id_text == NULL) {
		return JS_NULL;
	}
	if (dom_string_create((const uint8_t *) id_text,
			      strlen(id_text), &id) == DOM_NO_ERR) {
		(void) dom_document_get_element_by_id(thread->htmlc->document,
						      id,
						      &element);
		dom_string_unref(id);
	}
#ifdef LEONOS_USER_APP
	static unsigned int get_id_log_count;
	if (get_id_log_count < 8u) {
		get_id_log_count += 1u;
		leonos_write("NETSURF QUICKJS GETELEMENT ");
		leonos_write(id_text);
		leonos_write(element != NULL ? " hit\r\n" : " miss\r\n");
	}
#endif
	JS_FreeCString(ctx, id_text);
	if (element != NULL) {
		JSValue obj = qjs_new_dom_element(ctx, element);
		dom_node_unref(element);
		return obj;
	}
	return JS_NULL;
}

static JSValue qjs_document_create_element(JSContext *ctx,
					   JSValueConst this_val,
					   int argc,
					   JSValueConst *argv)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	dom_string *tag = NULL;
	dom_element *element = NULL;
	(void) this_val;
	if (thread == NULL || thread->htmlc == NULL ||
	    thread->htmlc->document == NULL || argc < 1) {
		return qjs_new_basic_element(ctx, "DIV", "", "");
	}
	if (!qjs_dom_string_from_js(ctx, argv[0], &tag)) {
		return qjs_new_basic_element(ctx, "DIV", "", "");
	}
	if (dom_document_create_element(thread->htmlc->document, tag,
					&element) != DOM_NO_ERR ||
	    element == NULL) {
		dom_string_unref(tag);
		return qjs_new_basic_element(ctx, "DIV", "", "");
	}
	dom_string_unref(tag);
	JSValue obj = qjs_new_dom_element(ctx, element);
	dom_node_unref(element);
	return obj;
}

static JSValue qjs_document_create_element_ns(JSContext *ctx,
					      JSValueConst this_val,
					      int argc,
					      JSValueConst *argv)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	dom_string *namespace_uri = NULL;
	dom_string *qname = NULL;
	dom_element *element = NULL;
	(void) this_val;
	if (thread == NULL || thread->htmlc == NULL ||
	    thread->htmlc->document == NULL || argc < 2) {
		return qjs_new_basic_element(ctx, "DIV", "", "");
	}
	if (!JS_IsNull(argv[0]) && !JS_IsUndefined(argv[0]) &&
	    !qjs_dom_string_from_js(ctx, argv[0], &namespace_uri)) {
		return qjs_new_basic_element(ctx, "DIV", "", "");
	}
	if (!qjs_dom_string_from_js(ctx, argv[1], &qname)) {
		if (namespace_uri != NULL) {
			dom_string_unref(namespace_uri);
		}
		return qjs_new_basic_element(ctx, "DIV", "", "");
	}
	if (namespace_uri != NULL) {
		(void) dom_document_create_element_ns(thread->htmlc->document,
						      namespace_uri, qname,
						      &element);
	} else {
		(void) dom_document_create_element(thread->htmlc->document,
						   qname, &element);
	}
	if (namespace_uri != NULL) {
		dom_string_unref(namespace_uri);
	}
	dom_string_unref(qname);
	if (element == NULL) {
		return qjs_new_basic_element(ctx, "DIV", "", "");
	}
	JSValue obj = qjs_new_dom_element(ctx, element);
	dom_node_unref(element);
	return obj;
}

static JSValue qjs_document_create_text_node(JSContext *ctx,
					     JSValueConst this_val,
					     int argc,
					     JSValueConst *argv)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	dom_string *text = NULL;
	dom_text *node = NULL;
	(void) this_val;
	if (thread == NULL || thread->htmlc == NULL ||
	    thread->htmlc->document == NULL || argc < 1) {
		return JS_NULL;
	}
	if (!qjs_dom_string_from_js(ctx, argv[0], &text)) {
		return JS_NULL;
	}
	if (dom_document_create_text_node(thread->htmlc->document, text,
					  &node) != DOM_NO_ERR ||
	    node == NULL) {
		dom_string_unref(text);
		return JS_NULL;
	}
	dom_string_unref(text);
	JSValue obj = qjs_new_dom_node(ctx, (dom_node *) node);
	dom_node_unref(node);
	return obj;
}

static JSValue qjs_document_create_document_fragment(JSContext *ctx,
						     JSValueConst this_val,
						     int argc,
						     JSValueConst *argv)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	dom_document_fragment *fragment = NULL;
	(void) this_val;
	(void) argc;
	(void) argv;
	if (thread == NULL || thread->htmlc == NULL ||
	    thread->htmlc->document == NULL ||
	    dom_document_create_document_fragment(thread->htmlc->document,
						  &fragment) != DOM_NO_ERR ||
	    fragment == NULL) {
		return JS_NULL;
	}
	JSValue obj = qjs_new_dom_node(ctx, (dom_node *) fragment);
	dom_node_unref(fragment);
	return obj;
}

static void qjs_document_set_node_property(JSContext *ctx,
					   JSValueConst document,
					   const char *name,
					   dom_node *node)
{
	if (node == NULL) {
		return;
	}
	JS_SetPropertyStr(ctx, document, name, qjs_new_dom_node(ctx, node));
}

static void qjs_document_bind_real_nodes(JSContext *ctx, JSValueConst document)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	dom_element *document_element = NULL;
	dom_html_element *body = NULL;
	dom_string *head_tag = NULL;
	dom_nodelist *heads = NULL;
	if (thread == NULL || thread->htmlc == NULL ||
	    thread->htmlc->document == NULL) {
		return;
	}
	if (dom_document_get_document_element(thread->htmlc->document,
					      &document_element) == DOM_NO_ERR &&
	    document_element != NULL) {
		qjs_document_set_node_property(ctx, document, "documentElement",
					       (dom_node *) document_element);
		dom_node_unref(document_element);
	}
	if (dom_html_document_get_body((dom_html_document *)
				       thread->htmlc->document,
				       &body) == DOM_NO_ERR &&
	    body != NULL) {
		qjs_document_set_node_property(ctx, document, "body",
					       (dom_node *) body);
		dom_node_unref(body);
	}
	if (dom_string_create((const uint8_t *) "head", 4u,
			      &head_tag) == DOM_NO_ERR) {
		if (dom_document_get_elements_by_tag_name(thread->htmlc->document,
							  head_tag,
							  &heads) == DOM_NO_ERR &&
		    heads != NULL) {
			dom_node *head = NULL;
			if (dom_nodelist_item(heads, 0u, &head) == DOM_NO_ERR &&
			    head != NULL) {
				qjs_document_set_node_property(ctx, document,
							       "head", head);
				dom_node_unref(head);
			}
			dom_nodelist_unref(heads);
		}
		dom_string_unref(head_tag);
	}
}

static void qjs_install_browser_bootstrap(JSContext *ctx)
{
	static const char bootstrap[] =
		"(function(g){"
		"function noop(){return void 0;}"
		"function arr(){var a=[];a.item=function(i){return this[i]||null;};return a;}"
		"function ctor(n,b){var f=g[n]||function(){};var old=f.prototype||{};"
		"f.prototype=b?Object.create(b.prototype):old;for(var k in old)f.prototype[k]=old[k];"
		"f.prototype.constructor=f;g[n]=f;return f;}"
		"var Node=ctor('Node'),Element=ctor('Element',Node),HTMLElement=ctor('HTMLElement',Element),"
		"Document=ctor('Document',Node),DocumentFragment=ctor('DocumentFragment',Node),"
		"SVGElement=ctor('SVGElement',Element),HTMLIFrameElement=ctor('HTMLIFrameElement',HTMLElement),"
		"HTMLInputElement=ctor('HTMLInputElement',HTMLElement),HTMLTextAreaElement=ctor('HTMLTextAreaElement',HTMLElement),"
		"HTMLSelectElement=ctor('HTMLSelectElement',HTMLElement),HTMLButtonElement=ctor('HTMLButtonElement',HTMLElement),"
		"HTMLImageElement=ctor('HTMLImageElement',HTMLElement),HTMLAnchorElement=ctor('HTMLAnchorElement',HTMLElement),"
		"HTMLFormElement=ctor('HTMLFormElement',HTMLElement);"
		"Node.ELEMENT_NODE=1;Node.TEXT_NODE=3;Node.DOCUMENT_NODE=9;Node.DOCUMENT_FRAGMENT_NODE=11;"
		"function hasClass(e,c){return (' '+(e.className||'')+' ').indexOf(' '+c+' ')>=0;}"
		"function cl(e){return {contains:function(c){return hasClass(e,String(c));},"
		"add:function(c){c=String(c);if(!hasClass(e,c))e.className=(e.className?e.className+' ':'')+c;},"
		"remove:function(c){e.className=(' '+(e.className||'')+' ').replace(' '+String(c)+' ',' ').trim();}};}"
		"if(Object.defineProperty&&!Object.getOwnPropertyDescriptor(Element.prototype,'classList'))"
		"Object.defineProperty(Element.prototype,'classList',{get:function(){return cl(this);}});"
		"function attr(e,n){return e.getAttribute?e.getAttribute(n):(e.attributes&&e.attributes[n])||e[n]||null;}"
		"function loc(n){return g.location&&g.location[n]||'';}"
		"function initAnchor(e){e.href='';e.protocol='';e.host='';e.hostname='';e.port='';"
		"e.pathname='';e.search='';e.hash='';e.origin='';}"
		"function setAnchor(e,u){u=String(u||'');var p=loc('protocol')||'https:',h=loc('host')||'',bp=loc('pathname')||'/';"
		"var full=u;if(!/^[A-Za-z][A-Za-z0-9+.-]*:/.test(u)){if(u.slice(0,2)==='//')full=p+u;"
		"else if(u.charAt(0)==='/')full=p+'//'+h+u;else{var i=bp.lastIndexOf('/'),d=i>=0?bp.slice(0,i+1):'/';"
		"full=h?p+'//'+h+d+u:u;}}"
		"var r=/^([A-Za-z][A-Za-z0-9+.-]*:)?(?:\\/\\/([^\\/?#]*))?([^?#]*)(\\?[^#]*)?(#.*)?$/.exec(full)||[];"
		"var host=r[2]||'',path=r[3]||'';if(host&&!path)path='/';if(host&&path.charAt(0)!=='/')path='/'+path;"
		"var hn=host,po='';if(host.charAt(0)==='['){var c=host.indexOf(']');if(c>=0){hn=host.slice(0,c+1);"
		"if(host.charAt(c+1)===':')po=host.slice(c+2);}}else{var x=host.indexOf(':');if(x>=0){hn=host.slice(0,x);po=host.slice(x+1);}}"
		"e.href=full;e.protocol=r[1]||'';e.host=host;e.hostname=hn;e.port=po;e.pathname=path;e.search=r[4]||'';"
		"e.hash=r[5]||'';e.origin=(e.protocol&&e.host)?e.protocol+'//'+e.host:'';return full;}"
		"Element.prototype.getAttribute=Element.prototype.getAttribute||function(n){"
		"n=String(n);return this.attributes&&this.attributes.hasOwnProperty(n)?this.attributes[n]:"
		"(n==='class'?this.className:(this[n]!==void 0?String(this[n]):null));};"
		"Element.prototype.setAttribute=Element.prototype.setAttribute||function(n,v){n=String(n);v=String(v);"
		"this.attributes=this.attributes||{};this.attributes[n]=v;if(n==='id')this.id=v;"
		"else if(n==='class')this.className=v;else if(n==='href'&&(this.tagName||'').toUpperCase()==='A')setAnchor(this,v);else this[n]=v;};"
		"Element.prototype.removeAttribute=Element.prototype.removeAttribute||function(n){"
		"n=String(n);if(this.attributes)delete this.attributes[n];};"
		"Element.prototype.hasAttribute=Element.prototype.hasAttribute||function(n){"
		"n=String(n);return !!(this.attributes&&this.attributes.hasOwnProperty(n));};"
		"Element.prototype.focus=Element.prototype.focus||noop;Element.prototype.blur=Element.prototype.blur||noop;"
		"Element.prototype.contains=Element.prototype.contains||function(n){"
		"for(;n;n=n.parentNode||n.parentElement){if(n===this)return true;}return false;};"
		"function textNode(v){return {nodeType:3,nodeName:'#text',nodeValue:String(v||''),"
		"textContent:String(v||''),parentNode:null,parentElement:null,cloneNode:function(){return textNode(this.nodeValue);}};}"
		"function resetKids(e){if(!e.childNodes)e.childNodes=arr();e.childNodes.length=0;e.children=e.childNodes;"
		"e.firstChild=null;e.lastChild=null;e.firstElementChild=null;e.lastElementChild=null;}"
		"function appendKid(e,c){if(c){c.parentNode=e;c.parentElement=e.nodeType===1?e:null;"
		"c.ownerDocument=e.ownerDocument||e;if(!e.childNodes)e.childNodes=arr();e.childNodes.push(c);"
		"if(!e.firstChild)e.firstChild=c;e.lastChild=c;if(c.nodeType===1){if(!e.firstElementChild)e.firstElementChild=c;"
		"e.lastElementChild=c;}}return c;}"
		"function resyncKids(e){var cs=e.childNodes||arr();e.firstChild=cs.length?cs[0]:null;"
		"e.lastChild=cs.length?cs[cs.length-1]:null;e.firstElementChild=null;e.lastElementChild=null;"
		"for(var i=0;i<cs.length;i++){if(cs[i]&&cs[i].nodeType===1){if(!e.firstElementChild)e.firstElementChild=cs[i];"
		"e.lastElementChild=cs[i];}}}"
		"function attrs(c,s){s=String(s||'');var r=/([A-Za-z_:][-A-Za-z0-9_:.]*)(?:\\s*=\\s*(?:\\\"([^\\\"]*)\\\"|'([^']*)'|([^\\s\\\"'>/]+)))?/g,m;"
		"while((m=r.exec(s))){var n=m[1],v=m[2]!==void 0?m[2]:(m[3]!==void 0?m[3]:(m[4]!==void 0?m[4]:''));"
		"c.setAttribute(n,v);if(n==='checked')c.checked=true;if(n==='selected')c.selected=true;"
		"if(n==='value')c.value=v;if(n==='type')c.type=v;if(n==='name')c.name=v;if(n==='href'&&c.tagName!=='A')c.href=v;}}"
		"function setInner(e,v){var html=String(v||'');e._innerHTML=html;resetKids(e);if(!html)return;"
		"var lead=html.match(/^\\s+/);if(lead)appendKid(e,textNode(lead[0]));"
		"var re=/<([A-Za-z][\\w:-]*)([^>]*)>([\\s\\S]*?)<\\/\\1\\s*>|<([A-Za-z][\\w:-]*)([^>]*)\\s*\\/?>/g,m;"
		"while((m=re.exec(html))){var name=(m[1]||m[4]||'div'),body=m[3]||'',c=el(name);"
		"attrs(c,m[2]||m[5]||'');if(name.toLowerCase()==='textarea'){c.defaultValue=body;c.value=body;c.textContent=body;}"
		"else if(body&&!/<[A-Za-z]/.test(body)){c.textContent=body;}appendKid(e,c);}"
		"if(!e.childNodes.length)appendKid(e,textNode(html));}"
		"function outer(e){var t=(e.tagName||'div').toLowerCase();return '<'+t+'>'+(e._innerHTML||e.textContent||'')+'</'+t+'>';}"
		"if(Object.defineProperty&&!Object.getOwnPropertyDescriptor(Element.prototype,'innerHTML'))"
		"Object.defineProperty(Element.prototype,'innerHTML',{get:function(){return this._innerHTML||'';},"
		"set:function(v){setInner(this,v);}});"
		"if(Object.defineProperty&&!Object.getOwnPropertyDescriptor(Element.prototype,'outerHTML'))"
		"Object.defineProperty(Element.prototype,'outerHTML',{get:function(){return this._outerHTML||outer(this);},"
		"set:function(v){this._outerHTML=String(v||'');}});"
		"Element.prototype.matches=Element.prototype.matches||Element.prototype.msMatchesSelector||"
		"Element.prototype.webkitMatchesSelector||function(s){var parts=String(s||'').split(',');"
		"for(var pi=0;pi<parts.length;pi++){var q=parts[pi].trim();if(!q)continue;"
		"if(q.indexOf(' ')>=0)q=q.split(/\\s+/)[0];"
		"if(q[0]==='#'&&this.id===q.slice(1))return true;"
		"if(q[0]==='.'&&hasClass(this,q.slice(1)))return true;"
		"if(q[0]==='['){var m=q.match(/^\\[([^=\\]|~^$*]+)(?:[|~^$*]?=['\\\"]?([^'\\\"]*)['\\\"]?)?\\]$/);"
		"if(m){var v=attr(this,m[1]);if(v!==null&&(m[2]===void 0||String(v)===m[2]||String(v).indexOf(m[2])>=0))return true;}}"
		"if((this.tagName||'').toLowerCase()===q.toLowerCase())return true;}return false;};"
		"Element.prototype.closest=Element.prototype.closest||function(s){var n=this;"
		"while(n&&n.nodeType===1){if(n.matches&&n.matches(s))return n;n=n.parentElement||n.parentNode;}return null;};"
		"function tagSearch(e,t){var a=arr();t=String(t||'').toLowerCase();function walk(n){if(!n)return;"
		"if(n!==e&&n.nodeType===1&&(t==='*'||(n.tagName||'').toLowerCase()===t))a.push(n);"
		"var cs=n.childNodes||[];for(var i=0;i<cs.length;i++)walk(cs[i]);}walk(e);"
		"if(!a.length){var h=(e._innerHTML||'').toLowerCase();if(t&&h.indexOf('<'+t)>=0){var c=el(t);"
		"if(t==='textarea'){var m=(e._innerHTML||'').match(/<textarea[^>]*>([\\s\\S]*?)<\\/textarea>/i);"
		"c.defaultValue=m?m[1]:'';c.value=c.defaultValue;}a.push(c);}}return a;}"
		"Element.prototype.querySelector=Element.prototype.querySelector||function(){return el('div');};"
		"Element.prototype.querySelectorAll=Element.prototype.querySelectorAll||arr;"
		"Element.prototype.getElementsByTagName=Element.prototype.getElementsByTagName||function(t){return tagSearch(this,t);};"
		"Element.prototype.getElementsByClassName=Element.prototype.getElementsByClassName||arr;"
		"function css(){return {cssText:'',display:'',visibility:'',opacity:'',"
		"position:'',width:'',height:'',color:'',backgroundColor:'',fontSize:'',"
		"getPropertyValue:function(n){return this[n]||'';},"
		"setProperty:function(n,v){this[n]=String(v||'');},"
		"removeProperty:function(n){var v=this[n]||'';this[n]='';return v;}};}"
		"function cloneElem(s,deep){if(!s)return null;if(s.nodeType===3)return textNode(s.nodeValue||s.textContent||'');"
		"var c=el(s.tagName||s.nodeName||'div');c.id=s.id||'';c.className=s.className||'';c.value=s.value||'';"
		"c.defaultValue=s.defaultValue||'';c.checked=!!s.checked;c.selected=!!s.selected;c.type=s.type||'';"
		"c.name=s.name||'';c.href=s.href||'';c.textContent=s.textContent||'';if(s.attributes){for(var k in s.attributes)"
		"if(s.attributes.hasOwnProperty(k))c.setAttribute(k,s.attributes[k]);}c._innerHTML=s._innerHTML||'';"
		"if(deep){var cs=s.childNodes||[];for(var i=0;i<cs.length;i++)appendKid(c,cloneElem(cs[i],true));}"
		"return c;}"
		"function el(tag){var e=Object.create(HTMLElement.prototype);tag=String(tag||'').toUpperCase();"
		"e.nodeType=1;e.tagName=tag;e.nodeName=tag;e.childNodes=arr();"
		"e.children=e.childNodes;e.style=css();e.attributes={};"
		"e.parentNode=null;e.ownerDocument=g.document||null;e.textContent='';"
		"e.parentElement=null;e.firstChild=null;e.lastChild=null;e.firstElementChild=null;e.lastElementChild=null;"
		"e.innerHTML='';e.className='';e.id='';e.value='';e.defaultValue='';e.checked=false;e.selected=false;"
		"e.type='';e.name='';e.href='';e.protocol='';e.host='';e.hostname='';e.port='';e.pathname='';e.search='';e.hash='';e.origin='';e.classList=cl(e);"
		"e.appendChild=function(c){return appendKid(e,c);};"
		"e.insertBefore=function(c,r){if(!r)return appendKid(e,c);var i=e.childNodes.indexOf(r);"
		"if(i<0)return appendKid(e,c);c.parentNode=e;c.parentElement=e;e.childNodes.splice(i,0,c);resyncKids(e);return c;};"
		"e.removeChild=function(c){var i=e.childNodes.indexOf(c);if(i>=0){e.childNodes.splice(i,1);"
		"c.parentNode=null;c.parentElement=null;resyncKids(e);}return c;};"
		"e.setAttribute=function(n,v){n=String(n);v=String(v);e.attributes[n]=v;if(n==='id')e.id=v;if(n==='class')e.className=v;"
		"if(n==='href'&&tag==='A')setAnchor(e,v);};"
		"e.getAttribute=function(n){n=String(n);return e.attributes.hasOwnProperty(n)?e.attributes[n]:null;};"
		"e.hasAttribute=function(n){return e.attributes.hasOwnProperty(String(n));};"
		"e.removeAttribute=function(n){delete e.attributes[String(n)];};"
		"e.addEventListener=noop;e.removeEventListener=noop;e.dispatchEvent=function(){return true;};"
		"e.focus=noop;e.blur=noop;"
		"e.querySelector=function(s){var r=tagSearch(e,s);return r.length?r[0]:null;};e.querySelectorAll=function(s){return tagSearch(e,s);};"
		"e.getElementsByTagName=function(t){return tagSearch(e,t);};e.getElementsByClassName=arr;"
		"e.cloneNode=function(deep){return cloneElem(e,!!deep);};return e;}"
		"g.window=g.window||g;g.self=g.self||g;g.globalThis=g.globalThis||g;"
		"function addEvt(n,f){if(typeof f==='function'&&(n==='load'||n==='DOMContentLoaded'||n==='readystatechange'))g.setTimeout(f,0);}"
		"g.addEventListener=g.addEventListener||addEvt;g.removeEventListener=g.removeEventListener||noop;"
		"g.dispatchEvent=g.dispatchEvent||function(){return true;};"
		"g.scroll=g.scroll||noop;g.scrollTo=g.scrollTo||g.scroll;g.scrollBy=g.scrollBy||noop;"
		"var nextTimer=1,timerDepth=0,timerCalls=0;"
		"g.setTimeout=g.setTimeout||function(f){var id=nextTimer++;"
		"if(typeof f==='function'&&timerDepth<2&&timerCalls<32){timerDepth++;timerCalls++;"
		"try{f();}catch(e){try{g._DumpException(e);}catch(x){}}timerDepth--;}return id;};"
		"g.clearTimeout=g.clearTimeout||noop;g.setInterval=g.setInterval||g.setTimeout;"
		"g.clearInterval=g.clearInterval||noop;"
		"g.requestAnimationFrame=g.requestAnimationFrame||function(f){return g.setTimeout(f,16);};"
		"g.cancelAnimationFrame=g.cancelAnimationFrame||g.clearTimeout;"
		"g.innerWidth=g.innerWidth||1440;g.innerHeight=g.innerHeight||900;"
		"g.performance=g.performance||{};g.performance.now=g.performance.now||function(){return 0;};"
		"g.performance.timing=g.performance.timing||{navigationStart:0};"
		"g.Intl=g.Intl||{};function intlList(a){return Array.isArray(a)?a:(a==null?[]:[String(a)]);}"
		"function intlLocale(l){var a=intlList(l);return String(a[0]||'en-US');}"
		"function intlDate(v){try{var d=v instanceof Date?v:new Date(v==null?Date.now():v);return isNaN(d.getTime())?'Invalid Date':d.toISOString();}catch(e){return '';}}"
		"g.Intl.DateTimeFormat=g.Intl.DateTimeFormat||function(l,o){if(!(this instanceof g.Intl.DateTimeFormat))return new g.Intl.DateTimeFormat(l,o);this.locale=intlLocale(l);this.options=o||{};};"
		"g.Intl.DateTimeFormat.prototype.format=function(v){return intlDate(v);};"
		"g.Intl.DateTimeFormat.prototype.formatToParts=function(v){return [{type:'literal',value:this.format(v)}];};"
		"g.Intl.DateTimeFormat.prototype.resolvedOptions=function(){return {locale:this.locale,calendar:'gregory',numberingSystem:'latn',timeZone:'UTC'};};"
		"g.Intl.DateTimeFormat.supportedLocalesOf=g.Intl.DateTimeFormat.supportedLocalesOf||function(a){return intlList(a);};"
		"g.Intl.NumberFormat=g.Intl.NumberFormat||function(l,o){if(!(this instanceof g.Intl.NumberFormat))return new g.Intl.NumberFormat(l,o);this.locale=intlLocale(l);this.options=o||{};};"
		"g.Intl.NumberFormat.prototype.format=function(v){var n=Number(v);return isFinite(n)?String(n):String(v==null?0:v);};"
		"g.Intl.NumberFormat.prototype.formatToParts=function(v){return [{type:'integer',value:this.format(v)}];};"
		"g.Intl.NumberFormat.prototype.resolvedOptions=function(){return {locale:this.locale,numberingSystem:'latn',style:this.options.style||'decimal'};};"
		"g.Intl.NumberFormat.supportedLocalesOf=g.Intl.NumberFormat.supportedLocalesOf||function(a){return intlList(a);};"
		"g.Intl.PluralRules=g.Intl.PluralRules||function(l,o){if(!(this instanceof g.Intl.PluralRules))return new g.Intl.PluralRules(l,o);this.locale=intlLocale(l);this.options=o||{};};"
		"g.Intl.PluralRules.prototype.select=function(v){return Number(v)===1?'one':'other';};"
		"g.Intl.PluralRules.prototype.resolvedOptions=function(){return {locale:this.locale,type:this.options.type||'cardinal'};};"
		"g.Intl.PluralRules.supportedLocalesOf=g.Intl.PluralRules.supportedLocalesOf||function(a){return intlList(a);};"
		"g.Intl.RelativeTimeFormat=g.Intl.RelativeTimeFormat||function(l,o){if(!(this instanceof g.Intl.RelativeTimeFormat))return new g.Intl.RelativeTimeFormat(l,o);this.locale=intlLocale(l);this.options=o||{};};"
		"g.Intl.RelativeTimeFormat.prototype.format=function(v,u){return String(v)+' '+String(u||'second');};"
		"g.Intl.RelativeTimeFormat.prototype.formatToParts=function(v,u){return [{type:'literal',value:this.format(v,u)}];};"
		"g.Intl.RelativeTimeFormat.prototype.resolvedOptions=function(){return {locale:this.locale,style:this.options.style||'long',numeric:this.options.numeric||'always'};};"
		"g.Intl.RelativeTimeFormat.supportedLocalesOf=g.Intl.RelativeTimeFormat.supportedLocalesOf||function(a){return intlList(a);};"
		"g.Intl.Collator=g.Intl.Collator||function(l,o){if(!(this instanceof g.Intl.Collator))return new g.Intl.Collator(l,o);this.locale=intlLocale(l);this.options=o||{};};"
		"g.Intl.Collator.prototype.compare=function(a,b){a=String(a);b=String(b);return a<b?-1:(a>b?1:0);};"
		"g.Intl.Collator.prototype.resolvedOptions=function(){return {locale:this.locale,usage:this.options.usage||'sort',sensitivity:this.options.sensitivity||'variant'};};"
		"g.Intl.Collator.supportedLocalesOf=g.Intl.Collator.supportedLocalesOf||function(a){return intlList(a);};"
		"g.Intl.getCanonicalLocales=g.Intl.getCanonicalLocales||function(a){return intlList(a);};"
		"try{RegExp.input=RegExp.input||'';RegExp.leftContext=RegExp.leftContext||'';RegExp.lastMatch=RegExp.lastMatch||'';"
		"RegExp.multiline=!!RegExp.multiline;for(var rx=1;rx<=9;rx++)if(RegExp['$'+rx]===void 0)RegExp['$'+rx]='';}catch(e){}"
		"function store(){var s={};return {getItem:function(k){k=String(k);return s.hasOwnProperty(k)?s[k]:null;},"
		"setItem:function(k,v){s[String(k)]=String(v);},removeItem:function(k){delete s[String(k)];},clear:function(){s={};}};}"
		"g.localStorage=g.localStorage||store();g.sessionStorage=g.sessionStorage||store();"
		"g.isSecureContext=g.isSecureContext!==void 0?g.isSecureContext:true;"
		"g.TextEncoder=g.TextEncoder||function(){};g.TextEncoder.prototype.encode=g.TextEncoder.prototype.encode||function(s){"
		"s=String(s||'');var a=new Uint8Array(s.length);for(var i=0;i<s.length;i++)a[i]=s.charCodeAt(i)&255;return a;};"
		"var cr=g.crypto=g.crypto||{};var seed=((Date.now?Date.now():123456789)^0x9e3779b9)>>>0;"
		"function rnd(){seed=(Math.imul?Math.imul(seed,1664525):(seed*1664525))>>>0;seed=(seed+1013904223)>>>0;return seed;}"
		"cr.getRandomValues=cr.getRandomValues||function(a){if(!a||typeof a.length!=='number')throw TypeError('Expected typed array');"
		"for(var i=0;i<a.length;i++)a[i]=rnd();return a;};"
		"cr.randomUUID=cr.randomUUID||function(){var b=new Uint8Array(16);cr.getRandomValues(b);b[6]=(b[6]&15)|64;b[8]=(b[8]&63)|128;"
		"var h=[];for(var i=0;i<16;i++)h[i]=b[i].toString(16).padStart(2,'0');return h[0]+h[1]+h[2]+h[3]+'-'+h[4]+h[5]+'-'+h[6]+h[7]+'-'+h[8]+h[9]+'-'+h[10]+h[11]+h[12]+h[13]+h[14]+h[15];};"
		"function bytes(n){var a=new Uint8Array(n);cr.getRandomValues(a);return a.buffer;}"
		"cr.subtle=cr.subtle||{};cr.subtle.digest=cr.subtle.digest||function(){return Promise.resolve(bytes(32));};"
		"cr.subtle.generateKey=cr.subtle.generateKey||function(){return Promise.resolve({publicKey:{type:'public'},privateKey:{type:'private'}});};"
		"cr.subtle.exportKey=cr.subtle.exportKey||function(){return Promise.resolve(bytes(65));};"
		"cr.subtle.importKey=cr.subtle.importKey||function(){return Promise.resolve({type:'public'});};"
		"cr.subtle.sign=cr.subtle.sign||function(){return Promise.resolve(bytes(64));};cr.subtle.verify=cr.subtle.verify||function(){return Promise.resolve(false);};"
		"g.msCrypto=g.msCrypto||cr;"
		"g.matchMedia=g.matchMedia||function(q){return {matches:false,media:String(q||''),onchange:null,"
		"addEventListener:noop,removeEventListener:noop,addListener:noop,removeListener:noop,dispatchEvent:function(){return true;}};};"
		"function Obs(cb){this.callback=cb||noop;}Obs.prototype.observe=noop;Obs.prototype.unobserve=noop;"
		"Obs.prototype.disconnect=noop;Obs.prototype.takeRecords=function(){return [];};"
		"g.MutationObserver=g.MutationObserver||Obs;g.WebKitMutationObserver=g.WebKitMutationObserver||g.MutationObserver;"
		"g.ResizeObserver=g.ResizeObserver||Obs;g.IntersectionObserver=g.IntersectionObserver||Obs;"
		"g.regeneratorRuntime=g.regeneratorRuntime||{mark:function(f){return f;},awrap:function(v){return v;},"
		"wrap:function(inner){return function(){var done=false,self=this,args=arguments;return {next:function(v){if(done)return {value:void 0,done:true};done=true;return {value:inner.apply(self,args),done:true};},"
		"throw:function(e){throw e;},return:function(v){done=true;return {value:v,done:true};},"
		"[Symbol.iterator]:function(){return this;}};};},"
		"async:function(inner){try{return Promise.resolve(inner());}catch(e){return Promise.reject(e);}},"
		"values:function(iter){if(iter&&iter[Symbol.iterator])return iter[Symbol.iterator]();var i=0,a=iter||[];return {next:function(){return i<a.length?{value:a[i++],done:false}:{done:true};}};}};"
		"g.google=g.google||{};g.google.log=g.google.log||noop;g.gbar_=g.gbar_||{};g.gbar_._DumpException=g.gbar_._DumpException||noop;"
		"g._DumpException=g._DumpException||g.gbar_._DumpException;"
		"g.Image=g.Image||function(){var i=el('img');i.onload=null;i.onerror=null;i.onabort=null;i.src='';return i;};"
		"var b64='ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';"
		"g.atob=g.atob||function(s){s=String(s||'').replace(/[\\t\\n\\f\\r ]/g,'');if(s.length%4===1)throw Error('InvalidCharacterError');"
		"var out='',buf=0,bits=0,i,c,v;for(i=0;i<s.length;i++){c=s.charAt(i);if(c==='=')break;v=b64.indexOf(c);"
		"if(v<0)throw Error('InvalidCharacterError');buf=(buf<<6)|v;bits+=6;if(bits>=8){bits-=8;out+=String.fromCharCode((buf>>bits)&255);}}return out;};"
		"g.btoa=g.btoa||function(s){s=String(s||'');var out='',i,a,b,c;for(i=0;i<s.length;i++)if(s.charCodeAt(i)>255)throw Error('InvalidCharacterError');"
		"for(i=0;i<s.length;i+=3){a=s.charCodeAt(i);b=i+1<s.length?s.charCodeAt(i+1):0;c=i+2<s.length?s.charCodeAt(i+2):0;"
		"out+=b64.charAt(a>>2)+b64.charAt(((a&3)<<4)|(b>>4))+(i+1<s.length?b64.charAt(((b&15)<<2)|(c>>6)):'=')+(i+2<s.length?b64.charAt(c&63):'=');}return out;};"
		"g.URLSearchParams=g.URLSearchParams||function(){this.get=function(){return null;};this.set=noop;this.toString=function(){return '';};};"
		"g.URL=g.URL||function(u){this.href=String(u||'');this.searchParams=new g.URLSearchParams('');this.toString=function(){return this.href;};};"
		"function jqObj(a){a=a||arr();a.each=function(f){for(var i=0;i<a.length;i++)if(f)f.call(a[i],i,a[i]);return a;};"
		"a.on=a.off=a.bind=a.unbind=a.delegate=a.undelegate=a.trigger=function(){return a;};"
		"a.ready=function(f){if(typeof f==='function')g.setTimeout(f,0);return a;};"
		"a.click=a.submit=a.change=a.keyup=a.keydown=a.focus=a.blur=function(f){if(typeof f==='function')return a.on('',f);return a;};"
		"a.attr=function(n,v){if(v===void 0)return a[0]&&a[0].getAttribute?a[0].getAttribute(n):void 0;a.each(function(){if(this.setAttribute)this.setAttribute(n,v);});return a;};"
		"a.prop=function(n,v){if(v===void 0)return a[0]?a[0][n]:void 0;a.each(function(){this[n]=v;});return a;};"
		"a.val=function(v){if(v===void 0)return a[0]&&a[0].value||'';a.each(function(){this.value=v;});return a;};"
		"a.text=function(v){if(v===void 0)return a[0]&&a[0].textContent||'';a.each(function(){this.textContent=String(v||'');});return a;};"
		"a.html=function(v){if(v===void 0)return a[0]&&a[0].innerHTML||'';a.each(function(){this.innerHTML=String(v||'');});return a;};"
		"a.addClass=a.removeClass=a.toggleClass=a.show=a.hide=a.remove=function(){return a;};"
		"a.css=function(n,v){if(v===void 0&&typeof n==='string')return a[0]&&a[0].style?a[0].style[n]||'':'';return a;};"
		"a.append=function(c){a.each(function(){if(this.appendChild)this.appendChild(c&&c.nodeType?c:el('span'));});return a;};"
		"a.find=function(s){var r=arr();a.each(function(){var q=this.querySelectorAll?this.querySelectorAll(s):[];for(var i=0;i<q.length;i++)r.push(q[i]);});return jqObj(r);};"
		"a.parent=function(){var r=arr();a.each(function(){if(this.parentNode)r.push(this.parentNode);});return jqObj(r);};"
		"a.children=function(){var r=arr();a.each(function(){var q=this.children||[];for(var i=0;i<q.length;i++)r.push(q[i]);});return jqObj(r);};"
		"a.data=function(){return a;};a.is=function(){return !!a.length;};return a;}"
		"function jq(s){var r=arr(),q,i;if(typeof s==='function')return jqObj(arr()).ready(s);"
		"if(s&&s.nodeType){r.push(s);}else if(s===g||s===g.document){r.push(s);}else if(typeof s==='string'&&g.document&&g.document.querySelectorAll){try{q=g.document.querySelectorAll(s);for(i=0;i<q.length;i++)r.push(q[i]);}catch(e){}}"
		"return jqObj(r);}g.$=g.$||jq;g.jQuery=g.jQuery||g.$;g.$.fn=g.jQuery.fn=jqObj(arr());"
		"g.$.extend=g.jQuery.extend=function(t){t=t||{};for(var i=1;i<arguments.length;i++){var o=arguments[i]||{};for(var k in o)t[k]=o[k];}return t;};"
		"g.$.each=g.jQuery.each=function(o,f){if(!o||!f)return o;var i;if(typeof o.length==='number'){for(i=0;i<o.length;i++)f.call(o[i],i,o[i]);}else{for(i in o)if(o.hasOwnProperty(i))f.call(o[i],i,o[i]);}return o;};"
		"g.$.ajax=g.jQuery.ajax=function(){return Promise.resolve({});};g.$.get=g.jQuery.get=function(){return Promise.resolve({});};g.$.post=g.jQuery.post=function(){return Promise.resolve({});};"
		"var ngmod={config:function(){return ngmod;},run:function(){return ngmod;},controller:function(){return ngmod;},service:function(){return ngmod;},factory:function(){return ngmod;},directive:function(){return ngmod;},filter:function(){return ngmod;},constant:function(){return ngmod;},value:function(){return ngmod;},provider:function(){return ngmod;},component:function(){return ngmod;}};"
		"g.angular=g.angular||{module:function(){return ngmod;},element:function(e){return g.$?g.$(e):e;},noop:noop,copy:function(o){return o;},extend:g.$.extend,forEach:g.$.each,isArray:Array.isArray,isString:function(v){return typeof v==='string';},isObject:function(v){return v!==null&&typeof v==='object';}};"
		"g.Roblox=g.Roblox||{};var rb=g.Roblox;"
		"function prop(){var f=function(){return null;};f.isRequired=f;return f;}"
		"var pv=prop(),pt=g.PropTypes=g.PropTypes||{};"
		"['array','bool','func','number','object','string','symbol','node','element','any'].forEach(function(k){pt[k]=pt[k]||pv;});"
		"pt.oneOf=pt.oneOf||function(){return pv;};pt.oneOfType=pt.oneOfType||function(){return pv;};"
		"pt.arrayOf=pt.arrayOf||function(){return pv;};pt.objectOf=pt.objectOf||function(){return pv;};"
		"pt.shape=pt.shape||function(){return pv;};pt.exact=pt.exact||function(){return pv;};pt.instanceOf=pt.instanceOf||function(){return pv;};"
		"if(g.React&&!g.React.PropTypes)g.React.PropTypes=pt;"
		"g.Event=g.Event||function(t,i){this.type=String(t||'');this.bubbles=!!(i&&i.bubbles);this.cancelable=!!(i&&i.cancelable);this.defaultPrevented=false;};"
		"g.Event.prototype.preventDefault=function(){this.defaultPrevented=true;};"
		"g.CustomEvent=g.CustomEvent||function(t,i){g.Event.call(this,t,i);this.detail=i&&i.detail;};"
		"g.CustomEvent.prototype=Object.create(g.Event.prototype);g.CustomEvent.prototype.constructor=g.CustomEvent;"
		"rb.EnvironmentUrls=rb.EnvironmentUrls||{};var eu=rb.EnvironmentUrls;"
		"eu.websiteUrl=eu.websiteUrl||'https://www.roblox.com';eu.domain=eu.domain||'roblox.com';"
		"eu.apiGatewayUrl=eu.apiGatewayUrl||'https://apis.roblox.com';eu.catalogApi=eu.catalogApi||'https://catalog.roblox.com';"
		"eu.gamesApi=eu.gamesApi||'https://games.roblox.com';eu.friendsApi=eu.friendsApi||'https://friends.roblox.com';"
		"eu.usersApi=eu.usersApi||'https://users.roblox.com';eu.thumbnailsApi=eu.thumbnailsApi||'https://thumbnails.roblox.com';"
		"eu.presenceApi=eu.presenceApi||'https://presence.roblox.com';eu.inventoryApi=eu.inventoryApi||'https://inventory.roblox.com';"
		"eu.authApi=eu.authApi||'https://auth.roblox.com';eu.accountSettingsApi=eu.accountSettingsApi||'https://accountsettings.roblox.com';"
		"eu.userAgreementsServiceApi=eu.userAgreementsServiceApi||'https://apis.roblox.com/user-agreements';"
		"eu.localeApi=eu.localeApi||'https://locale.roblox.com';eu.localizationTablesApi=eu.localizationTablesApi||'https://localizationtables.roblox.com';"
		"eu.gameInternationalizationApi=eu.gameInternationalizationApi||'https://gameinternationalization.roblox.com';"
		"eu.shareLinksApi=eu.shareLinksApi||'https://sharelinks.roblox.com';eu.shareLinksApiV2=eu.shareLinksApiV2||'https://sharelinks.roblox.com';"
		"eu.assetDeliveryApi=eu.assetDeliveryApi||'https://assetdelivery.roblox.com';eu.chatApi=eu.chatApi||'https://chat.roblox.com';"
		"eu.voiceApi=eu.voiceApi||'https://voice.roblox.com';eu.translationRolesApi=eu.translationRolesApi||'https://translationroles.roblox.com';"
		"eu.stripeCheckoutDomain=eu.stripeCheckoutDomain||'https://checkout.stripe.com';"
		"rb['core-scripts']=rb['core-scripts']||{};var cs=rb['core-scripts'];cs.environmentUrls=cs.environmentUrls||eu;"
		"function ok(v){return Promise.resolve({data:v||{},status:200});}"
		"cs.http=cs.http||{};cs.http.http=cs.http.http||{get:function(){return ok({});},post:function(){return ok({});},put:function(){return ok({});},delete:function(){return ok({});}};"
		"cs.eventStream=cs.eventStream||{sendEvent:noop,SendEvent:noop,SendEventWithTarget:noop,LocalEventLog:[],TargetTypes:{DEFAULT:0,WWW:1}};"
		"cs.eventStream.Init=cs.eventStream.Init||noop;cs.eventStream.init=cs.eventStream.init||cs.eventStream.Init;"
		"cs.eventStream.sendEvent=cs.eventStream.sendEvent||noop;cs.eventStream.sendEventWithTarget=cs.eventStream.sendEventWithTarget||noop;"
		"cs.eventStream.SendEvent=cs.eventStream.SendEvent||cs.eventStream.sendEvent;cs.eventStream.SendEventWithTarget=cs.eventStream.SendEventWithTarget||cs.eventStream.sendEventWithTarget;"
		"cs.eventStream.TargetTypes=cs.eventStream.TargetTypes||{DEFAULT:0,WWW:1};cs.eventStream.targetTypes=cs.eventStream.targetTypes||cs.eventStream.TargetTypes;"
		"cs.eventStream.eventTypes=cs.eventStream.eventTypes||{pageLoad:'pageLoad',formInteraction:'formInteraction',custom:'custom'};"
		"cs.eventStream.EventTypes=cs.eventStream.EventTypes||cs.eventStream.eventTypes;"
		"cs.eventStream.pageLoad=cs.eventStream.pageLoad||{sendPageLoad:noop,sendPageLoadTiming:noop,sendTiming:noop};"
		"cs.guac=cs.guac||{getTreatment:function(){return null;},getBoolTreatment:function(){return false;},getIntTreatment:function(){return 0;},getGuacValues:function(){return Promise.resolve({});}};"
		"cs.meta=cs.meta||{};cs.meta.user=cs.meta.user||{isAuthenticated:false,userId:0,name:'',displayName:''};"
		"cs.meta.device=cs.meta.device||{isMobile:false,isDesktop:true,isTablet:false};"
		"cs.endpoints=cs.endpoints||{supportLocalizedUrls:false,removeUrlLocale:function(u){return String(u||'');},getAbsoluteUrl:function(p){p=String(p||'');return p.charAt(0)==='/'?eu.websiteUrl+p:p;},isAbsolute:function(u){return /^[A-Za-z][A-Za-z0-9+.-]*:/.test(String(u||''));}};"
		"cs.entityUrl=cs.entityUrl||{gameUrl:function(id){return '/games/'+id;},userUrl:function(id){return '/users/'+id;}};"
		"cs.format=cs.format||{};cs.format.string=cs.format.string||{format:function(s){return String(s||'');},escapeHTML:function(s){return String(s||'');}};"
		"cs.format.number=cs.format.number||{abbreviate:function(n){return String(n||0);},format:function(n){return String(n||0);}};"
		"cs.util=cs.util||{};cs.util.ready=cs.util.ready||function(f){if(typeof f==='function')g.setTimeout(f,0);};"
		"function qp(k){k=String(k||'');var q=loc('search');if(q.charAt(0)==='?')q=q.slice(1);var p=q?q.split('&'):[];for(var i=0;i<p.length;i++){var x=p[i].split('='),n=decodeURIComponent(x[0]||'');if(n===k)return decodeURIComponent((x[1]||'').replace(/\\+/g,' '));}return null;}"
		"function qs(o){var a=[];o=o||{};for(var k in o)if(o.hasOwnProperty(k)&&o[k]!=null)a.push(encodeURIComponent(k)+'='+encodeURIComponent(String(o[k])));return a.join('&');}"
		"cs.util.url=cs.util.url||{};cs.util.url.getQueryParam=cs.util.url.getQueryParam||qp;cs.util.url.parseQueryString=cs.util.url.parseQueryString||function(){return {};};cs.util.url.composeQueryString=cs.util.url.composeQueryString||qs;"
		"cs.urlService=cs.urlService||cs.util.url;"
		"cs.util.elementVisibility=cs.util.elementVisibility||{isElementVisible:function(){return true;}};"
		"cs.dataStore=cs.dataStore||{};cs.dataStores=cs.dataStores||{};"
		"cs.dataStores.hbacIndexedDB=cs.dataStores.hbacIndexedDB||{putCryptoKeyPair:function(){return Promise.resolve();},getCryptoKeyPair:function(){return Promise.resolve({});},deleteCryptoDB:function(){return Promise.resolve();}};"
		"cs.dataStores.userDataStore=cs.dataStores.userDataStore||{FriendsUserSortType:{StatusFrequents:'StatusFrequents',Status:'Status',Frequents:'Frequents',Name:'Name'}};"
		"cs.dataStores.userDataStoreV2=cs.dataStores.userDataStoreV2||{getFriends:function(){return Promise.resolve({data:{data:[]}});}};"
		"cs.dataStores.gamesDataStore=cs.dataStores.gamesDataStore||{getGameDetails:function(){return Promise.resolve({data:{data:[]}});},getPlaceDetails:function(){return Promise.resolve({data:[]});}};"
		"cs.dataStores.localeDataStore=cs.dataStores.localeDataStore||{};"
		"cs.dataStores.localeDataStore.getSupportedLocales=cs.dataStores.localeDataStore.getSupportedLocales||function(){return Promise.resolve({data:[]});};"
		"cs.dataStores.localeDataStore.getLocales=cs.dataStores.localeDataStore.getLocales||function(){return Promise.resolve({data:[]});};"
		"cs.dataStores.localeDataStore.getLocalesWithCache=cs.dataStores.localeDataStore.getLocalesWithCache||function(){return Promise.resolve([]);};"
		"cs.dataStores.localeDataStore.getUserLocale=cs.dataStores.localeDataStore.getUserLocale||function(){return Promise.resolve({status:200,data:{signupAndLogin:{locale:'en_us'}}});};"
		"cs.dataStore.hbacIndexedDB=cs.dataStore.hbacIndexedDB||cs.dataStores.hbacIndexedDB;"
		"cs.dataStore.userDataStore=cs.dataStore.userDataStore||cs.dataStores.userDataStore;"
		"cs.dataStore.userDataStoreV2=cs.dataStore.userDataStoreV2||cs.dataStores.userDataStoreV2;"
		"cs.dataStore.gamesDataStore=cs.dataStore.gamesDataStore||cs.dataStores.gamesDataStore;"
		"cs.dataStore.localeDataStore=cs.dataStore.localeDataStore||cs.dataStores.localeDataStore;"
		"cs.cryptoUtil=cs.cryptoUtil||{};cs.cryptoUtil.getHbaMeta=cs.cryptoUtil.getHbaMeta||function(){return {hbaIndexedDBName:'',hbaIndexedDBObjStoreName:'',hbaIndexedDBKeyName:'',isSecureAuthenticationIntentEnabled:false};};"
		"cs.cryptoUtil.generateSigningKeyPairUnextractable=cs.cryptoUtil.generateSigningKeyPairUnextractable||function(){return Promise.resolve({publicKey:{},privateKey:{}});};"
		"cs.cryptoUtil.exportPublicKeyAsSpki=cs.cryptoUtil.exportPublicKeyAsSpki||function(){return Promise.resolve('');};cs.cryptoUtil.sign=cs.cryptoUtil.sign||function(){return Promise.resolve('');};"
		"cs.game=cs.game||{};cs.react=cs.react||{};cs.realtime=cs.realtime||{};cs.intl=cs.intl||{intl:g.Intl||{}};"
		"g.CoreUtilities=g.CoreUtilities||{uuidService:{generateRandomUuid:function(){return cr.randomUUID();}},ready:cs.util.ready,url:cs.util.url};"
		"g.CoreUtilities.urlService=g.CoreUtilities.urlService||cs.util.url;g.CoreUtilities.httpService=g.CoreUtilities.httpService||cs.http.http;"
		"g.CoreUtilities.Endpoints=g.CoreUtilities.Endpoints||cs.endpoints;g.CoreUtilities.endpoints=g.CoreUtilities.endpoints||cs.endpoints;"
		"g.CoreRobloxUtilities=g.CoreRobloxUtilities||{};var cru=g.CoreRobloxUtilities;"
		"cru.environmentUrls=cru.environmentUrls||eu;cru.EnvironmentUrls=cru.EnvironmentUrls||eu;"
		"cru.Endpoints=cru.Endpoints||cs.endpoints;cru.endpoints=cru.endpoints||cs.endpoints;"
		"cru.http=cru.http||cs.http.http;cru.eventStream=cru.eventStream||cs.eventStream;cru.EventStream=cru.EventStream||cs.eventStream;"
		"cru.eventStreamService=cru.eventStreamService||cs.eventStream;cru.EventStreamService=cru.EventStreamService||cs.eventStream;"
		"cru.uuidService=cru.uuidService||g.CoreUtilities.uuidService;cru.ready=cru.ready||cs.util.ready;cru.url=cru.url||cs.util.url;cru.urlService=cru.urlService||cs.util.url;"
		"cru.format=cru.format||cs.format;cru.dataStore=cru.dataStore||cs.dataStore;cru.dataStores=cru.dataStores||cs.dataStores;cru.cryptoUtil=cru.cryptoUtil||cs.cryptoUtil;"
		"cru.realtime=cru.realtime||cs.realtime;cru.coreScripts=cru.coreScripts||cs;"
		"rb.CoreRobloxUtilities=rb.CoreRobloxUtilities||cru;"
		"rb.Endpoints=rb.Endpoints||cs.endpoints;rb.endpoints=rb.endpoints||cs.endpoints;"
		"rb.EventStream=rb.EventStream||cs.eventStream;rb.eventStream=rb.eventStream||cs.eventStream;"
		"rb.eventStreamService=rb.eventStreamService||cs.eventStream;rb.EventStreamService=rb.EventStreamService||cs.eventStream;"
		"rb.urlService=rb.urlService||cs.util.url;"
		"rb.dataStores=rb.dataStores||cs.dataStores;rb.cryptoUtil=rb.cryptoUtil||cs.cryptoUtil;"
		"rb.CaptchaConstants=rb.CaptchaConstants||{errorCodes:{failedToLoadProviderScript:'failedToLoadProviderScript',failedToVerify:'failedToVerify'}};"
		"rb.AccountIntegrityChallengeService=rb.AccountIntegrityChallengeService||{};"
		"rb.AccountIntegrityChallengeService.Generic=rb.AccountIntegrityChallengeService.Generic||{ChallengeError:{matchAbandoned:function(){return false;}}};"
		"rb.AccountIntegrityChallengeService.Captcha=rb.AccountIntegrityChallengeService.Captcha||{ActionType:{Signup:'Signup'},renderChallenge:function(){return Promise.resolve(false);}};"
		"rb.Cookies=rb.Cookies||{};rb.Cookies.getBrowserTrackerId=rb.Cookies.getBrowserTrackerId||function(){return '0';};"
		"rb.Cookies.getGuestId=rb.Cookies.getGuestId||function(){return '0';};rb.Cookies.getCookieValue=rb.Cookies.getCookieValue||function(){return '';};"
		"rb.LangDynamic=rb.LangDynamic||{};rb.LangDynamicDefault=rb.LangDynamicDefault||{};rb.Lang=rb.Lang||{};"
		"function langFill(ns,m){var cur=rb.LangDynamic[ns]||rb.Lang[ns]||rb.LangDynamicDefault[ns]||{};for(var k in m)if(cur[k]===void 0)cur[k]=m[k];rb.LangDynamic[ns]=cur;return cur;}"
		"langFill('Feature.Landing',{'Action.LogIn':'Log In','Action.SignUp':'Sign Up','Action.Cancel':'Cancel','Action.Continue':'Continue','Label.Play':'Play','Label.About':'About','Label.Platforms':'Platforms','Heading.WhatIsRoblox':'What is Roblox?','Heading.LeavingRoblox':'You are leaving Roblox','Label.BrazilContentRatingLogoTitle':'ADVISORY RATING: 12 YEARS OLD','Label.BrazilContentRatingLogoSubtitle':'Online purchases','Label.ItalyContentRatingLogoTitle':'In-Experience Purchases','Description.ExternalWebsiteRedirect':'You will be redirected to an external website.'});"
		"langFill('Authentication.SignUp',{'Action.SignUp':'Sign Up','Action.LogIn':'Log In','Label.Username':'Username','Label.Password':'Password','Label.Birthday':'Birthday','Label.Month':'Month','Label.Day':'Day','Label.Year':'Year','Label.Gender':'Gender','Label.Female':'Female','Label.Male':'Male','Message.Username':'Username','Message.Password':'Password'});"
		"langFill('Authentication.Login',{'Action.LogIn':'Log In','Label.Username':'Username','Label.Password':'Password'});"
		"langFill('Authentication.AccountSwitch',{'Action.LogIn':'Log In','Action.SwitchAccount':'Switch Account'});"
		"langFill('Common.Captcha',{'Action.Verify':'Verify','Message.VerificationRequired':'Verification required'});"
		"langFill('CommonUI.Controls',{'Action.Ok':'OK','Action.Cancel':'Cancel','Action.Close':'Close','Action.Continue':'Continue'});"
		"rb.Lang.get=rb.Lang.get||function(k){return String(k||'');};"
		"rb.Lang.getTranslationResource=rb.Lang.getTranslationResource||function(n){return rb.LangDynamic[String(n||'')]||{};};"
		"rb.Lang.getResource=rb.Lang.getResource||rb.Lang.getTranslationResource;rb.Lang.translate=rb.Lang.translate||rb.Lang.get;"
		"rb.Thumbnails=rb.Thumbnails||{};rb.Thumbnails.Thumbnail2d=rb.Thumbnails.Thumbnail2d||function(){return null;};"
		"rb.Thumbnails.ThumbnailTypes=rb.Thumbnails.ThumbnailTypes||{gameIcon:'gameIcon',gameThumbnail:'gameThumbnail',assetThumbnail:'assetThumbnail',avatarHeadshot:'avatarHeadshot'};"
		"rb.Thumbnails.ThumbnailFormat=rb.Thumbnails.ThumbnailFormat||{jpeg:'jpeg',webp:'webp',png:'png'};"
		"rb.Thumbnails.DefaultThumbnailSize=rb.Thumbnails.DefaultThumbnailSize||'150x150';"
		"rb.Thumbnails.ThumbnailAvatarHeadshotSize=rb.Thumbnails.ThumbnailAvatarHeadshotSize||{size48:'48x48',size150:'150x150',size352:'352x352'};"
		"rb.Thumbnails.ThumbnailGameThumbnailSize=rb.Thumbnails.ThumbnailGameThumbnailSize||{width384:'384x216',width480:'480x270',width576:'576x324',width768:'768x432'};"
		"rb.Thumbnails.ThumbnailGameIconSize=rb.Thumbnails.ThumbnailGameIconSize||{size150:'150x150',size256:'256x256',size512:'512x512'};"
		"rb.Thumbnails.ThumbnailAssetsSize=rb.Thumbnails.ThumbnailAssetsSize||{size150:'150x150',size420:'420x420',size768:'768x432'};"
		"g.RobloxThumbnails=g.RobloxThumbnails||rb.Thumbnails;"
		"function rsgEl(tag){return function(p){var R=g.React,props={},kids=[],k;for(k in (p||{}))if(k!=='children')props[k]=p[k];if(p&&p.children!==void 0)kids=Array.isArray(p.children)?p.children:[p.children];return R&&R.createElement?R.createElement.apply(R,[tag,props].concat(kids)):null;};}"
		"g.ReactStyleGuide=g.ReactStyleGuide||{};g.ReactStyleGuide.Button=g.ReactStyleGuide.Button||rsgEl('button');"
		"g.ReactStyleGuide.Button.widths=g.ReactStyleGuide.Button.widths||{full:'full',large:'large',medium:'medium',small:'small'};"
		"g.ReactStyleGuide.Button.variants=g.ReactStyleGuide.Button.variants||{primary:'primary',secondary:'secondary',control:'control',alert:'alert',emphasis:'emphasis'};"
		"g.ReactStyleGuide.Link=g.ReactStyleGuide.Link||rsgEl('a');g.ReactStyleGuide.Loading=g.ReactStyleGuide.Loading||rsgEl('span');"
		"g.ReactStyleGuide.Modal=g.ReactStyleGuide.Modal||rsgEl('div');g.ReactStyleGuide.Alert=g.ReactStyleGuide.Alert||rsgEl('div');"
		"g.ReactStyleGuide.createModal=g.ReactStyleGuide.createModal||function(){var R=g.React;function M(p){return R&&R.createElement?R.createElement('div',p||{},p&&p.children||null):null;}return [M,{open:noop,close:noop}];};"
		"var rsgTags={Text:'span',Typography:'span',Heading:'h2',Box:'div',Stack:'div',Grid:'div',Card:'div',Paper:'div',Image:'img',IconButton:'button',FormGroup:'div',TextInput:'input',PasswordInput:'input',Select:'select',Checkbox:'input',Tooltip:'span'};"
		"for(var rsgK in rsgTags)g.ReactStyleGuide[rsgK]=g.ReactStyleGuide[rsgK]||rsgEl(rsgTags[rsgK]);"
		"rb.ui=rb.ui||{};rb.ui.theme=rb.ui.theme||{spacing:function(){return 0;},palette:{content:{static:{light:'#fff'}}}};"
		"rb.ui.makeStyles=rb.ui.makeStyles||function(s){function use(){return {};};if(arguments.length===0)return function(){return use;};return use;};"
		"rb.ui.useTheme=rb.ui.useTheme||function(){return rb.ui.theme;};"
		"rb.ui.createCache=rb.ui.createCache||function(){return {};};rb.ui.createTheme=rb.ui.createTheme||function(t){return t||rb.ui.theme;};"
		"rb.ui.styled=rb.ui.styled||function(tag){return function(){return rsgEl(tag||'div');};};"
		"rb.ui.withStyles=rb.ui.withStyles||function(){return function(C){return C;};};rb.ui.CacheProvider=rb.ui.CacheProvider||rsgEl('div');"
		"rb.ui.UIThemeProvider=rb.ui.UIThemeProvider||rsgEl('div');rb.ui.ThemeProvider=rb.ui.ThemeProvider||rsgEl('div');"
		"for(var uiK in rsgTags)rb.ui[uiK]=rb.ui[uiK]||g.ReactStyleGuide[uiK]||rsgEl(rsgTags[uiK]);"
		"rb.ui.Button=rb.ui.Button||g.ReactStyleGuide.Button;rb.ui.Link=rb.ui.Link||g.ReactStyleGuide.Link;rb.ui.Modal=rb.ui.Modal||g.ReactStyleGuide.Modal;rb.ui.Alert=rb.ui.Alert||g.ReactStyleGuide.Alert;"
		"g.WebBlox=g.WebBlox||rb.ui;"
		"var d=g.document=g.document||{};Object.setPrototypeOf&&Object.setPrototypeOf(d,Document.prototype);"
		"d.nodeType=9;d.readyState='complete';d.cookie=d.cookie||'';d.referrer=d.referrer||'';"
		"d.compatMode=d.compatMode||'CSS1Compat';"
		"var fallbackEl=null;function fallback(){if(!fallbackEl){fallbackEl=el('div');fallbackEl.appendChild(el('span'));}return fallbackEl;}"
		"d.createElement=function(t){return el(t);};d.createTextNode=function(t){return textNode(t);};"
		"d.createDocumentFragment=function(){var f=el('fragment');f.nodeType=11;f.nodeName='#document-fragment';f.tagName='';return f;};"
		"d.createEvent=function(t){return {type:String(t||''),target:null,currentTarget:null,defaultPrevented:false,"
		"initEvent:function(n,b,c){this.type=String(n||this.type);this.bubbles=!!b;this.cancelable=!!c;},"
		"preventDefault:function(){this.defaultPrevented=true;},stopPropagation:noop,stopImmediatePropagation:noop};};"
		"d.implementation=d.implementation||{};"
		"d.implementation.hasFeature=d.implementation.hasFeature||function(){return true;};"
		"d.implementation.createHTMLDocument=d.implementation.createHTMLDocument||function(){var x={};Object.setPrototypeOf&&Object.setPrototypeOf(x,Document.prototype);x.createElement=d.createElement;x.createTextNode=d.createTextNode;x.createDocumentFragment=d.createDocumentFragment;x.createEvent=d.createEvent;x.head=el('head');x.body=el('body');x.documentElement=el('html');x.defaultView=g;x[0]=x;x.length=1;x.item=function(i){return i===0?x:null;};x.documentElement.ownerDocument=x;x.head.ownerDocument=x;x.body.ownerDocument=x;return x;};"
		"d.getElementById=function(){return fallback();};d.querySelector=function(){return fallback();};"
		"d.querySelectorAll=arr;d.getElementsByTagName=function(t){var a=arr();t=String(t||'').toLowerCase();if(t==='head')a.push(d.head);else if(t==='body')a.push(d.body);return a;};"
		"d.getElementsByClassName=arr;d.addEventListener=g.addEventListener;d.removeEventListener=g.removeEventListener;"
		"d.documentElement=d.documentElement||el('html');d.head=d.head||el('head');d.body=d.body||el('body');"
		"d.defaultView=g;d[0]=d;d.length=1;d.item=function(i){return i===0?d:null;};"
		"d.documentElement.ownerDocument=d;d.head.ownerDocument=d;d.body.ownerDocument=d;"
		"d.documentElement.clientWidth=d.documentElement.clientWidth||g.innerWidth;d.documentElement.clientHeight=d.documentElement.clientHeight||g.innerHeight;"
		"d.body.clientWidth=d.body.clientWidth||g.innerWidth;d.body.clientHeight=d.body.clientHeight||g.innerHeight;"
		"d.documentElement.appendChild(d.head);d.documentElement.appendChild(d.body);"
		"g.getComputedStyle=g.getComputedStyle||function(e){return (e&&e.style)||css();};"
		"g.history=g.history||{pushState:noop,replaceState:noop,back:noop,forward:noop};"
		"})(globalThis);";
	JSValue result = JS_Eval(ctx, bootstrap, sizeof(bootstrap) - 1u,
				 "leonos-quickjs-browser-bootstrap.js",
				 JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(result)) {
		qjs_dump_exception(ctx, "leonos-quickjs-browser-bootstrap.js");
	}
	JS_FreeValue(ctx, result);
}

static void qjs_install_globals(JSContext *ctx)
{
	JSValue global = JS_GetGlobalObject(ctx);
	JSValue console = JS_NewObject(ctx);
	JSValue navigator = JS_NewObject(ctx);
	JSValue document = JS_NewObject(ctx);
	JSValue location = JS_NewObject(ctx);

	JS_SetPropertyStr(ctx, global, "window", JS_DupValue(ctx, global));
	JS_SetPropertyStr(ctx, global, "self", JS_DupValue(ctx, global));
	JS_SetPropertyStr(ctx, global, "globalThis", JS_DupValue(ctx, global));

	qjs_install_function(ctx, console, "log", qjs_console_log, 1);
	qjs_install_function(ctx, console, "warn", qjs_console_log, 1);
	qjs_install_function(ctx, console, "error", qjs_console_log, 1);
	JS_SetPropertyStr(ctx, global, "console", console);

	qjs_set_string(ctx, navigator, "userAgent",
		       "Mozilla/5.0 (LeonOS; i386) QuickJS-NetSurf");
	qjs_set_string(ctx, navigator, "appName", "Netscape");
	qjs_set_string(ctx, navigator, "appVersion",
		       "5.0 (LeonOS; i386) QuickJS-NetSurf");
	qjs_set_string(ctx, navigator, "language", "en-US");
	qjs_set_string(ctx, navigator, "platform", "Win32");
	qjs_set_string(ctx, navigator, "product", "Gecko");
	JS_SetPropertyStr(ctx, navigator, "maxTouchPoints", JS_NewInt32(ctx, 0));
	JS_SetPropertyStr(ctx, global, "navigator", navigator);

	qjs_set_location_from_thread(ctx, location);
	qjs_install_function(ctx, location, "toString", qjs_location_to_string, 0);

	JS_SetPropertyStr(ctx, document, "__leonos_dom_bound",
			  JS_NewBool(ctx, false));
	JS_SetPropertyStr(ctx, document, "location", JS_DupValue(ctx, location));
	{
		JSValue href = JS_GetPropertyStr(ctx, location, "href");
		JS_SetPropertyStr(ctx, document, "URL", JS_DupValue(ctx, href));
		JS_SetPropertyStr(ctx, document, "documentURI", href);
	}
	qjs_install_function(ctx, document, "createElement", qjs_dom_unsupported, 1);
	qjs_install_function(ctx, document, "createElementNS",
			     qjs_dom_unsupported, 2);
	qjs_install_function(ctx, document, "createDocumentFragment",
			     qjs_dom_unsupported, 0);
	qjs_install_function(ctx, document, "querySelector", qjs_dom_unsupported, 1);
	qjs_install_function(ctx, document, "querySelectorAll", qjs_dom_unsupported, 1);
	qjs_install_function(ctx, document, "getElementById", qjs_dom_unsupported, 1);
	JS_SetPropertyStr(ctx, global, "location", location);
	JS_SetPropertyStr(ctx, global, "document", document);

	JS_FreeValue(ctx, global);
	qjs_install_browser_bootstrap(ctx);
	global = JS_GetGlobalObject(ctx);
	document = JS_GetPropertyStr(ctx, global, "document");
	qjs_install_function(ctx, document, "createElement",
			     qjs_document_create_element, 1);
	qjs_install_function(ctx, document, "createElementNS",
			     qjs_document_create_element_ns, 2);
	qjs_install_function(ctx, document, "createTextNode",
			     qjs_document_create_text_node, 1);
	qjs_install_function(ctx, document, "createDocumentFragment",
			     qjs_document_create_document_fragment, 0);
	qjs_install_function(ctx, document, "getElementById",
			     qjs_document_get_element_by_id, 1);
	qjs_install_function(ctx, document, "querySelector",
			     qjs_document_query_selector, 1);
	qjs_install_function(ctx, document, "querySelectorAll",
			     qjs_document_query_selector_all, 1);
	qjs_install_function(ctx, document, "getElementsByTagName",
			     qjs_document_get_elements_by_tag_name, 1);
	qjs_install_function(ctx, document, "getElementsByClassName",
			     qjs_document_get_elements_by_class_name, 1);
	qjs_document_bind_real_nodes(ctx, document);
	JS_FreeValue(ctx, document);
	JS_FreeValue(ctx, global);
}

static void qjs_dump_exception(JSContext *ctx, const char *name)
{
	JSValue exception = JS_GetException(ctx);
	JSValue stack = JS_GetPropertyStr(ctx, exception, "stack");
	const char *message = JS_ToCString(ctx, exception);
	const char *stack_text = JS_ToCString(ctx, stack);
#ifdef LEONOS_USER_APP
	leonos_write("NETSURF QUICKJS EXCEPTION ");
	leonos_write(name != NULL ? name : "?script?");
	leonos_write(" ");
	leonos_write(message != NULL ? message : "(no message)");
	leonos_write("\r\n");
	if (stack_text != NULL && stack_text[0] != 0) {
		char preview[241];
		size_t i = 0u;
		for (; stack_text[i] != 0 && i + 1u < sizeof(preview); i++) {
			char ch = stack_text[i];
			if (ch == '\r' || ch == '\n' || ch == '\t') {
				ch = ' ';
			}
			if ((unsigned char) ch < 32u ||
			    (unsigned char) ch > 126u) {
				ch = '.';
			}
			preview[i] = ch;
		}
		preview[i] = 0;
		leonos_write("NETSURF QUICKJS STACK ");
		leonos_write(name != NULL ? name : "?script?");
		leonos_write(" ");
		leonos_write(preview);
		leonos_write("\r\n");
	}
#else
	(void) fprintf(stderr, "QuickJS exception in %s: %s\n",
		       name != NULL ? name : "?script?",
		       message != NULL ? message : "(no message)");
#endif
	JS_FreeCString(ctx, stack_text);
	JS_FreeCString(ctx, message);
	JS_FreeValue(ctx, stack);
	JS_FreeValue(ctx, exception);
}

static void qjs_dump_source_preview(const uint8_t *txt, size_t txtlen,
				    const char *name)
{
#ifdef LEONOS_USER_APP
	char detail[64];
	char preview[193];
	char bytes[193];
	size_t limit = txtlen < 160u ? txtlen : 160u;
	size_t out = 0u;
	size_t byte_limit = txtlen < 32u ? txtlen : 32u;
	size_t byte_out = 0u;
	int detail_len;
	static const char hex[] = "0123456789abcdef";
	for (size_t i = 0u; i < limit && out + 1u < sizeof(preview); i++) {
		char ch = (char) txt[i];
		if (ch == '\r' || ch == '\n' || ch == '\t') {
			ch = ' ';
		}
		if ((unsigned char) ch < 32u || (unsigned char) ch > 126u) {
			ch = '.';
		}
		preview[out++] = ch;
	}
	preview[out] = 0;
	for (size_t i = 0u; i < byte_limit && byte_out + 3u < sizeof(bytes);
	     i++) {
		uint8_t ch = txt[i];
		bytes[byte_out++] = hex[(ch >> 4) & 0x0f];
		bytes[byte_out++] = hex[ch & 0x0f];
		bytes[byte_out++] = ' ';
	}
	if (byte_out != 0u) {
		bytes[byte_out - 1u] = 0;
	} else {
		bytes[0] = 0;
	}
	leonos_write("NETSURF QUICKJS SOURCE ");
	leonos_write(name != NULL ? name : "?script?");
	detail_len = snprintf(detail, sizeof(detail), " len=%u ",
			(unsigned int) txtlen);
	if (detail_len > 0) {
		leonos_write(detail);
	}
	leonos_write(preview);
	leonos_write("\r\n");
	leonos_write("NETSURF QUICKJS SOURCE HEX ");
	leonos_write(name != NULL ? name : "?script?");
	leonos_write(" ");
	leonos_write(bytes);
	leonos_write("\r\n");
	if (txtlen > byte_limit) {
		byte_out = 0u;
		for (size_t i = txtlen - byte_limit; i < txtlen &&
		     byte_out + 3u < sizeof(bytes); i++) {
			uint8_t ch = txt[i];
			bytes[byte_out++] = hex[(ch >> 4) & 0x0f];
			bytes[byte_out++] = hex[ch & 0x0f];
			bytes[byte_out++] = ' ';
		}
		if (byte_out != 0u) {
			bytes[byte_out - 1u] = 0;
		} else {
			bytes[0] = 0;
		}
		leonos_write("NETSURF QUICKJS SOURCE TAIL ");
		leonos_write(name != NULL ? name : "?script?");
		leonos_write(" ");
		leonos_write(bytes);
		leonos_write("\r\n");
	}
#else
	(void) txt;
	(void) txtlen;
	(void) name;
#endif
}

void js_initialise(void)
{
	javascript_init();
}

void js_finalise(void)
{
}

nserror js_newheap(int timeout, jsheap **heap)
{
	jsheap *ret = calloc(1, sizeof(*ret));
	*heap = NULL;
	if (ret == NULL) {
		return NSERROR_NOMEM;
	}
	ret->runtime = JS_NewRuntime();
	if (ret->runtime == NULL) {
		free(ret);
		return NSERROR_NOMEM;
	}
	if (!qjs_register_dom_node_class(ret->runtime)) {
		JS_FreeRuntime(ret->runtime);
		free(ret);
		return NSERROR_NOMEM;
	}
	ret->timeout = timeout;
	JS_SetMemoryLimit(ret->runtime, 16u * 1024u * 1024u);
	JS_SetGCThreshold(ret->runtime, 4u * 1024u * 1024u);
	JS_SetInterruptHandler(ret->runtime, qjs_interrupt_handler, ret);
	*heap = ret;
#ifdef LEONOS_USER_APP
	leonos_write("NETSURF QUICKJS CORE HEAP\r\n");
#endif
	return NSERROR_OK;
}

void js_destroyheap(jsheap *heap)
{
	if (heap == NULL) {
		return;
	}
	heap->pending_destroy = true;
	if (heap->live_threads == 0) {
		JS_FreeRuntime(heap->runtime);
		free(heap);
	}
}

nserror js_newthread(jsheap *heap, void *win_priv, void *doc_priv,
		     jsthread **thread)
{
	jsthread *ret;
	(void) win_priv;
	(void) doc_priv;
	*thread = NULL;
	if (heap == NULL || heap->pending_destroy) {
		return NSERROR_BAD_PARAMETER;
	}
	ret = calloc(1, sizeof(*ret));
	if (ret == NULL) {
		return NSERROR_NOMEM;
	}
	ret->heap = heap;
	ret->htmlc = (html_content *) doc_priv;
	ret->ctx = JS_NewContext(heap->runtime);
	if (ret->ctx == NULL) {
		free(ret);
		return NSERROR_NOMEM;
	}
	JS_SetContextOpaque(ret->ctx, ret);
	qjs_install_globals(ret->ctx);
	heap->live_threads += 1;
	*thread = ret;
#ifdef LEONOS_USER_APP
	leonos_write("NETSURF QUICKJS CORE THREAD DOM_BINDINGS=0\r\n");
#endif
	return NSERROR_OK;
}

nserror js_closethread(jsthread *thread)
{
	if (thread != NULL) {
		thread->closed = true;
	}
	return NSERROR_OK;
}

void js_destroythread(jsthread *thread)
{
	jsheap *heap;
	if (thread == NULL) {
		return;
	}
	heap = thread->heap;
	if (thread->ctx != NULL) {
		JS_FreeContext(thread->ctx);
	}
	free(thread);
	if (heap != NULL && heap->live_threads != 0) {
		heap->live_threads -= 1;
		if (heap->pending_destroy && heap->live_threads == 0) {
			JS_FreeRuntime(heap->runtime);
			free(heap);
		}
	}
}

bool js_exec(jsthread *thread, const uint8_t *txt, size_t txtlen,
	     const char *name)
{
	JSValue result;
	const uint8_t *source = txt;
	const char *eval_text;
	char *eval_copy = NULL;
	size_t source_len = txtlen;
	size_t eval_len;
	size_t trailing = 0u;
	bool had_bom = false;
	if (thread == NULL || thread->ctx == NULL || thread->closed) {
		return false;
	}
	if (txt == NULL) {
		return false;
	}
	if (source_len >= 3u && source[0] == 0xefu &&
	    source[1] == 0xbbu && source[2] == 0xbfu) {
		source += 3u;
		source_len -= 3u;
		had_bom = true;
	}
	while (source_len != 0u &&
	       (source[source_len - 1u] == 0u ||
		source[source_len - 1u] == 0x1au)) {
		source_len -= 1u;
		trailing += 1u;
	}
	if (source_len >= 4u && source_len <= 6u &&
	    memcmp(source, "true", 4u) == 0 &&
	    (source_len == 4u || !qjs_is_identifier_char(source[4]))) {
		eval_text = "true";
		eval_len = 4u;
#ifdef LEONOS_USER_APP
		leonos_write("NETSURF QUICKJS NORMALIZE true-token\r\n");
#endif
	} else if (source_len >= 5u && source_len <= 7u &&
	    memcmp(source, "false", 5u) == 0 &&
	    (source_len == 5u || !qjs_is_identifier_char(source[5]))) {
		eval_text = "false";
		eval_len = 5u;
#ifdef LEONOS_USER_APP
		leonos_write("NETSURF QUICKJS NORMALIZE false-token\r\n");
#endif
	} else {
		eval_copy = malloc(source_len + 1u);
		if (eval_copy == NULL) {
#ifdef LEONOS_USER_APP
			leonos_write("NETSURF QUICKJS EVAL COPY OOM\r\n");
#endif
			return false;
		}
		if (source_len != 0u) {
			memcpy(eval_copy, source, source_len);
		}
		eval_copy[source_len] = 0;
		eval_text = eval_copy;
		eval_len = source_len;
	}
#ifdef LEONOS_USER_APP
	if (had_bom || trailing != 0u || eval_len != txtlen) {
		char detail[80];
		int detail_len = snprintf(detail, sizeof(detail),
			"NETSURF QUICKJS PREP len=%u eval=%u trim=%u bom=%u\r\n",
			(unsigned int) txtlen,
			(unsigned int) eval_len,
			(unsigned int) trailing,
			had_bom ? 1u : 0u);
		if (detail_len > 0) {
			leonos_write(detail);
		}
	}
#endif
	if (qjs_should_skip_blocking_script(name, eval_text, eval_len)) {
#ifdef LEONOS_USER_APP
		char detail[40];
		int detail_len;
		leonos_write("NETSURF QUICKJS SKIP ");
		leonos_write(name != NULL ? name : "?script?");
		detail_len = snprintf(detail, sizeof(detail),
			" bytes=%u\r\n", (unsigned int) eval_len);
		if (detail_len > 0) {
			leonos_write(detail);
		}
#endif
		free(eval_copy);
		return true;
	}
#ifdef LEONOS_USER_APP
	leonos_write("NETSURF QUICKJS EXEC ");
	leonos_write(name != NULL ? name : "?script?");
	leonos_write("\r\n");
#endif
	qjs_begin_script_interrupt(thread->heap, eval_len, name);
	result = JS_Eval(thread->ctx, eval_text, eval_len,
			 name != NULL ? name : "inline-script",
			 JS_EVAL_TYPE_GLOBAL);
	uint32_t interrupt_limit = thread->heap != NULL ?
		thread->heap->interrupt_limit : 0u;
	bool interrupted = qjs_end_script_interrupt(thread->heap);
	if (JS_IsException(result)) {
#ifdef LEONOS_USER_APP
		if (interrupted) {
			char detail[320];
			int detail_len = snprintf(detail, sizeof(detail),
				"NETSURF QUICKJS TIMEOUT %s budget=%u\r\n",
				name != NULL ? name : "?script?",
				(unsigned int) interrupt_limit);
			if (detail_len > 0) {
				leonos_write(detail);
			}
		}
#endif
		qjs_dump_exception(thread->ctx, name);
		qjs_dump_source_preview((const uint8_t *) eval_text, eval_len, name);
		JS_FreeValue(thread->ctx, result);
		free(eval_copy);
		return false;
	}
	JS_FreeValue(thread->ctx, result);
	free(eval_copy);
	return true;
}

bool js_fire_event(jsthread *thread, const char *type,
		   struct dom_document *doc, struct dom_node *target)
{
	(void) thread;
	(void) type;
	(void) doc;
	(void) target;
	return true;
}

bool js_dom_event_add_listener(jsthread *thread, struct dom_document *document,
			       struct dom_node *node,
			       struct dom_string *event_type_dom,
			       void *js_funcval)
{
	(void) thread;
	(void) document;
	(void) node;
	(void) event_type_dom;
	(void) js_funcval;
	return false;
}

void js_handle_new_element(jsthread *thread, struct dom_element *node)
{
	(void) thread;
	(void) node;
}

void js_event_cleanup(jsthread *thread, struct dom_event *evt)
{
	(void) thread;
	(void) evt;
}
