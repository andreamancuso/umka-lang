#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "../src/umka_api.h"


typedef UmkaDynArray(int64_t) IntArray;


typedef struct
{
    int64_t left, right;
} Pair;


static UmkaHostHandle stringHandle;
static UmkaHostHandle arrayHandle;
static UmkaHostHandle stringIntMapHandle;
static UmkaHostHandle stringStringMapHandle;
static UmkaHostHandle replaceHandle;
static UmkaHostHandle dataHandle;
static UmkaHostHandle pairHandle;
static UmkaHostHandle fixedArrayHandle;
static int callbackFailures = 0;
static int dataFreeCount = 0;


static const char *source =
    "type Pair* = struct {left, right: int}\n"
    "\n"
    "fn hostCaptureString*(s: str)\n"
    "fn hostCaptureIntArray*(a: []int)\n"
    "fn hostCaptureStringIntMap*(m: map[str]int)\n"
    "fn hostCaptureStringStringMap*(m: map[str]str)\n"
    "\n"
    "fn captureValues*() {\n"
    "    hostCaptureString(\"one\" + \"two\")\n"
    "    hostCaptureIntArray([]int{3, 4, 5})\n"
    "    hostCaptureStringIntMap(map[str]int{\"alpha\": 17, \"beta\": 25})\n"
    "    hostCaptureStringStringMap(map[str]str{\"left\": \"north\", \"right\": \"south\"})\n"
    "}\n"
    "\n"
    "fn stringLen*(s: str): int {\n"
    "    return len(s)\n"
    "}\n"
    "\n"
    "fn arraySum*(a: []int): int {\n"
    "    return a[0] + a[1] + a[2]\n"
    "}\n"
    "\n"
    "fn stringIntMapSum*(m: map[str]int): int {\n"
    "    return m[\"alpha\"] + m[\"beta\"]\n"
    "}\n"
    "\n"
    "fn stringStringMapLen*(m: map[str]str): int {\n"
    "    return len(m[\"left\"]) + len(m[\"right\"])\n"
    "}\n"
    "\n"
    "fn pairSum*(p: Pair): int {\n"
    "    return p.left + p.right\n"
    "}\n"
    "\n"
    "fn fixedArraySum*(a: [3]int): int {\n"
    "    return a[0] + a[1] + a[2]\n"
    "}\n"
    "\n"
    "fn pointerParamSink*(p: ^int): int {\n"
    "    return 0\n"
    "}\n"
    "\n"
    "fn failRuntime*(): int {\n"
    "    a := []int{}\n"
    "    return a[1]\n"
    "}\n";


static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}


static int failUmka(Umka *umka, const char *operation)
{
    UmkaError *error = umkaGetError(umka);
    fprintf(stderr, "%s failed: %s (%d, %d): %s\n",
            operation,
            error->fileName ? error->fileName : "<unknown>",
            error->line,
            error->pos,
            error->msg ? error->msg : "<no message>");
    return 1;
}


static void noteCallbackFailure(const char *message)
{
    callbackFailures++;
    fprintf(stderr, "callback failure: %s\n", message);
}


static void hostCaptureString(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaStackSlot value = *api->umkaGetParam(params, 0);

    if (!api->umkaRetainHostValue(umka, &stringHandle, type, value))
        noteCallbackFailure("hostCaptureString");
}


static void hostCaptureIntArray(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaStackSlot value = {0};

    value.ptrVal = api->umkaGetParam(params, 0);
    if (!api->umkaRetainHostValue(umka, &arrayHandle, type, value))
        noteCallbackFailure("hostCaptureIntArray");
}


static void hostCaptureStringIntMap(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaStackSlot value = {0};

    value.ptrVal = api->umkaGetParam(params, 0);
    if (!api->umkaRetainHostValue(umka, &stringIntMapHandle, type, value))
        noteCallbackFailure("hostCaptureStringIntMap");
}


static void hostCaptureStringStringMap(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaStackSlot value = {0};

    value.ptrVal = api->umkaGetParam(params, 0);
    if (!api->umkaRetainHostValue(umka, &stringStringMapHandle, type, value))
        noteCallbackFailure("hostCaptureStringStringMap");
}


static void dataOnFree(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)params;
    (void)result;
    dataFreeCount++;
}


static int getFunc(Umka *umka, const char *name, UmkaFuncContext *fn)
{
    if (!umkaGetFunc(umka, NULL, name, fn))
    {
        fprintf(stderr, "umkaGetFunc failed for %s\n", name);
        return 1;
    }
    return 0;
}


static int callNoArgs(Umka *umka, const char *name)
{
    UmkaFuncContext fn = {0};

    if (getFunc(umka, name, &fn))
        return 1;

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, name);

    return 0;
}


static int callStringHandle(Umka *umka, const char *name, const UmkaHostHandle *handle, int64_t expected)
{
    UmkaFuncContext fn = {0};

    if (getFunc(umka, name, &fn))
        return 1;

    *umkaGetParam(fn.params, 0) = umkaGetHostHandleValue(handle);

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, name);

    int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != expected)
    {
        fprintf(stderr, "%s returned %lld, expected %lld\n", name, (long long)actual, (long long)expected);
        return 1;
    }

    return 0;
}


static int callArrayHandle(Umka *umka, const char *name, const UmkaHostHandle *handle, int64_t expected)
{
    UmkaFuncContext fn = {0};
    UmkaStackSlot value = umkaGetHostHandleValue(handle);

    if (getFunc(umka, name, &fn))
        return 1;

    *(IntArray *)umkaGetParam(fn.params, 0) = *(IntArray *)value.ptrVal;

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, name);

    int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != expected)
    {
        fprintf(stderr, "%s returned %lld, expected %lld\n", name, (long long)actual, (long long)expected);
        return 1;
    }

    return 0;
}


static int callMapHandle(Umka *umka, const char *name, const UmkaHostHandle *handle, int64_t expected)
{
    UmkaFuncContext fn = {0};
    UmkaStackSlot value = umkaGetHostHandleValue(handle);

    if (getFunc(umka, name, &fn))
        return 1;

    *(UmkaMap *)umkaGetParam(fn.params, 0) = *(UmkaMap *)value.ptrVal;

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, name);

    int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != expected)
    {
        fprintf(stderr, "%s returned %lld, expected %lld\n", name, (long long)actual, (long long)expected);
        return 1;
    }

    return 0;
}


static int callPairHandle(Umka *umka, const char *name, const UmkaHostHandle *handle, int64_t expected)
{
    UmkaFuncContext fn = {0};
    UmkaStackSlot value = umkaGetHostHandleValue(handle);

    if (getFunc(umka, name, &fn))
        return 1;

    *(Pair *)umkaGetParam(fn.params, 0) = *(Pair *)value.ptrVal;

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, name);

    int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != expected)
    {
        fprintf(stderr, "%s returned %lld, expected %lld\n", name, (long long)actual, (long long)expected);
        return 1;
    }

    return 0;
}


static int callFixedArrayHandle(Umka *umka, const char *name, const UmkaHostHandle *handle, int64_t expected)
{
    UmkaFuncContext fn = {0};
    UmkaStackSlot value = umkaGetHostHandleValue(handle);

    if (getFunc(umka, name, &fn))
        return 1;

    int64_t *param = (int64_t *)umkaGetParam(fn.params, 0);
    int64_t *items = (int64_t *)value.ptrVal;
    for (int i = 0; i < 3; i++)
        param[i] = items[i];

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, name);

    int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != expected)
    {
        fprintf(stderr, "%s returned %lld, expected %lld\n", name, (long long)actual, (long long)expected);
        return 1;
    }

    return 0;
}


static int callExpectRuntimeError(Umka *umka, const char *name)
{
    UmkaFuncContext fn = {0};

    if (getFunc(umka, name, &fn))
        return 1;

    return umkaCall(umka, &fn) ? 0 : fail("expected runtime error did not occur");
}


static int checkFixedValueHandles(Umka *umka)
{
    UmkaFuncContext pairFn = {0};
    UmkaFuncContext arrayFn = {0};
    UmkaStackSlot value = {0};

    if (getFunc(umka, "pairSum", &pairFn) || getFunc(umka, "fixedArraySum", &arrayFn))
        return 1;

    Pair *pair = (Pair *)umkaMakeStruct(umka, umkaGetParamType(pairFn.params, 0));
    if (!pair)
        return fail("umkaMakeStruct failed for Pair");

    pair->left = 7;
    pair->right = 8;
    value.ptrVal = pair;

    if (!umkaRetainHostValue(umka, &pairHandle, umkaGetParamType(pairFn.params, 0), value))
        return fail("umkaRetainHostValue failed for Pair");

    umkaDecRef(umka, pair);

    int64_t *items = (int64_t *)umkaMakeStruct(umka, umkaGetParamType(arrayFn.params, 0));
    if (!items)
        return fail("umkaMakeStruct failed for [3]int");

    items[0] = 1;
    items[1] = 2;
    items[2] = 3;
    value.ptrVal = items;

    if (!umkaRetainHostValue(umka, &fixedArrayHandle, umkaGetParamType(arrayFn.params, 0), value))
        return fail("umkaRetainHostValue failed for [3]int");

    umkaDecRef(umka, items);

    int status = 0;
    status |= callPairHandle(umka, "pairSum", &pairHandle, 15);
    status |= callFixedArrayHandle(umka, "fixedArraySum", &fixedArrayHandle, 6);
    return status;
}


static int checkNegativeCases(Umka *umka)
{
    UmkaHostHandle empty;
    UmkaHostHandle unsupported;
    UmkaFuncContext pointerFn = {0};
    int stackData = 123;
    UmkaStackSlot zero = {0};

    umkaMakeHostHandle(&empty);
    umkaMakeHostHandle(&unsupported);

    if (umkaHostHandleValid(&empty))
        return fail("empty handle reported valid");

    if (umkaGetHostHandleType(&empty))
        return fail("empty handle reported a type");

    if (umkaGetHostHandleValue(&empty).ptrVal)
        return fail("empty handle reported a value");

    umkaClearHostHandle(&empty);
    umkaReleaseHostHandle(&empty);

    if (umkaRetainHostValue(umka, &unsupported, NULL, zero))
        return fail("retained value with NULL type");

    if (getFunc(umka, "pointerParamSink", &pointerFn))
        return 1;

    if (umkaRetainHostValue(umka, &unsupported, umkaGetParamType(pointerFn.params, 0), zero))
        return fail("retained unsupported pointer value type");

    if (umkaRetainHostData(umka, &unsupported, NULL))
        return fail("retained NULL host data");

    if (umkaRetainHostData(umka, &unsupported, &stackData))
        return fail("retained non-Umka host data");

    umkaReleaseHostHandle(&unsupported);
    return 0;
}


static int checkDataHandle(Umka *umka)
{
    int64_t *data = umkaAllocData(umka, sizeof(int64_t), dataOnFree);
    if (!data)
        return fail("umkaAllocData failed");

    *data = 12345;

    if (!umkaRetainHostData(umka, &dataHandle, data))
        return fail("umkaRetainHostData failed");

    umkaDecRef(umka, data);

    if (!umkaHostHandleValid(&dataHandle))
        return fail("data handle is invalid after retain");

    if (*(int64_t *)umkaGetHostHandleValue(&dataHandle).ptrVal != 12345)
        return fail("data handle returned wrong data");

    if (dataFreeCount != 0)
        return fail("data freed while retained by handle");

    umkaClearHostHandle(&dataHandle);

    if (dataFreeCount != 1)
        return fail("data handle did not release final data reference");

    umkaClearHostHandle(&dataHandle);

    if (dataFreeCount != 1)
        return fail("double clear released data twice");

    return 0;
}


static int checkReplaceRetain(Umka *umka)
{
    UmkaStackSlot replacement = {0};

    if (!umkaHostHandleValid(&stringHandle))
        return fail("string handle is not valid before replacement");

    replacement.ptrVal = umkaMakeStr(umka, "replacement");
    if (!replacement.ptrVal)
        return fail("umkaMakeStr failed");

    if (!umkaRetainHostValue(umka, &replaceHandle, umkaGetHostHandleType(&stringHandle), replacement))
        return fail("replacement retain failed");

    umkaDecRef(umka, replacement.ptrVal);

    return callStringHandle(umka, "stringLen", &replaceHandle, 11);
}


int main(void)
{
    int status = 0;
    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    umkaMakeHostHandle(&stringHandle);
    umkaMakeHostHandle(&arrayHandle);
    umkaMakeHostHandle(&stringIntMapHandle);
    umkaMakeHostHandle(&stringStringMapHandle);
    umkaMakeHostHandle(&replaceHandle);
    umkaMakeHostHandle(&dataHandle);
    umkaMakeHostHandle(&pairHandle);
    umkaMakeHostHandle(&fixedArrayHandle);

    if (!umkaInit(umka, "hostapi_handles.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return status;
    }

    umkaAddFunc(umka, "hostCaptureString", hostCaptureString);
    umkaAddFunc(umka, "hostCaptureIntArray", hostCaptureIntArray);
    umkaAddFunc(umka, "hostCaptureStringIntMap", hostCaptureStringIntMap);
    umkaAddFunc(umka, "hostCaptureStringStringMap", hostCaptureStringStringMap);

    if (!umkaCompile(umka))
    {
        status = failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return status;
    }

    status |= checkNegativeCases(umka);
    status |= checkDataHandle(umka);
    status |= checkFixedValueHandles(umka);
    status |= callNoArgs(umka, "captureValues");

    if (callbackFailures)
        status = 1;

    for (int i = 0; i < 2; i++)
    {
        status |= callStringHandle(umka, "stringLen", &stringHandle, 6);
        status |= callArrayHandle(umka, "arraySum", &arrayHandle, 12);
        status |= callMapHandle(umka, "stringIntMapSum", &stringIntMapHandle, 42);
        status |= callMapHandle(umka, "stringStringMapLen", &stringStringMapHandle, 10);
    }

    status |= checkReplaceRetain(umka);
    status |= callExpectRuntimeError(umka, "failRuntime");

    umkaClearHostHandle(&stringHandle);
    umkaClearHostHandle(&arrayHandle);
    umkaClearHostHandle(&stringIntMapHandle);
    umkaClearHostHandle(&stringStringMapHandle);
    umkaClearHostHandle(&replaceHandle);
    umkaClearHostHandle(&dataHandle);
    umkaClearHostHandle(&pairHandle);
    umkaClearHostHandle(&fixedArrayHandle);

    umkaFree(umka);
    return status;
}
