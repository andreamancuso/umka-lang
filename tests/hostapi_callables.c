#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/umka_api.h"


static UmkaHostHandle retainedAdder;
static UmkaHostHandle retainedAnyCallable;
static UmkaHostHandle retainedFailingCallable;
static int callbackFailures = 0;
static int callableCaptureIndex = 0;


static const char *source =
    "fn hostCaptureCallable*(f: fn (x: int): int)\n"
    "fn hostCaptureCallableAny*(v: any)\n"
    "fn hostCaptureFailingCallable*(f: fn (x: int): int)\n"
    "\n"
    "fn addOne*(x: int): int {return x + 1}\n"
    "\n"
    "fn makeAdder*(base: int): fn (x: int): int {\n"
    "    return fn (x: int): int |base| {return base + x}\n"
    "}\n"
    "\n"
    "fn makeLabelScorer*(prefix: str): fn (x: int): int {\n"
    "    return fn (x: int): int |prefix| {return len(prefix) + x}\n"
    "}\n"
    "\n"
    "fn makeFailingCallable*(): fn (x: int): int {\n"
    "    return fn (x: int): int {\n"
    "        a := []int{}\n"
    "        return a[1] + x\n"
    "    }\n"
    "}\n"
    "\n"
    "fn emitAdder*() {hostCaptureCallable(makeAdder(10))}\n"
    "fn emitTopLevel*() {hostCaptureCallable(addOne)}\n"
    "fn emitAnyCallable*() {hostCaptureCallableAny(makeLabelScorer(\"abcd\"))}\n"
    "fn emitFailingCallable*() {hostCaptureFailingCallable(makeFailingCallable())}\n"
    "fn acceptInt*(x: int): int {return x}\n"
    "fn acceptAny*(v: any): bool {return valid(v)}\n"
    "fn acceptFiber*(f: fiber): bool {return valid(f)}\n";


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


static bool expectKind(UmkaAPI *api, const UmkaType *type, UmkaTypeKind expected, const char *label)
{
    UmkaTypeKind actual = api->umkaGetTypeKind(type);
    if (actual != expected)
    {
        fprintf(stderr, "%s kind %d, expected %d\n", label, actual, expected);
        return false;
    }
    return true;
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


static int callCallableValue(Umka *umka, const UmkaType *type, UmkaStackSlot value, int64_t arg, int64_t expected, const char *label)
{
    UmkaAPI *api = umkaGetAPI(umka);

    if (!api->umkaCallableValid(type, value))
        return fail("callable value is not valid");

    UmkaFuncContext fn = {0};
    if (!api->umkaMakeCallableContext(umka, type, value, &fn))
        return fail("umkaMakeCallableContext failed");

    api->umkaGetParam(fn.params, 0)->intVal = arg;

    int err = api->umkaCallCallable(umka, type, value, &fn);
    if (err)
        return failUmka(umka, label);

    int64_t actual = api->umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != expected)
    {
        fprintf(stderr, "%s returned %lld, expected %lld\n", label, (long long)actual, (long long)expected);
        return 1;
    }

    return 0;
}


static void hostCaptureCallable(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaStackSlot value = {0};
    value.ptrVal = api->umkaGetParam(params, 0);

    if (!expectKind(api, type, UMKA_TYPE_CLOSURE, "callable param"))
    {
        noteCallbackFailure("callable type");
        return;
    }

    if (callableCaptureIndex == 0)
    {
        if (callCallableValue(umka, type, value, 7, 17, "captured adder"))
            noteCallbackFailure("captured adder call");
        if (!api->umkaRetainHostValue(umka, &retainedAdder, type, value))
            noteCallbackFailure("retained adder");
    }
    else if (callableCaptureIndex == 1)
    {
        if (callCallableValue(umka, type, value, 7, 8, "captured top-level callable"))
            noteCallbackFailure("captured top-level call");
    }
    else
        noteCallbackFailure("unexpected callable capture");

    callableCaptureIndex++;
}


static void hostCaptureCallableAny(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *anyType = api->umkaGetParamType(params, 0);
    UmkaAny *any = (UmkaAny *)api->umkaGetParam(params, 0);
    const UmkaType *callableType = NULL;
    UmkaStackSlot callable = {0};

    if (!expectKind(api, anyType, UMKA_TYPE_INTERFACE, "any callable param") ||
        !api->umkaGetAnyValue(any, &callableType, &callable) ||
        !expectKind(api, callableType, UMKA_TYPE_CLOSURE, "any callable concrete"))
    {
        noteCallbackFailure("callable any inspect");
        return;
    }

    if (callCallableValue(umka, callableType, callable, 6, 10, "callable from any"))
        noteCallbackFailure("callable any call");

    UmkaStackSlot anySlot = {0};
    anySlot.ptrVal = any;
    if (!api->umkaRetainHostValue(umka, &retainedAnyCallable, anyType, anySlot))
        noteCallbackFailure("retain callable any");
}


static void hostCaptureFailingCallable(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaStackSlot value = {0};
    value.ptrVal = api->umkaGetParam(params, 0);

    if (!api->umkaRetainHostValue(umka, &retainedFailingCallable, type, value))
        noteCallbackFailure("retain failing callable");
}


static int callRetainedAdder(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetHostHandleType(&retainedAdder);
    UmkaStackSlot value = api->umkaGetHostHandleValue(&retainedAdder);

    if (!type || !value.ptrVal)
        return fail("retained adder is empty");

    return callCallableValue(umka, type, value, 5, 15, "retained adder");
}


static int callRetainedAnyCallable(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaStackSlot anyValue = api->umkaGetHostHandleValue(&retainedAnyCallable);
    const UmkaType *callableType = NULL;
    UmkaStackSlot callable = {0};

    if (!anyValue.ptrVal || !api->umkaGetAnyValue((UmkaAny *)anyValue.ptrVal, &callableType, &callable))
        return fail("retained callable any did not deconstruct");

    return callCallableValue(umka, callableType, callable, 2, 6, "retained any callable");
}


static int boxRetainedCallableAsAny(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *closureType = api->umkaGetHostHandleType(&retainedAdder);
    UmkaStackSlot closureValue = api->umkaGetHostHandleValue(&retainedAdder);
    const UmkaType *anyType = getParamType(umka, "acceptAny");
    UmkaAny any = {0};

    if (!closureType || !closureValue.ptrVal || !anyType)
        return fail("missing closure or any type");

    if (!api->umkaMakeAny(umka, &any, closureType, closureValue))
        return fail("umkaMakeAny failed for closure");

    const UmkaType *callableType = NULL;
    UmkaStackSlot callable = {0};
    int status = 0;

    if (!api->umkaGetAnyValue(&any, &callableType, &callable))
        status |= fail("boxed callable any did not deconstruct");
    else
        status |= callCallableValue(umka, callableType, callable, 3, 13, "boxed callable any");

    if (!api->umkaReleaseHostValue(umka, &any, anyType))
        status |= fail("boxed callable any release failed");

    return status;
}


static int checkFiberRejected(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *fiberType = getParamType(umka, "acceptFiber");
    UmkaStackSlot value = {0};
    UmkaHostHandle handle = {0};
    UmkaFuncContext fn = {0};

    if (!fiberType || !expectKind(api, fiberType, UMKA_TYPE_FIBER, "fiber type"))
        return fail("failed to get fiber type");

    api->umkaMakeHostHandle(&handle);

    if (api->umkaCallableValid(fiberType, value))
        return fail("fiber unexpectedly reported callable");
    if (api->umkaMakeCallableContext(umka, fiberType, value, &fn))
        return fail("fiber unexpectedly made callable context");
    if (api->umkaRetainHostValue(umka, &handle, fiberType, value))
    {
        api->umkaClearHostHandle(&handle);
        return fail("fiber unexpectedly retained");
    }

    return 0;
}


static int checkNegativeCallableCases(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *closureType = api->umkaGetHostHandleType(&retainedAdder);
    const UmkaType *intType = getParamType(umka, "acceptInt");
    UmkaStackSlot adder = api->umkaGetHostHandleValue(&retainedAdder);
    UmkaStackSlot empty = {0};
    UmkaFuncContext fn = {0};

    if (!closureType || !adder.ptrVal || !intType)
        return fail("negative callable setup failed");

    if (api->umkaCallableValid(closureType, empty))
        return fail("empty closure unexpectedly reported callable");
    if (api->umkaMakeCallableContext(umka, closureType, empty, &fn))
        return fail("empty closure unexpectedly made callable context");
    if (api->umkaCallableValid(intType, adder))
        return fail("non-callable type unexpectedly reported callable");
    if (api->umkaMakeCallableContext(umka, intType, adder, &fn))
        return fail("non-callable type unexpectedly made callable context");

    UmkaStackSlot anyValue = api->umkaGetHostHandleValue(&retainedAnyCallable);
    const UmkaType *otherCallableType = NULL;
    UmkaStackSlot otherCallable = {0};
    if (!anyValue.ptrVal || !api->umkaGetAnyValue((UmkaAny *)anyValue.ptrVal, &otherCallableType, &otherCallable))
        return fail("negative callable any setup failed");

    if (!api->umkaMakeCallableContext(umka, closureType, adder, &fn))
        return fail("negative callable context setup failed");

    api->umkaGetParam(fn.params, 0)->intVal = 1;
    int err = api->umkaCallCallable(umka, otherCallableType, otherCallable, &fn);
    if (err != UMKA_ERR_RUNTIME)
    {
        fprintf(stderr, "mismatched callable context returned %d, expected %d\n", err, UMKA_ERR_RUNTIME);
        return 1;
    }

    return 0;
}


static int callFailingCallable(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetHostHandleType(&retainedFailingCallable);
    UmkaStackSlot value = api->umkaGetHostHandleValue(&retainedFailingCallable);
    UmkaFuncContext fn = {0};

    if (!type || !value.ptrVal)
        return fail("retained failing callable is empty");
    if (!api->umkaMakeCallableContext(umka, type, value, &fn))
        return fail("failing callable context failed");

    api->umkaGetParam(fn.params, 0)->intVal = 1;
    int err = api->umkaCallCallable(umka, type, value, &fn);
    if (err != UMKA_ERR_RUNTIME)
    {
        fprintf(stderr, "failing callable returned %d, expected %d\n", err, UMKA_ERR_RUNTIME);
        return 1;
    }

    return 0;
}


static void clearHandles(void)
{
    umkaClearHostHandle(&retainedAdder);
    umkaClearHostHandle(&retainedAnyCallable);
    umkaClearHostHandle(&retainedFailingCallable);
}


static int initUmka(Umka **umkaOut)
{
    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    if (!umkaInit(umka, "hostapi_callables.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        int status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return status;
    }

    umkaAddFunc(umka, "hostCaptureCallable", hostCaptureCallable);
    umkaAddFunc(umka, "hostCaptureCallableAny", hostCaptureCallableAny);
    umkaAddFunc(umka, "hostCaptureFailingCallable", hostCaptureFailingCallable);

    if (!umkaCompile(umka))
    {
        int status = failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return status;
    }

    *umkaOut = umka;
    return 0;
}


static int testInterruptedCallable(void)
{
    Umka *umka = NULL;
    int status = initUmka(&umka);
    if (status)
        return status;

    callableCaptureIndex = 0;
    callbackFailures = 0;
    umkaMakeHostHandle(&retainedAdder);

    status |= callNoArgs(umka, "emitAdder");
    if (callbackFailures)
        status = 1;

    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetHostHandleType(&retainedAdder);
    UmkaStackSlot value = api->umkaGetHostHandleValue(&retainedAdder);
    UmkaFuncContext fn = {0};

    if (!type || !value.ptrVal || !api->umkaMakeCallableContext(umka, type, value, &fn))
        status |= fail("interrupted callable setup failed");
    else
    {
        api->umkaGetParam(fn.params, 0)->intVal = 1;
        api->umkaRequestInterrupt(umka, "stop callable");
        int err = api->umkaCallCallable(umka, type, value, &fn);
        if (err != UMKA_ERR_INTERRUPTED)
        {
            fprintf(stderr, "interrupted callable returned %d, expected %d\n", err, UMKA_ERR_INTERRUPTED);
            status = 1;
        }
    }

    clearHandles();
    umkaFree(umka);
    return status;
}


int main(void)
{
    umkaMakeHostHandle(&retainedAdder);
    umkaMakeHostHandle(&retainedAnyCallable);
    umkaMakeHostHandle(&retainedFailingCallable);

    Umka *umka = NULL;
    int status = initUmka(&umka);
    if (status)
        return status;

    status |= callNoArgs(umka, "emitAdder");
    status |= callRetainedAdder(umka);

    status |= callNoArgs(umka, "emitTopLevel");
    status |= callNoArgs(umka, "emitAnyCallable");
    status |= callRetainedAnyCallable(umka);
    status |= boxRetainedCallableAsAny(umka);
    status |= checkNegativeCallableCases(umka);
    status |= checkFiberRejected(umka);

    status |= callNoArgs(umka, "emitFailingCallable");

    if (callbackFailures)
        status = 1;

    if (!status)
        status |= callFailingCallable(umka);

    clearHandles();
    umkaFree(umka);

    if (!status)
        status |= testInterruptedCallable();

    return status;
}
