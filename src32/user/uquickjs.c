#include "leonos_user.h"
#include "quickjs.h"

static unsigned int cstr_len(const char *text)
{
    unsigned int len = 0u;
    while (text[len] != 0) {
        len += 1u;
    }
    return len;
}

static void write_uint(leonos_u32 value)
{
    char buf[12];
    leonos_u32 pos = 0u;
    char tmp[12];
    leonos_u32 len = 0u;

    if (value == 0u) {
        leonos_write("0");
        return;
    }

    while (value != 0u && len < sizeof(tmp)) {
        tmp[len++] = (char) ('0' + (value % 10u));
        value /= 10u;
    }
    while (len != 0u && pos + 1u < sizeof(buf)) {
        buf[pos++] = tmp[--len];
    }
    buf[pos] = 0;
    leonos_write(buf);
}

static int run_eval(JSContext *ctx, const char *label, const char *source,
                    int expected)
{
    int value = 0;
    JSValue result;

    leonos_write("UQJS eval ");
    leonos_write(label);
    leonos_write("\r\n");

    result = JS_Eval(ctx, source, cstr_len(source), label,
                     JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(ctx);
        const char *message = JS_ToCString(ctx, exception);
        leonos_write("UQJS exception ");
        leonos_write(message != 0 ? message : "(no message)");
        leonos_write("\r\n");
        JS_FreeCString(ctx, message);
        JS_FreeValue(ctx, exception);
        JS_FreeValue(ctx, result);
        return 0;
    }

    if (JS_ToInt32(ctx, &value, result) < 0) {
        leonos_write("UQJS result conversion failed\r\n");
        JS_FreeValue(ctx, result);
        return 0;
    }
    JS_FreeValue(ctx, result);

    leonos_write("UQJS result ");
    write_uint((leonos_u32) value);
    leonos_write("\r\n");
    return value == expected;
}

int leonos_user_main(void)
{
    JSRuntime *rt;
    JSContext *ctx;
    int ok;

    leonos_write("UQJS QuickJS proof app\r\n");

    rt = JS_NewRuntime();
    if (rt == 0) {
        leonos_write("UQJS runtime alloc failed\r\n");
        return 1;
    }
    JS_SetMemoryLimit(rt, 2u * 1024u * 1024u);
    JS_SetGCThreshold(rt, 512u * 1024u);

    ctx = JS_NewContext(rt);
    if (ctx == 0) {
        leonos_write("UQJS context alloc failed\r\n");
        JS_FreeRuntime(rt);
        return 1;
    }

    ok = run_eval(ctx, "modern-js-1",
                  "let xs=[1,2,3]; xs.map(x=>x*x).reduce((a,b)=>a+b,0);",
                  14);
    ok = ok && run_eval(ctx, "modern-js-2",
                        "class Box{#x=40; get v(){return this.#x+2}}; "
                        "new Box().v;",
                        42);
    ok = ok && run_eval(ctx, "modern-js-3",
                        "let obj={a:{b:21}}; (obj?.a?.b ?? 0) * 2;",
                        42);

    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    if (!ok) {
        leonos_write("UQJS FAIL\r\n");
        return 1;
    }
    leonos_write("UQJS OK modern JavaScript core\r\n");
    return 0;
}
