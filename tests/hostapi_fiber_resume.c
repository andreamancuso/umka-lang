#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/umka_api.h"


typedef UmkaDynArray(UmkaAny) AnyArray;


static UmkaHostHandle retainedCallbackFiber;
static int callbackFailures = 0;


static const char *source =
    "var step: int = 0\n"
    "\n"
    "fn hostCaptureFiber*(f: fiber)\n"
    "fn hostAttemptResume*(f: fiber)\n"
    "fn acceptStr*(s: str): int {return len(s)}\n"
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
    "fn callHostAttempt*() {\n"
    "    child := makeChild()\n"
    "    hostAttemptResume(child)\n"
    "    resume(child)\n"
    "    resume(child)\n"
    "}\n"
    "\n"
    "fn makeNestedOwner*(): fiber {\n"
    "    return make(fiber, fn () {\n"
    "        inner := makeChild()\n"
    "        hostCaptureFiber(inner)\n"
    "        resume()\n"
    "        resume(inner)\n"
    "        resume(inner)\n"
    "    })\n"
    "}\n"
    "\n"
    "fn setStep*(value: int) {step = value}\n"
    "fn getStep*(): int {return step}\n"
    "fn makeFiberAny*(): any {return makeChild()}\n"
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


static const char *errorSource =
    "fn makeErrorChild*(): fiber {\n"
    "    return make(fiber, fn () {exit(91, \"fiber failed\")})\n"
    "}\n";


static const char *interruptSource =
    "fn makeLoopChild*(): fiber {\n"
    "    return make(fiber, fn () {for true {}})\n"
    "}\n";


static const char *foreignSource =
    "fn makeForeignChild*(): fiber {\n"
    "    return make(fiber, fn () {})\n"
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


static int callNoArgs(Umka *umka, const char *name)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, name, &fn))
        return 1;

    int err = umkaCall(umka, &fn);
    return err ? failUmka(umka, name) : 0;
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


static int expectHandleResume(Umka *umka, UmkaHostHandle *handle, UmkaFiberResumeStatus expectedStatus, int64_t expectedStep, bool expectedAlive)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFiberResumeStatus actualStatus = api->umkaResumeFiber(umka, handle);
    if (actualStatus != expectedStatus)
    {
        fprintf(stderr, "resume status %d, expected %d\n", actualStatus, expectedStatus);
        return 1;
    }

    int status = callGetStep(umka, expectedStep);
    UmkaStackSlot fiber = api->umkaGetHostHandleValue(handle);
    if (api->umkaFiberAlive(umka, fiber) != expectedAlive)
        status |= fail("fiber alive status mismatch after host resume");
    if (api->umkaFiberRunning(umka, fiber))
        status |= fail("fiber running after host resume returned");
    return status;
}


static int resumeHandleToCompletion(Umka *umka, UmkaHostHandle *handle)
{
    int status = callSetStep(umka, 0);
    status |= expectHandleResume(umka, handle, UMKA_FIBER_RESUME_YIELDED, 1, true);
    status |= expectHandleResume(umka, handle, UMKA_FIBER_RESUME_DONE, 2, false);
    status |= expectHandleResume(umka, handle, UMKA_FIBER_RESUME_DONE, 2, false);
    return status;
}


static int retainFiber(Umka *umka, UmkaStackSlot fiber, UmkaHostHandle *handle)
{
    UmkaAPI *api = umkaGetAPI(umka);
    if (!api->umkaFiberValid(umka, fiber) || !api->umkaFiberAlive(umka, fiber))
        return fail("fiber invalid before retain");
    if (!api->umkaRetainHostFiber(umka, handle, fiber))
        return fail("fiber retain failed");
    return 0;
}


static void hostCaptureFiber(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaStackSlot fiber = *api->umkaGetParam(params, 0);

    if (retainFiber(umka, fiber, &retainedCallbackFiber))
        noteCallbackFailure("hostCaptureFiber");
}


static void hostAttemptResume(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaStackSlot fiber = *api->umkaGetParam(params, 0);

    if (api->umkaResumeFiberValue(umka, fiber) != UMKA_FIBER_RESUME_INVALID)
        noteCallbackFailure("hostAttemptResume accepted reentrant resume");
}


static int makeDirectFiber(Umka *umka, UmkaStackSlot *fiber, const UmkaType **fiberType)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "makeChild", &fn))
        return 1;

    int err = api->umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "makeChild");

    if (fiberType)
        *fiberType = api->umkaGetResultType(fn.params, fn.result);
    *fiber = *api->umkaGetResult(fn.params, fn.result);
    return 0;
}


static int checkHandleResumeFromResult(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *fiberType = NULL;
    UmkaStackSlot fiber = {0};
    if (makeDirectFiber(umka, &fiber, &fiberType))
        return 1;

    UmkaHostHandle handle = {0};
    api->umkaMakeHostHandle(&handle);
    int status = retainFiber(umka, fiber, &handle);

    void *resultStorage = fiber.ptrVal;
    if (!api->umkaReleaseHostValue(umka, &resultStorage, fiberType))
        status |= fail("releasing direct fiber result failed");

    status |= resumeHandleToCompletion(umka, &handle);
    api->umkaClearHostHandle(&handle);
    return status;
}


static int checkDirectValueResume(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *fiberType = NULL;
    UmkaStackSlot fiber = {0};
    if (makeDirectFiber(umka, &fiber, &fiberType))
        return 1;

    int status = callSetStep(umka, 0);
    if (api->umkaResumeFiberValue(umka, fiber) != UMKA_FIBER_RESUME_YIELDED)
        status |= fail("direct fiber resume did not yield");
    status |= callGetStep(umka, 1);
    if (!api->umkaFiberAlive(umka, fiber))
        status |= fail("direct fiber unexpectedly dead after yield");

    if (api->umkaResumeFiberValue(umka, fiber) != UMKA_FIBER_RESUME_DONE)
        status |= fail("direct fiber resume did not complete");
    status |= callGetStep(umka, 2);
    if (api->umkaFiberAlive(umka, fiber))
        status |= fail("direct fiber unexpectedly alive after completion");

    if (api->umkaResumeFiberValue(umka, fiber) != UMKA_FIBER_RESUME_DONE)
        status |= fail("dead direct fiber did not report done");

    void *resultStorage = fiber.ptrVal;
    if (!api->umkaReleaseHostValue(umka, &resultStorage, fiberType))
        status |= fail("releasing direct-resumed fiber failed");
    return status;
}


static int checkCallbackFiberResume(Umka *umka)
{
    int status = callNoArgs(umka, "passFiber");
    if (callbackFailures)
        status |= fail("callback capture failed");

    status |= resumeHandleToCompletion(umka, &retainedCallbackFiber);
    umkaClearHostHandle(&retainedCallbackFiber);
    umkaMakeHostHandle(&retainedCallbackFiber);
    return status;
}


static int checkNonChildFiberRejected(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "makeNestedOwner", &fn))
        return 1;

    int err = api->umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "makeNestedOwner");

    UmkaStackSlot outer = *api->umkaGetResult(fn.params, fn.result);
    const UmkaType *fiberType = api->umkaGetResultType(fn.params, fn.result);
    UmkaHostHandle outerHandle = {0};
    api->umkaMakeHostHandle(&outerHandle);

    int status = retainFiber(umka, outer, &outerHandle);

    void *resultStorage = outer.ptrVal;
    if (!api->umkaReleaseHostValue(umka, &resultStorage, fiberType))
        status |= fail("releasing nested owner result failed");

    status |= callSetStep(umka, 0);
    if (!status && api->umkaResumeFiber(umka, &outerHandle) != UMKA_FIBER_RESUME_YIELDED)
        status |= fail("nested owner did not yield to host");
    if (callbackFailures)
        status |= fail("nested callback capture failed");

    if (!status && api->umkaResumeFiber(umka, &retainedCallbackFiber) != UMKA_FIBER_RESUME_INVALID)
        status |= fail("fiber owned by suspended parent was host-resumed");

    if (!status && api->umkaResumeFiber(umka, &outerHandle) != UMKA_FIBER_RESUME_DONE)
        status |= fail("nested owner did not complete");
    status |= callGetStep(umka, 2);

    api->umkaClearHostHandle(&retainedCallbackFiber);
    api->umkaMakeHostHandle(&retainedCallbackFiber);
    api->umkaClearHostHandle(&outerHandle);
    return status;
}


static int retainFiberFromAny(Umka *umka, const UmkaAny *any, UmkaHostHandle *handle)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *payloadType = NULL;
    UmkaStackSlot payload = {0};

    if (!api->umkaGetAnyValue(any, &payloadType, &payload))
        return fail("fiber any did not deconstruct");
    if (api->umkaGetTypeKind(payloadType) != UMKA_TYPE_FIBER)
        return fail("any payload is not fiber");
    return retainFiber(umka, payload, handle);
}


static int checkAnyFiberResume(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *anyType = getResultType(umka, "makeFiberAny");
    UmkaAny any = {0};
    UmkaFuncContext fn = {0};
    if (!anyType || getFunc(umka, "makeFiberAny", &fn))
        return fail("any resume setup failed");

    fn.result->ptrVal = &any;
    int err = api->umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "makeFiberAny");

    UmkaHostHandle handle = {0};
    api->umkaMakeHostHandle(&handle);
    int status = retainFiberFromAny(umka, &any, &handle);

    if (!api->umkaReleaseHostValue(umka, &any, anyType))
        status |= fail("releasing fiber any failed");

    status |= resumeHandleToCompletion(umka, &handle);
    api->umkaClearHostHandle(&handle);
    return status;
}


static int checkMapFiberResume(Umka *umka)
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
        status |= fail("retaining map with fiber failed");

    if (!api->umkaReleaseHostValue(umka, resultBuffer, mapType))
        status |= fail("releasing map result failed");
    free(resultBuffer);

    UmkaHostMapEntry entry = {0};
    UmkaAny any = {0};
    if (!status && (!api->umkaGetHostMapEntry(umka, &mapHandle, 0, &entry) ||
                    !api->umkaGetHostMapEntryAnyValue(umka, &entry, &any)))
        status |= fail("reading map fiber failed");

    if (!status)
        status |= retainFiberFromAny(umka, &any, &fiberHandle);

    api->umkaClearHostHandle(&mapHandle);
    status |= resumeHandleToCompletion(umka, &fiberHandle);
    api->umkaClearHostHandle(&fiberHandle);
    return status;
}


static int checkArrayFiberResume(Umka *umka)
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
        status |= fail("retaining array with fiber failed");

    if (!api->umkaReleaseHostValue(umka, resultBuffer, arrayType))
        status |= fail("releasing array result failed");
    free(resultBuffer);

    UmkaStackSlot retained = api->umkaGetHostHandleValue(&arrayHandle);
    UmkaAny any = {0};
    if (!status && !api->umkaGetDynArrayAnyItem(umka, (const AnyArray *)retained.ptrVal, 0, &any))
        status |= fail("reading array fiber failed");

    if (!status)
        status |= retainFiberFromAny(umka, &any, &fiberHandle);

    api->umkaClearHostHandle(&arrayHandle);
    status |= resumeHandleToCompletion(umka, &fiberHandle);
    api->umkaClearHostHandle(&fiberHandle);
    return status;
}


static int checkNegativeCases(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaHostHandle emptyHandle = {0};
    api->umkaMakeHostHandle(&emptyHandle);

    int status = 0;
    if (api->umkaResumeFiber(NULL, &emptyHandle) != UMKA_FIBER_RESUME_INVALID)
        status |= fail("NULL runtime accepted by umkaResumeFiber");
    if (api->umkaResumeFiber(umka, NULL) != UMKA_FIBER_RESUME_INVALID)
        status |= fail("NULL handle accepted by umkaResumeFiber");
    if (api->umkaResumeFiber(umka, &emptyHandle) != UMKA_FIBER_RESUME_INVALID)
        status |= fail("empty handle accepted by umkaResumeFiber");
    if (api->umkaResumeFiberValue(NULL, (UmkaStackSlot){0}) != UMKA_FIBER_RESUME_INVALID)
        status |= fail("NULL runtime accepted by umkaResumeFiberValue");
    if (api->umkaResumeFiberValue(umka, (UmkaStackSlot){0}) != UMKA_FIBER_RESUME_INVALID)
        status |= fail("empty fiber accepted by umkaResumeFiberValue");

    UmkaStackSlot invalid = {0};
    invalid.ptrVal = (void *)1;
    if (api->umkaResumeFiberValue(umka, invalid) != UMKA_FIBER_RESUME_INVALID)
        status |= fail("invalid fiber pointer accepted by umkaResumeFiberValue");

    const UmkaType *strType = getParamType(umka, "acceptStr");
    UmkaHostHandle strHandle = {0};
    api->umkaMakeHostHandle(&strHandle);
    UmkaStackSlot str = {0};
    str.ptrVal = api->umkaMakeStr(umka, "not a fiber");
    if (!strType || !str.ptrVal || !api->umkaRetainHostValue(umka, &strHandle, strType, str))
        status |= fail("string handle setup failed");
    else if (api->umkaResumeFiber(umka, &strHandle) != UMKA_FIBER_RESUME_INVALID)
        status |= fail("non-fiber handle accepted by umkaResumeFiber");
    if (str.ptrVal)
        api->umkaDecRef(umka, str.ptrVal);
    api->umkaClearHostHandle(&strHandle);

    status |= callNoArgs(umka, "callHostAttempt");
    if (callbackFailures)
        status |= fail("reentrant resume callback was accepted");

    return status;
}


static Umka *makeRuntime(const char *fileName, const char *program)
{
    Umka *umka = umkaAlloc();
    if (!umka)
    {
        fail("umkaAlloc failed");
        return NULL;
    }

    if (!umkaInit(umka, fileName, program, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        failUmka(umka, "umkaInit");
        umkaFree(umka);
        return NULL;
    }

    if (!umkaCompile(umka))
    {
        failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return NULL;
    }

    return umka;
}


static int checkForeignFiberRejected(Umka *umka)
{
    Umka *foreign = makeRuntime("hostapi_fiber_resume_foreign.um", foreignSource);
    if (!foreign)
        return 1;

    UmkaFuncContext fn = {0};
    if (getFunc(foreign, "makeForeignChild", &fn))
    {
        umkaFree(foreign);
        return 1;
    }

    int status = 0;
    int err = umkaCall(foreign, &fn);
    if (err)
        status |= failUmka(foreign, "makeForeignChild");

    UmkaStackSlot foreignFiber = *umkaGetResult(fn.params, fn.result);
    if (!status && umkaResumeFiberValue(umka, foreignFiber) != UMKA_FIBER_RESUME_INVALID)
        status |= fail("foreign fiber was accepted by umkaResumeFiberValue");

    if (!status && umkaResumeFiberValue(foreign, foreignFiber) != UMKA_FIBER_RESUME_DONE)
        status |= fail("foreign runtime could not complete its own fiber");

    const UmkaType *fiberType = umkaGetResultType(fn.params, fn.result);
    void *resultStorage = foreignFiber.ptrVal;
    if (!umkaReleaseHostValue(foreign, &resultStorage, fiberType))
        status |= fail("releasing foreign fiber failed");

    umkaFree(foreign);
    return status;
}


static int checkRuntimeErrorResume(void)
{
    Umka *umka = makeRuntime("hostapi_fiber_resume_error.um", errorSource);
    if (!umka)
        return 1;

    UmkaFuncContext fn = {0};
    if (getFunc(umka, "makeErrorChild", &fn))
    {
        umkaFree(umka);
        return 1;
    }

    int status = 0;
    int err = umkaCall(umka, &fn);
    if (err)
        status |= failUmka(umka, "makeErrorChild");

    UmkaStackSlot fiber = *umkaGetResult(fn.params, fn.result);
    UmkaHostHandle handle = {0};
    umkaMakeHostHandle(&handle);
    if (!umkaRetainHostFiber(umka, &handle, fiber))
        status |= fail("retaining error fiber failed");

    const UmkaType *fiberType = umkaGetResultType(fn.params, fn.result);
    void *resultStorage = fiber.ptrVal;
    if (!umkaReleaseHostValue(umka, &resultStorage, fiberType))
        status |= fail("releasing error fiber result failed");

    if (!status && umkaResumeFiber(umka, &handle) != UMKA_FIBER_RESUME_ERROR)
        status |= fail("runtime error fiber did not report resume error");
    if (!status && umkaGetError(umka)->code != 91)
        status |= fail("runtime error code mismatch");

    umkaClearHostHandle(&handle);
    umkaFree(umka);
    return status;
}


static int checkInterruptedResume(void)
{
    Umka *umka = makeRuntime("hostapi_fiber_resume_interrupt.um", interruptSource);
    if (!umka)
        return 1;

    UmkaFuncContext fn = {0};
    if (getFunc(umka, "makeLoopChild", &fn))
    {
        umkaFree(umka);
        return 1;
    }

    int status = 0;
    int err = umkaCall(umka, &fn);
    if (err)
        status |= failUmka(umka, "makeLoopChild");

    UmkaStackSlot fiber = *umkaGetResult(fn.params, fn.result);
    UmkaHostHandle handle = {0};
    umkaMakeHostHandle(&handle);
    if (!umkaRetainHostFiber(umka, &handle, fiber))
        status |= fail("retaining loop fiber failed");

    umkaRequestInterrupt(umka, "stop fiber");
    if (!status && umkaResumeFiber(umka, &handle) != UMKA_FIBER_RESUME_ERROR)
        status |= fail("interrupted fiber did not report resume error");
    if (!status && umkaGetError(umka)->code != UMKA_ERR_INTERRUPTED)
        status |= fail("interrupted resume error code mismatch");

    umkaFree(umka);
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

    if (!umkaInit(umka, "hostapi_fiber_resume.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return status;
    }

    umkaAddFunc(umka, "hostCaptureFiber", hostCaptureFiber);
    umkaAddFunc(umka, "hostAttemptResume", hostAttemptResume);

    if (!umkaCompile(umka))
    {
        status = failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return status;
    }

    status |= runCheck("checkHandleResumeFromResult", checkHandleResumeFromResult, umka);
    status |= runCheck("checkDirectValueResume", checkDirectValueResume, umka);
    status |= runCheck("checkCallbackFiberResume", checkCallbackFiberResume, umka);
    status |= runCheck("checkNonChildFiberRejected", checkNonChildFiberRejected, umka);
    status |= runCheck("checkAnyFiberResume", checkAnyFiberResume, umka);
    status |= runCheck("checkMapFiberResume", checkMapFiberResume, umka);
    status |= runCheck("checkArrayFiberResume", checkArrayFiberResume, umka);
    status |= runCheck("checkNegativeCases", checkNegativeCases, umka);
    status |= checkForeignFiberRejected(umka);

    if (callbackFailures)
        status = 1;

    umkaClearHostHandle(&retainedCallbackFiber);
    umkaFree(umka);

    status |= checkRuntimeErrorResume();
    status |= checkInterruptedResume();
    return status;
}
