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


static UmkaHostHandle retainedClosure;
static int callbackFailures = 0;


static const char *source =
    "type Pair* = struct {left, right: int}\n"
    "\n"
    "fn hostAnyMap*(): map[str]any\n"
    "fn hostCaptureClosure*(f: fn(): int)\n"
    "\n"
    "fn acceptInt*(x: int): int {return x}\n"
    "fn acceptUint*(x: uint): uint {return x}\n"
    "fn acceptBool*(x: bool): bool {return x}\n"
    "fn acceptChar*(x: char): char {return x}\n"
    "fn acceptReal*(x: real): real {return x}\n"
    "fn acceptReal32*(x: real32): real32 {return x}\n"
    "fn acceptStr*(x: str): int {return len(x)}\n"
    "fn acceptPair*(x: Pair): int {return x.left + x.right}\n"
    "fn acceptArray*(x: []int): int {return x[0] + x[1] + x[2]}\n"
    "fn acceptFiber*(f: fiber): bool {return valid(f)}\n"
    "\n"
    "fn emitClosure*() {\n"
    "    hostCaptureClosure(fn (): int {return 7})\n"
    "}\n"
    "\n"
    "fn scoreAny*(v: any): int {\n"
    "    if !valid(v) {return 1}\n"
    "    switch x := type(v) {\n"
    "        case int: return x\n"
    "        case uint: return 43\n"
    "        case bool:\n"
    "            if x {return 1}\n"
    "            return 0\n"
    "        case char: return 90\n"
    "        case real: return trunc(x * 10.0)\n"
    "        case real32: return 12\n"
    "        case str: return len(x)\n"
    "        case Pair: return x.left + x.right\n"
    "        case []int: return x[0] + x[1] + x[2]\n"
    "        case fn(): int: return x()\n"
    "        default: return -1000\n"
    "    }\n"
    "    return -1000\n"
    "}\n"
    "\n"
    "fn scoreMap*(m: map[str]any): int {\n"
    "    return scoreAny(m[\"null\"]) + scoreAny(m[\"int\"]) + scoreAny(m[\"uint\"]) +\n"
    "           scoreAny(m[\"bool\"]) + scoreAny(m[\"char\"]) + scoreAny(m[\"real\"]) +\n"
    "           scoreAny(m[\"real32\"]) + scoreAny(m[\"str\"]) + scoreAny(m[\"pair\"]) +\n"
    "           scoreAny(m[\"slice\"]) + scoreAny(m[\"closure\"])\n"
    "}\n"
    "\n"
    "fn useHostAnyMap*(): int {\n"
    "    return scoreMap(hostAnyMap())\n"
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
    return err ? failUmka(umka, name) : 0;
}


static int callIntResult(Umka *umka, const char *name, int64_t expected)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, name, &fn))
        return 1;

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


static bool setAnyItem(Umka *umka, UmkaAPI *api, UmkaMap *map, const char *key, const UmkaType *type, UmkaStackSlot value)
{
    const UmkaType *mapAnyType = api->umkaGetMapItemType(map->type);
    UmkaAny any = {0};
    UmkaStackSlot keySlot = {0};
    UmkaStackSlot itemSlot = {0};

    if (!mapAnyType || !api->umkaMakeAny(umka, &any, type, value))
        return false;

    keySlot.ptrVal = api->umkaMakeStr(umka, key);
    itemSlot.ptrVal = &any;
    bool ok = keySlot.ptrVal && api->umkaSetMapItem(umka, map, keySlot, itemSlot);

    if (keySlot.ptrVal)
        api->umkaDecRef(umka, keySlot.ptrVal);

    if (!api->umkaReleaseHostValue(umka, &any, mapAnyType))
        return false;

    return ok;
}


static bool fillAnyMap(Umka *umka, UmkaAPI *api, UmkaMap *map, const UmkaType *mapType)
{
    const UmkaType *intType = getParamType(umka, "acceptInt");
    const UmkaType *uintType = getParamType(umka, "acceptUint");
    const UmkaType *boolType = getParamType(umka, "acceptBool");
    const UmkaType *charType = getParamType(umka, "acceptChar");
    const UmkaType *realType = getParamType(umka, "acceptReal");
    const UmkaType *real32Type = getParamType(umka, "acceptReal32");
    const UmkaType *strType = getParamType(umka, "acceptStr");
    const UmkaType *pairType = getParamType(umka, "acceptPair");
    const UmkaType *arrayType = getParamType(umka, "acceptArray");
    const UmkaType *closureType = api->umkaGetHostHandleType(&retainedClosure);
    UmkaStackSlot closureValue = api->umkaGetHostHandleValue(&retainedClosure);

    if (!intType || !uintType || !boolType || !charType || !realType || !real32Type ||
        !strType || !pairType || !arrayType || !closureType || !closureValue.ptrVal)
        return false;

    if (!api->umkaMakeMap(umka, map, mapType))
        return false;

    UmkaStackSlot value = {0};
    if (!setAnyItem(umka, api, map, "null", NULL, value))
        return false;

    value = (UmkaStackSlot){0};
    value.intVal = 42;
    if (!setAnyItem(umka, api, map, "int", intType, value))
        return false;

    value = (UmkaStackSlot){0};
    value.uintVal = 43;
    if (!setAnyItem(umka, api, map, "uint", uintType, value))
        return false;

    value = (UmkaStackSlot){0};
    value.intVal = 1;
    if (!setAnyItem(umka, api, map, "bool", boolType, value))
        return false;

    value = (UmkaStackSlot){0};
    value.intVal = 'Z';
    if (!setAnyItem(umka, api, map, "char", charType, value))
        return false;

    value = (UmkaStackSlot){0};
    value.realVal = 2.5;
    if (!setAnyItem(umka, api, map, "real", realType, value))
        return false;

    value = (UmkaStackSlot){0};
    value.realVal = 1.25;
    if (!setAnyItem(umka, api, map, "real32", real32Type, value))
        return false;

    value = (UmkaStackSlot){0};
    value.ptrVal = api->umkaMakeStr(umka, "hello");
    bool ok = value.ptrVal && setAnyItem(umka, api, map, "str", strType, value);
    if (value.ptrVal)
        api->umkaDecRef(umka, value.ptrVal);
    if (!ok)
        return false;

    Pair pair = {7, 8};
    value = (UmkaStackSlot){0};
    value.ptrVal = &pair;
    if (!setAnyItem(umka, api, map, "pair", pairType, value))
        return false;

    IntArray array = {0};
    api->umkaMakeDynArray(umka, &array, arrayType, 3);
    array.data[0] = 3;
    array.data[1] = 4;
    array.data[2] = 5;
    value = (UmkaStackSlot){0};
    value.ptrVal = &array;
    ok = setAnyItem(umka, api, map, "slice", arrayType, value);
    if (!api->umkaReleaseHostValue(umka, &array, arrayType))
        return false;
    if (!ok)
        return false;

    if (!setAnyItem(umka, api, map, "closure", closureType, closureValue))
        return false;

    return true;
}


static void hostCaptureClosure(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *type = api->umkaGetParamType(params, 0);
    UmkaStackSlot value = {0};
    value.ptrVal = api->umkaGetParam(params, 0);

    if (!expectKind(api, type, UMKA_TYPE_CLOSURE, "closure param") ||
        !api->umkaRetainHostValue(umka, &retainedClosure, type, value))
        noteCallbackFailure("hostCaptureClosure");
}


static void hostAnyMap(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaMap *map = (UmkaMap *)api->umkaGetResult(params, result)->ptrVal;
    const UmkaType *type = api->umkaGetResultType(params, result);

    if (!fillAnyMap(umka, api, map, type))
        noteCallbackFailure("hostAnyMap");
}


static int callScoreMapWithHostAssignedMap(Umka *umka, int64_t expected)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "scoreMap", &fn))
        return 1;

    const UmkaType *mapType = api->umkaGetParamType(fn.params, 0);
    UmkaMap map = {0};
    if (!fillAnyMap(umka, api, &map, mapType))
        return fail("failed to build map[str]any argument");

    UmkaStackSlot mapValue = {0};
    mapValue.ptrVal = &map;
    if (!api->umkaAssignHostValue(umka, api->umkaGetParam(fn.params, 0), mapType, mapValue))
    {
        api->umkaReleaseHostValue(umka, &map, mapType);
        return fail("assigning host-created map[str]any failed");
    }

    if (!api->umkaReleaseHostValue(umka, &map, mapType))
        return fail("releasing local map[str]any after assignment failed");

    int err = api->umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "scoreMap");

    int64_t actual = api->umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != expected)
    {
        fprintf(stderr, "scoreMap returned %lld, expected %lld\n", (long long)actual, (long long)expected);
        return 1;
    }

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
            return fail("map entry string key inspection failed");

        if (actualKey && strcmp(actualKey, key) == 0)
        {
            *entry = current;
            return 0;
        }
    }

    fprintf(stderr, "map key '%s' not found\n", key);
    return 1;
}


static int inspectAnyEntry(Umka *umka, UmkaAPI *api, const UmkaHostHandle *handle, const char *key, UmkaTypeKind expectedKind)
{
    UmkaHostMapEntry entry = {0};
    if (findStringEntry(umka, api, handle, key, &entry))
        return 1;

    UmkaAny any = {0};
    if (!api->umkaGetHostMapEntryAnyValue(umka, &entry, &any))
        return fail("retained any entry extraction failed");

    const UmkaType *payloadType = NULL;
    UmkaStackSlot payload = {0};
    if (expectedKind == UMKA_TYPE_NULL)
    {
        if (api->umkaGetAnyValue(&any, &payloadType, &payload))
            return fail("retained null any deconstructed");
        return 0;
    }

    if (!api->umkaGetAnyValue(&any, &payloadType, &payload) ||
        !expectKind(api, payloadType, expectedKind, key))
        return fail("retained any payload kind mismatch");

    switch (expectedKind)
    {
        case UMKA_TYPE_INT:
            return payload.intVal == 42 ? 0 : fail("retained int payload mismatch");

        case UMKA_TYPE_UINT:
            return payload.uintVal == 43 ? 0 : fail("retained uint payload mismatch");

        case UMKA_TYPE_BOOL:
            return payload.intVal == 1 ? 0 : fail("retained bool payload mismatch");

        case UMKA_TYPE_CHAR:
            return payload.intVal == 'Z' ? 0 : fail("retained char payload mismatch");

        case UMKA_TYPE_REAL:
            return payload.realVal > 2.49 && payload.realVal < 2.51 ? 0 : fail("retained real payload mismatch");

        case UMKA_TYPE_REAL32:
            return payload.realVal > 1.24 && payload.realVal < 1.26 ? 0 : fail("retained real32 payload mismatch");

        case UMKA_TYPE_STR:
            return payload.ptrVal && strcmp((const char *)payload.ptrVal, "hello") == 0 ? 0 : fail("retained str payload mismatch");

        case UMKA_TYPE_STRUCT:
        {
            Pair *pair = (Pair *)payload.ptrVal;
            return pair && pair->left == 7 && pair->right == 8 ? 0 : fail("retained Pair payload mismatch");
        }

        case UMKA_TYPE_DYNARRAY:
        {
            IntArray *array = (IntArray *)payload.ptrVal;
            return array && api->umkaGetDynArrayLen(array) == 3 &&
                   array->data[0] == 3 && array->data[1] == 4 && array->data[2] == 5
                   ? 0 : fail("retained []int payload mismatch");
        }

        case UMKA_TYPE_CLOSURE:
        {
            UmkaHostHandle retainedAny = {0};
            api->umkaMakeHostHandle(&retainedAny);
            bool ok = api->umkaRetainHostMapEntryValue(umka, &entry, &retainedAny);
            api->umkaClearHostHandle(&retainedAny);
            return ok ? 0 : fail("retaining retained closure any entry failed");
        }

        default:
            return 0;
    }
}


static int inspectRetainedHostMap(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "scoreMap", &fn))
        return 1;

    const UmkaType *mapType = api->umkaGetParamType(fn.params, 0);
    const UmkaType *anyType = api->umkaGetMapItemType(mapType);
    UmkaMap map = {0};
    UmkaHostHandle handle = {0};
    api->umkaMakeHostHandle(&handle);

    int status = 0;
    if (!fillAnyMap(umka, api, &map, mapType))
        status |= fail("failed to build retained inspection map");

    UmkaStackSlot mapValue = {0};
    mapValue.ptrVal = &map;
    if (!status && !api->umkaRetainHostValue(umka, &handle, mapType, mapValue))
        status |= fail("retaining host-created map[str]any failed");

    int64_t count = 0;
    if (!status && (!api->umkaGetHostMapCount(umka, &handle, &count) || count != 11))
        status |= fail("retained map[str]any count mismatch");

    if (!status)
    {
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
    }

    api->umkaClearHostHandle(&handle);
    if (!api->umkaReleaseHostValue(umka, &map, mapType))
        status |= fail("releasing retained inspection map failed");

    if (!anyType)
        status |= fail("missing any map item type");

    return status;
}


static int checkNegativeCases(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "scoreMap", &fn))
        return 1;

    const UmkaType *mapType = api->umkaGetParamType(fn.params, 0);
    const UmkaType *anyType = api->umkaGetMapItemType(mapType);
    const UmkaType *fiberType = getParamType(umka, "acceptFiber");
    if (!mapType || !anyType || !fiberType)
        return fail("missing negative-case types");

    int status = 0;
    UmkaMap map = {0};
    if (api->umkaMakeMap(NULL, &map, mapType))
        status |= fail("NULL runtime accepted by umkaMakeMap");
    if (api->umkaMakeMap(umka, NULL, mapType))
        status |= fail("NULL map accepted by umkaMakeMap");
    if (!api->umkaMakeMap(umka, &map, mapType))
        return fail("negative-case map creation failed");

    UmkaAny any = {0};
    if (api->umkaMakeAny(umka, &any, fiberType, (UmkaStackSlot){0}))
    {
        api->umkaReleaseHostValue(umka, &any, anyType);
        status |= fail("fiber unexpectedly boxed into any");
    }

    UmkaStackSlot key = {0};
    key.ptrVal = api->umkaMakeStr(umka, "bad");
    UmkaStackSlot item = {0};
    if (key.ptrVal && api->umkaSetMapItem(umka, &map, key, item))
        status |= fail("NULL any item accepted");
    if (key.ptrVal)
        api->umkaDecRef(umka, key.ptrVal);

    key = (UmkaStackSlot){0};
    key.intVal = 1;
    item = (UmkaStackSlot){0};
    if (!api->umkaMakeAny(umka, &any, NULL, (UmkaStackSlot){0}))
        status |= fail("negative-case null any setup failed");
    item.ptrVal = &any;
    if (api->umkaSetMapItem(umka, &map, key, item))
        status |= fail("non-string key accepted for map[str]any");
    api->umkaReleaseHostValue(umka, &any, anyType);

    key = (UmkaStackSlot){0};
    key.ptrVal = api->umkaMakeStr(umka, "fiber");
    UmkaAny fiberLike = {0};
    fiberLike.self = (void *)1;
    fiberLike.selfType = fiberType;
    item = (UmkaStackSlot){0};
    item.ptrVal = &fiberLike;
    if (key.ptrVal && api->umkaSetMapItem(umka, &map, key, item))
        status |= fail("fiber-like any item accepted");
    if (key.ptrVal)
        api->umkaDecRef(umka, key.ptrVal);

    if (api->umkaSetMapItem(NULL, &map, key, item))
        status |= fail("NULL runtime accepted by umkaSetMapItem");
    if (api->umkaSetMapItem(umka, NULL, key, item))
        status |= fail("NULL map accepted by umkaSetMapItem");

    if (!api->umkaReleaseHostValue(umka, &map, mapType))
        status |= fail("negative-case map release failed");

    return status;
}


int main(void)
{
    const int64_t expectedScore = 253;
    int status = 0;

    umkaMakeHostHandle(&retainedClosure);

    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    if (!umkaInit(umka, "hostapi_map_any_construct.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return status;
    }

    umkaAddFunc(umka, "hostAnyMap", hostAnyMap);
    umkaAddFunc(umka, "hostCaptureClosure", hostCaptureClosure);

    if (!umkaCompile(umka))
    {
        status = failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return status;
    }

    status |= callNoArgs(umka, "emitClosure");
    status |= callIntResult(umka, "useHostAnyMap", expectedScore);
    status |= callScoreMapWithHostAssignedMap(umka, expectedScore);
    status |= inspectRetainedHostMap(umka);
    status |= checkNegativeCases(umka);

    if (callbackFailures)
        status = 1;

    umkaClearHostHandle(&retainedClosure);
    umkaFree(umka);
    return status;
}
