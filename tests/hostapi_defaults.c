#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "../src/umka_api.h"


static const char *source =
    "type DefaultsFn* = fn (a: int, b: int = 2, u: uint = 3, flag: bool = true, ch: char = 'A', r32: real32 = 1.5, r: real = 2.5, text: str = \"ok\", p: ^int = null): int\n"
    "type MixedFn* = fn (a: int, b: int = 4, label: str = \"xyz\"): int\n"
    "type AnyDefaultFn* = fn (value: any = null): int\n"
    "type NoDefaultFn* = fn (a: int, b: int): int\n"
    "\n"
    "fn checkDefaults*(a: int, b: int = 2, u: uint = 3, flag: bool = true, ch: char = 'A', r32: real32 = 1.5, r: real = 2.5, text: str = \"ok\", p: ^int = null): int {\n"
    "    score := a + b + int(u) + int(ch) + len(text)\n"
    "    if flag {score = score + 10}\n"
    "    if r32 > 1.4 && r32 < 1.6 {score = score + 20}\n"
    "    if r > 2.4 && r < 2.6 {score = score + 30}\n"
    "    if p == null {score = score + 40}\n"
    "    return score\n"
    "}\n"
    "\n"
    "fn mixed*(a: int, b: int = 4, label: str = \"xyz\"): int {return a + b + len(label)}\n"
    "fn anyDefault*(value: any = null): int {return 99}\n"
    "fn noDefault*(a: int, b: int): int {return a + b}\n"
    "fn acceptDefaults*(f: DefaultsFn): int {return 0}\n"
    "fn acceptMixed*(f: MixedFn): int {return 0}\n"
    "fn acceptAnyDefault*(f: AnyDefaultFn): int {return 0}\n"
    "fn acceptNoDefault*(f: NoDefaultFn): int {return 0}\n"
    "fn acceptInt*(x: int): int {return x}\n";


static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}


static int failUmka(Umka *umka, const char *operation)
{
    UmkaError *error = umkaGetError(umka);
    fprintf(stderr, "%s failed: code %d, %s (%d, %d): %s\n",
            operation,
            error->code,
            error->fileName ? error->fileName : "<unknown>",
            error->line,
            error->pos,
            error->msg ? error->msg : "<no message>");
    return 1;
}


static int expectBool(const char *label, bool actual, bool expected)
{
    if (actual != expected)
    {
        fprintf(stderr, "%s: got %d, expected %d\n", label, actual ? 1 : 0, expected ? 1 : 0);
        return 1;
    }
    return 0;
}


static int expectInt64(const char *label, int64_t actual, int64_t expected)
{
    if (actual != expected)
    {
        fprintf(stderr, "%s: got %lld, expected %lld\n", label, (long long)actual, (long long)expected);
        return 1;
    }
    return 0;
}


static int getFunc(Umka *umka, const char *name, UmkaFuncContext *fn)
{
    if (!umkaGetFunc(umka, NULL, name, fn))
    {
        fprintf(stderr, "function not found: %s\n", name);
        return 1;
    }
    return 0;
}


static const UmkaType *getParamType(Umka *umka, const char *name)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, name, &fn))
        return NULL;
    return umkaGetParamType(fn.params, 0);
}


static int callAndExpect(Umka *umka, UmkaFuncContext *fn, int64_t expected, const char *label)
{
    int err = umkaCall(umka, fn);
    if (err)
        return failUmka(umka, label);

    return expectInt64(label, umkaGetResult(fn->params, fn->result)->intVal, expected);
}


static int checkProvidedCountDefaults(Umka *umka, UmkaAPI *api)
{
    int status = 0;
    const UmkaType *defaultsType = getParamType(umka, "acceptDefaults");
    UmkaFuncContext fn = {0};

    if (!defaultsType || getFunc(umka, "checkDefaults", &fn))
        return fail("missing provided-count default test inputs");

    status |= expectBool("API table set default param present", api->umkaSetDefaultParam != NULL, true);
    status |= expectBool("API table set default params present", api->umkaSetDefaultParams != NULL, true);
    status |= expectInt64("DefaultsFn parameter count", api->umkaGetFuncParamCount(defaultsType), 9);
    status |= expectInt64("DefaultsFn default count", api->umkaGetFuncDefaultParamCount(defaultsType), 8);

    api->umkaGetParam(fn.params, 0)->intVal = 1;

    status |= expectBool("provided count too low", api->umkaSetDefaultParams(umka, defaultsType, &fn, 0), false);
    status |= expectBool("provided count too high", api->umkaSetDefaultParams(umka, defaultsType, &fn, 10), false);
    status |= expectBool("provided count applies trailing defaults", api->umkaSetDefaultParams(umka, defaultsType, &fn, 1), true);

    if (!status)
        status |= callAndExpect(umka, &fn, 173, "checkDefaults with provided-count defaults");

    return status;
}


static int checkSingleDefaults(Umka *umka, UmkaAPI *api)
{
    int status = 0;
    const UmkaType *defaultsType = getParamType(umka, "acceptDefaults");
    UmkaFuncContext fn = {0};

    if (!defaultsType || getFunc(umka, "checkDefaults", &fn))
        return fail("missing single-default test inputs");

    api->umkaGetParam(fn.params, 0)->intVal = 1;

    status |= expectBool("negative default index", api->umkaSetDefaultParam(umka, defaultsType, &fn, -1), false);
    status |= expectBool("required param has no default", api->umkaSetDefaultParam(umka, defaultsType, &fn, 0), false);
    status |= expectBool("out of range default index", api->umkaSetDefaultParam(umka, defaultsType, &fn, 9), false);

    for (int i = 1; i < 9; i++)
    {
        char label[64];
        snprintf(label, sizeof(label), "single default index %d", i);
        status |= expectBool(label, api->umkaSetDefaultParam(umka, defaultsType, &fn, i), true);
    }

    if (!status)
        status |= callAndExpect(umka, &fn, 173, "checkDefaults with single defaults");

    return status;
}


static int checkMixedDefaults(Umka *umka, UmkaAPI *api)
{
    int status = 0;
    const UmkaType *mixedType = getParamType(umka, "acceptMixed");
    UmkaFuncContext allDefaults = {0};
    UmkaFuncContext oneDefault = {0};

    if (!mixedType || getFunc(umka, "mixed", &allDefaults) || getFunc(umka, "mixed", &oneDefault))
        return fail("missing mixed default test inputs");

    status |= expectInt64("MixedFn default count", api->umkaGetFuncDefaultParamCount(mixedType), 2);

    api->umkaGetParam(allDefaults.params, 0)->intVal = 5;
    status |= expectBool("mixed all trailing defaults", umkaSetDefaultParams(umka, mixedType, &allDefaults, 1), true);
    if (!status)
        status |= callAndExpect(umka, &allDefaults, 12, "mixed with all defaults");

    api->umkaGetParam(oneDefault.params, 0)->intVal = 5;
    api->umkaGetParam(oneDefault.params, 1)->intVal = 7;
    status |= expectBool("mixed one trailing default", api->umkaSetDefaultParams(umka, mixedType, &oneDefault, 2), true);
    if (!status)
        status |= callAndExpect(umka, &oneDefault, 15, "mixed with one default");

    return status;
}


static int checkInvalidAndUnsupportedCases(Umka *umka, UmkaAPI *api)
{
    int status = 0;
    const UmkaType *defaultsType = getParamType(umka, "acceptDefaults");
    const UmkaType *anyDefaultType = getParamType(umka, "acceptAnyDefault");
    const UmkaType *noDefaultType = getParamType(umka, "acceptNoDefault");
    const UmkaType *intType = getParamType(umka, "acceptInt");
    UmkaFuncContext fn = {0};
    UmkaFuncContext empty = {0};
    UmkaFuncContext anyFn = {0};
    UmkaFuncContext noDefaultFn = {0};

    if (!defaultsType || !anyDefaultType || !noDefaultType || !intType ||
        getFunc(umka, "checkDefaults", &fn) ||
        getFunc(umka, "anyDefault", &anyFn) ||
        getFunc(umka, "noDefault", &noDefaultFn))
        return fail("missing invalid/default test inputs");

    status |= expectBool("NULL runtime", api->umkaSetDefaultParam(NULL, defaultsType, &fn, 1), false);
    status |= expectBool("NULL type", api->umkaSetDefaultParam(umka, NULL, &fn, 1), false);
    status |= expectBool("NULL context", api->umkaSetDefaultParam(umka, defaultsType, NULL, 1), false);
    status |= expectBool("empty context", api->umkaSetDefaultParam(umka, defaultsType, &empty, 1), false);
    status |= expectBool("non-function type", api->umkaSetDefaultParam(umka, intType, &fn, 0), false);
    status |= expectBool("negative provided count", api->umkaSetDefaultParams(umka, defaultsType, &fn, -1), false);

    status |= expectInt64("AnyDefaultFn default count", api->umkaGetFuncDefaultParamCount(anyDefaultType), 1);
    status |= expectBool("unsupported any default by index", api->umkaSetDefaultParam(umka, anyDefaultType, &anyFn, 0), false);
    status |= expectBool("unsupported any default by provided count", api->umkaSetDefaultParams(umka, anyDefaultType, &anyFn, 0), false);

    status |= expectInt64("NoDefaultFn default count", api->umkaGetFuncDefaultParamCount(noDefaultType), 0);
    status |= expectBool("parameter without default", api->umkaSetDefaultParam(umka, noDefaultType, &noDefaultFn, 1), false);
    return status;
}


int main(void)
{
    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    if (!umkaInit(umka, "hostapi_defaults.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        int status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return status;
    }

    if (!umkaCompile(umka))
    {
        int status = failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return status;
    }

    UmkaAPI *api = umkaGetAPI(umka);
    int status = 0;

    status |= checkProvidedCountDefaults(umka, api);
    status |= checkSingleDefaults(umka, api);
    status |= checkMixedDefaults(umka, api);
    status |= checkInvalidAndUnsupportedCases(umka, api);

    umkaFree(umka);
    return status;
}
