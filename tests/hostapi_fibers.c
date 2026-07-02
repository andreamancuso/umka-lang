#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "../src/umka_api.h"


typedef UmkaDynArray(UmkaAny) AnyArray;


static UmkaHostHandle retainedCallbackFiber;
static int callbackFailures = 0;


static const char *source =
    "var step: int = 0\n"
    "\n"
    "fn hostCaptureFiber*(f: fiber)\n"
    "fn acceptAny*(v: any): bool {return valid(v)}\n"
    "\n"
    "fn makeChild*(): fiber {\n"
    "    return make(fiber, fn () {\n"
    "        step = 1\n"
    "        resume()\n"
    "        step = 2\n"
    "    })\n"
    "}\n"
    "\n"
    "fn passFiber*() {\n"
    "    child := makeChild()\n"
    "    hostCaptureFiber(child)\n"
    "}\n"
    "\n"
    "fn resumeChild*(child: fiber) {\n"
    "    resume(child)\n"
    "}\n"
    "\n"
    "fn setStep*(value: int) {\n"
    "    step = value\n"
    "}\n"
    "\n"
    "fn getStep*(): int {\n"
    "    return step\n"
    "}\n"
    "\n"
    "fn makeFiberMap*(): map[str]any {\n"
    "    m := map[str]any{}\n"
    "    m[\"child\"] = makeChild()\n"
    "    return m\n"
    "}\n"
    "\n"
    "fn makeFiberArray*(): []any {\n"
    "    a := make([]any, 1)\n"
    "    a[0] = makeChild()\n"
    "    return a\n"
    "}\n";


static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}


static void noteCallbackFailure(const char *message)
{
    callbackFailures++;
    fprintf(stderr, "callback failure: %s\n", message);
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


static const UmkaType *getResultType(Umka *umka, const char *name)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, name, &fn))
        return NULL;
    return umkaGetResultType(fn.params, fn.result);
}


static int callSetStep(Umka *umka, int64_t value)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "setStep", &fn))
        return 1;

    umkaGetParam(fn.params, 0)->intVal = value;
    int err = umkaCall(umka, &fn);
    return err ? failUmka(umka, "setStep") : 0;
}


static int callGetStep(Umka *umka, int64_t expected)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "getStep", &fn))
        return 1;

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "getStep");

    int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != expected)
    {
        fprintf(stderr, "step is %lld, expected %lld\n", (long long)actual, (long long)expected);
        return 1;
    }
    return 0;
}


static int retainFiber(Umka *umka, UmkaStackSlot fiber, UmkaHostHandle *handle, const char *label)
{
    UmkaAPI *api = umkaGetAPI(umka);

    if (!api->umkaFiberValid(umka, fiber))
        return fail(label);
    if (!api->umkaFiberAlive(umka, fiber))
        return fail("fiber unexpectedly dead before retain");
    if (api->umkaFiberRunning(umka, fiber))
        return fail("fiber unexpectedly running before retain");
    if (!api->umkaRetainHostFiber(umka, handle, fiber))
        return fail("fiber retain failed");

    UmkaStackSlot retained = api->umkaGetHostHandleValue(handle);
    if (!api->umkaFiberValid(umka, retained) || !api->umkaFiberAlive(umka, retained))
        return fail("retained fiber status mismatch");

    return 0;
}


static int resumeFiberOnce(Umka *umka, UmkaHostHandle *handle, int64_t expectedStep, bool expectedAlive)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "resumeChild", &fn))
        return 1;

    const UmkaType *fiberType = api->umkaGetParamType(fn.params, 0);
    UmkaStackSlot fiber = api->umkaGetHostHandleValue(handle);
    if (!api->umkaAssignHostValue(umka, api->umkaGetParam(fn.params, 0), fiberType, fiber))
        return fail("assigning fiber parameter failed");

    int err = api->umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "resumeChild");

    int status = callGetStep(umka, expectedStep);
    if (api->umkaFiberAlive(umka, fiber) != expectedAlive)
        status |= fail("fiber alive status mismatch after resume");
    if (api->umkaFiberRunning(umka, fiber))
        status |= fail("fiber running after resume returned to host");
    return status;
}


static int finishFiberHandle(Umka *umka, UmkaHostHandle *handle)
{
    int status = callSetStep(umka, 0);
    status |= resumeFiberOnce(umka, handle, 1, true);
    status |= resumeFiberOnce(umka, handle, 2, false);
    return status;
}


static void hostCaptureFiber(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaStackSlot fiber = *api->umkaGetParam(params, 0);

    if (retainFiber(umka, fiber, &retainedCallbackFiber, "callback fiber invalid"))
        noteCallbackFailure("hostCaptureFiber");
}


static int checkCallbackFiber(Umka *umka)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "passFiber", &fn))
        return 1;

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "passFiber");
    if (callbackFailures)
        return fail("callback failed");

    int status = finishFiberHandle(umka, &retainedCallbackFiber);
    umkaClearHostHandle(&retainedCallbackFiber);
    return status;
}


static int checkDirectFiberResult(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "makeChild", &fn))
        return 1;

    const UmkaType *fiberType = api->umkaGetResultType(fn.params, fn.result);
    UmkaHostHandle handle = {0};
    api->umkaMakeHostHandle(&handle);

    int err = api->umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "makeChild");

    UmkaStackSlot fiber = *api->umkaGetResult(fn.params, fn.result);
    int status = retainFiber(umka, fiber, &handle, "direct result fiber invalid");

    void *resultStorage = fiber.ptrVal;
    if (!api->umkaReleaseHostValue(umka, &resultStorage, fiberType))
        status |= fail("releasing direct fiber result failed");

    status |= finishFiberHandle(umka, &handle);
    UmkaStackSlot stale = api->umkaGetHostHandleValue(&handle);
    api->umkaClearHostHandle(&handle);
    if (api->umkaFiberValid(umka, stale))
        status |= fail("released completed fiber still reported valid");
    return status;
}


static int retainFiberFromAny(Umka *umka, const UmkaAny *any, UmkaHostHandle *handle)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = NULL;
    UmkaStackSlot fiber = {0};

    if (!api->umkaGetAnyValue(any, &type, &fiber))
        return fail("fiber any did not deconstruct");
    if (api->umkaGetTypeKind(type) != UMKA_TYPE_FIBER)
        return fail("any payload is not fiber");
    return retainFiber(umka, fiber, handle, "fiber any payload invalid");
}


static int checkFiberMap(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *mapType = getResultType(umka, "makeFiberMap");
    void *resultBuffer = calloc(1, (size_t)api->umkaGetTypeSize(mapType));
    if (!resultBuffer)
        return fail("map result allocation failed");

    UmkaFuncContext fn = {0};
    if (getFunc(umka, "makeFiberMap", &fn))
    {
        free(resultBuffer);
        return 1;
    }
    fn.result->ptrVal = resultBuffer;

    int err = api->umkaCall(umka, &fn);
    if (err)
    {
        free(resultBuffer);
        return failUmka(umka, "makeFiberMap");
    }

    UmkaHostHandle mapHandle = {0};
    UmkaHostHandle fiberHandle = {0};
    api->umkaMakeHostHandle(&mapHandle);
    api->umkaMakeHostHandle(&fiberHandle);

    UmkaStackSlot mapValue = {0};
    mapValue.ptrVal = resultBuffer;
    int status = 0;
    if (!api->umkaRetainHostValue(umka, &mapHandle, mapType, mapValue))
        status |= fail("retaining map with fiber any failed");

    if (!api->umkaReleaseHostValue(umka, resultBuffer, mapType))
        status |= fail("releasing map result failed");
    free(resultBuffer);

    UmkaHostMapEntry entry = {0};
    UmkaAny any = {0};
    if (!status && (!api->umkaGetHostMapEntry(umka, &mapHandle, 0, &entry) ||
                    !api->umkaGetHostMapEntryAnyValue(umka, &entry, &any)))
        status |= fail("reading map fiber any failed");

    if (!status)
        status |= retainFiberFromAny(umka, &any, &fiberHandle);

    api->umkaClearHostHandle(&mapHandle);
    if (!status)
        status |= finishFiberHandle(umka, &fiberHandle);
    api->umkaClearHostHandle(&fiberHandle);
    return status;
}


static int checkFiberArray(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *arrayType = getResultType(umka, "makeFiberArray");
    void *resultBuffer = calloc(1, (size_t)api->umkaGetTypeSize(arrayType));
    if (!resultBuffer)
        return fail("array result allocation failed");

    UmkaFuncContext fn = {0};
    if (getFunc(umka, "makeFiberArray", &fn))
    {
        free(resultBuffer);
        return 1;
    }
    fn.result->ptrVal = resultBuffer;

    int err = api->umkaCall(umka, &fn);
    if (err)
    {
        free(resultBuffer);
        return failUmka(umka, "makeFiberArray");
    }

    UmkaHostHandle arrayHandle = {0};
    UmkaHostHandle fiberHandle = {0};
    api->umkaMakeHostHandle(&arrayHandle);
    api->umkaMakeHostHandle(&fiberHandle);

    UmkaStackSlot arrayValue = {0};
    arrayValue.ptrVal = resultBuffer;
    int status = 0;
    if (!api->umkaRetainHostValue(umka, &arrayHandle, arrayType, arrayValue))
        status |= fail("retaining array with fiber any failed");

    if (!api->umkaReleaseHostValue(umka, resultBuffer, arrayType))
        status |= fail("releasing array result failed");
    free(resultBuffer);

    UmkaStackSlot retained = api->umkaGetHostHandleValue(&arrayHandle);
    UmkaAny any = {0};
    if (!status && !api->umkaGetDynArrayAnyItem(umka, (const AnyArray *)retained.ptrVal, 0, &any))
        status |= fail("reading array fiber any failed");

    if (!status)
        status |= retainFiberFromAny(umka, &any, &fiberHandle);

    api->umkaClearHostHandle(&arrayHandle);
    if (!status)
        status |= finishFiberHandle(umka, &fiberHandle);
    api->umkaClearHostHandle(&fiberHandle);
    return status;
}


static int checkFiberAnyConstruction(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "makeChild", &fn))
        return 1;

    const UmkaType *fiberType = api->umkaGetResultType(fn.params, fn.result);
    const UmkaType *anyType = getParamType(umka, "acceptAny");
    if (!anyType)
        return fail("missing any type");

    int err = api->umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "makeChild for any");

    UmkaStackSlot fiber = *api->umkaGetResult(fn.params, fn.result);
    UmkaHostHandle handle = {0};
    api->umkaMakeHostHandle(&handle);
    int status = retainFiber(umka, fiber, &handle, "fiber for any invalid");

    UmkaAny any = {0};
    if (!api->umkaMakeAny(umka, &any, fiberType, fiber))
        status |= fail("constructing any from fiber failed");

    void *resultStorage = fiber.ptrVal;
    if (!api->umkaReleaseHostValue(umka, &resultStorage, fiberType))
        status |= fail("releasing fiber any source failed");

    const UmkaType *payloadType = NULL;
    UmkaStackSlot payload = {0};
    if (!api->umkaGetAnyValue(&any, &payloadType, &payload) ||
        api->umkaGetTypeKind(payloadType) != UMKA_TYPE_FIBER ||
        !api->umkaFiberAlive(umka, payload))
        status |= fail("constructed fiber any payload mismatch");

    status |= finishFiberHandle(umka, &handle);
    if (!api->umkaReleaseHostValue(umka, &any, anyType))
        status |= fail("releasing constructed fiber any failed");
    api->umkaClearHostHandle(&handle);
    return status;
}


static int checkNegativeCases(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *fiberType = getParamType(umka, "resumeChild");
    UmkaStackSlot empty = {0};
    UmkaStackSlot invalid = {0};
    invalid.ptrVal = (void *)1;
    UmkaHostHandle handle = {0};
    api->umkaMakeHostHandle(&handle);

    int status = 0;
    if (api->umkaFiberValid(NULL, empty))
        status |= fail("NULL runtime accepted by umkaFiberValid");
    if (api->umkaFiberValid(umka, empty))
        status |= fail("empty fiber reported valid");
    if (api->umkaFiberValid(umka, invalid))
        status |= fail("invalid pointer reported valid fiber");
    if (api->umkaFiberAlive(umka, empty))
        status |= fail("empty fiber reported alive");
    if (api->umkaFiberRunning(umka, empty))
        status |= fail("empty fiber reported running");
    if (api->umkaRetainHostFiber(NULL, &handle, empty))
        status |= fail("NULL runtime accepted by umkaRetainHostFiber");
    if (api->umkaRetainHostFiber(umka, NULL, empty))
        status |= fail("NULL handle accepted by umkaRetainHostFiber");
    if (api->umkaRetainHostFiber(umka, &handle, empty))
        status |= fail("empty fiber retained");
    if (api->umkaRetainHostValue(umka, &handle, fiberType, invalid))
        status |= fail("invalid fiber retained through generic handle API");
    if (api->umkaCallableValid(fiberType, empty))
        status |= fail("fiber unexpectedly callable");

    api->umkaClearHostHandle(&handle);
    return status;
}


static int runCheck(const char *name, int (*check)(Umka *), Umka *umka)
{
    int status = check(umka);
    if (status)
        fprintf(stderr, "%s failed\n", name);
    fflush(stderr);
    return status;
}


int main(void)
{
    int status = 0;
    umkaMakeHostHandle(&retainedCallbackFiber);

    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    if (!umkaInit(umka, "hostapi_fibers.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return status;
    }

    umkaAddFunc(umka, "hostCaptureFiber", hostCaptureFiber);

    if (!umkaCompile(umka))
    {
        status = failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return status;
    }

    status |= runCheck("checkCallbackFiber", checkCallbackFiber, umka);
    status |= runCheck("checkDirectFiberResult", checkDirectFiberResult, umka);
    status |= runCheck("checkFiberMap", checkFiberMap, umka);
    status |= runCheck("checkFiberArray", checkFiberArray, umka);
    status |= runCheck("checkFiberAnyConstruction", checkFiberAnyConstruction, umka);
    status |= runCheck("checkNegativeCases", checkNegativeCases, umka);

    umkaClearHostHandle(&retainedCallbackFiber);
    umkaFree(umka);
    return status;
}
