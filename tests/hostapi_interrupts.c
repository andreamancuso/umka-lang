#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
    #include <windows.h>
#endif

#include "../src/umka_api.h"


static const char *callSource =
    "fn hostRequestInterrupt*()\n"
    "\n"
    "fn add*(a, b: int): int {\n"
    "    return a + b\n"
    "}\n"
    "\n"
    "fn spin*(): int {\n"
    "    var i: int\n"
    "    for true {\n"
    "        i++\n"
    "    }\n"
    "    return i\n"
    "}\n"
    "\n"
    "fn callbackInterrupt*(): int {\n"
    "    hostRequestInterrupt()\n"
    "    var i: int\n"
    "    for true {\n"
    "        i++\n"
    "    }\n"
    "    return i\n"
    "}\n"
    "\n"
    "fn failRuntime*(): int {\n"
    "    a := []int{}\n"
    "    return a[1]\n"
    "}\n";


static const char *runInterruptSource =
    "fn hostRequestInterrupt*()\n"
    "\n"
    "fn main() {\n"
    "    hostRequestInterrupt()\n"
    "    for true {\n"
    "    }\n"
    "}\n";


static const char *normalRunSource =
    "fn main() {\n"
    "    x := 1\n"
    "}\n";


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


static int checkError(Umka *umka, const char *operation, int actual, int expected, const char *messagePart)
{
    UmkaError *error = umkaGetError(umka);

    if (actual != expected)
    {
        fprintf(stderr, "%s returned %d, expected %d\n", operation, actual, expected);
        return failUmka(umka, operation);
    }

    if (messagePart && (!error->msg || !strstr(error->msg, messagePart)))
    {
        fprintf(stderr, "%s message was '%s', expected it to contain '%s'\n",
                operation,
                error->msg ? error->msg : "<no message>",
                messagePart);
        return 1;
    }

    return 0;
}


static void hostRequestInterrupt(UmkaStackSlot *params, UmkaStackSlot *result)
{
    (void)params;

    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);

    api->umkaRequestInterrupt(umka, "callback interrupt");
}


static Umka *makeUmka(const char *name, const char *source, int *status)
{
    Umka *umka = umkaAlloc();
    if (!umka)
    {
        *status = fail("umkaAlloc failed");
        return NULL;
    }

    if (!umkaInit(umka, name, source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        *status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return NULL;
    }

    if (!umkaAddFunc(umka, "hostRequestInterrupt", hostRequestInterrupt))
    {
        *status = fail("umkaAddFunc failed");
        umkaFree(umka);
        return NULL;
    }

    if (!umkaCompile(umka))
    {
        *status = failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return NULL;
    }

    return umka;
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


static int callAdd(Umka *umka, int64_t left, int64_t right, int64_t expected)
{
    UmkaFuncContext fn = {0};

    if (getFunc(umka, "add", &fn))
        return 1;

    umkaGetParam(fn.params, 0)->intVal = left;
    umkaGetParam(fn.params, 1)->intVal = right;

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "add");

    const int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != expected)
    {
        fprintf(stderr, "add returned %lld, expected %lld\n", (long long)actual, (long long)expected);
        return 1;
    }

    return 0;
}


static int testNormalCall(void)
{
    int status = 0;
    Umka *umka = makeUmka("hostapi_interrupts_call.um", callSource, &status);
    if (!umka)
        return status;

    status |= callAdd(umka, 2, 3, 5);

    if (umkaInterruptRequested(umka))
        status |= fail("interrupt flag set after normal call");

    umkaFree(umka);
    return status;
}


static int testClearBeforeExecution(void)
{
    int status = 0;
    Umka *umka = makeUmka("hostapi_interrupts_clear.um", callSource, &status);
    if (!umka)
        return status;

    UmkaAPI *api = umkaGetAPI(umka);
    if (!api->umkaRequestInterrupt || !api->umkaClearInterrupt || !api->umkaInterruptRequested)
        status |= fail("interrupt APIs missing from UmkaAPI");

    api->umkaRequestInterrupt(umka, "clear me");
    if (!api->umkaInterruptRequested(umka))
        status |= fail("interrupt flag not set");

    api->umkaClearInterrupt(umka);
    if (api->umkaInterruptRequested(umka))
        status |= fail("interrupt flag not cleared");

    status |= callAdd(umka, 7, 8, 15);

    umkaFree(umka);
    return status;
}


static int testRepeatedPreCallInterrupt(void)
{
    int status = 0;
    Umka *umka = makeUmka("hostapi_interrupts_repeated.um", callSource, &status);
    if (!umka)
        return status;

    UmkaFuncContext fn = {0};
    if (getFunc(umka, "spin", &fn))
    {
        umkaFree(umka);
        return 1;
    }

    umkaRequestInterrupt(umka, "first interrupt");
    umkaRequestInterrupt(umka, "second interrupt");

    int err = umkaCall(umka, &fn);
    status |= checkError(umka, "spin", err, UMKA_ERR_INTERRUPTED, "second interrupt");

    if (umkaAlive(umka))
        status |= fail("interrupted interpreter is still alive");
    if (!umkaInterruptRequested(umka))
        status |= fail("interrupt flag should remain sticky until cleared");

    umkaClearInterrupt(umka);
    if (umkaInterruptRequested(umka))
        status |= fail("interrupt flag still set after clear");

    umkaFree(umka);
    return status;
}


static int testCallbackCallInterrupt(void)
{
    int status = 0;
    Umka *umka = makeUmka("hostapi_interrupts_callback_call.um", callSource, &status);
    if (!umka)
        return status;

    UmkaFuncContext fn = {0};
    if (getFunc(umka, "callbackInterrupt", &fn))
    {
        umkaFree(umka);
        return 1;
    }

    int err = umkaCall(umka, &fn);
    status |= checkError(umka, "callbackInterrupt", err, UMKA_ERR_INTERRUPTED, "callback interrupt");

    if (umkaAlive(umka))
        status |= fail("callback-interrupted interpreter is still alive");

    umkaFree(umka);
    return status;
}


static int testCallbackRunInterrupt(void)
{
    int status = 0;
    Umka *umka = makeUmka("hostapi_interrupts_callback_run.um", runInterruptSource, &status);
    if (!umka)
        return status;

    int err = umkaRun(umka);
    status |= checkError(umka, "umkaRun", err, UMKA_ERR_INTERRUPTED, "callback interrupt");

    if (umkaAlive(umka))
        status |= fail("run-interrupted interpreter is still alive");

    umkaFree(umka);
    return status;
}


static int testPreRunInterrupt(void)
{
    int status = 0;
    Umka *umka = makeUmka("hostapi_interrupts_prerun.um", normalRunSource, &status);
    if (!umka)
        return status;

    umkaRequestInterrupt(umka, "pre-run interrupt");

    int err = umkaRun(umka);
    status |= checkError(umka, "pre-run umkaRun", err, UMKA_ERR_INTERRUPTED, "pre-run interrupt");

    if (!umkaInterruptRequested(umka))
        status |= fail("pre-run interrupt flag should remain sticky");

    umkaFree(umka);
    return status;
}


static int testRequestAfterRuntimeTermination(void)
{
    int status = 0;
    Umka *umka = makeUmka("hostapi_interrupts_after_runtime.um", callSource, &status);
    if (!umka)
        return status;

    UmkaFuncContext fn = {0};
    if (getFunc(umka, "failRuntime", &fn))
    {
        umkaFree(umka);
        return 1;
    }

    int err = umkaCall(umka, &fn);
    if (err != UMKA_ERR_RUNTIME)
        status |= checkError(umka, "failRuntime", err, UMKA_ERR_RUNTIME, NULL);

    umkaRequestInterrupt(umka, "after runtime termination");
    if (!umkaInterruptRequested(umka))
        status |= fail("interrupt request after runtime termination was not recorded");

    umkaClearInterrupt(umka);
    if (umkaInterruptRequested(umka))
        status |= fail("interrupt after runtime termination was not cleared");

    umkaFree(umka);
    return status;
}


static int testRequestAfterNormalTermination(void)
{
    int status = 0;
    Umka *umka = makeUmka("hostapi_interrupts_after_normal.um", normalRunSource, &status);
    if (!umka)
        return status;

    int err = umkaRun(umka);
    if (err)
        status |= failUmka(umka, "normal umkaRun");

    umkaRequestInterrupt(umka, "after normal termination");
    if (!umkaInterruptRequested(umka))
        status |= fail("interrupt request after normal termination was not recorded");

    umkaClearInterrupt(umka);
    if (umkaInterruptRequested(umka))
        status |= fail("interrupt after normal termination was not cleared");

    umkaFree(umka);
    return status;
}


static int testPendingInterruptBeforeFree(void)
{
    int status = 0;
    Umka *umka = makeUmka("hostapi_interrupts_before_free.um", callSource, &status);
    if (!umka)
        return status;

    umkaRequestInterrupt(umka, "pending before free");
    umkaFree(umka);
    return 0;
}


#ifdef _WIN32
typedef struct
{
    Umka *umka;
    UmkaFuncContext fn;
    int err;
} ThreadCallContext;


static DWORD WINAPI callSpinThread(LPVOID param)
{
    ThreadCallContext *context = (ThreadCallContext *)param;
    context->err = umkaCall(context->umka, &context->fn);
    return 0;
}


static int testThreadInterruptsTightCall(void)
{
    int status = 0;
    Umka *umka = makeUmka("hostapi_interrupts_thread.um", callSource, &status);
    if (!umka)
        return status;

    ThreadCallContext context = {0};
    context.umka = umka;

    if (getFunc(umka, "spin", &context.fn))
    {
        umkaFree(umka);
        return 1;
    }

    HANDLE thread = CreateThread(NULL, 0, callSpinThread, &context, 0, NULL);
    if (!thread)
    {
        umkaFree(umka);
        return fail("CreateThread failed");
    }

    Sleep(50);
    umkaRequestInterrupt(umka, "thread interrupt");

    DWORD waitResult = WaitForSingleObject(thread, 5000);
    if (waitResult != WAIT_OBJECT_0)
    {
        TerminateThread(thread, 1);
        CloseHandle(thread);
        umkaFree(umka);
        return fail("thread interrupt did not stop tight Umka loop");
    }

    CloseHandle(thread);

    status |= checkError(umka, "thread spin", context.err, UMKA_ERR_INTERRUPTED, "thread interrupt");

    if (umkaAlive(umka))
        status |= fail("thread-interrupted interpreter is still alive");

    umkaFree(umka);
    return status;
}
#else
static int testThreadInterruptsTightCall(void)
{
    return 0;
}
#endif


int main(void)
{
    int status = 0;

    status |= testNormalCall();
    status |= testClearBeforeExecution();
    status |= testRepeatedPreCallInterrupt();
    status |= testCallbackCallInterrupt();
    status |= testCallbackRunInterrupt();
    status |= testPreRunInterrupt();
    status |= testRequestAfterRuntimeTermination();
    status |= testRequestAfterNormalTermination();
    status |= testPendingInterruptBeforeFree();
    status |= testThreadInterruptsTightCall();

    return status;
}
