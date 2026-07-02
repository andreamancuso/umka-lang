#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/umka_api.h"


typedef UmkaDynArray(UmkaAny) AnyArray;
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
    "fn hostAnyArray*(): []any\n"
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
    "fn acceptIntArray*(x: []int): int {return x[0] + x[1] + x[2]}\n"
    "fn acceptAnyMap*(m: map[str]any): int {return len(m)}\n"
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
    "        case map[str]any: return scoreAny(x[\"inner\"])\n"
    "        case fn(): int: return x()\n"
    "        default: return -1000\n"
    "    }\n"
    "    return -1000\n"
    "}\n"
    "\n"
    "fn scoreArray*(a: []any): int {\n"
    "    total := 0\n"
    "    for _, v in a {\n"
    "        total += scoreAny(v)\n"
    "    }\n"
    "    return total\n"
    "}\n"
    "\n"
    "fn useHostAnyArray*(): int {\n"
    "    return scoreArray(hostAnyArray())\n"
    "}\n"
    "\n"
    "fn makeAnyArray*(): []any {\n"
    "    a := make([]any, 12)\n"
    "    a[0] = null\n"
    "    a[1] = 42\n"
    "    a[2] = uint(43)\n"
    "    a[3] = true\n"
    "    a[4] = 'Z'\n"
    "    a[5] = 2.5\n"
    "    a[6] = real32(1.25)\n"
    "    a[7] = \"hello\"\n"
    "    a[8] = Pair{7, 8}\n"
    "    a[9] = []int{3, 4, 5}\n"
    "    m := map[str]any{}\n"
    "    m[\"inner\"] = 6\n"
    "    a[10] = m\n"
    "    a[11] = fn (): int {return 7}\n"
    "    return a\n"
    "}\n"
    "\n"
    "fn makeFiberAnyArray*(): []any {\n"
    "    child := make(fiber, fn () {})\n"
    "    resume(child)\n"
    "    a := make([]any, 1)\n"
    "    a[0] = child\n"
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


static bool setAnyArrayItem(Umka *umka, UmkaAPI *api, AnyArray *array, int64_t index, const UmkaType *type, UmkaStackSlot value)
{
    const UmkaType *anyType = api->umkaGetBaseType(array->type);
    UmkaAny any = {0};
    UmkaStackSlot item = {0};

    if (!anyType || !api->umkaMakeAny(umka, &any, type, value))
        return false;

    item.ptrVal = &any;
    bool ok = api->umkaSetDynArrayItem(umka, array, index, item);
    bool released = api->umkaReleaseHostValue(umka, &any, anyType);
    return ok && released;
}


static bool setSmallAnyMapItem(Umka *umka, UmkaAPI *api, UmkaMap *map, const char *key, const UmkaType *type, UmkaStackSlot value)
{
    const UmkaType *anyType = api->umkaGetMapItemType(map->type);
    UmkaAny any = {0};
    UmkaStackSlot keySlot = {0};
    UmkaStackSlot itemSlot = {0};

    if (!anyType || !api->umkaMakeAny(umka, &any, type, value))
        return false;

    keySlot.ptrVal = api->umkaMakeStr(umka, key);
    itemSlot.ptrVal = &any;
    bool ok = keySlot.ptrVal && api->umkaSetMapItem(umka, map, keySlot, itemSlot);

    if (keySlot.ptrVal)
        api->umkaDecRef(umka, keySlot.ptrVal);

    bool released = api->umkaReleaseHostValue(umka, &any, anyType);
    return ok && released;
}


static bool fillSmallAnyMap(Umka *umka, UmkaAPI *api, UmkaMap *map, const UmkaType *mapType, int64_t inner)
{
    const UmkaType *intType = getParamType(umka, "acceptInt");
    if (!intType || !api->umkaMakeMap(umka, map, mapType))
        return false;

    UmkaStackSlot value = {0};
    value.intVal = inner;
    return setSmallAnyMapItem(umka, api, map, "inner", intType, value);
}


static bool fillAnyArray(Umka *umka, UmkaAPI *api, AnyArray *array, const UmkaType *arrayType)
{
    const UmkaType *intType = getParamType(umka, "acceptInt");
    const UmkaType *uintType = getParamType(umka, "acceptUint");
    const UmkaType *boolType = getParamType(umka, "acceptBool");
    const UmkaType *charType = getParamType(umka, "acceptChar");
    const UmkaType *realType = getParamType(umka, "acceptReal");
    const UmkaType *real32Type = getParamType(umka, "acceptReal32");
    const UmkaType *strType = getParamType(umka, "acceptStr");
    const UmkaType *pairType = getParamType(umka, "acceptPair");
    const UmkaType *intArrayType = getParamType(umka, "acceptIntArray");
    const UmkaType *mapType = getParamType(umka, "acceptAnyMap");
    const UmkaType *closureType = api->umkaGetHostHandleType(&retainedClosure);
    UmkaStackSlot closureValue = api->umkaGetHostHandleValue(&retainedClosure);

    if (!intType || !uintType || !boolType || !charType || !realType || !real32Type ||
        !strType || !pairType || !intArrayType || !mapType || !closureType || !closureValue.ptrVal)
        return false;

    api->umkaMakeDynArray(umka, array, arrayType, 12);

    UmkaStackSlot value = {0};
    if (!setAnyArrayItem(umka, api, array, 0, NULL, value))
        return false;

    value = (UmkaStackSlot){0};
    value.intVal = 42;
    if (!setAnyArrayItem(umka, api, array, 1, intType, value))
        return false;

    value = (UmkaStackSlot){0};
    value.uintVal = 43;
    if (!setAnyArrayItem(umka, api, array, 2, uintType, value))
        return false;

    value = (UmkaStackSlot){0};
    value.intVal = 1;
    if (!setAnyArrayItem(umka, api, array, 3, boolType, value))
        return false;

    value = (UmkaStackSlot){0};
    value.intVal = 'Z';
    if (!setAnyArrayItem(umka, api, array, 4, charType, value))
        return false;

    value = (UmkaStackSlot){0};
    value.realVal = 2.5;
    if (!setAnyArrayItem(umka, api, array, 5, realType, value))
        return false;

    value = (UmkaStackSlot){0};
    value.realVal = 1.25;
    if (!setAnyArrayItem(umka, api, array, 6, real32Type, value))
        return false;

    value = (UmkaStackSlot){0};
    value.ptrVal = api->umkaMakeStr(umka, "hello");
    bool ok = value.ptrVal && setAnyArrayItem(umka, api, array, 7, strType, value);
    if (value.ptrVal)
        api->umkaDecRef(umka, value.ptrVal);
    if (!ok)
        return false;

    Pair pair = {7, 8};
    value = (UmkaStackSlot){0};
    value.ptrVal = &pair;
    if (!setAnyArrayItem(umka, api, array, 8, pairType, value))
        return false;

    IntArray nested = {0};
    api->umkaMakeDynArray(umka, &nested, intArrayType, 3);
    nested.data[0] = 3;
    nested.data[1] = 4;
    nested.data[2] = 5;
    value = (UmkaStackSlot){0};
    value.ptrVal = &nested;
    ok = setAnyArrayItem(umka, api, array, 9, intArrayType, value);
    if (!api->umkaReleaseHostValue(umka, &nested, intArrayType))
        return false;
    if (!ok)
        return false;

    UmkaMap map = {0};
    if (!fillSmallAnyMap(umka, api, &map, mapType, 6))
        return false;
    value = (UmkaStackSlot){0};
    value.ptrVal = &map;
    ok = setAnyArrayItem(umka, api, array, 10, mapType, value);
    if (!api->umkaReleaseHostValue(umka, &map, mapType))
        return false;
    if (!ok)
        return false;

    if (!setAnyArrayItem(umka, api, array, 11, closureType, closureValue))
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


static void hostAnyArray(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    AnyArray *array = (AnyArray *)api->umkaGetResult(params, result)->ptrVal;
    const UmkaType *type = api->umkaGetResultType(params, result);

    if (!fillAnyArray(umka, api, array, type))
        noteCallbackFailure("hostAnyArray");
}


static int callScoreArrayWithHostAssignedArray(Umka *umka, int64_t expected)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "scoreArray", &fn))
        return 1;

    const UmkaType *arrayType = api->umkaGetParamType(fn.params, 0);
    AnyArray array = {0};
    if (!fillAnyArray(umka, api, &array, arrayType))
        return fail("failed to build []any argument");

    UmkaStackSlot value = {0};
    value.ptrVal = &array;
    if (!api->umkaAssignHostValue(umka, api->umkaGetParam(fn.params, 0), arrayType, value))
    {
        api->umkaReleaseHostValue(umka, &array, arrayType);
        return fail("assigning host-created []any failed");
    }

    if (!api->umkaReleaseHostValue(umka, &array, arrayType))
        return fail("releasing local []any after assignment failed");

    int err = api->umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "scoreArray");

    int64_t actual = api->umkaGetResult(fn.params, fn.result)->intVal;
    if (actual != expected)
    {
        fprintf(stderr, "scoreArray returned %lld, expected %lld\n", (long long)actual, (long long)expected);
        return 1;
    }

    return 0;
}


static int inspectAnyArrayItem(Umka *umka, UmkaAPI *api, const AnyArray *array, int64_t index, UmkaTypeKind expectedKind)
{
    UmkaAny any = {0};
    if (!api->umkaGetDynArrayAnyItem(umka, array, index, &any))
        return fail("any array item extraction failed");

    const UmkaType *payloadType = NULL;
    UmkaStackSlot payload = {0};
    if (expectedKind == UMKA_TYPE_NULL)
    {
        if (api->umkaGetAnyValue(&any, &payloadType, &payload))
            return fail("null any array item deconstructed");
        return 0;
    }

    if (!api->umkaGetAnyValue(&any, &payloadType, &payload) ||
        !expectKind(api, payloadType, expectedKind, "array any payload"))
        return fail("array any payload kind mismatch");

    switch (expectedKind)
    {
        case UMKA_TYPE_INT:
            return payload.intVal == 42 ? 0 : fail("int array any payload mismatch");

        case UMKA_TYPE_UINT:
            return payload.uintVal == 43 ? 0 : fail("uint array any payload mismatch");

        case UMKA_TYPE_BOOL:
            return payload.intVal == 1 ? 0 : fail("bool array any payload mismatch");

        case UMKA_TYPE_CHAR:
            return payload.intVal == 'Z' ? 0 : fail("char array any payload mismatch");

        case UMKA_TYPE_REAL:
            return payload.realVal > 2.49 && payload.realVal < 2.51 ? 0 : fail("real array any payload mismatch");

        case UMKA_TYPE_REAL32:
            return payload.realVal > 1.24 && payload.realVal < 1.26 ? 0 : fail("real32 array any payload mismatch");

        case UMKA_TYPE_STR:
            return payload.ptrVal && strcmp((const char *)payload.ptrVal, "hello") == 0 ? 0 : fail("str array any payload mismatch");

        case UMKA_TYPE_STRUCT:
        {
            Pair *pair = (Pair *)payload.ptrVal;
            return pair && pair->left == 7 && pair->right == 8 ? 0 : fail("Pair array any payload mismatch");
        }

        case UMKA_TYPE_DYNARRAY:
        {
            IntArray *nested = (IntArray *)payload.ptrVal;
            return nested && api->umkaGetDynArrayLen(nested) == 3 &&
                   nested->data[0] == 3 && nested->data[1] == 4 && nested->data[2] == 5
                   ? 0 : fail("[]int array any payload mismatch");
        }

        case UMKA_TYPE_MAP:
        {
            UmkaMap *map = (UmkaMap *)payload.ptrVal;
            UmkaStackSlot key = {0};
            key.ptrVal = api->umkaMakeStr(umka, "inner");
            UmkaAny *inner = key.ptrVal ? (UmkaAny *)api->umkaGetMapItem(umka, map, key) : NULL;
            if (key.ptrVal)
                api->umkaDecRef(umka, key.ptrVal);

            const UmkaType *innerType = NULL;
            UmkaStackSlot innerPayload = {0};
            return inner && api->umkaGetAnyValue(inner, &innerType, &innerPayload) &&
                   api->umkaGetTypeKind(innerType) == UMKA_TYPE_INT && innerPayload.intVal == 6
                   ? 0 : fail("map array any payload mismatch");
        }

        case UMKA_TYPE_CLOSURE:
        {
            UmkaHostHandle handle = {0};
            api->umkaMakeHostHandle(&handle);
            bool ok = api->umkaRetainHostDynArrayItem(umka, array, index, &handle);
            api->umkaClearHostHandle(&handle);
            return ok ? 0 : fail("retaining closure array any item failed");
        }

        case UMKA_TYPE_FIBER:
        {
            UmkaHostHandle handle = {0};
            api->umkaMakeHostHandle(&handle);
            bool ok = api->umkaFiberValid(umka, payload) &&
                      api->umkaRetainHostDynArrayItem(umka, array, index, &handle);
            api->umkaClearHostHandle(&handle);
            return ok ? 0 : fail("retaining fiber array any item failed");
        }

        default:
            return 0;
    }
}


static int inspectAnyArray(Umka *umka, const AnyArray *array)
{
    UmkaAPI *api = umkaGetAPI(umka);
    int status = 0;

    if (api->umkaGetDynArrayLen(array) != 12)
        status |= fail("[]any length mismatch");

    status |= inspectAnyArrayItem(umka, api, array, 0, UMKA_TYPE_NULL);
    status |= inspectAnyArrayItem(umka, api, array, 1, UMKA_TYPE_INT);
    status |= inspectAnyArrayItem(umka, api, array, 2, UMKA_TYPE_UINT);
    status |= inspectAnyArrayItem(umka, api, array, 3, UMKA_TYPE_BOOL);
    status |= inspectAnyArrayItem(umka, api, array, 4, UMKA_TYPE_CHAR);
    status |= inspectAnyArrayItem(umka, api, array, 5, UMKA_TYPE_REAL);
    status |= inspectAnyArrayItem(umka, api, array, 6, UMKA_TYPE_REAL32);
    status |= inspectAnyArrayItem(umka, api, array, 7, UMKA_TYPE_STR);
    status |= inspectAnyArrayItem(umka, api, array, 8, UMKA_TYPE_STRUCT);
    status |= inspectAnyArrayItem(umka, api, array, 9, UMKA_TYPE_DYNARRAY);
    status |= inspectAnyArrayItem(umka, api, array, 10, UMKA_TYPE_MAP);
    status |= inspectAnyArrayItem(umka, api, array, 11, UMKA_TYPE_CLOSURE);

    return status;
}


static int inspectFiberAnyArray(Umka *umka, const AnyArray *array)
{
    UmkaAPI *api = umkaGetAPI(umka);
    int status = 0;

    if (api->umkaGetDynArrayLen(array) != 1)
        status |= fail("fiber []any length mismatch");

    status |= inspectAnyArrayItem(umka, api, array, 0, UMKA_TYPE_FIBER);
    return status;
}


static int inspectRetainedHostArray(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *arrayType = getParamType(umka, "scoreArray");
    AnyArray array = {0};
    UmkaHostHandle handle = {0};
    api->umkaMakeHostHandle(&handle);

    int status = 0;
    if (!arrayType || !fillAnyArray(umka, api, &array, arrayType))
        status |= fail("failed to build retained host []any");

    UmkaStackSlot value = {0};
    value.ptrVal = &array;
    if (!status && !api->umkaRetainHostValue(umka, &handle, arrayType, value))
        status |= fail("retaining host-created []any failed");

    UmkaStackSlot retained = api->umkaGetHostHandleValue(&handle);
    if (!status)
        status |= inspectAnyArray(umka, (const AnyArray *)retained.ptrVal);

    api->umkaClearHostHandle(&handle);
    if (!api->umkaReleaseHostValue(umka, &array, arrayType))
        status |= fail("releasing retained host []any failed");

    return status;
}


static int inspectUmkaArrayResult(Umka *umka, const char *name, bool expectRetain)
{
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaFuncContext fn = {0};
    if (getFunc(umka, name, &fn))
        return 1;

    const UmkaType *arrayType = api->umkaGetResultType(fn.params, fn.result);
    void *resultBuffer = calloc(1, (size_t)api->umkaGetTypeSize(arrayType));
    if (!resultBuffer)
        return fail("result buffer allocation failed");

    fn.result->ptrVal = resultBuffer;
    int err = api->umkaCall(umka, &fn);
    if (err)
    {
        free(resultBuffer);
        return failUmka(umka, name);
    }

    UmkaHostHandle handle = {0};
    api->umkaMakeHostHandle(&handle);
    UmkaStackSlot value = {0};
    value.ptrVal = resultBuffer;
    bool retained = api->umkaRetainHostValue(umka, &handle, arrayType, value);

    int status = 0;
    if (expectRetain && !retained)
        status |= fail("retaining Umka-created []any failed");
    if (!expectRetain && retained)
        status |= fail("retaining unsupported Umka-created []any succeeded");

    if (!status && expectRetain)
    {
        UmkaStackSlot retainedValue = api->umkaGetHostHandleValue(&handle);
        if (strcmp(name, "makeFiberAnyArray") == 0)
            status |= inspectFiberAnyArray(umka, (const AnyArray *)retainedValue.ptrVal);
        else
            status |= inspectAnyArray(umka, (const AnyArray *)retainedValue.ptrVal);
    }

    api->umkaClearHostHandle(&handle);
    if (!api->umkaReleaseHostValue(umka, resultBuffer, arrayType))
        status |= fail("releasing Umka-created []any result failed");

    free(resultBuffer);
    fn.result->ptrVal = NULL;
    return status;
}


static int testDirectIntArrayItems(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *arrayType = getParamType(umka, "acceptIntArray");
    if (!arrayType)
        return fail("missing []int type");

    IntArray array = {0};
    api->umkaMakeDynArray(umka, &array, arrayType, 3);

    int status = 0;
    for (int i = 0; i < 3; i++)
    {
        UmkaStackSlot value = {0};
        value.intVal = (i + 1) * 10;
        if (!api->umkaSetDynArrayItem(umka, &array, i, value))
            status |= fail("setting []int item failed");
    }

    UmkaStackSlot item = {0};
    if (!api->umkaGetDynArrayItem(umka, &array, 1, &item) || item.intVal != 20)
        status |= fail("getting []int item failed");

    UmkaAny wrongAny = {0};
    if (api->umkaGetDynArrayAnyItem(umka, &array, 1, &wrongAny))
        status |= fail("[]int item accepted as any");

    if (!api->umkaReleaseHostValue(umka, &array, arrayType))
        status |= fail("releasing []int failed");

    return status;
}


static int testReplacement(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *arrayType = getParamType(umka, "scoreArray");
    const UmkaType *strType = getParamType(umka, "acceptStr");
    if (!arrayType || !strType)
        return fail("missing replacement types");

    AnyArray array = {0};
    api->umkaMakeDynArray(umka, &array, arrayType, 1);

    UmkaStackSlot value = {0};
    value.ptrVal = api->umkaMakeStr(umka, "first");
    bool ok = value.ptrVal && setAnyArrayItem(umka, api, &array, 0, strType, value);
    if (value.ptrVal)
        api->umkaDecRef(umka, value.ptrVal);
    if (!ok)
        return fail("first replacement item failed");

    value = (UmkaStackSlot){0};
    value.ptrVal = api->umkaMakeStr(umka, "second");
    ok = value.ptrVal && setAnyArrayItem(umka, api, &array, 0, strType, value);
    if (value.ptrVal)
        api->umkaDecRef(umka, value.ptrVal);
    if (!ok)
        return fail("second replacement item failed");

    UmkaAny any = {0};
    const UmkaType *payloadType = NULL;
    UmkaStackSlot payload = {0};
    int status = 0;
    if (!api->umkaGetDynArrayAnyItem(umka, &array, 0, &any) ||
        !api->umkaGetAnyValue(&any, &payloadType, &payload) ||
        !expectKind(api, payloadType, UMKA_TYPE_STR, "replacement item") ||
        !payload.ptrVal ||
        strcmp((const char *)payload.ptrVal, "second") != 0)
        status |= fail("replacement item mismatch");

    if (!api->umkaReleaseHostValue(umka, &array, arrayType))
        status |= fail("replacement array release failed");

    return status;
}


static int testNegativeCases(Umka *umka)
{
    UmkaAPI *api = umkaGetAPI(umka);
    const UmkaType *arrayType = getParamType(umka, "scoreArray");
    const UmkaType *fiberType = getParamType(umka, "acceptFiber");
    if (!arrayType || !fiberType)
        return fail("missing negative-case types");

    AnyArray array = {0};
    api->umkaMakeDynArray(umka, &array, arrayType, 1);

    int status = 0;
    UmkaStackSlot item = {0};
    if (api->umkaSetDynArrayItem(NULL, &array, 0, item))
        status |= fail("NULL runtime accepted by umkaSetDynArrayItem");
    if (api->umkaSetDynArrayItem(umka, NULL, 0, item))
        status |= fail("NULL array accepted by umkaSetDynArrayItem");
    if (api->umkaSetDynArrayItem(umka, &array, -1, item))
        status |= fail("negative index accepted by umkaSetDynArrayItem");
    if (api->umkaSetDynArrayItem(umka, &array, 1, item))
        status |= fail("out-of-range index accepted by umkaSetDynArrayItem");

    UmkaStackSlot out = {0};
    if (api->umkaGetDynArrayItem(NULL, &array, 0, &out))
        status |= fail("NULL runtime accepted by umkaGetDynArrayItem");
    if (api->umkaGetDynArrayItem(umka, NULL, 0, &out))
        status |= fail("NULL array accepted by umkaGetDynArrayItem");
    if (api->umkaGetDynArrayItem(umka, &array, 0, NULL))
        status |= fail("NULL output accepted by umkaGetDynArrayItem");
    if (api->umkaGetDynArrayAnyItem(umka, &array, 0, NULL))
        status |= fail("NULL output accepted by umkaGetDynArrayAnyItem");

    UmkaHostHandle handle = {0};
    api->umkaMakeHostHandle(&handle);
    if (api->umkaRetainHostDynArrayItem(NULL, &array, 0, &handle))
        status |= fail("NULL runtime accepted by umkaRetainHostDynArrayItem");
    if (api->umkaRetainHostDynArrayItem(umka, NULL, 0, &handle))
        status |= fail("NULL array accepted by umkaRetainHostDynArrayItem");
    if (api->umkaRetainHostDynArrayItem(umka, &array, 1, &handle))
        status |= fail("out-of-range index accepted by umkaRetainHostDynArrayItem");
    api->umkaClearHostHandle(&handle);

    UmkaAny any = {0};
    if (api->umkaMakeAny(umka, &any, fiberType, (UmkaStackSlot){0}))
    {
        const UmkaType *anyType = api->umkaGetBaseType(array.type);
        api->umkaReleaseHostValue(umka, &any, anyType);
        status |= fail("fiber unexpectedly boxed into any");
    }

    UmkaAny fiberLike = {0};
    fiberLike.self = (void *)1;
    fiberLike.selfType = fiberType;
    item = (UmkaStackSlot){0};
    item.ptrVal = &fiberLike;
    if (api->umkaSetDynArrayItem(umka, &array, 0, item))
        status |= fail("fiber-like any item accepted");

    if (!api->umkaReleaseHostValue(umka, &array, arrayType))
        status |= fail("negative-case array release failed");

    return status;
}


int main(void)
{
    const int64_t expectedScore = 259;
    int status = 0;

    umkaMakeHostHandle(&retainedClosure);

    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    if (!umkaInit(umka, "hostapi_dynarray_any.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return status;
    }

    umkaAddFunc(umka, "hostAnyArray", hostAnyArray);
    umkaAddFunc(umka, "hostCaptureClosure", hostCaptureClosure);

    if (!umkaCompile(umka))
    {
        status = failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return status;
    }

    status |= callNoArgs(umka, "emitClosure");
    status |= callIntResult(umka, "useHostAnyArray", expectedScore);
    status |= callScoreArrayWithHostAssignedArray(umka, expectedScore);
    status |= inspectRetainedHostArray(umka);
    status |= inspectUmkaArrayResult(umka, "makeAnyArray", true);
    status |= inspectUmkaArrayResult(umka, "makeFiberAnyArray", true);
    status |= testDirectIntArrayItems(umka);
    status |= testReplacement(umka);
    status |= testNegativeCases(umka);

    if (callbackFailures)
        status = 1;

    umkaClearHostHandle(&retainedClosure);
    umkaFree(umka);
    return status;
}
