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


typedef struct
{
    char *name;
    int64_t count;
} Dog;


typedef struct
{
    char *name;
} Silent;


static const char *source =
    "type Pair* = struct {left, right: int}\n"
    "type Dog* = struct {name: str; count: int}\n"
    "type Silent* = struct {name: str}\n"
    "type Speaker* = interface {speak(): str}\n"
    "\n"
    "fn (d: ^Dog) speak(): str {\n"
    "    d.count++\n"
    "    return d.name\n"
    "}\n"
    "\n"
    "fn acceptInt*(x: int): int {return x}\n"
    "fn acceptReal*(x: real): real {return x}\n"
    "fn acceptBool*(x: bool): bool {return x}\n"
    "fn strLen*(s: str): int {return len(s)}\n"
    "fn arraySum*(a: []int): int {return a[0] + a[1] + a[2]}\n"
    "fn mapValue*(m: map[str]int): int {return m[\"a\"]}\n"
    "fn fixedArraySum*(a: [3]int): int {return a[0] + a[1] + a[2]}\n"
    "fn pairSum*(p: Pair): int {return p.left + p.right}\n"
    "fn dogMarker*(d: Dog): int {return d.count}\n"
    "fn silentNameLen*(s: Silent): int {return len(s.name)}\n"
    "fn sampleFunc*(): int {return 1}\n"
    "fn acceptFunc*(f: fn(): int): int {return f()}\n"
    "\n"
    "fn anyScore*(v: any): int {\n"
    "    switch x := type(v) {\n"
    "        case int: return x\n"
    "        case real: return trunc(x * 10.0)\n"
    "        case bool:\n"
    "            if x {return 1}\n"
    "            return 0\n"
    "        case str: return len(x)\n"
    "        case []int: return x[0] + x[1] + x[2]\n"
    "        case map[str]int: return x[\"a\"]\n"
    "        case [3]int: return x[0] + x[1] + x[2]\n"
    "        case Pair: return x.left + x.right\n"
    "        default: return -1\n"
    "    }\n"
    "    return -1\n"
    "}\n"
    "\n"
    "fn anyValid*(v: any): bool {return valid(v)}\n"
    "\n"
    "fn speakerName*(s: Speaker): str {return s.speak()}\n"
    "fn speakerCountAfterSpeak*(s: Speaker): int {\n"
    "    s.speak()\n"
    "    d := Dog(s)\n"
    "    return d.count\n"
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


static int callIntFuncWithAssignedValue(Umka *umka, const char *name, const UmkaType *type, UmkaStackSlot value, int64_t expected)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, name, &fn))
        return 1;

    void *param = umkaGetParam(fn.params, 0);
    if (!umkaAssignHostValue(umka, param, type, value))
        return fail("umkaAssignHostValue failed");

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


static int callAnyScore(Umka *umka, const UmkaAny *value, int64_t expected)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "anyScore", &fn))
        return 1;

    const UmkaType *anyType = umkaGetParamType(fn.params, 0);
    UmkaStackSlot slot = {0};
    slot.ptrVal = (void *)value;

    void *param = umkaGetParam(fn.params, 0);
    if (!umkaAssignHostValue(umka, param, anyType, slot))
        return fail("assign any param failed");

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "anyScore");

    int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;

    if (actual != expected)
    {
        fprintf(stderr, "anyScore returned %lld, expected %lld\n", (long long)actual, (long long)expected);
        return 1;
    }

    return 0;
}


static int callAnyValid(Umka *umka, const UmkaAny *value, int expected)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "anyValid", &fn))
        return 1;

    const UmkaType *anyType = umkaGetParamType(fn.params, 0);
    UmkaStackSlot slot = {0};
    slot.ptrVal = (void *)value;

    void *param = umkaGetParam(fn.params, 0);
    if (!umkaAssignHostValue(umka, param, anyType, slot))
        return fail("assign any valid param failed");

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "anyValid");

    int actual = umkaGetResult(fn.params, fn.result)->intVal ? 1 : 0;

    if (actual != expected)
    {
        fprintf(stderr, "anyValid returned %d, expected %d\n", actual, expected);
        return 1;
    }

    return 0;
}


static int testDirectAssignment(Umka *umka)
{
    const UmkaType *intType = getParamType(umka, "acceptInt");
    const UmkaType *strType = getParamType(umka, "strLen");
    if (!intType || !strType)
        return fail("failed to get direct assignment types");

    UmkaStackSlot value = {0};
    value.intVal = 123;
    if (callIntFuncWithAssignedValue(umka, "acceptInt", intType, value, 123))
        return 1;

    UmkaFuncContext fn = {0};
    if (getFunc(umka, "strLen", &fn))
        return 1;

    void *param = umkaGetParam(fn.params, 0);

    UmkaStackSlot first = {0};
    first.ptrVal = umkaMakeStr(umka, "first");
    if (!umkaAssignHostValue(umka, param, strType, first))
        return fail("first string assign failed");
    umkaDecRef(umka, first.ptrVal);

    UmkaStackSlot second = {0};
    second.ptrVal = umkaMakeStr(umka, "second");
    if (!umkaAssignHostValue(umka, param, strType, second))
        return fail("second string assign failed");
    umkaDecRef(umka, second.ptrVal);

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "strLen");

    int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;

    if (actual != 6)
    {
        fprintf(stderr, "strLen returned %lld, expected 6\n", (long long)actual);
        return 1;
    }

    char *owned = NULL;
    UmkaStackSlot ownedValue = {0};
    ownedValue.ptrVal = umkaMakeStr(umka, "owned");
    if (!umkaAssignHostValue(umka, &owned, strType, ownedValue))
        return fail("owned string assign failed");
    umkaDecRef(umka, ownedValue.ptrVal);

    ownedValue = (UmkaStackSlot){0};
    ownedValue.ptrVal = umkaMakeStr(umka, "replace");
    if (!umkaAssignHostValue(umka, &owned, strType, ownedValue))
        return fail("owned string replace failed");
    umkaDecRef(umka, ownedValue.ptrVal);

    if (!owned || strcmp(owned, "replace") != 0)
        return fail("owned string replacement has wrong value");

    if (!umkaReleaseHostValue(umka, &owned, strType) || owned)
        return fail("owned string release failed");

    return 0;
}


static int makeStringIntMap(Umka *umka, UmkaMap *map, const UmkaType *type)
{
    if (!umkaMakeMap(umka, map, type))
        return fail("umkaMakeMap failed");

    UmkaStackSlot key = {0};
    key.ptrVal = umkaMakeStr(umka, "a");
    UmkaStackSlot item = {0};
    item.intVal = 77;

    if (!umkaSetMapItem(umka, map, key, item))
        return fail("umkaSetMapItem failed");

    umkaDecRef(umka, key.ptrVal);
    return 0;
}


static int testConstructedAnyValues(Umka *umka)
{
    const UmkaType *anyType = getParamType(umka, "anyScore");
    const UmkaType *intType = getParamType(umka, "acceptInt");
    const UmkaType *realType = getParamType(umka, "acceptReal");
    const UmkaType *boolType = getParamType(umka, "acceptBool");
    const UmkaType *strType = getParamType(umka, "strLen");
    const UmkaType *arrayType = getParamType(umka, "arraySum");
    const UmkaType *mapType = getParamType(umka, "mapValue");
    const UmkaType *fixedArrayType = getParamType(umka, "fixedArraySum");
    const UmkaType *pairType = getParamType(umka, "pairSum");

    if (!anyType || !intType || !realType || !boolType || !strType || !arrayType || !mapType || !fixedArrayType || !pairType)
        return fail("failed to get any construction types");

    UmkaAny any = {0};
    UmkaStackSlot value = {0};

    value.intVal = 42;
    if (!umkaMakeAny(umka, &any, intType, value) || callAnyScore(umka, &any, 42))
        return fail("constructed any int failed");
    umkaReleaseHostValue(umka, &any, anyType);

    value = (UmkaStackSlot){0};
    value.realVal = 2.5;
    if (!umkaMakeAny(umka, &any, realType, value) || callAnyScore(umka, &any, 25))
        return fail("constructed any real failed");
    umkaReleaseHostValue(umka, &any, anyType);

    value = (UmkaStackSlot){0};
    value.intVal = 1;
    if (!umkaMakeAny(umka, &any, boolType, value) || callAnyScore(umka, &any, 1))
        return fail("constructed any bool failed");
    umkaReleaseHostValue(umka, &any, anyType);

    value = (UmkaStackSlot){0};
    value.ptrVal = umkaMakeStr(umka, "hello");
    if (!umkaMakeAny(umka, &any, strType, value))
        return fail("constructed any str failed");
    umkaDecRef(umka, value.ptrVal);
    if (callAnyScore(umka, &any, 5))
        return 1;
    umkaReleaseHostValue(umka, &any, anyType);

    IntArray array = {0};
    umkaMakeDynArray(umka, &array, arrayType, 3);
    array.data[0] = 10;
    array.data[1] = 20;
    array.data[2] = 30;
    value = (UmkaStackSlot){0};
    value.ptrVal = &array;
    if (!umkaMakeAny(umka, &any, arrayType, value))
        return fail("constructed any array failed");
    umkaReleaseHostValue(umka, &array, arrayType);
    if (callAnyScore(umka, &any, 60))
        return 1;
    umkaReleaseHostValue(umka, &any, anyType);

    UmkaMap map = {0};
    if (makeStringIntMap(umka, &map, mapType))
        return 1;
    value = (UmkaStackSlot){0};
    value.ptrVal = &map;
    if (!umkaMakeAny(umka, &any, mapType, value))
        return fail("constructed any map failed");
    umkaReleaseHostValue(umka, &map, mapType);
    if (callAnyScore(umka, &any, 77))
        return 1;
    umkaReleaseHostValue(umka, &any, anyType);

    int64_t fixedArray[3] = {3, 4, 5};
    value = (UmkaStackSlot){0};
    value.ptrVal = fixedArray;
    if (!umkaMakeAny(umka, &any, fixedArrayType, value) || callAnyScore(umka, &any, 12))
        return fail("constructed any fixed array failed");
    umkaReleaseHostValue(umka, &any, anyType);

    Pair pair = {11, 13};
    value = (UmkaStackSlot){0};
    value.ptrVal = &pair;
    if (!umkaMakeAny(umka, &any, pairType, value) || callAnyScore(umka, &any, 24))
        return fail("constructed any pair failed");
    umkaReleaseHostValue(umka, &any, anyType);

    if (!umkaMakeAny(umka, &any, NULL, (UmkaStackSlot){0}) || callAnyValid(umka, &any, 0))
        return fail("constructed null any failed");
    umkaReleaseHostValue(umka, &any, anyType);

    return 0;
}


static int callSpeakerName(Umka *umka, void *speaker, const UmkaType *speakerType, const char *expected)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "speakerName", &fn))
        return 1;

    UmkaStackSlot value = {0};
    value.ptrVal = speaker;
    void *param = umkaGetParam(fn.params, 0);
    if (!umkaAssignHostValue(umka, param, speakerType, value))
        return fail("assign speaker param failed");

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "speakerName");

    char *actual = umkaGetResult(fn.params, fn.result)->ptrVal;

    if (!actual || strcmp(actual, expected) != 0)
    {
        fprintf(stderr, "speakerName returned %s, expected %s\n", actual ? actual : "<null>", expected);
        return 1;
    }

    return 0;
}


static int callSpeakerCount(Umka *umka, void *speaker, const UmkaType *speakerType, int64_t expected)
{
    UmkaFuncContext fn = {0};
    if (getFunc(umka, "speakerCountAfterSpeak", &fn))
        return 1;

    UmkaStackSlot value = {0};
    value.ptrVal = speaker;
    void *param = umkaGetParam(fn.params, 0);
    if (!umkaAssignHostValue(umka, param, speakerType, value))
        return fail("assign speaker count param failed");

    int err = umkaCall(umka, &fn);
    if (err)
        return failUmka(umka, "speakerCountAfterSpeak");

    int64_t actual = umkaGetResult(fn.params, fn.result)->intVal;

    if (actual != expected)
    {
        fprintf(stderr, "speakerCountAfterSpeak returned %lld, expected %lld\n", (long long)actual, (long long)expected);
        return 1;
    }

    return 0;
}


static int testConstructedInterface(Umka *umka)
{
    const UmkaType *speakerType = getParamType(umka, "speakerName");
    const UmkaType *dogType = getParamType(umka, "dogMarker");
    const UmkaType *silentType = getParamType(umka, "silentNameLen");

    if (!speakerType || !dogType || !silentType)
        return fail("failed to get interface construction types");

    void *speaker = calloc(1, umkaGetTypeSize(speakerType));
    if (!speaker)
        return fail("speaker allocation failed");

    Dog dog = {0};
    dog.name = umkaMakeStr(umka, "rex");
    dog.count = 0;

    UmkaStackSlot value = {0};
    value.ptrVal = &dog;
    if (!umkaMakeInterface(umka, speaker, speakerType, dogType, value))
    {
        umkaDecRef(umka, dog.name);
        free(speaker);
        return fail("umkaMakeInterface failed for Dog/Speaker");
    }
    umkaDecRef(umka, dog.name);

    int status = 0;
    status |= callSpeakerName(umka, speaker, speakerType, "rex");
    status |= callSpeakerCount(umka, speaker, speakerType, 2);

    if (!umkaReleaseHostValue(umka, speaker, speakerType))
        status |= fail("speaker release failed");
    free(speaker);

    void *unsupported = calloc(1, umkaGetTypeSize(speakerType));
    if (!unsupported)
        return fail("unsupported speaker allocation failed");

    Silent silent = {0};
    silent.name = umkaMakeStr(umka, "quiet");
    value = (UmkaStackSlot){0};
    value.ptrVal = &silent;
    if (umkaMakeInterface(umka, unsupported, speakerType, silentType, value))
    {
        umkaReleaseHostValue(umka, unsupported, speakerType);
        status |= fail("Silent unexpectedly converted to Speaker");
    }
    umkaDecRef(umka, silent.name);
    free(unsupported);

    return status;
}


static int testNegativeCases(Umka *umka)
{
    const UmkaType *funcType = getParamType(umka, "acceptFunc");
    const UmkaType *intType = getParamType(umka, "acceptInt");
    if (!funcType || !intType)
        return fail("failed to get negative case types");

    UmkaAny any = {0};
    if (umkaMakeAny(umka, &any, funcType, (UmkaStackSlot){0}))
    {
        const UmkaType *anyType = getParamType(umka, "anyScore");
        umkaReleaseHostValue(umka, &any, anyType);
        return fail("function value unexpectedly converted to any");
    }

    UmkaStackSlot value = {0};
    value.intVal = 1;
    if (umkaAssignHostValue(umka, NULL, intType, value))
        return fail("assignment to NULL destination succeeded");

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


int main(void)
{
    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    if (!umkaInit(umka, "hostapi_construct.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
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

    int status = 0;

    status |= testDirectAssignment(umka);
    status |= testConstructedAnyValues(umka);
    status |= testConstructedInterface(umka);
    status |= testNegativeCases(umka);

    const UmkaType *anyType = getParamType(umka, "anyScore");
    const UmkaType *strType = getParamType(umka, "strLen");
    UmkaAny afterError = {0};
    UmkaStackSlot value = {0};
    value.ptrVal = umkaMakeStr(umka, "after");
    if (!umkaMakeAny(umka, &afterError, strType, value))
        status |= fail("failed to construct after-error any");
    umkaDecRef(umka, value.ptrVal);

    status |= callRuntimeError(umka);

    if (!umkaReleaseHostValue(umka, &afterError, anyType))
        status |= fail("release after runtime error failed");

    umkaFree(umka);
    return status;
}
