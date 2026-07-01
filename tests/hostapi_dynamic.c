#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/umka_api.h"


typedef UmkaDynArray(int64_t) IntArray;


typedef struct
{
    int64_t left, right;
} Pair;


typedef struct
{
    char *name;
    int64_t count;
} Dog;


enum
{
    ANY_INT,
    ANY_REAL,
    ANY_BOOL,
    ANY_STR,
    ANY_ARRAY,
    ANY_MAP,
    ANY_FIXED_ARRAY,
    ANY_PAIR,
    ANY_COUNT
};


static UmkaHostHandle anyHandles[ANY_COUNT];
static UmkaHostHandle speakerHandle;
static UmkaHostHandle nullHandle;
static UmkaHostHandle closureAnyHandle;
static int anyIndex = 0;
static int callbackFailures = 0;
static int funcTypeCaptured = 0;


static const char *source =
    "type Pair* = struct {left, right: int}\n"
    "type Dog* = struct {name: str; count: int}\n"
    "type Speaker* = interface {speak(): str}\n"
    "\n"
    "fn hostCaptureAny*(v: any)\n"
    "fn hostCaptureSpeaker*(s: Speaker)\n"
    "fn hostCaptureNullAny*(v: any)\n"
    "fn hostCaptureClosureAny*(v: any)\n"
    "fn hostCaptureFuncType*(f: fn (a: int, b: str): real)\n"
    "\n"
    "fn (d: ^Dog) speak(): str {\n"
    "    d.count++\n"
    "    return d.name\n"
    "}\n"
    "\n"
    "fn sampleFunc*(a: int, b: str): real {\n"
    "    return real(a + len(b))\n"
    "}\n"
    "\n"
    "fn emitFuncType*() {\n"
    "    hostCaptureFuncType(sampleFunc)\n"
    "}\n"
    "\n"
    "fn emitAnyValues*() {\n"
    "    hostCaptureAny(42)\n"
    "    hostCaptureAny(2.5)\n"
    "    hostCaptureAny(true)\n"
    "    hostCaptureAny(\"hello\")\n"
    "    hostCaptureAny([]int{3, 4, 5})\n"
    "    hostCaptureAny(map[str]int{\"a\": 7, \"b\": 9})\n"
    "    hostCaptureAny([3]int{10, 20, 30})\n"
    "    hostCaptureAny(Pair{11, 13})\n"
    "}\n"
    "\n"
    "fn emitSpeaker*() {\n"
    "    var s: Speaker = Dog{name: \"rex\", count: 0}\n"
    "    hostCaptureSpeaker(s)\n"
    "}\n"
    "\n"
    "fn emitNullAny*() {\n"
    "    var v: any = null\n"
    "    hostCaptureNullAny(v)\n"
    "}\n"
    "\n"
    "fn emitClosureAny*() {\n"
    "    hostCaptureClosureAny(fn (): int {return 1})\n"
    "}\n"
    "\n"
    "fn anyInt*(v: any): int {\n"
    "    return int(v)\n"
    "}\n"
    "\n"
    "fn speakerName*(s: Speaker): str {\n"
    "    return s.speak()\n"
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


static bool expectTypeName(UmkaAPI *api, const UmkaType *type, const char *expected, const char *label)
{
    const char *actual = api->umkaGetTypeName(type);
    if (!actual || strcmp(actual, expected) != 0)
    {
        fprintf(stderr, "%s type name '%s', expected '%s'\n", label, actual ? actual : "<null>", expected);
        return false;
    }
    return true;
}


static bool checkStringIntMap(Umka *umka, UmkaAPI *api, UmkaMap *map, int64_t expected)
{
    UmkaStackSlot key = {0};
    key.ptrVal = api->umkaMakeStr(umka, "a");
    if (!key.ptrVal)
        return false;

    int64_t *item = (int64_t *)api->umkaGetMapItem(umka, map, key);
    api->umkaDecRef(umka, key.ptrVal);

    return item && *item == expected;
}


static bool checkPairType(UmkaAPI *api, const UmkaType *type)
{
    const char *name = NULL;
    const UmkaType *fieldType = NULL;
    int offset = 0;

    if (!expectKind(api, type, UMKA_TYPE_STRUCT, "Pair"))
        return false;
    if (!expectTypeName(api, type, "Pair", "Pair"))
        return false;
    if (api->umkaGetFieldCount(type) != 2)
        return false;
    if (!api->umkaGetField(type, 0, &name, &fieldType, &offset) || strcmp(name, "left") != 0 || offset != 0)
        return false;
    if (!expectKind(api, fieldType, UMKA_TYPE_INT, "Pair.left"))
        return false;
    if (!api->umkaGetField(type, 1, &name, &fieldType, &offset) || strcmp(name, "right") != 0 || offset != (int)sizeof(int64_t))
        return false;

    return expectKind(api, fieldType, UMKA_TYPE_INT, "Pair.right");
}


static bool checkAnyValue(Umka *umka, UmkaAPI *api, int index, const UmkaAny *any)
{
    const UmkaType *selfType = NULL;
    const UmkaType *type = NULL;
    void *self = NULL;
    UmkaStackSlot slot = {0};

    if (!api->umkaGetAnySelf(any, &selfType, &self) || !self || !selfType)
        return false;
    if (!expectKind(api, selfType, UMKA_TYPE_PTR, "any self type"))
        return false;
    if (!api->umkaGetAnyValue(any, &type, &slot) || !type)
        return false;

    switch (index)
    {
        case ANY_INT:
            return expectKind(api, type, UMKA_TYPE_INT, "any int") && slot.intVal == 42;

        case ANY_REAL:
            return expectKind(api, type, UMKA_TYPE_REAL, "any real") && slot.realVal > 2.49 && slot.realVal < 2.51;

        case ANY_BOOL:
            return expectKind(api, type, UMKA_TYPE_BOOL, "any bool") && slot.intVal == 1;

        case ANY_STR:
            return expectKind(api, type, UMKA_TYPE_STR, "any str") && slot.ptrVal && strcmp((char *)slot.ptrVal, "hello") == 0;

        case ANY_ARRAY:
        {
            IntArray *array = (IntArray *)slot.ptrVal;
            return expectKind(api, type, UMKA_TYPE_DYNARRAY, "any []int") &&
                   array &&
                   api->umkaGetDynArrayLen(array) == 3 &&
                   array->data[0] == 3 &&
                   array->data[2] == 5 &&
                   expectKind(api, api->umkaGetBaseType(type), UMKA_TYPE_INT, "[]int item");
        }

        case ANY_MAP:
            return expectKind(api, type, UMKA_TYPE_MAP, "any map") &&
                   expectKind(api, api->umkaGetMapKeyType(type), UMKA_TYPE_STR, "map key") &&
                   expectKind(api, api->umkaGetMapItemType(type), UMKA_TYPE_INT, "map item") &&
                   checkStringIntMap(umka, api, (UmkaMap *)slot.ptrVal, 7);

        case ANY_FIXED_ARRAY:
        {
            int64_t *items = (int64_t *)slot.ptrVal;
            return expectKind(api, type, UMKA_TYPE_ARRAY, "any [3]int") &&
                   api->umkaGetTypeSize(type) == 3 * (int)sizeof(int64_t) &&
                   items &&
                   items[0] == 10 &&
                   items[2] == 30 &&
                   expectKind(api, api->umkaGetBaseType(type), UMKA_TYPE_INT, "[3]int item");
        }

        case ANY_PAIR:
        {
            Pair *pair = (Pair *)slot.ptrVal;
            return checkPairType(api, type) && pair && pair->left == 11 && pair->right == 13;
        }

        default:
            return false;
    }
}


static void hostCaptureAny(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaAny *value = (UmkaAny *)api->umkaGetParam(params, 0);

    if (anyIndex >= ANY_COUNT)
    {
        noteCallbackFailure("too many any values");
        return;
    }

    if (!expectKind(api, type, UMKA_TYPE_INTERFACE, "any param") ||
        !checkAnyValue(umka, api, anyIndex, value))
    {
        noteCallbackFailure("hostCaptureAny inspect");
        return;
    }

    UmkaStackSlot slot = {0};
    slot.ptrVal = value;
    if (!api->umkaRetainHostValue(umka, &anyHandles[anyIndex], type, slot))
    {
        noteCallbackFailure("hostCaptureAny retain");
        return;
    }

    anyIndex++;
}


static void hostCaptureSpeaker(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaAny *value = (UmkaAny *)api->umkaGetParam(params, 0);
    const UmkaType *concreteType = NULL;
    UmkaStackSlot concrete = {0};

    if (!expectKind(api, type, UMKA_TYPE_INTERFACE, "Speaker param") ||
        !expectTypeName(api, type, "Speaker", "Speaker") ||
        !api->umkaGetAnyValue(value, &concreteType, &concrete) ||
        !expectTypeName(api, concreteType, "Dog", "Speaker concrete"))
    {
        noteCallbackFailure("hostCaptureSpeaker inspect");
        return;
    }

    Dog *dog = (Dog *)concrete.ptrVal;
    if (!dog || !dog->name || strcmp(dog->name, "rex") != 0 || dog->count != 0)
    {
        noteCallbackFailure("hostCaptureSpeaker concrete value");
        return;
    }

    UmkaStackSlot slot = {0};
    slot.ptrVal = value;
    if (!api->umkaRetainHostValue(umka, &speakerHandle, type, slot))
        noteCallbackFailure("hostCaptureSpeaker retain");
}


static void hostCaptureNullAny(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaAny *value = (UmkaAny *)api->umkaGetParam(params, 0);
    const UmkaType *dynamicType = NULL;
    void *self = NULL;
    UmkaStackSlot slot = {0};

    if (api->umkaGetAnySelf(value, &dynamicType, &self) ||
        api->umkaGetAnyValue(value, &dynamicType, &slot))
    {
        noteCallbackFailure("null any should not deconstruct");
        return;
    }

    slot.ptrVal = value;
    if (!api->umkaRetainHostValue(umka, &nullHandle, type, slot))
        noteCallbackFailure("null any retain");
}


static void hostCaptureClosureAny(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaAny *value = (UmkaAny *)api->umkaGetParam(params, 0);
    const UmkaType *dynamicType = NULL;
    UmkaStackSlot slot = {0};
    UmkaHostHandle retained = {0};

    if (!api->umkaGetAnyValue(value, &dynamicType, &slot) ||
        !expectKind(api, dynamicType, UMKA_TYPE_CLOSURE, "closure any"))
    {
        noteCallbackFailure("closure any inspect");
        return;
    }

    slot.ptrVal = value;
    api->umkaMakeHostHandle(&retained);
    if (!api->umkaRetainHostValue(umka, &retained, type, slot))
    {
        noteCallbackFailure("closure any retain");
        return;
    }

    api->umkaClearHostHandle(&closureAnyHandle);
    closureAnyHandle = retained;
}


static void hostCaptureFuncType(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    char spelling[128];

    if (!expectKind(api, type, UMKA_TYPE_CLOSURE, "function type") ||
        api->umkaGetFuncParamCount(type) != 2 ||
        strcmp(api->umkaGetFuncParamName(type, 0), "a") != 0 ||
        strcmp(api->umkaGetFuncParamName(type, 1), "b") != 0 ||
        !expectKind(api, api->umkaGetFuncParamType(type, 0), UMKA_TYPE_INT, "function param a") ||
        !expectKind(api, api->umkaGetFuncParamType(type, 1), UMKA_TYPE_STR, "function param b") ||
        !expectKind(api, api->umkaGetFuncResultType(type), UMKA_TYPE_REAL, "function result") ||
        api->umkaGetTypeSpelling(type, spelling, sizeof(spelling)) <= 0 ||
        !strstr(spelling, "fn"))
    {
        noteCallbackFailure("hostCaptureFuncType");
        return;
    }

    funcTypeCaptured = 1;
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


static int checkRetainedAnyValues(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);

    if (anyIndex != ANY_COUNT)
        return fail("not all any values were captured");

    for (int i = 0; i < ANY_COUNT; i++)
    {
        UmkaStackSlot value = api->umkaGetHostHandleValue(&anyHandles[i]);
        if (!value.ptrVal)
            return fail("retained any handle has no value");
        if (!checkAnyValue(umka, api, i, (UmkaAny *)value.ptrVal))
            return fail("retained any value check failed");
    }

    return 0;
}


static int callAnyIntWithRetainedHandle(Umka *umka)
{
    UmkaFuncContext fn = {0};

    if (getFunc(umka, "anyInt", &fn))
        return 1;

    UmkaStackSlot value = umkaGetHostHandleValue(&anyHandles[ANY_INT]);
    *(UmkaAny *)umkaGetParam(fn.params, 0) = *(UmkaAny *)value.ptrVal;

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "anyInt");

    int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != 42)
    {
        fprintf(stderr, "anyInt returned %lld, expected 42\n", (long long)actual);
        return 1;
    }

    return 0;
}


static int callSpeakerWithRetainedHandle(Umka *umka)
{
    UmkaFuncContext fn = {0};

    if (getFunc(umka, "speakerName", &fn))
        return 1;

    UmkaStackSlot value = umkaGetHostHandleValue(&speakerHandle);
    if (!value.ptrVal)
        return fail("speaker handle has no value");

    memcpy(umkaGetParam(fn.params, 0), value.ptrVal, umkaGetTypeSize(umkaGetHostHandleType(&speakerHandle)));

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "speakerName");

    char *actual = umkaGetResult(fn.params, fn.result)->ptrVal;
    if (!actual || strcmp(actual, "rex") != 0)
    {
        fprintf(stderr, "speakerName returned %s, expected rex\n", actual ? actual : "<null>");
        return 1;
    }

    return 0;
}


static int checkNegativeHandleCases(void)
{
    UmkaHostHandle empty = {0};
    UmkaStackSlot slot = {0};

    umkaMakeHostHandle(&empty);
    if (umkaHostHandleValid(&empty))
        return fail("empty handle is valid");
    if (umkaGetAnyValue(NULL, NULL, &slot))
        return fail("NULL any deconstructed");

    umkaClearHostHandle(&empty);
    umkaReleaseHostHandle(&empty);
    return 0;
}


static int callRuntimeError(Umka *umka)
{
    UmkaFuncContext fn = {0};

    if (getFunc(umka, "failRuntime", &fn))
        return 1;

    int err = umkaCall(umka, &fn);
    if (err != UMKA_ERR_RUNTIME)
    {
        fprintf(stderr, "failRuntime returned %d, expected %d\n", err, UMKA_ERR_RUNTIME);
        return failUmka(umka, "failRuntime");
    }

    return 0;
}


static void clearHandles(void)
{
    for (int i = 0; i < ANY_COUNT; i++)
        umkaClearHostHandle(&anyHandles[i]);

    umkaClearHostHandle(&speakerHandle);
    umkaClearHostHandle(&nullHandle);
    umkaClearHostHandle(&closureAnyHandle);
}


int main(void)
{
    int status = 0;

    for (int i = 0; i < ANY_COUNT; i++)
        umkaMakeHostHandle(&anyHandles[i]);
    umkaMakeHostHandle(&speakerHandle);
    umkaMakeHostHandle(&nullHandle);
    umkaMakeHostHandle(&closureAnyHandle);

    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    if (!umkaInit(umka, "hostapi_dynamic.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return status;
    }

    umkaAddFunc(umka, "hostCaptureAny", hostCaptureAny);
    umkaAddFunc(umka, "hostCaptureSpeaker", hostCaptureSpeaker);
    umkaAddFunc(umka, "hostCaptureNullAny", hostCaptureNullAny);
    umkaAddFunc(umka, "hostCaptureClosureAny", hostCaptureClosureAny);
    umkaAddFunc(umka, "hostCaptureFuncType", hostCaptureFuncType);

    if (!umkaCompile(umka))
    {
        status = failUmka(umka, "umkaCompile");
        clearHandles();
        umkaFree(umka);
        return status;
    }

    status |= callNoArgs(umka, "emitFuncType");
    if (!funcTypeCaptured)
        status |= fail("function type was not captured");

    status |= callNoArgs(umka, "emitAnyValues");
    if (callbackFailures)
        status = 1;

    status |= checkRetainedAnyValues(umka);
    status |= callAnyIntWithRetainedHandle(umka);

    status |= callNoArgs(umka, "emitSpeaker");
    if (callbackFailures)
        status = 1;
    status |= callSpeakerWithRetainedHandle(umka);

    status |= callNoArgs(umka, "emitNullAny");
    status |= callNoArgs(umka, "emitClosureAny");
    if (callbackFailures)
        status = 1;

    status |= checkNegativeHandleCases();
    status |= callRuntimeError(umka);

    clearHandles();
    umkaFree(umka);
    return status;
}
