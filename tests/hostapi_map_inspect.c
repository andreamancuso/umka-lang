#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/umka_api.h"


typedef UmkaDynArray(int64_t) IntArray;


typedef struct
{
    int64_t left, right;
} Pair;


static const char *source =
    "type Pair* = struct {left, right: int}\n"
    "\n"
    "fn makeStringIntMap*(): map[str]int {\n"
    "    return map[str]int{\"alpha\": 17, \"beta\": 25}\n"
    "}\n"
    "\n"
    "fn makeIntIntMap*(): map[int]int {\n"
    "    return map[int]int{2: 40, 5: 2}\n"
    "}\n"
    "\n"
    "fn makeAnyMap*(): map[str]any {\n"
    "    m := map[str]any{}\n"
    "    m[\"null\"] = null\n"
    "    m[\"int\"] = 42\n"
    "    m[\"uint\"] = uint(43)\n"
    "    m[\"bool\"] = true\n"
    "    m[\"char\"] = 'Z'\n"
    "    m[\"real\"] = 2.5\n"
    "    m[\"real32\"] = real32(1.25)\n"
    "    m[\"str\"] = \"hello\"\n"
    "    m[\"pair\"] = Pair{7, 8}\n"
    "    m[\"slice\"] = []int{3, 4, 5}\n"
    "    m[\"closure\"] = fn (): int { return 7 }\n"
    "    return m\n"
    "}\n"
    "\n"
    "fn makeFiberAnyMap*(): map[str]any {\n"
    "    child := make(fiber, fn () {})\n"
    "    resume(child)\n"
    "    m := map[str]any{}\n"
    "    m[\"fiber\"] = child\n"
    "    return m\n"
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
        fprintf(stderr, "umkaGetFunc failed for %s\n", name);
        return 1;
    }
    return 0;
}


static int callRetainedResult(Umka *umka, const char *name, UmkaHostHandle *handle, bool expectRetain)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, name, &fn))
        return 1;

    const UmkaType *resultType = umkaGetResultType(fn.params, fn.result);
    int resultSize = umkaGetTypeSize(resultType);
    void *resultBuffer = calloc(1, resultSize > 0 ? (size_t)resultSize : 1);
    if (!resultBuffer)
        return fail("result buffer allocation failed");

    fn.result->ptrVal = resultBuffer;

    int err = umkaCall(umka, &fn);
    if (err)
    {
        free(resultBuffer);
        return failUmka(umka, name);
    }

    UmkaStackSlot value = {0};
    value.ptrVal = resultBuffer;
    bool retained = umkaRetainHostValue(umka, handle, resultType, value);

    if (expectRetain && !retained)
    {
        (void)umkaReleaseHostValue(umka, resultBuffer, resultType);
        free(resultBuffer);
        return fail("retaining returned map failed");
    }

    if (!expectRetain && retained)
    {
        (void)umkaReleaseHostValue(umka, resultBuffer, resultType);
        free(resultBuffer);
        return fail("retaining unsupported returned map succeeded");
    }

    if (!umkaReleaseHostValue(umka, resultBuffer, resultType))
    {
        free(resultBuffer);
        return fail("releasing returned map buffer failed");
    }

    free(resultBuffer);
    fn.result->ptrVal = NULL;
    return 0;
}


static int findStringEntry(Umka *umka, UmkaAPI *api, const UmkaHostHandle *handle, const char *key, UmkaHostMapEntry *entry)
{
    int64_t count = 0;
    if (!api->umkaGetHostMapCount(umka, handle, &count))
        return fail("umkaGetHostMapCount failed");

    for (int64_t i = 0; i < count; i++)
    {
        UmkaHostMapEntry current = {0};
        const char *actualKey = NULL;
        if (!api->umkaGetHostMapEntry(umka, handle, i, &current) ||
            !api->umkaGetHostMapEntryStringKey(umka, &current, &actualKey))
            return fail("string map entry inspection failed");

        if (actualKey && strcmp(actualKey, key) == 0)
        {
            *entry = current;
            return 0;
        }
    }

    fprintf(stderr, "map key '%s' not found\n", key);
    return 1;
}


static int checkStringIntMap(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaHostHandle handle = {0};
    umkaMakeHostHandle(&handle);

    int status = callRetainedResult(umka, "makeStringIntMap", &handle, true);
    if (status)
        return status;

    int64_t count = 0;
    if (!api->umkaGetHostMapCount(umka, &handle, &count) || count != 2)
        status |= fail("map[str]int count mismatch");

    UmkaHostMapEntry alpha = {0};
    UmkaHostMapEntry beta = {0};
    status |= findStringEntry(umka, api, &handle, "alpha", &alpha);
    status |= findStringEntry(umka, api, &handle, "beta", &beta);

    UmkaStackSlot value = {0};
    if (!status && (!api->umkaGetHostMapEntryValue(umka, &alpha, &value) || value.intVal != 17))
        status |= fail("alpha value mismatch");
    if (!status && (!api->umkaGetHostMapEntryValue(umka, &beta, &value) || value.intVal != 25))
        status |= fail("beta value mismatch");

    UmkaHostHandle keyHandle = {0};
    umkaMakeHostHandle(&keyHandle);
    if (!status && !api->umkaRetainHostMapEntryKey(umka, &alpha, &keyHandle))
        status |= fail("string key retain failed");
    umkaClearHostHandle(&keyHandle);

    UmkaAny any = {0};
    if (!status && api->umkaGetHostMapEntryAnyValue(umka, &alpha, &any))
        status |= fail("map[str]int entry was accepted as any");

    umkaClearHostHandle(&handle);
    return status;
}


static int checkIntKeyNegativeCase(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaHostHandle handle = {0};
    umkaMakeHostHandle(&handle);

    int status = callRetainedResult(umka, "makeIntIntMap", &handle, true);
    if (status)
        return status;

    UmkaHostMapEntry entry = {0};
    const char *key = NULL;
    if (!api->umkaGetHostMapEntry(umka, &handle, 0, &entry))
        status |= fail("map[int]int entry lookup failed");
    if (!status && api->umkaGetHostMapEntryStringKey(umka, &entry, &key))
        status |= fail("integer key was accepted as string key");

    umkaClearHostHandle(&handle);
    return status;
}


static int inspectAnyEntry(Umka *umka, UmkaAPI *api, const UmkaHostHandle *handle, const char *key, UmkaTypeKind expectedKind)
{
    UmkaHostMapEntry entry = {0};
    if (findStringEntry(umka, api, handle, key, &entry))
        return 1;

    UmkaAny any = {0};
    if (!api->umkaGetHostMapEntryAnyValue(umka, &entry, &any))
        return fail("any entry extraction failed");

    const UmkaType *payloadType = NULL;
    UmkaStackSlot payload = {0};
    if (expectedKind == UMKA_TYPE_NULL)
    {
        if (api->umkaGetAnyValue(&any, &payloadType, &payload))
            return fail("null any deconstructed");
        return 0;
    }

    if (!api->umkaGetAnyValue(&any, &payloadType, &payload) ||
        !expectKind(api, payloadType, expectedKind, key))
        return fail("any payload kind mismatch");

    switch (expectedKind)
    {
        case UMKA_TYPE_INT:
            return payload.intVal == 42 ? 0 : fail("int payload mismatch");

        case UMKA_TYPE_UINT:
            return payload.uintVal == 43 ? 0 : fail("uint payload mismatch");

        case UMKA_TYPE_BOOL:
            return payload.intVal == 1 ? 0 : fail("bool payload mismatch");

        case UMKA_TYPE_CHAR:
            return payload.intVal == 'Z' ? 0 : fail("char payload mismatch");

        case UMKA_TYPE_REAL:
            return payload.realVal > 2.49 && payload.realVal < 2.51 ? 0 : fail("real payload mismatch");

        case UMKA_TYPE_REAL32:
            return payload.realVal > 1.24 && payload.realVal < 1.26 ? 0 : fail("real32 payload mismatch");

        case UMKA_TYPE_STR:
            return payload.ptrVal && strcmp((const char *)payload.ptrVal, "hello") == 0 ? 0 : fail("string payload mismatch");

        case UMKA_TYPE_STRUCT:
        {
            Pair *pair = (Pair *)payload.ptrVal;
            if (!pair || pair->left != 7 || pair->right != 8)
                return fail("Pair payload mismatch");

            UmkaHostHandle retained = {0};
            umkaMakeHostHandle(&retained);
            bool ok = api->umkaRetainHostValue(umka, &retained, payloadType, payload);
            umkaClearHostHandle(&retained);
            return ok ? 0 : fail("Pair payload retain failed");
        }

        case UMKA_TYPE_DYNARRAY:
        {
            IntArray *array = (IntArray *)payload.ptrVal;
            if (!array || api->umkaGetDynArrayLen(array) != 3 || array->data[0] != 3 || array->data[2] != 5)
                return fail("[]int payload mismatch");

            UmkaHostHandle retained = {0};
            umkaMakeHostHandle(&retained);
            bool ok = api->umkaRetainHostValue(umka, &retained, payloadType, payload);
            umkaClearHostHandle(&retained);
            return ok ? 0 : fail("[]int payload retain failed");
        }

        case UMKA_TYPE_CLOSURE:
        {
            UmkaHostHandle retained = {0};
            umkaMakeHostHandle(&retained);
            bool ok = api->umkaRetainHostValue(umka, &retained, payloadType, payload);
            umkaClearHostHandle(&retained);
            return ok ? 0 : fail("closure payload retain failed");
        }

        case UMKA_TYPE_FIBER:
        {
            UmkaHostHandle retained = {0};
            umkaMakeHostHandle(&retained);
            bool ok = api->umkaFiberValid(umka, payload) &&
                      api->umkaRetainHostValue(umka, &retained, payloadType, payload);
            umkaClearHostHandle(&retained);
            return ok ? 0 : fail("fiber payload retain failed");
        }

        default:
            return 0;
    }
}


static int checkAnyMap(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaHostHandle handle = {0};
    umkaMakeHostHandle(&handle);

    int status = callRetainedResult(umka, "makeAnyMap", &handle, true);
    if (status)
        return status;

    int64_t count = 0;
    if (!api->umkaGetHostMapCount(umka, &handle, &count) || count != 11)
        status |= fail("map[str]any count mismatch");

    status |= inspectAnyEntry(umka, api, &handle, "null", UMKA_TYPE_NULL);
    status |= inspectAnyEntry(umka, api, &handle, "int", UMKA_TYPE_INT);
    status |= inspectAnyEntry(umka, api, &handle, "uint", UMKA_TYPE_UINT);
    status |= inspectAnyEntry(umka, api, &handle, "bool", UMKA_TYPE_BOOL);
    status |= inspectAnyEntry(umka, api, &handle, "char", UMKA_TYPE_CHAR);
    status |= inspectAnyEntry(umka, api, &handle, "real", UMKA_TYPE_REAL);
    status |= inspectAnyEntry(umka, api, &handle, "real32", UMKA_TYPE_REAL32);
    status |= inspectAnyEntry(umka, api, &handle, "str", UMKA_TYPE_STR);
    status |= inspectAnyEntry(umka, api, &handle, "pair", UMKA_TYPE_STRUCT);
    status |= inspectAnyEntry(umka, api, &handle, "slice", UMKA_TYPE_DYNARRAY);
    status |= inspectAnyEntry(umka, api, &handle, "closure", UMKA_TYPE_CLOSURE);

    UmkaHostMapEntry pairEntry = {0};
    UmkaHostHandle retainedAny = {0};
    umkaMakeHostHandle(&retainedAny);
    if (!status &&
        (findStringEntry(umka, api, &handle, "pair", &pairEntry) ||
         !api->umkaRetainHostMapEntryValue(umka, &pairEntry, &retainedAny)))
        status |= fail("retaining map any entry failed");
    umkaClearHostHandle(&retainedAny);

    umkaClearHostHandle(&handle);
    return status;
}


static int checkFiberPayloadRetained(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaHostHandle handle = {0};
    umkaMakeHostHandle(&handle);

    int status = callRetainedResult(umka, "makeFiberAnyMap", &handle, true);
    if (!status)
        status |= inspectAnyEntry(umka, api, &handle, "fiber", UMKA_TYPE_FIBER);
    umkaClearHostHandle(&handle);
    return status;
}


static int checkInvalidCases(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaHostHandle mapHandle = {0};
    UmkaHostHandle stringHandle = {0};
    umkaMakeHostHandle(&mapHandle);
    umkaMakeHostHandle(&stringHandle);

    int status = callRetainedResult(umka, "makeStringIntMap", &mapHandle, true);
    if (status)
        return status;

    int64_t count = -1;
    if (api->umkaGetHostMapCount(NULL, &mapHandle, &count))
        status |= fail("NULL runtime accepted for host map count");
    if (api->umkaGetHostMapCount(umka, NULL, &count))
        status |= fail("NULL map handle accepted");

    UmkaStackSlot stringValue = {0};
    stringValue.ptrVal = umkaMakeStr(umka, "not a map");
    const UmkaType *stringType = api->umkaGetMapKeyType(api->umkaGetHostHandleType(&mapHandle));
    if (!stringValue.ptrVal || !api->umkaRetainHostValue(umka, &stringHandle, stringType, stringValue))
    {
        if (stringValue.ptrVal)
            umkaDecRef(umka, stringValue.ptrVal);
        status |= fail("test string retain setup failed");
    }
    else
    {
        umkaDecRef(umka, stringValue.ptrVal);
        if (api->umkaGetHostMapCount(umka, &stringHandle, &count))
            status |= fail("non-map handle accepted");
    }

    UmkaHostMapEntry entry = {0};
    if (api->umkaGetHostMapEntry(umka, &mapHandle, -1, &entry))
        status |= fail("negative map index accepted");
    if (api->umkaGetHostMapEntry(umka, &mapHandle, 2, &entry))
        status |= fail("out-of-range map index accepted");

    if (!api->umkaGetHostMapEntry(umka, &mapHandle, 0, &entry))
        status |= fail("valid map entry rejected");
    else
    {
        entry.index = 99;
        UmkaStackSlot value = {0};
        if (api->umkaGetHostMapEntryValue(umka, &entry, &value))
            status |= fail("stale map entry index accepted");
    }

    umkaClearHostHandle(&stringHandle);
    umkaClearHostHandle(&mapHandle);
    return status;
}


int main(void)
{
    int status = 0;
    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    if (!umkaInit(umka, "hostapi_map_inspect.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return status;
    }

    if (!umkaCompile(umka))
    {
        status = failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return status;
    }

    status |= checkStringIntMap(umka);
    status |= checkIntKeyNegativeCase(umka);
    status |= checkAnyMap(umka);
    status |= checkFiberPayloadRetained(umka);
    status |= checkInvalidCases(umka);

    umkaFree(umka);
    return status;
}
