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
extern void monkey_window_process_pending_redraws(void) __attribute__((weak));
#endif

#define LEONOS_QUICKJS_FETCH_MAX (512u * 1024u)
#define LEONOS_QUICKJS_JOB_LIMIT 128u

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
	const char *active_script_name;
	unsigned int active_script_dom_appends;
	bool active_script_dom_budget_hit;
	bool active_script_dom_native_budget_hit;
	bool active_script_react_landing_attached;
	bool selector_selftest_ran;
	dom_node *active_script_react_landing_candidate;
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
static JSValue qjs_document_query_selector_all(JSContext *ctx,
					       JSValueConst this_val,
					       int argc,
					       JSValueConst *argv);
static JSValue qjs_document_query_selector(JSContext *ctx,
					   JSValueConst this_val,
					   int argc,
					   JSValueConst *argv);
static JSValue qjs_document_get_elements_by_tag_name(JSContext *ctx,
						     JSValueConst this_val,
						     int argc,
						     JSValueConst *argv);
static JSValue qjs_document_get_elements_by_class_name(JSContext *ctx,
						       JSValueConst this_val,
						       int argc,
						       JSValueConst *argv);
static JSValue qjs_document_get_elements_by_name(JSContext *ctx,
						 JSValueConst this_val,
						 int argc,
						 JSValueConst *argv);
static JSValue qjs_native_matches(JSContext *ctx, JSValueConst this_val,
				  int argc, JSValueConst *argv);
static JSValue qjs_native_closest(JSContext *ctx, JSValueConst this_val,
				  int argc, JSValueConst *argv);
static uint32_t qjs_array_length(JSContext *ctx, JSValueConst array);
static void qjs_sync_native_subtree_props(JSContext *ctx, JSValueConst obj);

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
	    strstr(name, "js.rbxcdn.com/") != NULL &&
	    (strstr(name, "SearchLandingPage.") != NULL ||
	     strstr(name, "AngularJsUtilities.") != NULL ||
	     strstr(name, "CoreUtilities.") != NULL ||
	     strstr(name, "Thumbnails.") != NULL ||
	     strstr(name, "GameCarousel.") != NULL ||
	     strstr(name, "ReactLanding.") != NULL ||
	     strstr(name, "ReactStyleGuide.") != NULL ||
	     strstr(name, "63b59480fef503ff6648900d1051bae7531757a38ce24f77587552fca279d16c") != NULL) &&
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
	     strstr(name, "3756ad214dde52cb58a1300177547475") != NULL ||
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

static void qjs_process_pending_redraws(void)
{
#ifdef LEONOS_USER_APP
	if (monkey_window_process_pending_redraws != NULL) {
		monkey_window_process_pending_redraws();
	}
#endif
}

static void qjs_log_js_error_stack(JSContext *ctx, JSValueConst value)
{
	JSValue stack = JS_GetPropertyStr(ctx, value, "stack");
	const char *stack_text = JS_ToCString(ctx, stack);
	if (stack_text != NULL && stack_text[0] != 0) {
		char preview[513];
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
		qjs_log("NETSURF QUICKJS CONSOLE STACK");
		qjs_log(preview);
	}
	JS_FreeCString(ctx, stack_text);
	JS_FreeValue(ctx, stack);
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
		if (JS_IsObject(argv[i])) {
			qjs_log_js_error_stack(ctx, argv[i]);
		}
	}
	return JS_UNDEFINED;
}

static JSValue qjs_leonos_fetch_text(JSContext *ctx, JSValueConst this_val,
				     int argc, JSValueConst *argv)
{
	JSValue response;
	const char *url;
	const char *method = NULL;
	const char *request_body = NULL;
	const char *content_type = NULL;
	const char *accept = NULL;
	size_t request_body_len = 0u;
	bool ok = false;
	(void) this_val;

	response = JS_NewObject(ctx);
	JS_SetPropertyStr(ctx, response, "ok", JS_NewBool(ctx, false));
	JS_SetPropertyStr(ctx, response, "status", JS_NewInt32(ctx, 0));
	JS_SetPropertyStr(ctx, response, "contentType", JS_NewString(ctx, ""));
	JS_SetPropertyStr(ctx, response, "body", JS_NewString(ctx, ""));
	if (argc < 1) {
		return response;
	}

	url = JS_ToCString(ctx, argv[0]);
	if (url == NULL) {
		return response;
	}
	if (argc >= 2 && !JS_IsUndefined(argv[1]) && !JS_IsNull(argv[1])) {
		method = JS_ToCString(ctx, argv[1]);
		if (method == NULL) {
			JS_FreeCString(ctx, url);
			return response;
		}
	}
	if (argc >= 3 && !JS_IsUndefined(argv[2]) && !JS_IsNull(argv[2])) {
		request_body = JS_ToCStringLen(ctx, &request_body_len, argv[2]);
		if (request_body == NULL) {
			if (method != NULL) {
				JS_FreeCString(ctx, method);
			}
			JS_FreeCString(ctx, url);
			return response;
		}
	}
	if (argc >= 4 && !JS_IsUndefined(argv[3]) && !JS_IsNull(argv[3])) {
		content_type = JS_ToCString(ctx, argv[3]);
		if (content_type == NULL) {
			if (request_body != NULL) {
				JS_FreeCString(ctx, request_body);
			}
			if (method != NULL) {
				JS_FreeCString(ctx, method);
			}
			JS_FreeCString(ctx, url);
			return response;
		}
	}
	if (argc >= 5 && !JS_IsUndefined(argv[4]) && !JS_IsNull(argv[4])) {
		accept = JS_ToCString(ctx, argv[4]);
		if (accept == NULL) {
			if (content_type != NULL) {
				JS_FreeCString(ctx, content_type);
			}
			if (request_body != NULL) {
				JS_FreeCString(ctx, request_body);
			}
			if (method != NULL) {
				JS_FreeCString(ctx, method);
			}
			JS_FreeCString(ctx, url);
			return response;
		}
	}
	JS_SetPropertyStr(ctx, response, "url", JS_NewString(ctx, url));
#ifdef LEONOS_USER_APP
	if (strncmp(url, "https://", 8) != 0) {
		leonos_write("NETSURF QUICKJS FETCH blocked non-https\r\n");
		if (accept != NULL) {
			JS_FreeCString(ctx, accept);
		}
		if (content_type != NULL) {
			JS_FreeCString(ctx, content_type);
		}
		if (request_body != NULL) {
			JS_FreeCString(ctx, request_body);
		}
		if (method != NULL) {
			JS_FreeCString(ctx, method);
		}
		JS_FreeCString(ctx, url);
		return response;
	}
	char *body = malloc(LEONOS_QUICKJS_FETCH_MAX + 1u);
	struct leonos_net_fetch_meta meta;
	struct leonos_net_fetch_request request;
	uint32_t got;
	if (body == NULL) {
		leonos_write("NETSURF QUICKJS FETCH oom\r\n");
		if (accept != NULL) {
			JS_FreeCString(ctx, accept);
		}
		if (content_type != NULL) {
			JS_FreeCString(ctx, content_type);
		}
		if (request_body != NULL) {
			JS_FreeCString(ctx, request_body);
		}
		if (method != NULL) {
			JS_FreeCString(ctx, method);
		}
		JS_FreeCString(ctx, url);
		return response;
	}
	leonos_write("NETSURF QUICKJS FETCH begin ");
	leonos_write(url);
	leonos_write(" method ");
	leonos_write(method != NULL ? method : "GET");
	if (request_body_len != 0u) {
		char detail[64];
		int detail_len = snprintf(detail, sizeof(detail),
			" body=%u", (unsigned int) request_body_len);
		if (detail_len > 0) {
			leonos_write(detail);
		}
	}
	leonos_write("\r\n");
	memset(&request, 0, sizeof(request));
	request.url = url;
	request.buffer = body;
	request.max_len = LEONOS_QUICKJS_FETCH_MAX;
	request.method = method != NULL ? method : "GET";
	request.body = request_body;
	request.body_len = request_body_len > UINT32_MAX ?
		UINT32_MAX : (uint32_t) request_body_len;
	request.content_type = content_type != NULL ? content_type : "";
	request.accept = accept != NULL ? accept : "*/*";
	got = leonos_net_fetch_ex(&request);
	memset(&meta, 0, sizeof(meta));
	(void) leonos_net_fetch_meta(&meta);
	if (got > LEONOS_QUICKJS_FETCH_MAX) {
		got = LEONOS_QUICKJS_FETCH_MAX;
	}
	body[got] = 0;
	ok = meta.status_code >= 200u && meta.status_code < 400u && got != 0u;
	{
		char detail[128];
		int detail_len = snprintf(detail, sizeof(detail),
			"NETSURF QUICKJS FETCH result status=%u bytes=%u type=%s\r\n",
			(unsigned int) meta.status_code,
			(unsigned int) got,
			meta.content_type[0] != 0 ? meta.content_type : "unknown");
		if (detail_len > 0) {
			leonos_write(detail);
		}
	}
	JS_SetPropertyStr(ctx, response, "ok", JS_NewBool(ctx, ok));
	JS_SetPropertyStr(ctx, response, "status",
			  JS_NewInt32(ctx, (int32_t) meta.status_code));
	JS_SetPropertyStr(ctx, response, "contentType",
			  JS_NewString(ctx, meta.content_type));
	JS_SetPropertyStr(ctx, response, "body",
			  JS_NewStringLen(ctx, body, got));
	free(body);
#else
	(void) ok;
#endif
	if (accept != NULL) {
		JS_FreeCString(ctx, accept);
	}
	if (content_type != NULL) {
		JS_FreeCString(ctx, content_type);
	}
	if (request_body != NULL) {
		JS_FreeCString(ctx, request_body);
	}
	if (method != NULL) {
		JS_FreeCString(ctx, method);
	}
	JS_FreeCString(ctx, url);
	return response;
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

static JSValue qjs_dom_arg0(JSContext *ctx, JSValueConst this_val,
			    int argc, JSValueConst *argv)
{
	(void) this_val;
	if (argc < 1) {
		return JS_UNDEFINED;
	}
	return JS_DupValue(ctx, argv[0]);
}

static void qjs_dom_log_event_exception(JSContext *ctx)
{
	JSValue exception = JS_GetException(ctx);
	const char *message = JS_ToCString(ctx, exception);
	qjs_log("NETSURF QUICKJS EVENT EXCEPTION");
	if (message != NULL && message[0] != 0) {
		qjs_log(message);
	}
	if (JS_IsObject(exception)) {
		qjs_log_js_error_stack(ctx, exception);
	}
	JS_FreeCString(ctx, message);
	JS_FreeValue(ctx, exception);
}

static JSValue qjs_dom_event_listener_list(JSContext *ctx,
					   JSValueConst target,
					   const char *type,
					   bool create)
{
	JSValue listeners;
	JSValue list;
	if (type == NULL || type[0] == 0) {
		return JS_UNDEFINED;
	}
	listeners = JS_GetPropertyStr(ctx, target, "__leonosEventListeners");
	if (!JS_IsObject(listeners)) {
		JS_FreeValue(ctx, listeners);
		if (!create) {
			return JS_UNDEFINED;
		}
		listeners = JS_NewObject(ctx);
		JS_SetPropertyStr(ctx, target, "__leonosEventListeners",
				  JS_DupValue(ctx, listeners));
	}
	list = JS_GetPropertyStr(ctx, listeners, type);
	if (!JS_IsObject(list)) {
		JS_FreeValue(ctx, list);
		if (!create) {
			JS_FreeValue(ctx, listeners);
			return JS_UNDEFINED;
		}
		list = JS_NewArray(ctx);
		JS_SetPropertyStr(ctx, listeners, type, JS_DupValue(ctx, list));
	}
	JS_FreeValue(ctx, listeners);
	return list;
}

static JSValue qjs_dom_add_event_listener(JSContext *ctx,
					  JSValueConst this_val,
					  int argc, JSValueConst *argv)
{
	const char *type;
	JSValue list;
	uint32_t length;
	if (argc < 2 || (!JS_IsFunction(ctx, argv[1]) &&
			 !JS_IsObject(argv[1]))) {
		return JS_UNDEFINED;
	}
	type = JS_ToCString(ctx, argv[0]);
	if (type == NULL) {
		return JS_EXCEPTION;
	}
#ifdef LEONOS_USER_APP
	if (strcmp(type, "click") == 0 ||
	    strcmp(type, "mouseenter") == 0 ||
	    strcmp(type, "mouseleave") == 0 ||
	    strcmp(type, "mouseover") == 0 ||
	    strcmp(type, "mouseout") == 0) {
		JS_FreeCString(ctx, type);
		return JS_UNDEFINED;
	}
#endif
	list = qjs_dom_event_listener_list(ctx, this_val, type, true);
	if (JS_IsObject(list)) {
		length = qjs_array_length(ctx, list);
		JS_SetPropertyUint32(ctx, list, length, JS_DupValue(ctx, argv[1]));
	}
	JS_FreeValue(ctx, list);
	JS_FreeCString(ctx, type);
	return JS_UNDEFINED;
}

static JSValue qjs_dom_remove_event_listener(JSContext *ctx,
					     JSValueConst this_val,
					     int argc, JSValueConst *argv)
{
	const char *type;
	JSValue list;
	uint32_t length;
	if (argc < 2) {
		return JS_UNDEFINED;
	}
	type = JS_ToCString(ctx, argv[0]);
	if (type == NULL) {
		return JS_EXCEPTION;
	}
	list = qjs_dom_event_listener_list(ctx, this_val, type, false);
	if (!JS_IsObject(list)) {
		JS_FreeValue(ctx, list);
		JS_FreeCString(ctx, type);
		return JS_UNDEFINED;
	}
	length = qjs_array_length(ctx, list);
	for (uint32_t i = 0u; i < length; i++) {
		JSValue item = JS_GetPropertyUint32(ctx, list, i);
		bool same = JS_StrictEq(ctx, item, argv[1]);
		JS_FreeValue(ctx, item);
		if (!same) {
			continue;
		}
		for (uint32_t j = i + 1u; j < length; j++) {
			item = JS_GetPropertyUint32(ctx, list, j);
			JS_SetPropertyUint32(ctx, list, j - 1u, item);
		}
		JS_SetPropertyStr(ctx, list, "length",
				  JS_NewUint32(ctx, length - 1u));
		break;
	}
	JS_FreeValue(ctx, list);
	JS_FreeCString(ctx, type);
	return JS_UNDEFINED;
}

static void qjs_dom_mark_event_prevented(JSContext *ctx, JSValueConst event)
{
	if (JS_IsObject(event)) {
		JS_SetPropertyStr(ctx, event, "defaultPrevented",
				  JS_NewBool(ctx, true));
	}
}

static void qjs_dom_call_event_listener(JSContext *ctx,
					JSValueConst target,
					JSValueConst listener,
					JSValueConst event)
{
	JSValue result = JS_UNDEFINED;
	if (JS_IsFunction(ctx, listener)) {
		result = JS_Call(ctx, listener, target, 1, &event);
	} else if (JS_IsObject(listener)) {
		JSValue handle = JS_GetPropertyStr(ctx, listener, "handleEvent");
		if (JS_IsFunction(ctx, handle)) {
			result = JS_Call(ctx, handle, listener, 1, &event);
		}
		JS_FreeValue(ctx, handle);
	}
	if (JS_IsException(result)) {
		qjs_dom_log_event_exception(ctx);
		return;
	}
	if (!JS_IsUndefined(result) && !JS_IsNull(result) &&
	    JS_ToBool(ctx, result) == 0) {
		qjs_dom_mark_event_prevented(ctx, event);
	}
	JS_FreeValue(ctx, result);
}

static JSValue qjs_dom_dispatch_event(JSContext *ctx, JSValueConst this_val,
				      int argc, JSValueConst *argv)
{
	JSValue event;
	JSValue type_value;
	JSValue list;
	JSValue default_prevented;
	char type_buf[96];
	const char *type_text;
	uint32_t length;
	bool allow_default = true;

	type_buf[0] = 0;
	if (argc >= 1 && JS_IsObject(argv[0])) {
		event = JS_DupValue(ctx, argv[0]);
		type_value = JS_GetPropertyStr(ctx, event, "type");
		type_text = JS_ToCString(ctx, type_value);
		if (type_text != NULL) {
			snprintf(type_buf, sizeof(type_buf), "%s", type_text);
			JS_FreeCString(ctx, type_text);
		}
		JS_FreeValue(ctx, type_value);
	} else {
		event = JS_NewObject(ctx);
		if (argc >= 1) {
			type_text = JS_ToCString(ctx, argv[0]);
			if (type_text != NULL) {
				snprintf(type_buf, sizeof(type_buf), "%s",
					 type_text);
				JS_FreeCString(ctx, type_text);
			}
		}
		JS_SetPropertyStr(ctx, event, "type",
				  JS_NewString(ctx, type_buf));
	}
	if (type_buf[0] == 0) {
		JS_FreeValue(ctx, event);
		return JS_NewBool(ctx, true);
	}
	{
		JSValue target = JS_GetPropertyStr(ctx, event, "target");
		if (JS_IsUndefined(target) || JS_IsNull(target)) {
			JS_SetPropertyStr(ctx, event, "target",
					  JS_DupValue(ctx, this_val));
		}
		JS_FreeValue(ctx, target);
	}
	JS_SetPropertyStr(ctx, event, "currentTarget",
			  JS_DupValue(ctx, this_val));
	default_prevented = JS_GetPropertyStr(ctx, event, "defaultPrevented");
	if (JS_IsUndefined(default_prevented) || JS_IsNull(default_prevented)) {
		JS_SetPropertyStr(ctx, event, "defaultPrevented",
				  JS_NewBool(ctx, false));
	}
	JS_FreeValue(ctx, default_prevented);
	{
		char handler_name[112];
		JSValue handler;
		snprintf(handler_name, sizeof(handler_name), "on%s", type_buf);
		handler = JS_GetPropertyStr(ctx, this_val, handler_name);
		qjs_dom_call_event_listener(ctx, this_val, handler, event);
		JS_FreeValue(ctx, handler);
	}
	list = qjs_dom_event_listener_list(ctx, this_val, type_buf, false);
	if (JS_IsObject(list)) {
		length = qjs_array_length(ctx, list);
		for (uint32_t i = 0u; i < length; i++) {
			JSValue listener = JS_GetPropertyUint32(ctx, list, i);
			qjs_dom_call_event_listener(ctx, this_val, listener,
						    event);
			JS_FreeValue(ctx, listener);
		}
	}
	JS_FreeValue(ctx, list);
	default_prevented = JS_GetPropertyStr(ctx, event, "defaultPrevented");
	if (JS_ToBool(ctx, default_prevented) > 0) {
		allow_default = false;
	}
	JS_FreeValue(ctx, default_prevented);
	JS_FreeValue(ctx, event);
	return JS_NewBool(ctx, allow_default);
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

static void qjs_delete_property_cstr(JSContext *ctx, JSValueConst obj,
				     const char *name)
{
	JSAtom atom;
	if (name == NULL || name[0] == 0) {
		return;
	}
	atom = JS_NewAtom(ctx, name);
	JS_DeleteProperty(ctx, obj, atom, 0);
	JS_FreeAtom(ctx, atom);
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

static JSValue qjs_basic_append_child(JSContext *ctx, JSValueConst this_val,
				      int argc, JSValueConst *argv);
static JSValue qjs_basic_insert_before(JSContext *ctx, JSValueConst this_val,
				       int argc, JSValueConst *argv);
static JSValue qjs_basic_remove_child(JSContext *ctx, JSValueConst this_val,
				      int argc, JSValueConst *argv);

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
	qjs_install_function(ctx, obj, "addEventListener",
			     qjs_dom_add_event_listener, 2);
	qjs_install_function(ctx, obj, "removeEventListener",
			     qjs_dom_remove_event_listener, 2);
	qjs_install_function(ctx, obj, "dispatchEvent",
			     qjs_dom_dispatch_event, 1);
	qjs_install_function(ctx, obj, "focus", qjs_dom_noop, 0);
	qjs_install_function(ctx, obj, "blur", qjs_dom_noop, 0);
	qjs_install_function(ctx, obj, "appendChild", qjs_basic_append_child, 1);
	qjs_install_function(ctx, obj, "insertBefore", qjs_basic_insert_before, 2);
	qjs_install_function(ctx, obj, "removeChild", qjs_basic_remove_child, 1);
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

static void qjs_remove_native_attribute_cstr(dom_node *node, const char *name)
{
	dom_string *attr_name = NULL;
	if (node == NULL || name == NULL || name[0] == 0 ||
	    qjs_native_node_type(node) != DOM_ELEMENT_NODE) {
		return;
	}
	if (dom_string_create((const uint8_t *) name, strlen(name),
			      &attr_name) != DOM_NO_ERR) {
		return;
	}
	(void) dom_element_remove_attribute((dom_element *) node, attr_name);
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

static void qjs_sync_native_string_attr(JSContext *ctx, JSValueConst obj,
					dom_node *node, const char *prop,
					const char *attr)
{
	JSValue value = JS_GetPropertyStr(ctx, obj, prop);
	char text[1024];
	qjs_copy_js_string(text, sizeof(text), ctx, value);
	JS_FreeValue(ctx, value);
	if (text[0] != 0) {
		qjs_set_native_attribute_cstr(node, attr, text);
	}
}

static void qjs_sync_native_bool_attr(JSContext *ctx, JSValueConst obj,
				      dom_node *node, const char *prop,
				      const char *attr)
{
	JSValue value = JS_GetPropertyStr(ctx, obj, prop);
	if (JS_IsUndefined(value) || JS_IsNull(value)) {
		JS_FreeValue(ctx, value);
		return;
	}
	if (JS_ToBool(ctx, value) > 0) {
		qjs_set_native_attribute_cstr(node, attr, attr);
	} else {
		qjs_remove_native_attribute_cstr(node, attr);
	}
	JS_FreeValue(ctx, value);
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
	qjs_sync_native_string_attr(ctx, obj, native->node, "src", "src");
	qjs_sync_native_string_attr(ctx, obj, native->node, "href", "href");
	qjs_sync_native_string_attr(ctx, obj, native->node, "alt", "alt");
	qjs_sync_native_string_attr(ctx, obj, native->node, "title", "title");
	qjs_sync_native_string_attr(ctx, obj, native->node, "type", "type");
	qjs_sync_native_string_attr(ctx, obj, native->node, "name", "name");
	qjs_sync_native_string_attr(ctx, obj, native->node, "value", "value");
	qjs_sync_native_string_attr(ctx, obj, native->node, "defaultValue",
				    "value");
	qjs_sync_native_string_attr(ctx, obj, native->node, "placeholder",
				    "placeholder");
	qjs_sync_native_string_attr(ctx, obj, native->node, "role", "role");
	qjs_sync_native_string_attr(ctx, obj, native->node, "htmlFor", "for");
	qjs_sync_native_string_attr(ctx, obj, native->node, "ariaLabel",
				    "aria-label");
	qjs_sync_native_bool_attr(ctx, obj, native->node, "disabled",
				  "disabled");
	qjs_sync_native_bool_attr(ctx, obj, native->node, "checked",
				  "checked");
	qjs_sync_native_bool_attr(ctx, obj, native->node, "selected",
				  "selected");
	qjs_sync_native_bool_attr(ctx, obj, native->node, "multiple",
				  "multiple");
	qjs_sync_native_bool_attr(ctx, obj, native->node, "required",
				  "required");
	qjs_sync_native_bool_attr(ctx, obj, native->node, "readOnly",
				  "readonly");
	qjs_sync_native_bool_attr(ctx, obj, native->node, "hidden",
				  "hidden");
	qjs_sync_style_attribute(ctx, obj, native->node);
}

static struct qjs_native_node *qjs_get_native_or_materialized_node(
		JSContext *ctx,
		JSValueConst value)
{
	struct qjs_native_node *native = qjs_get_native_node(value);
	JSValue materialized;
	if (native != NULL) {
		return native;
	}
	if (!JS_IsObject(value)) {
		return NULL;
	}
	materialized = JS_GetPropertyStr(ctx, value, "__leonosNativeNode");
	native = qjs_get_native_node(materialized);
	JS_FreeValue(ctx, materialized);
	return native;
}

static void qjs_store_materialized_native_node(JSContext *ctx,
					       JSValueConst value,
					       dom_node *node)
{
	JSValue wrapped;
	if (!JS_IsObject(value) || node == NULL ||
	    qjs_get_native_node(value) != NULL) {
		return;
	}
	wrapped = qjs_new_dom_node_limited(ctx, node, 0u, false);
	if (!JS_IsException(wrapped) && !JS_IsNull(wrapped)) {
		JS_SetPropertyStr(ctx, value, "__leonosNativeNode", wrapped);
	} else {
		JS_FreeValue(ctx, wrapped);
	}
}

static void qjs_materialize_sync_attributes(JSContext *ctx,
					    JSValueConst obj,
					    dom_node *node)
{
	JSValue attributes;
	JSPropertyEnum *props = NULL;
	uint32_t prop_count = 0u;
	if (node == NULL || qjs_native_node_type(node) != DOM_ELEMENT_NODE) {
		return;
	}
	attributes = JS_GetPropertyStr(ctx, obj, "attributes");
	if (!JS_IsObject(attributes)) {
		JS_FreeValue(ctx, attributes);
		return;
	}
	if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, attributes,
				   JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
		for (uint32_t i = 0u; i < prop_count; i++) {
			const char *name = JS_AtomToCString(ctx, props[i].atom);
			JSValue value;
			char text[1024];
			if (name == NULL || name[0] == 0) {
				JS_FreeCString(ctx, name);
				continue;
			}
			value = JS_GetProperty(ctx, attributes, props[i].atom);
			qjs_copy_js_string(text, sizeof(text), ctx, value);
			JS_FreeValue(ctx, value);
			if (text[0] != 0) {
				qjs_set_native_attribute_cstr(node, name, text);
			}
			JS_FreeCString(ctx, name);
		}
		JS_FreePropertyEnum(ctx, props, prop_count);
	}
	JS_FreeValue(ctx, attributes);
}

static void qjs_materialize_sync_element_props(JSContext *ctx,
					       JSValueConst obj,
					       dom_node *node)
{
	if (node == NULL || qjs_native_node_type(node) != DOM_ELEMENT_NODE) {
		return;
	}
	qjs_materialize_sync_attributes(ctx, obj, node);
	qjs_sync_native_string_attr(ctx, obj, node, "id", "id");
	qjs_sync_native_string_attr(ctx, obj, node, "className", "class");
	qjs_sync_native_string_attr(ctx, obj, node, "src", "src");
	qjs_sync_native_string_attr(ctx, obj, node, "href", "href");
	qjs_sync_native_string_attr(ctx, obj, node, "alt", "alt");
	qjs_sync_native_string_attr(ctx, obj, node, "title", "title");
	qjs_sync_native_string_attr(ctx, obj, node, "type", "type");
	qjs_sync_native_string_attr(ctx, obj, node, "name", "name");
	qjs_sync_native_string_attr(ctx, obj, node, "value", "value");
	qjs_sync_native_string_attr(ctx, obj, node, "defaultValue", "value");
	qjs_sync_native_string_attr(ctx, obj, node, "placeholder",
				    "placeholder");
	qjs_sync_native_string_attr(ctx, obj, node, "role", "role");
	qjs_sync_native_string_attr(ctx, obj, node, "htmlFor", "for");
	qjs_sync_native_string_attr(ctx, obj, node, "ariaLabel",
				    "aria-label");
	qjs_sync_native_bool_attr(ctx, obj, node, "disabled", "disabled");
	qjs_sync_native_bool_attr(ctx, obj, node, "checked", "checked");
	qjs_sync_native_bool_attr(ctx, obj, node, "selected", "selected");
	qjs_sync_native_bool_attr(ctx, obj, node, "multiple", "multiple");
	qjs_sync_native_bool_attr(ctx, obj, node, "required", "required");
	qjs_sync_native_bool_attr(ctx, obj, node, "readOnly", "readonly");
	qjs_sync_native_bool_attr(ctx, obj, node, "hidden", "hidden");
	qjs_sync_style_attribute(ctx, obj, node);
}

static dom_node *qjs_materialize_js_node_with_doc(JSContext *ctx,
						  JSValueConst value,
						  dom_document *document,
						  unsigned int depth)
{
	struct qjs_native_node *native;
	JSValue node_type_value;
	int32_t node_type = 0;
	if (document == NULL || depth > 96u ||
	    JS_IsUndefined(value) || JS_IsNull(value)) {
		return NULL;
	}
	native = qjs_get_native_or_materialized_node(ctx, value);
	if (native != NULL && native->node != NULL) {
		qjs_sync_native_subtree_props(ctx, value);
		return dom_node_ref(native->node);
	}
	if (!JS_IsObject(value)) {
		dom_string *text = NULL;
		dom_text *text_node = NULL;
		if (!qjs_dom_string_from_js(ctx, value, &text)) {
			return NULL;
		}
		if (dom_document_create_text_node(document, text,
						  &text_node) != DOM_NO_ERR) {
			dom_string_unref(text);
			return NULL;
		}
		dom_string_unref(text);
		return (dom_node *) text_node;
	}
	node_type_value = JS_GetPropertyStr(ctx, value, "nodeType");
	if (JS_IsUndefined(node_type_value) || JS_IsNull(node_type_value) ||
	    JS_ToInt32(ctx, &node_type, node_type_value) < 0) {
		JSValue tag = JS_GetPropertyStr(ctx, value, "tagName");
		char tag_text[32];
		qjs_copy_js_string(tag_text, sizeof(tag_text), ctx, tag);
		JS_FreeValue(ctx, tag);
		node_type = tag_text[0] != 0 ? DOM_ELEMENT_NODE : 0;
	}
	JS_FreeValue(ctx, node_type_value);
	if (node_type == DOM_TEXT_NODE) {
		dom_string *text = NULL;
		dom_text *text_node = NULL;
		JSValue text_value = JS_GetPropertyStr(ctx, value, "nodeValue");
		if (JS_IsUndefined(text_value) || JS_IsNull(text_value)) {
			JS_FreeValue(ctx, text_value);
			text_value = JS_GetPropertyStr(ctx, value, "textContent");
		}
		if (!qjs_dom_string_from_js(ctx, text_value, &text)) {
			JS_FreeValue(ctx, text_value);
			return NULL;
		}
		JS_FreeValue(ctx, text_value);
		if (dom_document_create_text_node(document, text,
						  &text_node) != DOM_NO_ERR) {
			dom_string_unref(text);
			return NULL;
		}
		dom_string_unref(text);
		qjs_store_materialized_native_node(ctx, value,
						   (dom_node *) text_node);
		return (dom_node *) text_node;
	}
	if (node_type == DOM_ELEMENT_NODE ||
	    node_type == DOM_DOCUMENT_FRAGMENT_NODE) {
		dom_node *node = NULL;
		JSValue child_nodes;
		uint32_t length = 0u;
		if (node_type == DOM_DOCUMENT_FRAGMENT_NODE) {
			dom_document_fragment *fragment = NULL;
			if (dom_document_create_document_fragment(document,
								  &fragment) !=
			    DOM_NO_ERR) {
				return NULL;
			}
			node = (dom_node *) fragment;
		} else {
			dom_string *tag = NULL;
			dom_element *element = NULL;
			JSValue tag_value = JS_GetPropertyStr(ctx, value,
							      "tagName");
			char tag_text[64];
			qjs_copy_js_string(tag_text, sizeof(tag_text), ctx,
					   tag_value);
			JS_FreeValue(ctx, tag_value);
			if (tag_text[0] == 0) {
				tag_value = JS_GetPropertyStr(ctx, value,
							      "nodeName");
				qjs_copy_js_string(tag_text, sizeof(tag_text),
						   ctx, tag_value);
				JS_FreeValue(ctx, tag_value);
			}
			if (tag_text[0] == 0 || tag_text[0] == '#') {
				qjs_copy_cstr(tag_text, sizeof(tag_text), "DIV");
			}
			if (dom_string_create((const uint8_t *) tag_text,
					      strlen(tag_text),
					      &tag) != DOM_NO_ERR) {
				return NULL;
			}
			if (dom_document_create_element(document, tag,
							&element) != DOM_NO_ERR) {
				dom_string_unref(tag);
				return NULL;
			}
			dom_string_unref(tag);
			node = (dom_node *) element;
			qjs_materialize_sync_element_props(ctx, value, node);
		}
		child_nodes = JS_GetPropertyStr(ctx, value, "childNodes");
		if (JS_IsObject(child_nodes)) {
			length = qjs_array_length(ctx, child_nodes);
		}
		if (node_type == DOM_ELEMENT_NODE && length == 0u) {
			JSValue text_value = JS_GetPropertyStr(ctx, value,
							       "textContent");
			char text[4096];
			qjs_copy_js_string(text, sizeof(text), ctx, text_value);
			JS_FreeValue(ctx, text_value);
			if (text[0] != 0) {
				dom_string *dom_text = NULL;
				if (dom_string_create((const uint8_t *) text,
						      strlen(text),
						      &dom_text) == DOM_NO_ERR) {
					(void) dom_node_set_text_content(node,
									 dom_text);
					dom_string_unref(dom_text);
				}
			}
		}
		for (uint32_t i = 0u; i < length; i++) {
			JSValue child = JS_GetPropertyUint32(ctx, child_nodes, i);
			dom_node *child_node =
				qjs_materialize_js_node_with_doc(ctx, child,
								 document,
								 depth + 1u);
			if (child_node != NULL) {
				dom_node *result = NULL;
				if (dom_node_append_child(node, child_node,
							  &result) == DOM_NO_ERR &&
				    result != NULL) {
					dom_node_unref(result);
				}
				dom_node_unref(child_node);
			}
			JS_FreeValue(ctx, child);
		}
		JS_FreeValue(ctx, child_nodes);
		qjs_store_materialized_native_node(ctx, value, node);
		return node;
	}
	return NULL;
}

static dom_node *qjs_materialize_js_node(JSContext *ctx, JSValueConst value)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	if (thread == NULL || thread->htmlc == NULL ||
	    thread->htmlc->document == NULL) {
		return NULL;
	}
	return qjs_materialize_js_node_with_doc(ctx, value,
						thread->htmlc->document, 0u);
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

static void qjs_refresh_js_select_options(JSContext *ctx,
					  JSValueConst parent,
					  JSValueConst child_nodes)
{
	JSValue tag = JS_GetPropertyStr(ctx, parent, "tagName");
	char tag_text[24];
	qjs_copy_js_string(tag_text, sizeof(tag_text), ctx, tag);
	JS_FreeValue(ctx, tag);
	if (qjs_ascii_equals_ci(tag_text, "SELECT")) {
		JS_SetPropertyStr(ctx, parent, "options",
				  JS_DupValue(ctx, child_nodes));
	}
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
	qjs_refresh_js_select_options(ctx, parent, child_nodes);
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
	qjs_refresh_js_select_options(ctx, parent, child_nodes);
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

static JSValue qjs_basic_append_child(JSContext *ctx, JSValueConst this_val,
				      int argc, JSValueConst *argv)
{
	if (argc < 1) {
		return JS_UNDEFINED;
	}
	if (qjs_js_node_is_document_fragment(ctx, argv[0])) {
		qjs_sync_js_fragment_insert_before(ctx, this_val, argv[0],
						   JS_NULL);
	} else {
		qjs_sync_js_child_append(ctx, this_val, argv[0]);
	}
	return JS_DupValue(ctx, argv[0]);
}

static JSValue qjs_basic_insert_before(JSContext *ctx, JSValueConst this_val,
				       int argc, JSValueConst *argv)
{
	JSValueConst ref = JS_NULL;

	if (argc < 1) {
		return JS_UNDEFINED;
	}
	if (argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
		ref = argv[1];
	}
	if (qjs_js_node_is_document_fragment(ctx, argv[0])) {
		qjs_sync_js_fragment_insert_before(ctx, this_val, argv[0], ref);
	} else {
		qjs_sync_js_child_insert_before(ctx, this_val, argv[0], ref);
	}
	return JS_DupValue(ctx, argv[0]);
}

static JSValue qjs_basic_remove_child(JSContext *ctx, JSValueConst this_val,
				      int argc, JSValueConst *argv)
{
	if (argc < 1) {
		return JS_UNDEFINED;
	}
	qjs_sync_js_child_remove(ctx, this_val, argv[0]);
	return JS_DupValue(ctx, argv[0]);
}

static void qjs_note_native_node_inserted(JSContext *ctx, dom_node *node)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	const char *name;
	if (thread == NULL || thread->htmlc == NULL || node == NULL) {
		return;
	}
	name = thread->active_script_name;
	if (name != NULL &&
	    strstr(name, "js.rbxcdn.com/") != NULL &&
	    strstr(name, "ReactLanding.") != NULL) {
		html_leonos_dom_mark_dirty(thread->htmlc);
#ifdef LEONOS_USER_APP
		if (!thread->active_script_dom_budget_hit) {
			leonos_write("NETSURF QUICKJS DOM REACT MUTATION BATCH\r\n");
			thread->active_script_dom_budget_hit = true;
		}
#endif
		return;
	}
	html_leonos_dom_node_inserted(thread->htmlc, node);
}

static void qjs_log_native_dom_mutation(JSContext *ctx, const char *op,
					dom_node *parent, dom_node *child)
{
#ifdef LEONOS_USER_APP
	jsthread *thread = JS_GetContextOpaque(ctx);
	dom_string *parent_name = NULL;
	dom_string *child_name = NULL;
	dom_string *parent_id = NULL;
	char parent_buf[64];
	char child_buf[64];
	char parent_id_buf[96];
	char detail[256];
	int detail_len;
	const char *name;
	if (thread == NULL || parent == NULL || child == NULL) {
		return;
	}
	if (thread->active_script_dom_appends > 24u &&
	    (thread->active_script_dom_appends &
	     (thread->active_script_dom_appends - 1u)) != 0u) {
		return;
	}
	name = thread->active_script_name;
	if (name == NULL ||
	    strstr(name, "js.rbxcdn.com/") == NULL ||
	    strstr(name, "ReactLanding.") == NULL) {
		return;
	}
	parent_buf[0] = 0;
	child_buf[0] = 0;
	parent_id_buf[0] = 0;
	if (dom_node_get_node_name(parent, &parent_name) == DOM_NO_ERR &&
	    parent_name != NULL) {
		qjs_copy_dom_string_cstr(parent_buf, sizeof(parent_buf),
					 parent_name);
		dom_string_unref(parent_name);
	}
	if (dom_node_get_node_name(child, &child_name) == DOM_NO_ERR &&
	    child_name != NULL) {
		qjs_copy_dom_string_cstr(child_buf, sizeof(child_buf),
					 child_name);
		dom_string_unref(child_name);
	}
	if (qjs_native_node_type(parent) == DOM_ELEMENT_NODE) {
		qjs_read_dom_attribute((dom_element *) parent, "id",
				       &parent_id);
		if (parent_id != NULL) {
			qjs_copy_dom_string_cstr(parent_id_buf,
						 sizeof(parent_id_buf),
						 parent_id);
			dom_string_unref(parent_id);
		}
	}
	detail_len = snprintf(detail, sizeof(detail),
		"NETSURF QUICKJS DOM %s native COUNT %u PARENT %s%s%s CHILD %s\r\n",
		op != NULL ? op : "mutate",
		thread->active_script_dom_appends,
		parent_buf[0] != 0 ? parent_buf : "?",
		parent_id_buf[0] != 0 ? "#" : "",
		parent_id_buf,
		child_buf[0] != 0 ? child_buf : "?");
	if (detail_len > 0) {
		leonos_write(detail);
	}
#else
	(void) ctx;
	(void) op;
	(void) parent;
	(void) child;
#endif
}

static bool qjs_native_node_id_equals(dom_node *node, const char *id)
{
	dom_string *value = NULL;
	char text[128];
	bool match = false;
	if (node == NULL || id == NULL ||
	    qjs_native_node_type(node) != DOM_ELEMENT_NODE) {
		return false;
	}
	qjs_read_dom_attribute((dom_element *) node, "id", &value);
	if (value == NULL) {
		return false;
	}
	qjs_copy_dom_string_cstr(text, sizeof(text), value);
	dom_string_unref(value);
	match = strcmp(text, id) == 0;
	return match;
}

static dom_node *qjs_get_element_by_id_node(JSContext *ctx, const char *id)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	dom_string *dom_id = NULL;
	dom_element *element = NULL;
	if (thread == NULL || thread->htmlc == NULL ||
	    thread->htmlc->document == NULL || id == NULL) {
		return NULL;
	}
	if (dom_string_create((const uint8_t *) id, strlen(id),
			      &dom_id) != DOM_NO_ERR) {
		return NULL;
	}
	(void) dom_document_get_element_by_id(thread->htmlc->document,
					      dom_id, &element);
	dom_string_unref(dom_id);
	return (dom_node *) element;
}

static bool qjs_node_text_has_landing_copy(dom_node *node)
{
	dom_string *text = NULL;
	char buffer[384];
	if (node == NULL ||
	    dom_node_get_text_content(node, &text) != DOM_NO_ERR ||
	    text == NULL) {
		return false;
	}
	qjs_copy_dom_string_cstr(buffer, sizeof(buffer), text);
	dom_string_unref(text);
	return strstr(buffer, "Create a new account") != NULL ||
	       strstr(buffer, "Discover millions") != NULL;
}

static void qjs_attach_react_landing_candidate(JSContext *ctx);

static void qjs_maybe_remember_react_landing_subtree(JSContext *ctx,
						     dom_node *candidate)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	const char *name;
	dom_node *parent = NULL;
	if (thread == NULL || candidate == NULL ||
	    qjs_native_node_type(candidate) != DOM_ELEMENT_NODE) {
		return;
	}
	if (thread->active_script_react_landing_candidate != NULL) {
		return;
	}
	name = thread->active_script_name;
	if (name == NULL ||
	    strstr(name, "js.rbxcdn.com/") == NULL ||
	    strstr(name, "ReactLanding.") == NULL) {
		return;
	}
	if (qjs_native_node_id_equals(candidate, "react-landing-container") ||
	    dom_node_get_parent_node(candidate, &parent) != DOM_NO_ERR) {
		return;
	}
	if (parent != NULL) {
		dom_node_unref(parent);
		return;
	}
	if (!qjs_node_text_has_landing_copy(candidate)) {
		return;
	}
	if (thread->active_script_react_landing_candidate != candidate) {
		if (thread->active_script_react_landing_candidate != NULL) {
			dom_node_unref(
				thread->active_script_react_landing_candidate);
		}
		thread->active_script_react_landing_candidate =
			dom_node_ref(candidate);
#ifdef LEONOS_USER_APP
		leonos_write("NETSURF QUICKJS DOM REACT CANDIDATE generated subtree\r\n");
#endif
		qjs_attach_react_landing_candidate(ctx);
	}
}

static void qjs_attach_react_landing_candidate(JSContext *ctx)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	dom_node *container = NULL;
	dom_node *result = NULL;
	if (thread == NULL ||
	    thread->active_script_react_landing_attached ||
	    thread->active_script_react_landing_candidate == NULL) {
		return;
	}
	container = qjs_get_element_by_id_node(ctx, "react-landing-container");
	if (container == NULL) {
		return;
	}
	if (dom_node_append_child(container,
				  thread->active_script_react_landing_candidate,
				  &result) == DOM_NO_ERR) {
		if (result != NULL) {
			dom_node_unref(result);
		}
		thread->active_script_react_landing_attached = true;
#ifdef LEONOS_USER_APP
		leonos_write("NETSURF QUICKJS DOM REACT ATTACH generated subtree\r\n");
#endif
		html_leonos_dom_mark_dirty(thread->htmlc);
		if (thread->heap != NULL && thread->heap->interrupt_enabled) {
			thread->heap->interrupt_budget = 0u;
		}
	}
	dom_node_unref(container);
}

static bool qjs_dom_append_budget_exceeded(JSContext *ctx)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	const char *name;

	if (thread == NULL) {
		return false;
	}

	name = thread->active_script_name;
	if (name == NULL ||
	    strstr(name, "js.rbxcdn.com/") == NULL ||
	    strstr(name, "ReactLanding.") == NULL) {
		return false;
	}

	thread->active_script_dom_appends += 1u;
	if (thread->active_script_react_landing_candidate != NULL &&
	    thread->active_script_dom_appends > 64u) {
		qjs_attach_react_landing_candidate(ctx);
		html_leonos_dom_mark_dirty(thread->htmlc);
#ifdef LEONOS_USER_APP
		if (!thread->active_script_dom_native_budget_hit) {
			char detail[192];
			int detail_len = snprintf(detail, sizeof(detail),
				"NETSURF QUICKJS DOM NATIVE BUDGET %s COUNT %u\r\n",
				name,
				thread->active_script_dom_appends);
			if (detail_len > 0) {
				leonos_write(detail);
			}
			thread->active_script_dom_native_budget_hit = true;
		}
#endif
		return true;
	}
	return false;
}

static JSValue qjs_native_append_child(JSContext *ctx, JSValueConst this_val,
				       int argc, JSValueConst *argv)
{
	struct qjs_native_node *parent = qjs_get_native_node(this_val);
	struct qjs_native_node *child;
	dom_node *materialized_child = NULL;
	dom_node *result = NULL;
	if (argc < 1) {
		return JS_UNDEFINED;
	}
	if (!qjs_native_node_id_equals(parent != NULL ? parent->node : NULL,
				       "react-landing-container") &&
	    qjs_dom_append_budget_exceeded(ctx)) {
		return JS_DupValue(ctx, argv[0]);
	}
	child = qjs_get_native_or_materialized_node(ctx, argv[0]);
	qjs_sync_native_subtree_props(ctx, argv[0]);
	if (parent != NULL && parent->node != NULL &&
	    child == NULL) {
		materialized_child = qjs_materialize_js_node(ctx, argv[0]);
		child = qjs_get_native_or_materialized_node(ctx, argv[0]);
	}
	if (parent != NULL && parent->node != NULL) {
		dom_node *native_child = child != NULL && child->node != NULL ?
			child->node : materialized_child;
		if (native_child != NULL &&
		    dom_node_append_child(parent->node, native_child,
					  &result) == DOM_NO_ERR) {
			if (result != NULL) {
				dom_node_unref(result);
			}
			qjs_log_native_dom_mutation(ctx, "appendChild",
						    parent->node,
						    native_child);
			qjs_maybe_remember_react_landing_subtree(ctx,
								 parent->node);
			qjs_note_native_node_inserted(ctx, native_child);
		}
	}
	if (materialized_child != NULL) {
		dom_node_unref(materialized_child);
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
	dom_node *materialized_child = NULL;
	dom_node *result = NULL;
	if (argc < 1) {
		return JS_UNDEFINED;
	}
	if (!qjs_native_node_id_equals(parent != NULL ? parent->node : NULL,
				       "react-landing-container") &&
	    qjs_dom_append_budget_exceeded(ctx)) {
		return JS_DupValue(ctx, argv[0]);
	}
	child = qjs_get_native_or_materialized_node(ctx, argv[0]);
	if (argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
		ref = qjs_get_native_or_materialized_node(ctx, argv[1]);
	}
	qjs_sync_native_subtree_props(ctx, argv[0]);
	if (parent != NULL && parent->node != NULL &&
	    child == NULL) {
		materialized_child = qjs_materialize_js_node(ctx, argv[0]);
		child = qjs_get_native_or_materialized_node(ctx, argv[0]);
	}
	if (parent != NULL && parent->node != NULL) {
		dom_node *native_child = child != NULL && child->node != NULL ?
			child->node : materialized_child;
		if (native_child != NULL &&
		    dom_node_insert_before(parent->node, native_child,
					   ref != NULL ? ref->node : NULL,
					   &result) == DOM_NO_ERR &&
		    result != NULL) {
			dom_node_unref(result);
			qjs_log_native_dom_mutation(ctx, "insertBefore",
						    parent->node,
						    native_child);
			qjs_maybe_remember_react_landing_subtree(ctx,
								 parent->node);
			qjs_note_native_node_inserted(ctx, native_child);
		}
	}
	if (materialized_child != NULL) {
		dom_node_unref(materialized_child);
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
	child = qjs_get_native_or_materialized_node(ctx, argv[0]);
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
	dom_node *materialized_child = NULL;
	dom_node *result = NULL;
	if (argc < 2) {
		return JS_UNDEFINED;
	}
	if (!qjs_native_node_id_equals(parent != NULL ? parent->node : NULL,
				       "react-landing-container") &&
	    qjs_dom_append_budget_exceeded(ctx)) {
		return JS_DupValue(ctx, argv[1]);
	}
	new_child = qjs_get_native_or_materialized_node(ctx, argv[0]);
	old_child = qjs_get_native_or_materialized_node(ctx, argv[1]);
	qjs_sync_native_subtree_props(ctx, argv[0]);
	if (parent != NULL && parent->node != NULL &&
	    new_child == NULL) {
		materialized_child = qjs_materialize_js_node(ctx, argv[0]);
		new_child = qjs_get_native_or_materialized_node(ctx, argv[0]);
	}
	if (parent != NULL && parent->node != NULL &&
	    ((new_child != NULL && new_child->node != NULL) ||
	     materialized_child != NULL) &&
	    old_child != NULL && old_child->node != NULL &&
	    dom_node_replace_child(parent->node,
				   new_child != NULL && new_child->node != NULL ?
					   new_child->node : materialized_child,
				   old_child->node, &result) == DOM_NO_ERR) {
		if (result != NULL) {
			dom_node_unref(result);
		}
		qjs_log_native_dom_mutation(ctx, "replaceChild",
			parent->node,
			new_child != NULL && new_child->node != NULL ?
				new_child->node : materialized_child);
		qjs_maybe_remember_react_landing_subtree(ctx, parent->node);
		qjs_note_native_node_inserted(ctx,
			new_child != NULL && new_child->node != NULL ?
				new_child->node : materialized_child);
	}
	if (materialized_child != NULL) {
		dom_node_unref(materialized_child);
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
		{
			char dataset_name[128];
			if (qjs_dataset_name_from_attr(dataset_name,
						       sizeof(dataset_name),
						       name_text)) {
				JSValue dataset = JS_GetPropertyStr(ctx,
								     this_val,
								     "dataset");
				if (!JS_IsObject(dataset)) {
					JS_FreeValue(ctx, dataset);
					dataset = JS_NewObject(ctx);
					JS_SetPropertyStr(ctx, this_val,
							  "dataset",
							  JS_DupValue(ctx,
								      dataset));
				}
				qjs_set_string(ctx, dataset, dataset_name,
					       value_text);
				JS_FreeValue(ctx, dataset);
			}
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
	const char *name_text;
	if (argc < 1 || native == NULL || native->node == NULL) {
		return JS_UNDEFINED;
	}
	name_text = JS_ToCString(ctx, argv[0]);
	if (qjs_dom_string_from_js(ctx, argv[0], &name)) {
		(void) dom_element_remove_attribute((dom_element *) native->node,
						    name);
		dom_string_unref(name);
	}
	if (name_text != NULL) {
		JSValue attrs = JS_GetPropertyStr(ctx, this_val, "attributes");
		if (JS_IsObject(attrs)) {
			qjs_delete_property_cstr(ctx, attrs, name_text);
		}
		JS_FreeValue(ctx, attrs);
		qjs_delete_property_cstr(ctx, this_val, name_text);
		if (strcmp(name_text, "id") == 0) {
			qjs_set_string(ctx, this_val, "id", "");
		} else if (strcmp(name_text, "class") == 0) {
			qjs_set_string(ctx, this_val, "className", "");
		}
		{
			char dataset_name[128];
			if (qjs_dataset_name_from_attr(dataset_name,
						       sizeof(dataset_name),
						       name_text)) {
				JSValue dataset = JS_GetPropertyStr(ctx,
								     this_val,
								     "dataset");
				if (JS_IsObject(dataset)) {
					qjs_delete_property_cstr(ctx, dataset,
								 dataset_name);
				}
				JS_FreeValue(ctx, dataset);
			}
		}
		JS_FreeCString(ctx, name_text);
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
	qjs_install_function(ctx, obj, "addEventListener",
			     qjs_dom_add_event_listener, 2);
	qjs_install_function(ctx, obj, "removeEventListener",
			     qjs_dom_remove_event_listener, 2);
	qjs_install_function(ctx, obj, "dispatchEvent",
			     qjs_dom_dispatch_event, 1);
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
	qjs_install_function(ctx, obj, "matches", qjs_native_matches, 1);
	qjs_install_function(ctx, obj, "closest", qjs_native_closest, 1);
	qjs_install_function(ctx, obj, "querySelector",
			     qjs_document_query_selector, 1);
	qjs_install_function(ctx, obj, "querySelectorAll",
			     qjs_document_query_selector_all, 1);
	qjs_install_function(ctx, obj, "getElementsByTagName",
			     qjs_document_get_elements_by_tag_name, 1);
	qjs_install_function(ctx, obj, "getElementsByClassName",
			     qjs_document_get_elements_by_class_name, 1);
	if (populate_children) {
		qjs_populate_native_child_edges(ctx, obj, node);
	}
	if (qjs_element_tag_is(ctx, obj, "SELECT")) {
		JSValue child_nodes = JS_GetPropertyStr(ctx, obj, "childNodes");
		qjs_refresh_js_select_options(ctx, obj, child_nodes);
		JS_FreeValue(ctx, child_nodes);
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

static JSValue qjs_new_dom_selector_result(JSContext *ctx, dom_node *node)
{
	return qjs_new_dom_node_limited(ctx, node, 1u, false);
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
	char not_tag[32];
	char not_id[96];
	char not_class_name[96];
	bool has_tag;
	bool has_id;
	bool has_class;
	bool has_attr;
	bool has_not_attr;
	bool has_not_tag;
	bool has_not_id;
	bool has_not_class;
	bool has_checked_pseudo;
	bool has_enabled_pseudo;
	bool want_enabled_pseudo;
	bool attr_has_value;
	bool attr_dash_match;
	bool attr_prefix_match;
	bool unsupported;
};

struct qjs_selector_chain {
	struct qjs_simple_selector parts[8];
	char combinators[8];
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
	       ch == ']' || ch == ':' || ch == '>' || ch == '+' || ch == '~' ||
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
			i += 5u;
			while (i < end && qjs_ascii_space(text[i])) {
				i += 1u;
			}
			if (i >= end) {
				selector->unsupported = true;
				return false;
			}
			if (text[i] == '[') {
				size_t attr_start;
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
			} else if (text[i] == '.') {
				size_t token_start = ++i;
				while (i < end && text[i] != ')' &&
				       !qjs_selector_special(text[i])) {
					i += 1u;
				}
				qjs_copy_selector_text(selector->not_class_name,
					sizeof(selector->not_class_name),
					text, token_start, i);
				selector->has_not_class =
					selector->not_class_name[0] != 0;
			} else if (text[i] == '#') {
				size_t token_start = ++i;
				while (i < end && text[i] != ')' &&
				       !qjs_selector_special(text[i])) {
					i += 1u;
				}
				qjs_copy_selector_text(selector->not_id,
						       sizeof(selector->not_id),
						       text, token_start, i);
				selector->has_not_id = selector->not_id[0] != 0;
			} else if (text[i] == '*') {
				selector->has_not_tag = true;
				selector->not_tag[0] = '*';
				selector->not_tag[1] = 0;
				i += 1u;
			} else if ((text[i] >= 'A' && text[i] <= 'Z') ||
				   (text[i] >= 'a' && text[i] <= 'z')) {
				size_t token_start = i;
				while (i < end && text[i] != ')' &&
				       !qjs_selector_special(text[i])) {
					i += 1u;
				}
				qjs_copy_selector_text(selector->not_tag,
						       sizeof(selector->not_tag),
						       text, token_start, i);
				selector->has_not_tag =
					selector->not_tag[0] != 0;
			} else {
				selector->unsupported = true;
				return false;
			}
			while (i < end && qjs_ascii_space(text[i])) {
				i += 1u;
			}
			if (i >= end || text[i] != ')' ||
			    !(selector->has_not_attr || selector->has_not_tag ||
			      selector->has_not_id || selector->has_not_class)) {
				selector->unsupported = true;
				return false;
			}
			i += 1u;
		} else if (text[i] == ':' && i + 8u <= end &&
			   strncmp(text + i, ":checked", 8u) == 0) {
			selector->has_checked_pseudo = true;
			i += 8u;
		} else if (text[i] == ':' && i + 8u <= end &&
			   strncmp(text + i, ":enabled", 8u) == 0) {
			selector->has_enabled_pseudo = true;
			selector->want_enabled_pseudo = true;
			i += 8u;
		} else if (text[i] == ':' && i + 9u <= end &&
			   strncmp(text + i, ":disabled", 9u) == 0) {
			selector->has_enabled_pseudo = true;
			selector->want_enabled_pseudo = false;
			i += 9u;
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
	unsigned int paren_depth = 0u;
	char quote = 0;
	char next_combinator = 0;
	memset(chain, 0, sizeof(*chain));
	while (start < len && qjs_ascii_space(text[start])) {
		start += 1u;
	}
	end = start;
	while (end < len &&
	       (text[end] != ',' || bracket_depth != 0u ||
		paren_depth != 0u || quote != 0)) {
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
		if (bracket_depth == 0u) {
			if (text[end] == '(') {
				paren_depth += 1u;
				end += 1u;
				continue;
			}
			if (text[end] == ')' && paren_depth != 0u) {
				paren_depth -= 1u;
				end += 1u;
				continue;
			}
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
		bool had_space = false;
		while (start < end && qjs_ascii_space(text[start])) {
			start += 1u;
			had_space = true;
		}
		if (start < end &&
		    (text[start] == '>' || text[start] == '+' ||
		     text[start] == '~')) {
			if (chain->count == 0u || next_combinator != 0) {
				chain->unsupported = true;
				return false;
			}
			next_combinator = text[start++];
			while (start < end && qjs_ascii_space(text[start])) {
				start += 1u;
			}
			if (start >= end) {
				chain->unsupported = true;
				return false;
			}
		} else if (had_space && chain->count != 0u &&
			   next_combinator == 0) {
			next_combinator = ' ';
		}
		part_start = start;
		bracket_depth = 0u;
		paren_depth = 0u;
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
			if (bracket_depth == 0u) {
				if (text[start] == '(') {
					paren_depth += 1u;
					start += 1u;
					continue;
				}
				if (text[start] == ')' && paren_depth != 0u) {
					paren_depth -= 1u;
					start += 1u;
					continue;
				}
				if (paren_depth == 0u &&
				    (qjs_ascii_space(text[start]) ||
				     text[start] == '>' ||
				     text[start] == '+' ||
				     text[start] == '~')) {
					break;
				}
			}
			start += 1u;
		}
		if (part_start == start) {
			continue;
		}
		if (chain->count >= 8u) {
			chain->unsupported = true;
			return false;
		}
		if (chain->count != 0u) {
			chain->combinators[chain->count] =
				next_combinator != 0 ? next_combinator : ' ';
		}
		if (!qjs_parse_selector_part(text, part_start, start,
					     &chain->parts[chain->count])) {
			chain->unsupported = true;
			return false;
		}
		chain->count += 1u;
		next_combinator = 0;
	}
	if (chain->count == 0u || next_combinator != 0) {
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
	if (selector->has_tag || selector->has_not_tag ||
	    selector->has_checked_pseudo || selector->has_enabled_pseudo) {
		if (dom_element_get_tag_name(element, &tag) != DOM_NO_ERR ||
		    tag == NULL) {
			matched = false;
		}
	}
	if (matched && selector->has_tag && selector->tag[0] != '*') {
		if (!qjs_ascii_equal_ci(dom_string_data(tag), selector->tag)) {
			matched = false;
		}
	}
	if (matched && selector->has_not_tag) {
		if (selector->not_tag[0] == '*' ||
		    qjs_ascii_equal_ci(dom_string_data(tag), selector->not_tag)) {
			matched = false;
		}
	}
	if (matched && (selector->has_id || selector->has_not_id)) {
		qjs_read_dom_attribute(element, "id", &id);
	}
	if (matched && selector->has_id) {
		matched = qjs_dom_string_equals_cstr(id, selector->id);
	}
	if (matched && selector->has_not_id) {
		matched = !qjs_dom_string_equals_cstr(id, selector->not_id);
	}
	if (matched && (selector->has_class || selector->has_not_class)) {
		qjs_read_dom_attribute(element, "class", &class_name);
	}
	if (matched && selector->has_class) {
		matched = qjs_dom_class_contains(class_name,
						 selector->class_name);
	}
	if (matched && selector->has_not_class) {
		matched = !qjs_dom_class_contains(class_name,
						  selector->not_class_name);
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
	if (matched && selector->has_checked_pseudo) {
		dom_string *checked = NULL;
		if (qjs_ascii_equal_ci(dom_string_data(tag), "input")) {
			qjs_read_dom_attribute(element, "checked", &checked);
		} else if (qjs_ascii_equal_ci(dom_string_data(tag), "option")) {
			qjs_read_dom_attribute(element, "selected", &checked);
		}
		matched = checked != NULL;
		if (checked != NULL) {
			dom_string_unref(checked);
		}
	}
	if (matched && selector->has_enabled_pseudo) {
		dom_string *disabled = NULL;
		bool control =
			qjs_ascii_equal_ci(dom_string_data(tag), "button") ||
			qjs_ascii_equal_ci(dom_string_data(tag), "input") ||
			qjs_ascii_equal_ci(dom_string_data(tag), "select") ||
			qjs_ascii_equal_ci(dom_string_data(tag), "textarea") ||
			qjs_ascii_equal_ci(dom_string_data(tag), "option") ||
			qjs_ascii_equal_ci(dom_string_data(tag), "optgroup") ||
			qjs_ascii_equal_ci(dom_string_data(tag), "fieldset");
		qjs_read_dom_attribute(element, "disabled", &disabled);
		matched = control &&
			  (selector->want_enabled_pseudo ?
				   disabled == NULL : disabled != NULL);
		if (disabled != NULL) {
			dom_string_unref(disabled);
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

static dom_node *qjs_find_matching_parent(dom_node *start,
					  const struct qjs_simple_selector *selector)
{
	dom_node *parent = NULL;
	dom_node_type type = DOM_NODE_TYPE_COUNT;
	if (start == NULL || selector == NULL ||
	    dom_node_get_parent_node(start, &parent) != DOM_NO_ERR ||
	    parent == NULL) {
		return NULL;
	}
	if (dom_node_get_node_type(parent, &type) == DOM_NO_ERR &&
	    type == DOM_ELEMENT_NODE &&
	    qjs_dom_element_matches((dom_element *) parent, selector)) {
		return parent;
	}
	dom_node_unref(parent);
	return NULL;
}

static dom_node *qjs_find_matching_previous_sibling(dom_node *start,
		const struct qjs_simple_selector *selector,
		bool adjacent_only)
{
	dom_node *cursor = NULL;
	dom_node *previous = NULL;
	if (start == NULL || selector == NULL ||
	    dom_node_get_previous_sibling(start, &cursor) != DOM_NO_ERR) {
		return NULL;
	}
	while (cursor != NULL) {
		dom_node_type type = DOM_NODE_TYPE_COUNT;
		if (dom_node_get_node_type(cursor, &type) == DOM_NO_ERR &&
		    type == DOM_ELEMENT_NODE) {
			if (qjs_dom_element_matches((dom_element *) cursor,
						    selector)) {
				return cursor;
			}
			if (adjacent_only) {
				dom_node_unref(cursor);
				return NULL;
			}
		}
		if (dom_node_get_previous_sibling(cursor, &previous) !=
		    DOM_NO_ERR) {
			dom_node_unref(cursor);
			return NULL;
		}
		dom_node_unref(cursor);
		cursor = previous;
		previous = NULL;
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
		char combinator = chain->combinators[part];
		dom_node *found = NULL;
		if (combinator == '>') {
			found = qjs_find_matching_parent(scope,
				&chain->parts[part - 1u]);
		} else if (combinator == '+') {
			found = qjs_find_matching_previous_sibling(scope,
				&chain->parts[part - 1u], true);
		} else if (combinator == '~') {
			found = qjs_find_matching_previous_sibling(scope,
				&chain->parts[part - 1u], false);
		} else {
			found = qjs_find_matching_ancestor(scope,
				&chain->parts[part - 1u]);
		}
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

static void qjs_append_matching_descendants(JSContext *ctx,
					    JSValueConst list,
					    dom_node *scope,
					    const struct qjs_selector_chain *chain,
					    uint32_t *visited,
					    uint32_t *out_index)
{
	dom_node *child = NULL;
	if (scope == NULL || chain == NULL || out_index == NULL ||
	    *out_index >= 512u ||
	    dom_node_get_first_child(scope, &child) != DOM_NO_ERR) {
		return;
	}
	while (child != NULL && *out_index < 512u) {
		dom_node *next = NULL;
		dom_node_type type = DOM_NODE_TYPE_COUNT;
		(void) dom_node_get_next_sibling(child, &next);
		if (dom_node_get_node_type(child, &type) == DOM_NO_ERR &&
		    type == DOM_ELEMENT_NODE) {
			if (visited != NULL) {
				*visited += 1u;
			}
			if (qjs_dom_element_matches_chain((dom_element *) child,
							  chain)) {
				JS_DefinePropertyValueUint32(ctx, list,
					(*out_index)++,
					qjs_new_dom_selector_result(ctx, child),
					JS_PROP_C_W_E);
			}
		}
		qjs_append_matching_descendants(ctx, list, child, chain,
						visited, out_index);
		dom_node_unref(child);
		child = next;
	}
}

static JSValue qjs_new_dom_node_array(JSContext *ctx)
{
	JSValue list = JS_NewArray(ctx);
	qjs_install_function(ctx, list, "item", qjs_array_item, 1);
	return list;
}

static size_t qjs_selector_find_group_end(const char *text,
					  size_t start,
					  size_t len)
{
	unsigned int bracket_depth = 0u;
	unsigned int paren_depth = 0u;
	char quote = 0;
	for (size_t i = start; i < len; i++) {
		if (quote != 0) {
			if (text[i] == quote) {
				quote = 0;
			}
			continue;
		}
		if (text[i] == '\'' || text[i] == '"') {
			quote = text[i];
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
		if (bracket_depth == 0u) {
			if (text[i] == '(') {
				paren_depth += 1u;
				continue;
			}
			if (text[i] == ')' && paren_depth != 0u) {
				paren_depth -= 1u;
				continue;
			}
			if (paren_depth == 0u && text[i] == ',') {
				return i;
			}
		}
	}
	return len;
}

static bool qjs_dom_element_matches_selector_text(dom_element *element,
						  const char *selector_text)
{
	size_t len;
	size_t start = 0u;

	if (element == NULL || selector_text == NULL) {
		return false;
	}
	len = strlen(selector_text);
	while (start < len) {
		size_t end = qjs_selector_find_group_end(selector_text, start, len);
		size_t trimmed_start = start;
		size_t trimmed_end = end;
		char group[512];
		size_t group_len;
		struct qjs_selector_chain chain;

		while (trimmed_start < trimmed_end &&
		       qjs_ascii_space(selector_text[trimmed_start])) {
			trimmed_start += 1u;
		}
		while (trimmed_end > trimmed_start &&
		       qjs_ascii_space(selector_text[trimmed_end - 1u])) {
			trimmed_end -= 1u;
		}
		group_len = trimmed_end - trimmed_start;
		if (group_len != 0u && group_len < sizeof(group)) {
			memcpy(group, selector_text + trimmed_start, group_len);
			group[group_len] = 0;
			if (qjs_parse_selector_chain(group, &chain) &&
			    qjs_dom_element_matches_chain(element, &chain)) {
				return true;
			}
		}
		if (end >= len) {
			break;
		}
		start = end + 1u;
	}
	return false;
}

static JSValue qjs_native_matches(JSContext *ctx, JSValueConst this_val,
				  int argc, JSValueConst *argv)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	const char *selector_text;
	bool matches = false;

	if (argc < 1 || native == NULL || native->node == NULL ||
	    qjs_native_node_type(native->node) != DOM_ELEMENT_NODE) {
		return JS_NewBool(ctx, false);
	}
	selector_text = JS_ToCString(ctx, argv[0]);
	if (selector_text == NULL) {
		return JS_NewBool(ctx, false);
	}
	matches = qjs_dom_element_matches_selector_text(
		(dom_element *) native->node, selector_text);
	JS_FreeCString(ctx, selector_text);
	return JS_NewBool(ctx, matches);
}

static JSValue qjs_native_closest(JSContext *ctx, JSValueConst this_val,
				  int argc, JSValueConst *argv)
{
	struct qjs_native_node *native = qjs_get_native_node(this_val);
	const char *selector_text;
	dom_node *cursor;

	if (argc < 1 || native == NULL || native->node == NULL) {
		return JS_NULL;
	}
	selector_text = JS_ToCString(ctx, argv[0]);
	if (selector_text == NULL) {
		return JS_NULL;
	}
	cursor = dom_node_ref(native->node);
	while (cursor != NULL) {
		dom_node_type type = DOM_NODE_TYPE_COUNT;
		if (dom_node_get_node_type(cursor, &type) == DOM_NO_ERR &&
		    type == DOM_ELEMENT_NODE &&
		    qjs_dom_element_matches_selector_text((dom_element *) cursor,
							  selector_text)) {
			bool same = false;
			(void) dom_node_is_same(cursor, native->node, &same);
			JS_FreeCString(ctx, selector_text);
			if (same) {
				dom_node_unref(cursor);
				return JS_DupValue(ctx, this_val);
			}
			JSValue result = qjs_new_dom_element(ctx,
				(dom_element *) cursor);
			dom_node_unref(cursor);
			return result;
		}
		{
			dom_node *parent = NULL;
			if (dom_node_get_parent_node(cursor, &parent) !=
			    DOM_NO_ERR) {
				dom_node_unref(cursor);
				cursor = NULL;
			} else {
				dom_node_unref(cursor);
				cursor = parent;
			}
		}
	}
	JS_FreeCString(ctx, selector_text);
	return JS_NULL;
}

static bool qjs_selector_result_list_contains(JSContext *ctx,
					      JSValueConst list,
					      uint32_t length,
					      JSValueConst candidate)
{
	struct qjs_native_node *candidate_native =
		qjs_get_native_or_materialized_node(ctx, candidate);
	for (uint32_t i = 0u; i < length; i++) {
		JSValue item = JS_GetPropertyUint32(ctx, list, i);
		bool same_js = JS_StrictEq(ctx, item, candidate);
		bool same_dom = false;
		if (!same_js && candidate_native != NULL &&
		    candidate_native->node != NULL) {
			struct qjs_native_node *item_native =
				qjs_get_native_or_materialized_node(ctx, item);
			if (item_native != NULL && item_native->node != NULL) {
				(void) dom_node_is_same(candidate_native->node,
							item_native->node,
							&same_dom);
			}
		}
		JS_FreeValue(ctx, item);
		if (same_js || same_dom) {
			return true;
		}
	}
	return false;
}

static void qjs_append_selector_group_results(JSContext *ctx,
					      JSValueConst this_val,
					      JSValueConst list,
					      uint32_t *out_index,
					      const char *selector_text)
{
	size_t len = strlen(selector_text);
	size_t start = 0u;
	while (start < len && *out_index < 512u) {
		size_t end;
		size_t trimmed_start;
		size_t trimmed_end;
		JSValue group_arg;
		JSValue group_list;
		uint32_t group_length = 0u;
		end = qjs_selector_find_group_end(selector_text, start, len);
		trimmed_start = start;
		trimmed_end = end;
		while (trimmed_start < trimmed_end &&
		       qjs_ascii_space(selector_text[trimmed_start])) {
			trimmed_start += 1u;
		}
		while (trimmed_end > trimmed_start &&
		       qjs_ascii_space(selector_text[trimmed_end - 1u])) {
			trimmed_end -= 1u;
		}
		if (trimmed_start < trimmed_end) {
			group_arg = JS_NewStringLen(ctx,
				selector_text + trimmed_start,
				trimmed_end - trimmed_start);
			group_list = qjs_document_query_selector_all(ctx,
				this_val, 1, &group_arg);
			group_length = qjs_array_length(ctx, group_list);
			for (uint32_t i = 0u;
			     i < group_length && *out_index < 512u;
			     i++) {
				JSValue item =
					JS_GetPropertyUint32(ctx,
							     group_list,
							     i);
				if (!JS_IsUndefined(item) &&
				    !qjs_selector_result_list_contains(ctx,
						list, *out_index, item)) {
					JS_DefinePropertyValueUint32(ctx,
						list, (*out_index)++,
						item, JS_PROP_C_W_E);
				} else {
					JS_FreeValue(ctx, item);
				}
			}
			JS_FreeValue(ctx, group_list);
			JS_FreeValue(ctx, group_arg);
		}
		if (end >= len) {
			break;
		}
		start = end + 1u;
	}
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

static bool qjs_query_scope_contains(dom_node *scope, dom_node *node)
{
	bool same = false;
	bool contains = false;
	if (scope == NULL) {
		return true;
	}
	if (node == NULL) {
		return false;
	}
	if (dom_node_is_same(scope, node, &same) == DOM_NO_ERR && same) {
		return false;
	}
	if (dom_node_contains(scope, node, &contains) != DOM_NO_ERR) {
		return false;
	}
	return contains;
}

static bool qjs_sync_js_child_nodes_into_native(JSContext *ctx,
						JSValueConst obj,
						dom_node *parent)
{
	JSValue child_nodes;
	uint32_t length = 0u;
	bool appended = false;

	if (!JS_IsObject(obj) || parent == NULL) {
		return false;
	}
	child_nodes = JS_GetPropertyStr(ctx, obj, "childNodes");
	if (!JS_IsObject(child_nodes)) {
		JS_FreeValue(ctx, child_nodes);
		return false;
	}
	length = qjs_array_length(ctx, child_nodes);
	for (uint32_t i = 0u; i < length; i++) {
		JSValue child = JS_GetPropertyUint32(ctx, child_nodes, i);
		dom_node *child_node = NULL;
		bool same = false;
		if (JS_IsObject(child)) {
			child_node = qjs_materialize_js_node(ctx, child);
		}
		if (child_node != NULL &&
		    !(dom_node_is_same(parent, child_node, &same) == DOM_NO_ERR &&
		      same) &&
		    !qjs_query_scope_contains(parent, child_node)) {
			dom_node *result = NULL;
			if (dom_node_append_child(parent, child_node,
						  &result) == DOM_NO_ERR) {
				appended = true;
				if (result != NULL) {
					dom_node_unref(result);
				}
			}
		}
		if (child_node != NULL) {
			dom_node_unref(child_node);
		}
		JS_FreeValue(ctx, child);
	}
	JS_FreeValue(ctx, child_nodes);
#ifdef LEONOS_USER_APP
	if (appended) {
		static unsigned int sync_log_count;
		if (sync_log_count < 16u) {
			char detail[96];
			int detail_len = snprintf(detail, sizeof(detail),
				"NETSURF QUICKJS DOM SCOPE SYNC children=%u\r\n",
				(unsigned int) length);
			sync_log_count += 1u;
			if (detail_len > 0) {
				leonos_write(detail);
			}
		}
	}
#endif
	return appended;
}

static JSValue qjs_document_query_selector_all(JSContext *ctx,
					       JSValueConst this_val,
					       int argc,
					       JSValueConst *argv)
{
	jsthread *thread = JS_GetContextOpaque(ctx);
	struct qjs_native_node *scope = qjs_get_native_node(this_val);
	const char *selector_text;
	struct qjs_selector_chain chain;
	const struct qjs_simple_selector *leaf;
	dom_string *tag_name = NULL;
	dom_nodelist *nodes = NULL;
	dom_node *scope_node = scope != NULL ? scope->node : NULL;
	dom_node_type scope_type = DOM_NODE_TYPE_COUNT;
	bool scoped_element_query = false;
	uint32_t length = 0u;
	uint32_t out_index = 0u;
	JSValue list = qjs_new_dom_node_array(ctx);
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
	if (qjs_selector_find_group_end(selector_text, 0u,
				       strlen(selector_text)) <
	    strlen(selector_text)) {
		uint32_t out_count = 0u;
		qjs_append_selector_group_results(ctx, this_val, list,
						  &out_count, selector_text);
#ifdef LEONOS_USER_APP
		{
			char detail[128];
			int detail_len = snprintf(detail, sizeof(detail),
				"NETSURF QUICKJS QUERYALL %s nodes=selector-list count=%u\r\n",
				selector_text, (unsigned int) out_count);
			if (detail_len > 0) {
				leonos_write(detail);
			}
		}
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
	if (scope_node != NULL &&
	    dom_node_get_node_type(scope_node, &scope_type) == DOM_NO_ERR &&
	    scope_type == DOM_ELEMENT_NODE) {
		(void) qjs_sync_js_child_nodes_into_native(ctx, this_val,
							   scope_node);
		scoped_element_query = true;
		qjs_append_matching_descendants(ctx, list, scope_node, &chain,
						&length, &out_index);
	} else {
		(void) dom_document_get_elements_by_tag_name(thread->htmlc->document,
							     tag_name,
							     &nodes);
	}
	if (!scoped_element_query && nodes != NULL &&
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
			    qjs_query_scope_contains(scope_node, node) &&
			    qjs_dom_element_matches_chain((dom_element *) node,
							  &chain)) {
				JS_DefinePropertyValueUint32(ctx, list,
					out_index++,
					qjs_new_dom_selector_result(ctx, node),
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

static JSValue qjs_document_get_elements_by_name(JSContext *ctx,
						 JSValueConst this_val,
						 int argc,
						 JSValueConst *argv)
{
	const char *name_text;
	char selector_text[160];
	size_t out = 0u;
	JSValue selector_arg;
	JSValue result;
	if (argc < 1) {
		return qjs_new_dom_node_array(ctx);
	}
	name_text = JS_ToCString(ctx, argv[0]);
	if (name_text == NULL) {
		return qjs_new_dom_node_array(ctx);
	}
	(void) snprintf(selector_text, sizeof(selector_text), "[name=\"");
	out = strlen(selector_text);
	for (size_t i = 0u; name_text[i] != 0 &&
	     out + 3u < sizeof(selector_text); i++) {
		char ch = name_text[i];
		if (ch == '"' || ch == ']' || ch == '\\') {
			break;
		}
		selector_text[out++] = ch;
	}
	selector_text[out++] = '"';
	selector_text[out++] = ']';
	selector_text[out] = 0;
	JS_FreeCString(ctx, name_text);
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
		"function cls(e){return String(e.className||'').trim().split(/\\s+/).filter(Boolean);}"
		"function setCls(e,a){e.className=a.join(' ');if(e.attributes)e.attributes['class']=e.className;}"
		"function hasClass(e,c){return cls(e).indexOf(String(c))>=0;}"
		"function dataName(n){return String(n||'').slice(5).replace(/-([a-z])/g,function(m,c){return c.toUpperCase();});}"
		"function syncData(e,n,v){if(String(n).indexOf('data-')===0){e.dataset=e.dataset||{};e.dataset[dataName(n)]=String(v);}}"
		"function delData(e,n){if(String(n).indexOf('data-')===0&&e.dataset)delete e.dataset[dataName(n)];}"
		"function cl(e){return {contains:function(c){return hasClass(e,String(c));},"
		"add:function(){var a=cls(e);for(var i=0;i<arguments.length;i++){var c=String(arguments[i]);if(c&&a.indexOf(c)<0)a.push(c);}setCls(e,a);},"
		"remove:function(){var r=[].map.call(arguments,String);setCls(e,cls(e).filter(function(c){return r.indexOf(c)<0;}));},"
		"toggle:function(c,f){c=String(c);var a=cls(e),i=a.indexOf(c),add=f===void 0?i<0:!!f;if(add&&i<0)a.push(c);else if(!add&&i>=0)a.splice(i,1);setCls(e,a);return add;},"
		"replace:function(o,n){var a=cls(e),i=a.indexOf(String(o));if(i<0)return false;a[i]=String(n);setCls(e,a);return true;},"
		"item:function(i){return cls(e)[i]||null;},forEach:function(f,t){cls(e).forEach(function(c,i){f.call(t,c,i,this);},this);},toString:function(){return e.className||'';}};}"
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
		"function evtObj(t){return {type:String(t||''),target:null,currentTarget:null,defaultPrevented:false,"
		"preventDefault:function(){this.defaultPrevented=true;},stopPropagation:noop,stopImmediatePropagation:noop};}"
		"function callEvt(o,f,e){try{var r;if(typeof f==='function')r=f.call(o,e);"
		"else if(f&&typeof f.handleEvent==='function')r=f.handleEvent(e);if(r===false&&e.preventDefault)e.preventDefault();}"
		"catch(x){try{g._DumpException(x);}catch(y){}}}"
		"function eventful(o){if(!o)return o;o._leonosListeners=o._leonosListeners||{};"
		"o.addEventListener=function(t,f){t=String(t||'');if(!t||(!f&&f!==0))return;"
		"var a=o._leonosListeners[t]||(o._leonosListeners[t]=[]);a.push(f);};"
		"o.removeEventListener=function(t,f){var a=o._leonosListeners&&o._leonosListeners[String(t||'')];"
		"if(!a)return;for(var i=0;i<a.length;i++)if(a[i]===f){a.splice(i,1);break;}};"
		"o.dispatchEvent=function(e){if(!e||typeof e!=='object')e=evtObj(e);if(!e.type)e.type=String(e.type||'');"
		"if(!e.target)e.target=o;e.currentTarget=o;if(e.defaultPrevented===void 0)e.defaultPrevented=false;"
		"var h=o['on'+e.type];callEvt(o,h,e);var a=(o._leonosListeners&&o._leonosListeners[e.type]||[]).slice();"
		"for(var i=0;i<a.length;i++)callEvt(o,a[i],e);return !e.defaultPrevented;};return o;}"
		"Element.prototype.getAttribute=Element.prototype.getAttribute||function(n){"
		"n=String(n);return this.attributes&&this.attributes.hasOwnProperty(n)?this.attributes[n]:"
		"(n==='class'?this.className:(this[n]!==void 0?String(this[n]):null));};"
		"Element.prototype.setAttribute=Element.prototype.setAttribute||function(n,v){n=String(n);v=String(v);"
		"this.attributes=this.attributes||{};this.attributes[n]=v;if(n==='id')this.id=v;"
		"else if(n==='class')this.className=v;else if(n==='href'&&(this.tagName||'').toUpperCase()==='A')setAnchor(this,v);else this[n]=v;syncData(this,n,v);};"
		"Element.prototype.removeAttribute=Element.prototype.removeAttribute||function(n){"
		"n=String(n);if(this.attributes)delete this.attributes[n];if(n==='id')this.id='';else if(n==='class')this.className='';delData(this,n);};"
		"Element.prototype.hasAttribute=Element.prototype.hasAttribute||function(n){"
		"n=String(n);return !!(this.attributes&&this.attributes.hasOwnProperty(n));};"
		"Element.prototype.focus=Element.prototype.focus||noop;Element.prototype.blur=Element.prototype.blur||noop;"
		"Element.prototype.contains=Element.prototype.contains||function(n){"
		"for(;n;n=n.parentNode||n.parentElement){if(n===this)return true;}return false;};"
		"function rect(e){var w=Number((e&&(e.clientWidth||e.offsetWidth))||g.innerWidth||1)||1,h=Number((e&&(e.clientHeight||e.offsetHeight))||g.innerHeight||1)||1;return {x:0,y:0,top:0,left:0,right:w,bottom:h,width:w,height:h};}"
		"Element.prototype.getBoundingClientRect=Element.prototype.getBoundingClientRect||function(){return rect(this);};"
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
		"c.name=s.name||'';c.href=s.href||'';c.src=s.src||'';c.alt=s.alt||'';c.placeholder=s.placeholder||'';c.textContent=s.textContent||'';if(s.attributes){for(var k in s.attributes)"
		"if(s.attributes.hasOwnProperty(k))c.setAttribute(k,s.attributes[k]);}c._innerHTML=s._innerHTML||'';"
		"if(deep){var cs=s.childNodes||[];for(var i=0;i<cs.length;i++)appendKid(c,cloneElem(cs[i],true));}"
		"return c;}"
		"function el(tag){var e=Object.create(HTMLElement.prototype);tag=String(tag||'').toUpperCase();"
		"e.nodeType=1;e.tagName=tag;e.nodeName=tag;e.childNodes=arr();"
		"e.children=e.childNodes;e.style=css();e.attributes={};e.dataset={};"
		"if(tag==='SELECT')e.options=e.childNodes;"
		"e.parentNode=null;e.ownerDocument=g.document||null;e.textContent='';"
		"e.parentElement=null;e.firstChild=null;e.lastChild=null;e.firstElementChild=null;e.lastElementChild=null;"
		"e.innerHTML='';e.className='';e.id='';e.value='';e.defaultValue='';e.checked=false;e.selected=false;"
		"e.disabled=false;e.multiple=false;e.required=false;e.readOnly=false;e.hidden=false;"
		"e.type='';e.name='';e.href='';e.src='';e.alt='';e.title='';e.placeholder='';e.role='';e.protocol='';e.host='';e.hostname='';e.port='';e.pathname='';e.search='';e.hash='';e.origin='';e.classList=cl(e);"
		"e.appendChild=function(c){return appendKid(e,c);};"
		"e.insertBefore=function(c,r){if(!r)return appendKid(e,c);var i=e.childNodes.indexOf(r);"
		"if(i<0)return appendKid(e,c);c.parentNode=e;c.parentElement=e;e.childNodes.splice(i,0,c);resyncKids(e);return c;};"
		"e.removeChild=function(c){var i=e.childNodes.indexOf(c);if(i>=0){e.childNodes.splice(i,1);"
		"c.parentNode=null;c.parentElement=null;resyncKids(e);}return c;};"
		"e.setAttribute=function(n,v){n=String(n);v=String(v);e.attributes[n]=v;if(n==='id')e.id=v;else if(n==='class')e.className=v;"
		"else if(n==='href'&&tag==='A')setAnchor(e,v);else if(n==='for')e.htmlFor=v;else e[n]=v;syncData(e,n,v);};"
		"e.getAttribute=function(n){n=String(n);return e.attributes.hasOwnProperty(n)?e.attributes[n]:null;};"
		"e.hasAttribute=function(n){return e.attributes.hasOwnProperty(String(n));};"
		"e.removeAttribute=function(n){n=String(n);delete e.attributes[n];if(n==='id')e.id='';else if(n==='class')e.className='';delData(e,n);};"
		"eventful(e);"
		"e.focus=noop;e.blur=noop;"
		"e.querySelector=function(s){var r=tagSearch(e,s);return r.length?r[0]:null;};e.querySelectorAll=function(s){return tagSearch(e,s);};"
		"e.getElementsByTagName=function(t){return tagSearch(e,t);};e.getElementsByClassName=arr;"
		"e.cloneNode=function(deep){return cloneElem(e,!!deep);};return e;}"
		"g.window=g.window||g;g.self=g.self||g;g.globalThis=g.globalThis||g;"
		"g.top=g.top||g;g.parent=g.parent||g;g.frames=g.frames||g;g.length=g.length||0;"
		"eventful(g);var rawAddEvt=g.addEventListener;g.addEventListener=function(n,f){rawAddEvt.call(g,n,f);};"
		"g.scroll=g.scroll||noop;g.scrollTo=g.scrollTo||g.scroll;g.scrollBy=g.scrollBy||noop;"
		"var nextTimer=1,timerDepth=0,timerCalls=0;g.__leonosExtraTimerBudget=g.__leonosExtraTimerBudget||0;"
		"g.setTimeout=g.setTimeout||function(f){var id=nextTimer++,maxDepth=Number(g.__leonosTimerDepthLimit||2)||2;"
		"if(typeof f==='function'&&timerDepth<maxDepth){var extra=Number(g.__leonosExtraTimerBudget||0)||0;"
		"if(timerCalls<32||extra>0){timerDepth++;if(timerCalls<32)timerCalls++;else g.__leonosExtraTimerBudget=extra-1;"
		"try{f();}catch(e){try{g._DumpException(e);}catch(x){}}timerDepth--;}}return id;};"
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
		"function Obs(cb){this.callback=cb||noop;}Obs.prototype.observe=function(t){var cb=this.callback;if(typeof cb==='function'){try{cb([{target:t,isIntersecting:true,intersectionRatio:1,boundingClientRect:rect(t),intersectionRect:rect(t),rootBounds:null}],this);}catch(e){try{g._DumpException(e);}catch(x){}}}};Obs.prototype.unobserve=noop;"
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
		"function absUrl(u){var a=el('a');setAnchor(a,String(u||''));return a.href||String(u||'');}"
		"if(!Object.fromEntries)Object.fromEntries=function(it){var o={},e,i;if(!it)return o;if(typeof it[Symbol.iterator]==='function'){for(var a=it[Symbol.iterator]();!(e=a.next()).done;){i=e.value;if(i)o[i[0]]=i[1];}}return o;};"
		"if(!g.Headers){g.Headers=function(init){this._m={};var self=this;function add(k,v){self.set(k,v);}if(init instanceof g.Headers)init.forEach(add);else if(init&&typeof init.length==='number'){for(var i=0;i<init.length;i++)if(init[i])add(init[i][0],init[i][1]);}else if(init&&typeof init==='object'){for(var k in init)if(init.hasOwnProperty(k))add(k,init[k]);}};"
		"var hp=g.Headers.prototype;hp.append=function(k,v){k=String(k||'').toLowerCase();this._m[k]=this._m[k]?this._m[k]+', '+String(v):String(v);};hp.set=function(k,v){this._m[String(k||'').toLowerCase()]=String(v);};hp.get=function(k){k=String(k||'').toLowerCase();return this._m.hasOwnProperty(k)?this._m[k]:null;};hp.has=function(k){return this.get(k)!==null;};hp.delete=function(k){delete this._m[String(k||'').toLowerCase()];};"
		"hp.forEach=function(f,t){for(var k in this._m)if(this._m.hasOwnProperty(k))f.call(t,this._m[k],k,this);};hp.entries=function(){var a=[];this.forEach(function(v,k){a.push([k,v]);});return uspIter(a);};hp.keys=function(){var a=[];this.forEach(function(v,k){a.push(k);});return uspIter(a);};hp.values=function(){var a=[];this.forEach(function(v){a.push(v);});return uspIter(a);};hp[Symbol.iterator]=hp.entries;}"
		"function strBuf(s){s=String(s||'');var a=new Uint8Array(s.length);for(var i=0;i<s.length;i++)a[i]=s.charCodeAt(i)&255;return a.buffer;}"
		"function bodyStr(b){if(b==null)return '';if(g.URLSearchParams&&b instanceof g.URLSearchParams)return b.toString();"
		"if(typeof ArrayBuffer!=='undefined'&&b instanceof ArrayBuffer){var aa=new Uint8Array(b),ss='';for(var ai=0;ai<aa.length;ai++)ss+=String.fromCharCode(aa[ai]);return ss;}"
		"if(typeof ArrayBuffer!=='undefined'&&ArrayBuffer.isView&&ArrayBuffer.isView(b)){var av=new Uint8Array(b.buffer,b.byteOffset||0,b.byteLength||b.length||0),sv='';for(var vi=0;vi<av.length;vi++)sv+=String.fromCharCode(av[vi]);return sv;}"
		"return String(b);}"
		"function reqData(input,init){init=init||{};var src=input&&typeof input==='object'?input:null,h=new g.Headers(src&&src.headers?src.headers:null);"
		"if(init.headers)new g.Headers(init.headers).forEach(function(v,k){h.set(k,v);});var hasBody=Object.prototype.hasOwnProperty.call(init,'body')||(src&&src._body!==void 0);"
		"var rawBody=Object.prototype.hasOwnProperty.call(init,'body')?init.body:(src&&src._body!==void 0?src._body:void 0);"
		"var m=String(init.method||(src&&src.method)||'GET').toUpperCase();if((m==='GET'||m==='HEAD')&&hasBody){rawBody=void 0;hasBody=false;}"
		"var ct=h.get('content-type')||'';if(!ct&&hasBody&&g.URLSearchParams&&rawBody instanceof g.URLSearchParams)ct='application/x-www-form-urlencoded;charset=UTF-8';"
		"else if(!ct&&hasBody&&typeof rawBody==='string')ct='text/plain;charset=UTF-8';"
		"return {url:absUrl(src&&src.url?src.url:input),method:m,headers:h,body:hasBody?bodyStr(rawBody):'',contentType:ct,accept:h.get('accept')||'*/*'};}"
		"function leonosResp(r,u){r=r||{};var b=String(r.body||''),s=Number(r.status||0)||0,ct=String(r.contentType||''),hh=new g.Headers();if(ct)hh.set('content-type',ct);"
		"return {ok:!!r.ok,status:s,statusText:'',url:String(r.url||u||''),headers:hh,bodyUsed:false,"
		"text:function(){this.bodyUsed=true;return Promise.resolve(b);},arrayBuffer:function(){this.bodyUsed=true;return Promise.resolve(strBuf(b));},"
		"json:function(){this.bodyUsed=true;try{return Promise.resolve(b?JSON.parse(b):null);}catch(e){return Promise.reject(e);}},"
		"clone:function(){return leonosResp(r,u);}};}"
		"g.Response=g.Response||function(body,init){init=init||{};var h=new g.Headers(init.headers||null),ct=h.get('content-type')||'';var r={ok:!init.status||init.status<400,status:init.status||200,contentType:ct,body:bodyStr(body)};return leonosResp(r,'');};"
		"if(!g.Request)g.Request=function(input,init){var r=reqData(input,init||{});this.url=r.url;this.method=r.method;this.headers=r.headers;this._body=r.body;this.bodyUsed=false;};"
		"if(g.Request&&!g.Request.prototype.clone)g.Request.prototype.clone=function(){return new g.Request(this,{method:this.method,headers:this.headers,body:this._body});};"
		"if(g.Request&&!g.Request.prototype.text)g.Request.prototype.text=function(){this.bodyUsed=true;return Promise.resolve(this._body||'');};"
		"if(!g.fetch&&g._leonosFetchText)g.fetch=function(input,init){var q=reqData(input,init||{});try{return Promise.resolve(leonosResp(g._leonosFetchText(q.url,q.method,q.body,q.contentType,q.accept),q.url));}catch(e){return Promise.reject(e);}};"
		"function xhrFire(x,t){return x.dispatchEvent?x.dispatchEvent(evtObj(t)):true;}"
		"if(!g.XMLHttpRequest&&g._leonosFetchText){var XHR=function(){eventful(this);this.readyState=0;this.responseType='';this.responseText='';this.response=null;this.responseURL='';this.status=0;this.statusText='';this.timeout=0;this.withCredentials=false;this.upload=eventful({});this._headers={};this._respHeaders={};};"
		"XHR.UNSENT=0;XHR.OPENED=1;XHR.HEADERS_RECEIVED=2;XHR.LOADING=3;XHR.DONE=4;var xp=XHR.prototype;xp.UNSENT=0;xp.OPENED=1;xp.HEADERS_RECEIVED=2;xp.LOADING=3;xp.DONE=4;"
		"xp.open=function(m,u,a){this._method=String(m||'GET').toUpperCase();this._url=absUrl(u||'');this._async=a!==false;this.readyState=1;xhrFire(this,'readystatechange');};"
		"xp.setRequestHeader=function(n,v){this._headers[String(n||'').toLowerCase()]=String(v||'');};"
		"xp.getResponseHeader=function(n){n=String(n||'').toLowerCase();return this._respHeaders.hasOwnProperty(n)?this._respHeaders[n]:null;};"
		"xp.getAllResponseHeaders=function(){var out='';for(var k in this._respHeaders)if(this._respHeaders.hasOwnProperty(k))out+=k+': '+this._respHeaders[k]+'\\r\\n';return out;};"
		"xp.overrideMimeType=function(t){this._overrideMime=String(t||'');};"
		"xp.abort=function(){this.readyState=0;this.status=0;this.responseText='';this.response=null;xhrFire(this,'abort');xhrFire(this,'loadend');};"
		"xp.send=function(body){var self=this;function finish(){var r,ct,rb,acc;try{self.readyState=2;xhrFire(self,'readystatechange');rb=(self._method==='GET'||self._method==='HEAD')?'':bodyStr(body);ct=self._headers['content-type']||'';acc=self._headers['accept']||'*/*';r=g._leonosFetchText(self._url,self._method||'GET',rb,ct,acc);self.status=Number(r.status||0)||0;self.statusText='';self.responseURL=String(r.url||self._url||'');ct=String(self._overrideMime||r.contentType||'');self._respHeaders={};if(ct)self._respHeaders['content-type']=ct;self.readyState=3;xhrFire(self,'readystatechange');self.responseText=String(r.body||'');if(self.responseType==='json'){try{self.response=self.responseText?JSON.parse(self.responseText):null;}catch(e){self.response=null;}}else if(self.responseType==='arraybuffer')self.response=strBuf(self.responseText);else self.response=self.responseText;self.readyState=4;xhrFire(self,'readystatechange');xhrFire(self,(self.status>=200&&self.status<400)?'load':'error');xhrFire(self,'loadend');}catch(e){self.status=0;self.readyState=4;xhrFire(self,'readystatechange');xhrFire(self,'error');xhrFire(self,'loadend');}}if(this._async)g.setTimeout(finish,0);else finish();};"
		"g.XMLHttpRequest=XHR;}"
		"function uspDec(s){try{return decodeURIComponent(String(s||'').replace(/\\+/g,' '));}catch(e){return String(s||'');}}"
		"function uspEnc(s){return encodeURIComponent(String(s)).replace(/%20/g,'+');}"
		"function uspIter(a){var i=0;return {next:function(){return i<a.length?{value:a[i++],done:false}:{value:void 0,done:true};},"
		"[Symbol.iterator]:function(){return this;}};}"
		"if(!g.URLSearchParams){g.URLSearchParams=function(init){this._pairs=[];var self=this;"
		"function add(k,v){self._pairs.push([String(k),String(v)]);}if(init==null)return;"
		"if(typeof init==='string'){var q=init.charAt(0)==='?'?init.slice(1):init;if(q)q.split('&').forEach(function(p){"
		"if(!p)return;var x=p.split('='),k=uspDec(x.shift()||''),v=uspDec(x.join('=')||'');add(k,v);});}"
		"else if(typeof init.length==='number'){for(var i=0;i<init.length;i++)if(init[i])add(init[i][0],init[i][1]);}"
		"else if(typeof init==='object'){for(var k in init)if(init.hasOwnProperty(k))add(k,init[k]);}};"
		"var up=g.URLSearchParams.prototype;up.append=function(k,v){this._pairs.push([String(k),String(v)]);};"
		"up.delete=function(k){k=String(k);this._pairs=this._pairs.filter(function(p){return p[0]!==k;});};"
		"up.get=function(k){k=String(k);for(var i=0;i<this._pairs.length;i++)if(this._pairs[i][0]===k)return this._pairs[i][1];return null;};"
		"up.getAll=function(k){k=String(k);var r=[];for(var i=0;i<this._pairs.length;i++)if(this._pairs[i][0]===k)r.push(this._pairs[i][1]);return r;};"
		"up.has=function(k){return this.get(k)!==null;};up.set=function(k,v){this.delete(k);this.append(k,v);};"
		"up.sort=function(){this._pairs.sort(function(a,b){return a[0]<b[0]?-1:(a[0]>b[0]?1:0);});};"
		"up.forEach=function(f,t){for(var i=0;i<this._pairs.length;i++)f.call(t,this._pairs[i][1],this._pairs[i][0],this);};"
		"up.toString=function(){return this._pairs.map(function(p){return uspEnc(p[0])+'='+uspEnc(p[1]);}).join('&');};"
		"up.entries=function(){return uspIter(this._pairs.map(function(p){return [p[0],p[1]];}));};"
		"up.keys=function(){return uspIter(this._pairs.map(function(p){return p[0];}));};"
		"up.values=function(){return uspIter(this._pairs.map(function(p){return p[1];}));};"
		"up[Symbol.iterator]=up.entries;}"
		"function absUrl(u,b){u=String(u||'');b=b===void 0?loc('href'):String(b||'');if(/^[A-Za-z][A-Za-z0-9+.-]*:/.test(u))return u;"
		"var bm=/^([A-Za-z][A-Za-z0-9+.-]*:)?(?:\\/\\/([^\\/?#]*))?([^?#]*)(\\?[^#]*)?(#.*)?$/.exec(b)||[],bp=bm[1]||loc('protocol')||'https:',bh=bm[2]||loc('host')||'',bd=bm[3]||'/';"
		"if(u.slice(0,2)==='//')return bp+u;if(u.charAt(0)==='/')return bp+'//'+bh+u;var i=bd.lastIndexOf('/'),dir=i>=0?bd.slice(0,i+1):'/';return bp+'//'+bh+dir+u;}"
		"g.URL=g.URL||function(u,b){this.href=absUrl(u,b);var m=/^([A-Za-z][A-Za-z0-9+.-]*:)?(?:\\/\\/([^\\/?#]*))?([^?#]*)(\\?[^#]*)?(#.*)?$/.exec(this.href)||[];"
		"this.protocol=m[1]||'';this.host=m[2]||'';var hp=this.host.split(':'),path=m[3]||'';this.hostname=hp[0]||'';this.port=hp[1]||'';"
		"this.pathname=path||'/';this.search=m[4]||'';this.hash=m[5]||'';this.origin=this.protocol&&this.host?this.protocol+'//'+this.host:'';"
		"this.searchParams=new g.URLSearchParams(this.search);};g.URL.prototype.toString=function(){return this.href;};"
		"function jqObj(a){a=a||arr();a.each=function(f){for(var i=0;i<a.length;i++)if(f)f.call(a[i],i,a[i]);return a;};"
		"a.on=a.bind=function(t,f){if(typeof t==='object'){for(var k in t)if(t.hasOwnProperty(k))a.on(k,t[k]);return a;}"
		"if(typeof f!=='function')return a;String(t||'').split(/\\s+/).forEach(function(n){if(!n)return;"
		"a.each(function(){if(this.addEventListener)this.addEventListener(n,f);});});return a;};"
		"a.off=a.unbind=function(t,f){String(t||'').split(/\\s+/).forEach(function(n){if(!n)return;"
		"a.each(function(){if(this.removeEventListener)this.removeEventListener(n,f);});});return a;};"
		"a.trigger=function(t){var e=typeof t==='string'?evtObj(t):(t||evtObj(''));a.each(function(){if(this.dispatchEvent)this.dispatchEvent(e);});return a;};"
		"a.delegate=function(s,t,f){return a.on(t,f);};a.undelegate=function(s,t,f){return a.off(t,f);};"
		"a.ready=function(f){if(typeof f==='function')g.setTimeout(f,0);return a;};"
		"a.click=function(f){return typeof f==='function'?a.on('click',f):a.trigger('click');};"
		"a.submit=function(f){return typeof f==='function'?a.on('submit',f):a.trigger('submit');};"
		"a.change=function(f){return typeof f==='function'?a.on('change',f):a.trigger('change');};"
		"a.keyup=function(f){return typeof f==='function'?a.on('keyup',f):a.trigger('keyup');};"
		"a.keydown=function(f){return typeof f==='function'?a.on('keydown',f):a.trigger('keydown');};"
		"a.focus=function(f){return typeof f==='function'?a.on('focus',f):a.trigger('focus');};"
		"a.blur=function(f){return typeof f==='function'?a.on('blur',f):a.trigger('blur');};"
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
		"function ajaxReq(u,o){if(u&&typeof u==='object'){o=u;u=o.url;}o=o||{};u=absUrl(u||'');if(!u||!g.fetch)return Promise.resolve({});"
		"return g.fetch(u,o).then(function(r){return r.text().then(function(t){if(o.dataType==='json'){try{return JSON.parse(t);}catch(e){return {};}}return t;});}).catch(function(){return {};});}"
		"g.$.ajax=g.jQuery.ajax=ajaxReq;g.$.get=g.jQuery.get=function(u){return ajaxReq(u,{method:'GET'});};g.$.post=g.jQuery.post=function(){return Promise.resolve({});};"
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
		"eu.beaconApi=eu.beaconApi||'https://metrics.roblox.com';eu.stripeCheckoutDomain=eu.stripeCheckoutDomain||'https://checkout.stripe.com';"
		"rb['core-scripts']=rb['core-scripts']||{};var cs=rb['core-scripts'];cs.environmentUrls=cs.environmentUrls||eu;"
		"cs.auth=cs.auth||{};cs.auth.xsrfToken=cs.auth.xsrfToken||{getToken:function(){return '';},setToken:noop,refreshToken:function(){return Promise.resolve('');}};"
		"function ok(v){return Promise.resolve({data:v||{},status:200});}"
		"function qval(v){return v==null?'':String(v);}function qstr(o){var a=[];if(!o)return '';for(var k in o)if(o.hasOwnProperty(k)){var v=o[k];if(v==null)continue;if(Array.isArray(v)){for(var i=0;i<v.length;i++)a.push(encodeURIComponent(k)+'='+encodeURIComponent(qval(v[i])));}else a.push(encodeURIComponent(k)+'='+encodeURIComponent(qval(v)));}return a.join('&');}"
		"function withQuery(u,p){var q=qstr(p);return q?u+(u.indexOf('?')>=0?'&':'?')+q:u;}"
		"function httpJson(m,u,d){u=absUrl(u&&u.url?u.url:u);m=String(m||'GET').toUpperCase();var o={method:m,credentials:'include',headers:{accept:'application/json'}};if(m==='GET')u=withQuery(u,d);else if(d!==void 0){o.body=JSON.stringify(d||{});o.headers['content-type']='application/json';}if(g.fetch&&u){console.log('LEONOS HTTP '+m+' '+u);return g.fetch(u,o).then(function(r){return r.text().then(function(t){var data={};try{data=t?JSON.parse(t):{};}catch(e){data={};}return {data:data,status:r.status};});}).catch(function(e){console.error(e);return {data:{},status:0};});}return ok({});}"
		"function httpGet(u,p){return httpJson('GET',u,p);}function httpPost(u,p){return httpJson('POST',u,p);}"
		"g.__leonosRobloxHttp={get:httpGet,post:httpPost,put:function(u,p){return httpJson('PUT',u,p);},delete:function(u,p){return httpJson('DELETE',u,p);}};"
		"g.__leonosInstallRobloxHttp=function(){var rb=g.Roblox=g.Roblox||{},cs=rb['core-scripts']=rb['core-scripts']||{};cs.http=cs.http||{};cs.http.http=g.__leonosRobloxHttp;if(g.CoreUtilities)g.CoreUtilities.httpService=g.__leonosRobloxHttp;if(g.CoreRobloxUtilities){g.CoreRobloxUtilities.http=g.__leonosRobloxHttp;g.CoreRobloxUtilities.httpService=g.__leonosRobloxHttp;}return g.__leonosRobloxHttp;};"
		"cs.http=cs.http||{};cs.http.http=cs.http.http||g.__leonosRobloxHttp;"
		"cs.eventStream=cs.eventStream||{sendEvent:noop,SendEvent:noop,SendEventWithTarget:noop,LocalEventLog:[],TargetTypes:{DEFAULT:0,WWW:1}};"
		"cs.eventStream.Init=cs.eventStream.Init||noop;cs.eventStream.init=cs.eventStream.init||cs.eventStream.Init;"
		"cs.eventStream.sendEvent=cs.eventStream.sendEvent||noop;cs.eventStream.sendEventWithTarget=cs.eventStream.sendEventWithTarget||noop;"
		"cs.eventStream.SendEvent=cs.eventStream.SendEvent||cs.eventStream.sendEvent;cs.eventStream.SendEventWithTarget=cs.eventStream.SendEventWithTarget||cs.eventStream.sendEventWithTarget;"
		"cs.eventStream.TargetTypes=cs.eventStream.TargetTypes||{DEFAULT:0,WWW:1};cs.eventStream.targetTypes=cs.eventStream.targetTypes||cs.eventStream.TargetTypes;"
		"cs.eventStream.eventTypes=cs.eventStream.eventTypes||{pageLoad:'pageLoad',formInteraction:'formInteraction',custom:'custom'};"
		"cs.eventStream.EventTypes=cs.eventStream.EventTypes||cs.eventStream.eventTypes;"
		"cs.eventStream.pageLoad=cs.eventStream.pageLoad||{sendPageLoad:noop,sendPageLoadTiming:noop,sendTiming:noop};"
		"cs.guac=cs.guac||{getTreatment:function(){return null;},getBoolTreatment:function(){return false;},getIntTreatment:function(){return 0;},getGuacValues:function(){return Promise.resolve({});},callBehaviour:function(){return Promise.resolve(null);}};"
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
		"function BatchRequestFactory(){}"
		"BatchRequestFactory.prototype.createExponentialBackoffCooldown=function(min,max){return function(){return Number(min||max||0)||0;};};"
		"BatchRequestFactory.prototype.createRequestProcessor=function(process,key,opts){opts=opts||{};return {queueItem:function(item){return Promise.resolve().then(function(){return process([item]);}).then(function(res){var want=key?String(key(item)):String(item&&item.taskId||'');if(Array.isArray(res)){if(res.length===1)return res[0];for(var i=0;i<res.length;i++){var got=key?String(key(res[i])):String(res[i]&&res[i].taskId||'');if(got===want)return res[i];}}if(res&&Array.isArray(res.data)){for(var j=0;j<res.data.length;j++){var gd=key?String(key(res.data[j])):String(res.data[j]&&res.data[j].taskId||'');if(gd===want)return res.data[j];}}return res;});},processBatch:function(items){return process(items||[]);},invalidateItem:noop,options:opts};};"
		"g.__leonosBatchRequestFactory=typeof g.__leonosBatchRequestFactory==='function'?g.__leonosBatchRequestFactory:BatchRequestFactory;"
		"cs.util.batchRequest=typeof cs.util.batchRequest==='function'?cs.util.batchRequest:g.__leonosBatchRequestFactory;"
		"g.CoreUtilities=g.CoreUtilities||{uuidService:{generateRandomUuid:function(){return cr.randomUUID();}},ready:cs.util.ready,url:cs.util.url};"
		"g.CoreUtilities.BatchRequestFactory=typeof g.CoreUtilities.BatchRequestFactory==='function'?g.CoreUtilities.BatchRequestFactory:g.__leonosBatchRequestFactory;"
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
		"langFill('Authentication.SignUp',{'Heading.CreateANewAccountSentenceCase':'Create a new account','Heading.DiscoverMillionsExperiences':'Discover millions of experiences','Message.Username.NoRealNameUse':'Do not use your real name.','Label.PasswordPlaceholder':'Password','Action.CreateAccountSentenceCase':'Create account','Label.AlreadyHaveAccountSignIn':'Already have an account? Sign in','Label.OptionalGender':'Gender (optional)','Label.TermsOfUse':'Terms of Use','Description.PrivacyPolicy':'Privacy Policy','Description.SignUpAgreement.FullCopy.FullParams':'By clicking Create Account, you agree to the Terms of Use and Privacy Policy.','Heading.ConfirmYourSelection':'Confirm your selection','Body.SignupExitAlmostDone':'You are almost done.','Label.SignInLowercase':'sign in','Label.BirthdayRequired':'Birthday is required'});"
		"langFill('Feature.Landing',{'Heading.DiscoverMillionsExperiences':'Discover millions of experiences','Heading.RobloxOnDevice':'Roblox on your device','Link.AppleAppStoreRobloxApp':'https://www.roblox.com/download','Label.RobloxAppStore':'Roblox on the App Store','Link.GooglePlayStoreRobloxApp':'https://www.roblox.com/download','Label.GetOnGooglePlay':'Get it on Google Play','Link.PlayStationStoreRobloxAppV2':'https://www.roblox.com/download','Label.PlayStationStoreRobloxApp':'Roblox on PlayStation','Link.XboxStoreRobloxApp':'https://www.roblox.com/download','Label.RobloxOnXbox':'Roblox on Xbox','Link.MetaQuestStoreRobloxApp':'https://www.roblox.com/download','Label.MetaQuestStoreRobloxApp':'Roblox on Meta Quest','Link.MicrosoftStoreRobloxApp':'https://www.roblox.com/download','Label.RobloxMicrosoftStore':'Roblox on Microsoft Store','Link.AmazonStoreRobloxApp':'https://www.roblox.com/download','Label.RobloxAmazonStore':'Roblox on Amazon','Link.GalaxyStoreRobloxApp':'https://www.roblox.com/download','Label.GalaxyStoreRobloxApp':'Roblox on Galaxy Store'});"
		"rb.Lang.get=rb.Lang.get||function(k){return String(k||'');};"
		"rb.Lang.getTranslationResource=rb.Lang.getTranslationResource||function(n){return rb.LangDynamic[String(n||'')]||{};};"
		"rb.Lang.getResource=rb.Lang.getResource||rb.Lang.getTranslationResource;rb.Lang.translate=rb.Lang.translate||rb.Lang.get;"
		"function thumbImg(p){p=p||{};var src=p.imageUrl||p.thumbnailUrl||p.src||p.url||(p.thumbnail&&p.thumbnail.imageUrl)||(p.thumbnail&&p.thumbnail.url)||'';"
		"var R=g.React,props={src:src,alt:p.alt||p.name||'',className:p.className||''};if(p.width)props.width=p.width;if(p.height)props.height=p.height;return R&&R.createElement?R.createElement('img',props):null;}"
		"rb.Thumbnails=rb.Thumbnails||{};rb.Thumbnails.Thumbnail2d=rb.Thumbnails.Thumbnail2d||thumbImg;"
		"rb.Thumbnails.ThumbnailTypes=rb.Thumbnails.ThumbnailTypes||{gameIcon:'gameIcon',gameThumbnail:'gameThumbnail',assetThumbnail:'assetThumbnail',avatarHeadshot:'avatarHeadshot'};"
		"rb.Thumbnails.ThumbnailFormat=rb.Thumbnails.ThumbnailFormat||{jpeg:'jpeg',webp:'png',png:'png'};"
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
		"d.nodeType=9;d.readyState='loading';d.cookie=d.cookie||'';d.referrer=d.referrer||'';"
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
		"d.getElementsByClassName=arr;eventful(d);var docAddEvt=d.addEventListener;d.addEventListener=function(n,f){docAddEvt.call(d,n,f);};"
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

static void qjs_refresh_runtime_shims(JSContext *ctx)
{
	static const char shims[] =
		"(function(g){"
		"function noop(){return void 0;}"
		"var BF=g.__leonosBatchRequestFactory;"
		"if(typeof BF!=='function'){"
		"BF=function(){};"
		"BF.prototype.createExponentialBackoffCooldown=function(min,max){return function(){return Number(min||max||0)||0;};};"
		"BF.prototype.createRequestProcessor=function(process,key,opts){opts=opts||{};return {queueItem:function(item){return Promise.resolve().then(function(){return process([item]);}).then(function(res){var want=key?String(key(item)):String(item&&item.taskId||'');if(Array.isArray(res)){if(res.length===1)return res[0];for(var i=0;i<res.length;i++){var got=key?String(key(res[i])):String(res[i]&&res[i].taskId||'');if(got===want)return res[i];}}if(res&&Array.isArray(res.data)){for(var j=0;j<res.data.length;j++){var gd=key?String(key(res.data[j])):String(res.data[j]&&res.data[j].taskId||'');if(gd===want)return res.data[j];}}return res;});},processBatch:function(items){return process(items||[]);},invalidateItem:noop,options:opts};};"
		"g.__leonosBatchRequestFactory=BF;"
		"}"
		"var rb=g.Roblox=g.Roblox||{};"
		"var cs=rb['core-scripts']=rb['core-scripts']||{};"
		"cs.util=cs.util||{};"
		"if(typeof cs.util.batchRequest!=='function')cs.util.batchRequest=BF;"
		"g.CoreUtilities=g.CoreUtilities||{};"
		"if(typeof g.CoreUtilities.BatchRequestFactory!=='function')g.CoreUtilities.BatchRequestFactory=BF;"
		"g.CoreRobloxUtilities=g.CoreRobloxUtilities||{};"
		"g.CoreRobloxUtilities.coreScripts=g.CoreRobloxUtilities.coreScripts||cs;"
		"})(globalThis);";
	JSValue result = JS_Eval(ctx, shims, sizeof(shims) - 1u,
				 "leonos-quickjs-runtime-shims.js",
				 JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(result)) {
		qjs_dump_exception(ctx, "leonos-quickjs-runtime-shims.js");
	}
	JS_FreeValue(ctx, result);
}

static void qjs_maybe_run_selector_selftest(JSContext *ctx)
{
#ifdef LEONOS_USER_APP
	jsthread *thread = JS_GetContextOpaque(ctx);
	static const char selftest[] =
		"(function(){try{"
		"var body=document.body||document.documentElement;"
		"if(!body||!body.appendChild)return 'wait';"
		"var root=document.createElement('div'),span=document.createElement('span'),"
		"em=document.createElement('em'),strong=document.createElement('strong');"
		"root.id='selector-root';span.className='a b';em.id='leaf';em.className='b';"
		"strong.className='after';root.appendChild(span);span.appendChild(em);"
		"root.appendChild(strong);body.appendChild(root);"
		"var direct=root.querySelectorAll('span.a > em#leaf, strong.missing');"
		"var desc=root.querySelectorAll('div.missing, span.a em.b');"
		"var adjacent=root.querySelectorAll('span.a + strong.after');"
		"var general=root.querySelectorAll('span.a ~ strong.after');"
		"var negated=root.querySelectorAll('span.a:not(.missing) > em#leaf');"
		"function isLeaf(n){return n&&n.id==='leaf'&&String(n.className||'').indexOf('b')>=0;}"
		"function isStrong(n){return n&&String(n.className||'').indexOf('after')>=0;}"
		"var closestRoot=em.closest&&em.closest('div#selector-root');"
		"var closestSpan=em.closest&&em.closest('span.a:not(.missing)');"
		"var matchOk=em.matches&&strong.matches&&em.matches('span.a:not(.missing) > em#leaf')&&"
		"strong.matches('span.a + strong.after')&&!em.matches('strong.after')&&"
		"closestRoot&&closestRoot.id==='selector-root'&&closestSpan&&"
		"String(closestSpan.className||'').indexOf('a')>=0&&!em.closest('strong.after');"
		"var ok=direct.length===1&&isLeaf(direct[0])&&desc.length===1&&"
		"isLeaf(desc[0])&&adjacent.length===1&&isStrong(adjacent[0])&&"
		"general.length===1&&isStrong(general[0])&&negated.length===1&&"
		"isLeaf(negated[0])&&matchOk;"
		"if(root.parentNode&&root.parentNode.removeChild)root.parentNode.removeChild(root);"
		"console.log('NETSURF QUICKJS SELECTOR SELFTEST '+(ok?'ok':'fail'));"
		"return ok?'ok':'fail';"
		"}catch(e){console.log('NETSURF QUICKJS SELECTOR SELFTEST exception '+"
		"(e&&e.message||e));return 'exception';}})();";
	JSValue global;
	JSValue document;
	const char *status;
	if (thread == NULL || thread->selector_selftest_ran) {
		return;
	}
	global = JS_GetGlobalObject(ctx);
	document = JS_GetPropertyStr(ctx, global, "document");
	if (JS_IsObject(document)) {
		qjs_document_bind_real_nodes(ctx, document);
	}
	JS_FreeValue(ctx, document);
	JS_FreeValue(ctx, global);
	JSValue result = JS_Eval(ctx, selftest, sizeof(selftest) - 1u,
				 "leonos-quickjs-selector-selftest.js",
				 JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(result)) {
		qjs_dump_exception(ctx, "leonos-quickjs-selector-selftest.js");
		thread->selector_selftest_ran = true;
	} else {
		status = JS_ToCString(ctx, result);
		if (status == NULL || strcmp(status, "wait") != 0) {
			thread->selector_selftest_ran = true;
		}
		if (status != NULL) {
			JS_FreeCString(ctx, status);
		}
	}
	JS_FreeValue(ctx, result);
#else
	(void) ctx;
#endif
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
	qjs_install_function(ctx, global, "_leonosFetchText",
			     qjs_leonos_fetch_text, 1);

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
	qjs_refresh_runtime_shims(ctx);
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
	qjs_install_function(ctx, document, "getElementsByName",
			     qjs_document_get_elements_by_name, 1);
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

static void qjs_drain_pending_jobs(jsheap *heap, const char *name)
{
	JSContext *job_ctx = NULL;
	unsigned int jobs = 0u;

	if (heap == NULL || heap->runtime == NULL) {
		return;
	}
	while (JS_IsJobPending(heap->runtime) && jobs < LEONOS_QUICKJS_JOB_LIMIT) {
		int status = JS_ExecutePendingJob(heap->runtime, &job_ctx);
		if (status < 0) {
#ifdef LEONOS_USER_APP
			if (job_ctx == NULL) {
				leonos_write("NETSURF QUICKJS JOB EXCEPTION missing context\r\n");
			}
#endif
			if (job_ctx != NULL) {
				qjs_dump_exception(job_ctx,
						   name != NULL ? name : "pending-job");
			}
			break;
		}
		if (status == 0) {
			break;
		}
		jobs += 1u;
	}
#ifdef LEONOS_USER_APP
	if (jobs != 0u) {
		char detail[96];
		int detail_len = snprintf(detail, sizeof(detail),
			"NETSURF QUICKJS JOBS drained=%u\r\n",
			(unsigned int) jobs);
		if (detail_len > 0) {
			leonos_write(detail);
		}
	}
	if (jobs >= LEONOS_QUICKJS_JOB_LIMIT && JS_IsJobPending(heap->runtime)) {
		leonos_write("NETSURF QUICKJS JOBS limit\r\n");
	}
#else
	(void) name;
#endif
}

static void qjs_boost_route_timer_budget(JSContext *ctx, const char *name)
{
	static const char budget_script[] =
		"globalThis.__leonosExtraTimerBudget="
		"Math.max(Number(globalThis.__leonosExtraTimerBudget||0)||0,96);"
		"globalThis.__leonosTimerDepthLimit="
		"Math.max(Number(globalThis.__leonosTimerDepthLimit||2)||2,5);"
		"if(globalThis.__leonosInstallRobloxHttp)globalThis.__leonosInstallRobloxHttp();"
		"(function(g){function patch(R){if(!R||typeof R.useEffect!=='function'||R.__leonosChartsEffectShim)return;"
		"var orig=R.useEffect;R.__leonosChartsEffectShim=true;g.__leonosRouteEffectRuns=g.__leonosRouteEffectRuns||0;"
		"console.log('LEONOS ROUTE SHIM installed');"
		"R.useEffect=function(fn,deps){try{if(typeof fn==='function'&&g.__leonosRouteEffectRuns<48){"
		"var id=++g.__leonosRouteEffectRuns;Promise.resolve().then(function(){try{console.log('LEONOS ROUTE EFFECT run '+id);fn();}"
		"catch(e){try{g._DumpException(e);}catch(x){}}});}}catch(e){}return orig.apply(this,arguments);};}"
		"if(g.React)patch(g.React);else if(Object.defineProperty&&!g.__leonosReactSetter){g.__leonosReactSetter=true;"
		"var rv;Object.defineProperty(g,'React',{configurable:true,get:function(){return rv;},set:function(v){rv=v;patch(v);}});}})(globalThis);";
	JSValue result;

	if (ctx == NULL || name == NULL ||
	    strstr(name, "js.rbxcdn.com/") == NULL ||
	    (strstr(name, "GameCarousel.") == NULL &&
	     strstr(name, "ReactLanding.") == NULL &&
	     strstr(name, "SearchLandingPage.") == NULL)) {
		return;
	}
#ifdef LEONOS_USER_APP
	leonos_write("NETSURF QUICKJS ROUTE BOOST ");
	leonos_write(name);
	leonos_write("\r\n");
#endif
	result = JS_Eval(ctx, budget_script, sizeof(budget_script) - 1u,
			 "leonos-quickjs-route-timer-budget.js",
			 JS_EVAL_TYPE_GLOBAL);
	JS_FreeValue(ctx, result);
}

static void qjs_run_roblox_thumbnail_probe(JSContext *ctx, const char *name)
{
	static const char probe[] =
		"(function(g){try{"
		"if(!g._leonosFetchText||g.__leonosEarlyThumbnailDone)return;"
		"g.__leonosEarlyThumbnailDone=true;"
		"console.log('LEONOS ROUTE PROBE early start');"
		"function pickHost(){var d=g.document,h=null,b=null;if(!d)return null;"
		"try{h=d.getElementById&&d.getElementById('react-landing-container');}catch(e){}"
		"if(h&&h.appendChild)return h;"
		"try{b=d.getElementsByTagName&&d.getElementsByTagName('body');h=b&&b.length?b[0]:null;}catch(e){}"
		"return (h&&h.appendChild)?h:(d.body||d.documentElement);}"
		"function append(u){try{var d=g.document,h=pickHost();if(!d||!h||!u)return false;"
		"var im=d.createElement('img');im.src=u;im.alt='Roblox thumbnail';im.width=256;im.height=256;"
		"if(im.style){im.style.width='256px';im.style.height='256px';im.display='block';im.margin='12px 0';}"
		"if(im.setAttribute){im.setAttribute('src',u);im.setAttribute('alt','Roblox thumbnail');"
		"im.setAttribute('width','256');im.setAttribute('height','256');}"
		"h.appendChild(im);console.log('LEONOS ROUTE IMG append '+u);return true;}catch(e){console.error(e);return false;}}"
		"function get(u){var r=g._leonosFetchText(u,'GET','','','application/json');"
		"console.log('LEONOS ROUTE SYNC fetch '+(r&&r.status)+' '+u);"
		"try{return JSON.parse(String(r&&r.body||'{}'));}catch(e){return {};}}"
		"function firstId(o,d){if(!o||d>7)return null;if(Array.isArray(o)){for(var i=0;i<o.length;i++){var a=firstId(o[i],d+1);if(a)return a;}return null;}"
		"if(typeof o==='object'){if(o.universeId)return {type:'universe',id:o.universeId};"
		"if(o.rootPlaceId)return {type:'place',id:o.rootPlaceId};if(o.placeId)return {type:'place',id:o.placeId};"
		"if(o.assetId)return {type:'asset',id:o.assetId};for(var k in o)if(o.hasOwnProperty(k)){var r=firstId(o[k],d+1);if(r)return r;}}return null;}"
		"function imageUrl(j){var a=j&&j.data;if(a&&a.length&&a[0]&&a[0].imageUrl)return a[0].imageUrl;return '';}"
		"var sorts=get('https://apis.roblox.com/explore-api/v1/get-sorts?sessionId=leonos'),sort='top-trending';"
		"if(sorts&&sorts.sorts){for(var i=0;i<sorts.sorts.length;i++){var s=sorts.sorts[i];if(s&&s.contentType==='Games'&&s.sortId){sort=s.sortId;break;}}}"
		"var data=get('https://apis.roblox.com/explore-api/v1/get-sort-content?sessionId=leonos&sortId='+encodeURIComponent(sort));"
		"var id=firstId(data,0),url='';console.log('LEONOS ROUTE PROBE id '+(id&&id.type)+':'+(id&&id.id));"
		"if(id&&id.type==='universe')url=imageUrl(get('https://thumbnails.roblox.com/v1/games/icons?universeIds='+encodeURIComponent(id.id)+'&size=512x512&format=Png&isCircular=false'));"
		"if(!url&&id&&id.type==='place')url=imageUrl(get('https://thumbnails.roblox.com/v1/places/gameicons?placeIds='+encodeURIComponent(id.id)+'&size=512x512&format=Png&isCircular=false'));"
		"if(!url&&id&&id.type==='asset')url=imageUrl(get('https://thumbnails.roblox.com/v1/assets?assetIds='+encodeURIComponent(id.id)+'&size=420x420&format=Png&isCircular=false'));"
		"console.log('LEONOS ROUTE PROBE imageUrl '+url);append(url);"
		"}catch(e){console.error(e);}})(globalThis);";
	JSValue result;

	if (ctx == NULL || name == NULL ||
	    strstr(name, "js.rbxcdn.com/") == NULL ||
	    strstr(name, "ReactLanding.") == NULL) {
		return;
	}
#ifdef LEONOS_USER_APP
	leonos_write("NETSURF QUICKJS ROUTE PROBE early ");
	leonos_write(name);
	leonos_write("\r\n");
#endif
	result = JS_Eval(ctx, probe, sizeof(probe) - 1u,
			 "leonos-quickjs-roblox-thumbnail-probe.js",
			 JS_EVAL_TYPE_GLOBAL);
	if (JS_IsException(result)) {
		qjs_dump_exception(ctx,
			"leonos-quickjs-roblox-thumbnail-probe.js");
	}
	JS_FreeValue(ctx, result);
}

static bool qjs_inject_gamecarousel_probe(const char *name, char **eval_copy,
					  size_t *eval_len)
{
	static const char marker[] =
		"}()}(),window.Roblox&&window.Roblox.BundleDetector";
	static const char probe[] =
		";try{console.log('LEONOS ROUTE PROBE start');"
		"var __leonosAppendProbeImg=function(u){try{if(!u)return;"
		"var bs=document.getElementsByTagName&&document.getElementsByTagName('body'),host=bs&&bs.length?bs[0]:(document.body||document.documentElement);"
		"var im=document.createElement('img');im.src=u;im.alt='Roblox thumbnail';"
		"im.width=384;im.height=216;if(im.style){im.style.width='384px';im.style.height='216px';}"
		"if(im.setAttribute){im.setAttribute('src',u);im.setAttribute('alt','Roblox thumbnail');im.setAttribute('width','384');im.setAttribute('height','216');}"
		"console.log('LEONOS ROUTE IMG append '+(host&&host.tagName||'?')+' '+u);"
		"host&&host.appendChild&&host.appendChild(im);}catch(e){console.error(e);}};"
		"var __leonosFindId=function f(o,d){if(!o||d>7)return null;"
		"if(Array.isArray(o)){for(var i=0;i<o.length;i++){var a=f(o[i],d+1);if(a)return a;}return null;}"
		"if(typeof o==='object'){var ks=['rootPlaceId','placeId','assetId'];"
		"for(var k=0;k<ks.length;k++){var v=o[ks[k]];if((typeof v==='number'&&v>0)||(typeof v==='string'&&v))return v;}"
		"for(var p in o)if(o.hasOwnProperty(p)){var r=f(o[p],d+1);if(r)return r;}}return null;};"
		"var __leonosPickSort=function(d){var a=d&&d.sorts;if(!a||!a.length)return 'top-trending';"
		"for(var i=0;i<a.length;i++)if(a[i]&&a[i].contentType==='Games'&&a[i].sortId)return a[i].sortId;"
		"return 'top-trending';};"
		"var __leonosProbeSync=function(){try{if(!globalThis._leonosFetchText||globalThis.__leonosRouteSyncDone)return;"
		"globalThis.__leonosRouteSyncDone=true;"
		"var r=globalThis._leonosFetchText('https://apis.roblox.com/explore-api/v1/get-sorts?sessionId=leonos','GET','','','application/json');"
		"console.log('LEONOS ROUTE SYNC sorts status '+(r&&r.status));"
		"var d=JSON.parse(String(r&&r.body||'{}')),sort=__leonosPickSort(d);console.log('LEONOS ROUTE PROBE sorts');"
		"var cu='https://apis.roblox.com/explore-api/v1/get-sort-content?sessionId=leonos&sortId='+encodeURIComponent(sort);"
		"var cr=globalThis._leonosFetchText(cu,'GET','','','application/json');"
		"console.log('LEONOS ROUTE SYNC content status '+(cr&&cr.status)+' sort '+sort);"
		"var cd=JSON.parse(String(cr&&cr.body||'{}')),id=__leonosFindId(cd);console.log('LEONOS ROUTE PROBE id '+id);"
		"if(id){var tu='https://thumbnails.roblox.com/v1/assets?assetIds='+encodeURIComponent(id)+'&size=768x432&format=Png';"
		"var tr=globalThis._leonosFetchText(tu,'GET','','','application/json');"
		"console.log('LEONOS ROUTE SYNC thumbnail status '+(tr&&tr.status));"
		"var td=JSON.parse(String(tr&&tr.body||'{}')),row=td&&td.data&&td.data[0],u=row&&row.imageUrl;"
		"console.log('LEONOS ROUTE PROBE imageUrl '+u);__leonosAppendProbeImg(u);}}catch(e){console.error(e);}};"
		"__leonosProbeSync();"
		"tm('leonos',null,[],{}).then(function(d){console.log('LEONOS ROUTE PROBE sorts');"
		"var id=__leonosFindId(d);console.log('LEONOS ROUTE PROBE id '+id);"
		"if(id)return tf(id).then(function(u){console.log('LEONOS ROUTE PROBE imageUrl '+u);"
		"__leonosAppendProbeImg(u);return u;});});"
		"}catch(e){console.error(e);}";
	char *pos;
	char *next;
	size_t prefix;
	size_t marker_offset;
	size_t suffix_len;
	size_t probe_len;

	if (name == NULL || eval_copy == NULL || *eval_copy == NULL ||
	    eval_len == NULL ||
	    strstr(name, "js.rbxcdn.com/") == NULL ||
	    strstr(name, "GameCarousel.") == NULL) {
		return false;
	}
	pos = strstr(*eval_copy, marker);
	if (pos == NULL) {
		return false;
	}
	prefix = (size_t)(pos - *eval_copy);
	marker_offset = prefix;
	suffix_len = *eval_len - marker_offset;
	probe_len = sizeof(probe) - 1u;
	next = malloc(*eval_len + probe_len + 1u);
	if (next == NULL) {
		return false;
	}
	memcpy(next, *eval_copy, prefix);
	memcpy(next + prefix, probe, probe_len);
	memcpy(next + prefix + probe_len, *eval_copy + marker_offset, suffix_len);
	next[*eval_len + probe_len] = 0;
	free(*eval_copy);
	*eval_copy = next;
	*eval_len += probe_len;
#ifdef LEONOS_USER_APP
	leonos_write("NETSURF QUICKJS ROUTE PROBE injected\r\n");
#endif
	return true;
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
	if (thread->active_script_react_landing_candidate != NULL) {
		dom_node_unref(thread->active_script_react_landing_candidate);
		thread->active_script_react_landing_candidate = NULL;
	}
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
	if (qjs_inject_gamecarousel_probe(name, &eval_copy, &eval_len)) {
		eval_text = eval_copy;
	}
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
	qjs_process_pending_redraws();
	qjs_refresh_runtime_shims(thread->ctx);
	qjs_maybe_run_selector_selftest(thread->ctx);
	qjs_boost_route_timer_budget(thread->ctx, name);
	qjs_begin_script_interrupt(thread->heap, eval_len, name);
	thread->active_script_name = name;
	thread->active_script_dom_appends = 0u;
	thread->active_script_dom_budget_hit = false;
	thread->active_script_dom_native_budget_hit = false;
	thread->active_script_react_landing_attached = false;
	if (thread->active_script_react_landing_candidate != NULL) {
		dom_node_unref(thread->active_script_react_landing_candidate);
		thread->active_script_react_landing_candidate = NULL;
	}
	result = JS_Eval(thread->ctx, eval_text, eval_len,
			 name != NULL ? name : "inline-script",
			 JS_EVAL_TYPE_GLOBAL);
	uint32_t interrupt_limit = thread->heap != NULL ?
		thread->heap->interrupt_limit : 0u;
	if (!JS_IsException(result)) {
		qjs_drain_pending_jobs(thread->heap, name);
	}
	bool interrupted = qjs_end_script_interrupt(thread->heap);
	qjs_attach_react_landing_candidate(thread->ctx);
	html_leonos_dom_flush_mutations(thread->htmlc);
	qjs_process_pending_redraws();
	qjs_run_roblox_thumbnail_probe(thread->ctx, name);
	html_leonos_dom_flush_mutations(thread->htmlc);
	qjs_process_pending_redraws();
	bool cooperative_react_stop = JS_IsException(result) &&
		interrupted &&
		thread->active_script_react_landing_attached &&
		name != NULL &&
		strstr(name, "js.rbxcdn.com/") != NULL &&
		strstr(name, "ReactLanding.") != NULL;
	thread->active_script_name = NULL;
	thread->active_script_dom_appends = 0u;
	thread->active_script_dom_budget_hit = false;
	thread->active_script_dom_native_budget_hit = false;
	thread->active_script_react_landing_attached = false;
	if (thread->active_script_react_landing_candidate != NULL) {
		dom_node_unref(thread->active_script_react_landing_candidate);
		thread->active_script_react_landing_candidate = NULL;
	}
	if (JS_IsException(result)) {
		if (cooperative_react_stop) {
#ifdef LEONOS_USER_APP
			leonos_write("NETSURF QUICKJS DOM REACT COOPERATIVE STOP ");
			leonos_write(name != NULL ? name : "?script?");
			leonos_write("\r\n");
#endif
			JSValue exception = JS_GetException(thread->ctx);
			JS_FreeValue(thread->ctx, exception);
			JS_FreeValue(thread->ctx, result);
			free(eval_copy);
			return true;
		}
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
	JSContext *ctx;
	JSValue global;
	JSValue document;

	(void) doc;
	(void) target;
	if (thread == NULL || thread->ctx == NULL || type == NULL) {
		return false;
	}
	ctx = thread->ctx;
	global = JS_GetGlobalObject(ctx);
	document = JS_GetPropertyStr(ctx, global, "document");
	if (!JS_IsObject(document)) {
		JS_FreeValue(ctx, document);
		JS_FreeValue(ctx, global);
		return false;
	}
	if (strcmp(type, "load") == 0) {
		qjs_set_string(ctx, document, "readyState", "complete");
	}
	JS_FreeValue(ctx, document);
	JS_FreeValue(ctx, global);
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
