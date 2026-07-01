#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../src/umka_api.h"


static const char *source =
    "type Pair* = struct {left, right: int}\n"
    "type Pair2* = struct {left, right: int}\n"
    "type StringBox* = struct {text: str}\n"
    "type Mode* = enum (uint8) {draw = 74; select; remove = 8; edit}\n"
    "type Speaker* = interface {speak(): str}\n"
    "type Worker* = struct {name: str}\n"
    "type Scorer* = fn (x: int, label: str = \"x\"): int\n"
    "type ScalarDefault* = fn (x: int = 42): int\n"
    "\n"
    "fn (w: ^Worker) speak(): str {return w.name}\n"
    "\n"
    "fn acceptInt*(x: int): int {return x}\n"
    "fn acceptStr*(s: str): int {return len(s)}\n"
    "fn acceptPair*(p: Pair): int {return p.left + p.right}\n"
    "fn acceptPair2*(p: Pair2): int {return p.left + p.right}\n"
    "fn acceptStringBox*(b: StringBox): int {return len(b.text)}\n"
    "fn acceptFixedArray*(a: [3]int): int {return a[0]}\n"
    "fn acceptStringFixedArray*(a: [2]str): int {return len(a[0])}\n"
    "fn acceptDynArray*(a: []int): int {return len(a)}\n"
    "fn acceptVariadic*(items: ..int): int {return len(items)}\n"
    "fn acceptNestedDynArray*(a: [][]uint8): int {return len(a)}\n"
    "fn acceptMap*(m: map[str][]int): int {return len(m[\"a\"])}\n"
    "fn acceptAny*(v: any): bool {return valid(v)}\n"
    "fn acceptSpeaker*(s: Speaker): str {return s.speak()}\n"
    "fn acceptScorer*(f: Scorer): int {return f(1)}\n"
    "fn acceptScalarDefault*(f: ScalarDefault): int {return f()}\n"
    "fn acceptFiber*(f: fiber): bool {return valid(f)}\n"
    "fn returnMode*(): Mode {return .draw}\n"
    "fn acceptMode*(m: Mode): int {return int(m)}\n";


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


static int expectInt(const char *label, int actual, int expected)
{
    if (actual != expected)
    {
        fprintf(stderr, "%s: got %d, expected %d\n", label, actual, expected);
        return 1;
    }
    return 0;
}


static int expectBool(const char *label, bool actual, bool expected)
{
    if (actual != expected)
    {
        fprintf(stderr, "%s: got %d, expected %d\n", label, actual ? 1 : 0, expected ? 1 : 0);
        return 1;
    }
    return 0;
}


static int expectKind(UmkaAPI *api, const UmkaType *type, UmkaTypeKind expected, const char *label)
{
    return expectInt(label, (int)api->umkaGetTypeKind(type), (int)expected);
}


static int expectName(UmkaAPI *api, const UmkaType *type, const char *expected, const char *label)
{
    const char *actual = api->umkaGetTypeName(type);
    if (!actual || strcmp(actual, expected) != 0)
    {
        fprintf(stderr, "%s: got '%s', expected '%s'\n", label, actual ? actual : "<null>", expected);
        return 1;
    }
    return 0;
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


static int checkNullQueries(UmkaAPI *api)
{
    int status = 0;
    const char *name = (const char *)1;
    int64_t signedValue = 1;
    uint64_t unsignedValue = 1;

    status |= expectBool("NULL equivalence", api->umkaTypesEquivalent(NULL, NULL), true);
    status |= expectBool("NULL/int equivalence", api->umkaTypesEquivalent(NULL, (const UmkaType *)1), false);
    status |= expectInt("NULL item count", api->umkaGetTypeItemCount(NULL), 0);
    status |= expectBool("NULL has references", api->umkaTypeHasReferences(NULL), false);
    status |= expectBool("NULL indirect slot", api->umkaTypeUsesIndirectValueSlot(NULL), false);
    status |= expectBool("NULL variadic parameter list", api->umkaTypeIsVariadicParamList(NULL), false);
    status |= expectInt("NULL enum count", api->umkaGetEnumMemberCount(NULL), 0);
    status |= expectBool("NULL enum member", api->umkaGetEnumMember(NULL, 0, &name, &signedValue, &unsignedValue), false);
    status |= expectBool("NULL enum clears name", name == NULL, true);
    status |= expectInt("NULL enum clears signed value", (int)signedValue, 0);
    status |= expectInt("NULL default params", api->umkaGetFuncDefaultParamCount(NULL), 0);
    status |= expectBool("NULL callable function type", api->umkaGetCallableFuncType(NULL) == NULL, true);
    return status;
}


static int checkScalarAndEquivalence(Umka *umka, UmkaAPI *api)
{
    int status = 0;
    const UmkaType *intType = getParamType(umka, "acceptInt");
    const UmkaType *strType = getParamType(umka, "acceptStr");
    const UmkaType *pairType = getParamType(umka, "acceptPair");
    const UmkaType *pair2Type = getParamType(umka, "acceptPair2");

    if (!intType || !strType || !pairType || !pair2Type)
        return fail("missing scalar/equivalence test types");

    status |= expectKind(api, intType, UMKA_TYPE_INT, "int kind");
    status |= expectBool("int references", api->umkaTypeHasReferences(intType), false);
    status |= expectBool("int indirect slot", api->umkaTypeUsesIndirectValueSlot(intType), false);
    status |= expectBool("str references", api->umkaTypeHasReferences(strType), true);
    status |= expectBool("str indirect slot", api->umkaTypeUsesIndirectValueSlot(strType), false);
    status |= expectBool("same type equivalence", api->umkaTypesEquivalent(pairType, pairType), true);
    status |= expectBool("distinct named struct equivalence", api->umkaTypesEquivalent(pairType, pair2Type), false);
    return status;
}


static int checkEnum(Umka *umka, UmkaAPI *api)
{
    int status = 0;
    const UmkaType *modeParamType = getParamType(umka, "acceptMode");
    const UmkaType *modeResultType = getResultType(umka, "returnMode");
    const char *name = NULL;
    int64_t signedValue = 0;
    uint64_t unsignedValue = 0;

    if (!modeParamType || !modeResultType)
        return fail("missing enum test types");

    status |= expectKind(api, modeParamType, UMKA_TYPE_UINT8, "Mode kind");
    status |= expectName(api, modeParamType, "Mode", "Mode name");
    status |= expectBool("Mode param/result equivalence", api->umkaTypesEquivalent(modeParamType, modeResultType), true);
    status |= expectInt("Mode enum member count", api->umkaGetEnumMemberCount(modeParamType), 5);

    if (!api->umkaGetEnumMember(modeParamType, 0, &name, &signedValue, &unsignedValue))
        status |= fail("Mode member 0 missing");
    else
    {
        status |= expectBool("Mode member 0 name", strcmp(name, "draw") == 0, true);
        status |= expectInt("Mode member 0 signed", (int)signedValue, 74);
        status |= expectInt("Mode member 0 unsigned", (int)unsignedValue, 74);
    }

    if (!api->umkaGetEnumMember(modeParamType, 4, &name, &signedValue, &unsignedValue))
        status |= fail("Mode member 4 missing");
    else
    {
        status |= expectBool("Mode member 4 name", strcmp(name, "zero") == 0, true);
        status |= expectInt("Mode member 4 signed", (int)signedValue, 0);
        status |= expectInt("Mode member 4 unsigned", (int)unsignedValue, 0);
    }

    status |= expectBool("Mode enum out of range", api->umkaGetEnumMember(modeParamType, 99, NULL, NULL, NULL), false);
    return status;
}


static int checkAggregates(Umka *umka, UmkaAPI *api)
{
    int status = 0;
    const UmkaType *pairType = getParamType(umka, "acceptPair");
    const UmkaType *boxType = getParamType(umka, "acceptStringBox");
    const UmkaType *fixedArrayType = getParamType(umka, "acceptFixedArray");
    const UmkaType *stringFixedArrayType = getParamType(umka, "acceptStringFixedArray");
    const UmkaType *dynArrayType = getParamType(umka, "acceptDynArray");
    const UmkaType *variadicType = getParamType(umka, "acceptVariadic");
    const UmkaType *nestedDynArrayType = getParamType(umka, "acceptNestedDynArray");
    const UmkaType *mapType = getParamType(umka, "acceptMap");
    const UmkaType *strType = getParamType(umka, "acceptStr");
    const char *fieldName = NULL;
    const UmkaType *fieldType = NULL;
    int offset = -1;

    if (!pairType || !boxType || !fixedArrayType || !stringFixedArrayType || !dynArrayType || !variadicType || !nestedDynArrayType || !mapType || !strType)
        return fail("missing aggregate test types");

    status |= expectKind(api, pairType, UMKA_TYPE_STRUCT, "Pair kind");
    status |= expectName(api, pairType, "Pair", "Pair name");
    status |= expectInt("Pair item count", api->umkaGetTypeItemCount(pairType), 2);
    status |= expectInt("Pair field count", api->umkaGetFieldCount(pairType), 2);
    status |= expectBool("Pair references", api->umkaTypeHasReferences(pairType), false);
    status |= expectBool("Pair indirect slot", api->umkaTypeUsesIndirectValueSlot(pairType), true);

    if (!api->umkaGetField(pairType, 0, &fieldName, &fieldType, &offset))
        status |= fail("Pair field 0 missing");
    else
    {
        status |= expectBool("Pair field 0 name", strcmp(fieldName, "left") == 0, true);
        status |= expectKind(api, fieldType, UMKA_TYPE_INT, "Pair field 0 type");
        status |= expectInt("Pair field 0 offset", offset, 0);
    }

    status |= expectBool("StringBox references", api->umkaTypeHasReferences(boxType), true);
    status |= expectKind(api, fixedArrayType, UMKA_TYPE_ARRAY, "[3]int kind");
    status |= expectInt("[3]int item count", api->umkaGetTypeItemCount(fixedArrayType), 3);
    status |= expectKind(api, api->umkaGetBaseType(fixedArrayType), UMKA_TYPE_INT, "[3]int base");
    status |= expectBool("[3]int references", api->umkaTypeHasReferences(fixedArrayType), false);
    status |= expectBool("[2]str references", api->umkaTypeHasReferences(stringFixedArrayType), true);

    status |= expectKind(api, dynArrayType, UMKA_TYPE_DYNARRAY, "[]int kind");
    status |= expectKind(api, api->umkaGetBaseType(dynArrayType), UMKA_TYPE_INT, "[]int base");
    status |= expectBool("[]int references", api->umkaTypeHasReferences(dynArrayType), true);
    status |= expectBool("[]int indirect slot", api->umkaTypeUsesIndirectValueSlot(dynArrayType), true);
    status |= expectBool("[]int variadic parameter list", api->umkaTypeIsVariadicParamList(dynArrayType), false);

    status |= expectKind(api, variadicType, UMKA_TYPE_DYNARRAY, "..int kind");
    status |= expectKind(api, api->umkaGetBaseType(variadicType), UMKA_TYPE_INT, "..int base");
    status |= expectBool("..int variadic parameter list", api->umkaTypeIsVariadicParamList(variadicType), true);

    status |= expectKind(api, nestedDynArrayType, UMKA_TYPE_DYNARRAY, "[][]uint8 kind");
    status |= expectKind(api, api->umkaGetBaseType(nestedDynArrayType), UMKA_TYPE_DYNARRAY, "[][]uint8 base");
    status |= expectKind(api, api->umkaGetBaseType(api->umkaGetBaseType(nestedDynArrayType)), UMKA_TYPE_UINT8, "[][]uint8 nested base");

    status |= expectKind(api, mapType, UMKA_TYPE_MAP, "map kind");
    status |= expectKind(api, api->umkaGetMapKeyType(mapType), UMKA_TYPE_STR, "map key type");
    status |= expectKind(api, api->umkaGetMapItemType(mapType), UMKA_TYPE_DYNARRAY, "map item type");
    status |= expectBool("map key equivalence", api->umkaTypesEquivalent(api->umkaGetMapKeyType(mapType), strType), true);
    status |= expectBool("map item equivalence", api->umkaTypesEquivalent(api->umkaGetMapItemType(mapType), dynArrayType), true);
    status |= expectBool("map references", api->umkaTypeHasReferences(mapType), true);
    return status;
}


static int checkDynamicAndCallableTypes(Umka *umka, UmkaAPI *api)
{
    int status = 0;
    const UmkaType *anyType = getParamType(umka, "acceptAny");
    const UmkaType *speakerType = getParamType(umka, "acceptSpeaker");
    const UmkaType *scorerType = getParamType(umka, "acceptScorer");
    const UmkaType *scalarDefaultType = getParamType(umka, "acceptScalarDefault");
    const UmkaType *fiberType = getParamType(umka, "acceptFiber");
    const UmkaType *fnType = NULL;
    const char *fieldName = NULL;
    const UmkaType *fieldType = NULL;
    int offset = -1;

    if (!anyType || !speakerType || !scorerType || !scalarDefaultType || !fiberType)
        return fail("missing dynamic/callable test types");

    status |= expectKind(api, anyType, UMKA_TYPE_INTERFACE, "any kind");
    status |= expectInt("any item count", api->umkaGetTypeItemCount(anyType), 2);
    status |= expectInt("any field count", api->umkaGetFieldCount(anyType), 2);
    status |= expectBool("any references", api->umkaTypeHasReferences(anyType), true);
    status |= expectBool("any indirect slot", api->umkaTypeUsesIndirectValueSlot(anyType), true);

    status |= expectKind(api, speakerType, UMKA_TYPE_INTERFACE, "Speaker kind");
    status |= expectInt("Speaker item count", api->umkaGetTypeItemCount(speakerType), 3);
    if (!api->umkaGetField(speakerType, 2, &fieldName, &fieldType, &offset))
        status |= fail("Speaker method field missing");
    else
    {
        status |= expectBool("Speaker method name", strcmp(fieldName, "speak") == 0, true);
        status |= expectKind(api, fieldType, UMKA_TYPE_FN, "Speaker method type");
        status |= expectInt("Speaker method source param count", api->umkaGetFuncParamCount(fieldType), 0);
    }

    status |= expectKind(api, scorerType, UMKA_TYPE_CLOSURE, "Scorer kind");
    status |= expectInt("Scorer closure item count", api->umkaGetTypeItemCount(scorerType), 2);
    status |= expectBool("Scorer closure indirect slot", api->umkaTypeUsesIndirectValueSlot(scorerType), true);
    fnType = api->umkaGetCallableFuncType(scorerType);
    status |= expectKind(api, fnType, UMKA_TYPE_FN, "Scorer function type");
    status |= expectBool("Scorer closure equivalence", api->umkaTypesEquivalent(scorerType, scorerType), true);
    status |= expectBool("Scorer function equivalence", api->umkaTypesEquivalent(fnType, api->umkaGetCallableFuncType(scorerType)), true);
    status |= expectBool("function type direct slot", api->umkaTypeUsesIndirectValueSlot(fnType), false);
    status |= expectBool("function type unwrap idempotent", api->umkaGetCallableFuncType(fnType) == fnType, true);
    status |= expectInt("Scorer param count", api->umkaGetFuncParamCount(scorerType), 2);
    status |= expectBool("Scorer param 0 name", strcmp(api->umkaGetFuncParamName(scorerType, 0), "x") == 0, true);
    status |= expectKind(api, api->umkaGetFuncParamType(scorerType, 0), UMKA_TYPE_INT, "Scorer param 0 type");
    status |= expectBool("Scorer param 1 name", strcmp(api->umkaGetFuncParamName(scorerType, 1), "label") == 0, true);
    status |= expectKind(api, api->umkaGetFuncParamType(scorerType, 1), UMKA_TYPE_STR, "Scorer param 1 type");
    status |= expectKind(api, api->umkaGetFuncResultType(scorerType), UMKA_TYPE_INT, "Scorer result type");
    status |= expectInt("Scorer default param count", api->umkaGetFuncDefaultParamCount(scorerType), 1);
    status |= expectInt("Scorer function default param count", api->umkaGetFuncDefaultParamCount(fnType), 1);

    status |= expectKind(api, scalarDefaultType, UMKA_TYPE_CLOSURE, "ScalarDefault kind");
    status |= expectInt("ScalarDefault default param count", api->umkaGetFuncDefaultParamCount(scalarDefaultType), 1);
    status |= expectKind(api, api->umkaGetFuncParamType(scalarDefaultType, 0), UMKA_TYPE_INT, "ScalarDefault param type");

    status |= expectKind(api, fiberType, UMKA_TYPE_FIBER, "fiber kind");
    status |= expectBool("fiber references", api->umkaTypeHasReferences(fiberType), true);
    status |= expectBool("fiber direct slot", api->umkaTypeUsesIndirectValueSlot(fiberType), false);
    status |= expectKind(api, api->umkaGetBaseType(fiberType), UMKA_TYPE_CLOSURE, "fiber base closure");
    return status;
}


int main(void)
{
    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    if (!umkaInit(umka, "hostapi_typeinfo.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
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

    UmkaAPI *api = umkaGetAPI(umka);
    int status = 0;

    status |= checkNullQueries(api);
    status |= checkScalarAndEquivalence(umka, api);
    status |= checkEnum(umka, api);
    status |= checkAggregates(umka, api);
    status |= checkDynamicAndCallableTypes(umka, api);

    umkaFree(umka);
    return status;
}
