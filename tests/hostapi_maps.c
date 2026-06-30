#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include "../src/umka_api.h"


static int callbackFailures = 0;


typedef bool (*MapFillFunc)(Umka *umka, UmkaAPI *api, UmkaMap *map, const UmkaType *type);


static const char *source =
    "fn hostIntMap*(): map[int]int\n"
    "fn hostStringIntMap*(): map[str]int\n"
    "fn hostStringStringMap*(): map[str]str\n"
    "\n"
    "fn takeIntMap*(m: map[int]int): int {\n"
    "    return m[2] + m[5]\n"
    "}\n"
    "\n"
    "fn takeStringIntMap*(m: map[str]int): int {\n"
    "    return m[\"alpha\"] + m[\"beta\"]\n"
    "}\n"
    "\n"
    "fn takeStringStringMap*(m: map[str]str): int {\n"
    "    return len(m[\"left\"]) + len(m[\"right\"])\n"
    "}\n"
    "\n"
    "fn useHostIntMap*(): int {\n"
    "    m := hostIntMap()\n"
    "    return m[2] + m[5]\n"
    "}\n"
    "\n"
    "fn useHostStringIntMap*(): int {\n"
    "    m := hostStringIntMap()\n"
    "    return m[\"alpha\"] + m[\"beta\"]\n"
    "}\n"
    "\n"
    "fn useHostStringStringMap*(): int {\n"
    "    m := hostStringStringMap()\n"
    "    return len(m[\"left\"]) + len(m[\"right\"])\n"
    "}\n"
    "\n"
    "fn unsupportedMapShape*(m: map[str][]int): int {\n"
    "    return 0\n"
    "}\n";


static void noteCallbackFailure(const char *message)
{
    callbackFailures++;
    fprintf(stderr, "callback failure: %s\n", message);
}


static int fail(const char *message)
{
    fprintf(stderr, "%s\n", message);
    return 1;
}


static int failUmka(Umka *umka, const char *operation)
{
    UmkaError *error = umkaGetError(umka);
    fprintf(stderr, "%s failed: %s (%d, %d): %s\n",
            operation,
            error->fileName ? error->fileName : "<unknown>",
            error->line,
            error->pos,
            error->msg ? error->msg : "<no message>");
    return 1;
}


static bool setIntItem(UmkaAPI *api, Umka *umka, UmkaMap *map, int64_t key, int64_t item)
{
    UmkaStackSlot keySlot = {0};
    UmkaStackSlot itemSlot = {0};

    keySlot.intVal = key;
    itemSlot.intVal = item;

    return api->umkaSetMapItem(umka, map, keySlot, itemSlot);
}


static bool setStringIntItem(UmkaAPI *api, Umka *umka, UmkaMap *map, const char *key, int64_t item)
{
    UmkaStackSlot keySlot = {0};
    UmkaStackSlot itemSlot = {0};

    keySlot.ptrVal = api->umkaMakeStr(umka, key);
    itemSlot.intVal = item;

    bool ok = keySlot.ptrVal && api->umkaSetMapItem(umka, map, keySlot, itemSlot);

    if (keySlot.ptrVal)
        api->umkaDecRef(umka, keySlot.ptrVal);

    return ok;
}


static bool setStringStringItem(UmkaAPI *api, Umka *umka, UmkaMap *map, const char *key, const char *item)
{
    UmkaStackSlot keySlot = {0};
    UmkaStackSlot itemSlot = {0};

    keySlot.ptrVal = api->umkaMakeStr(umka, key);
    itemSlot.ptrVal = api->umkaMakeStr(umka, item);

    bool ok = keySlot.ptrVal && itemSlot.ptrVal && api->umkaSetMapItem(umka, map, keySlot, itemSlot);

    if (keySlot.ptrVal)
        api->umkaDecRef(umka, keySlot.ptrVal);
    if (itemSlot.ptrVal)
        api->umkaDecRef(umka, itemSlot.ptrVal);

    return ok;
}


static bool fillIntMap(Umka *umka, UmkaAPI *api, UmkaMap *map, const UmkaType *type)
{
    return api->umkaMakeMap(umka, map, type) &&
           setIntItem(api, umka, map, 2, 40) &&
           setIntItem(api, umka, map, 5, 2);
}


static bool fillStringIntMap(Umka *umka, UmkaAPI *api, UmkaMap *map, const UmkaType *type)
{
    return api->umkaMakeMap(umka, map, type) &&
           setStringIntItem(api, umka, map, "alpha", 17) &&
           setStringIntItem(api, umka, map, "beta", 25);
}


static bool fillStringStringMap(Umka *umka, UmkaAPI *api, UmkaMap *map, const UmkaType *type)
{
    return api->umkaMakeMap(umka, map, type) &&
           setStringStringItem(api, umka, map, "left", "north") &&
           setStringStringItem(api, umka, map, "right", "south");
}


static void hostIntMap(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaMap *map = (UmkaMap *)api->umkaGetResult(params, result)->ptrVal;
    const UmkaType *type = api->umkaGetResultType(params, result);

    if (!fillIntMap(umka, api, map, type))
        noteCallbackFailure("hostIntMap");
}


static void hostStringIntMap(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaMap *map = (UmkaMap *)api->umkaGetResult(params, result)->ptrVal;
    const UmkaType *type = api->umkaGetResultType(params, result);

    if (!fillStringIntMap(umka, api, map, type))
        noteCallbackFailure("hostStringIntMap");
}


static void hostStringStringMap(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    UmkaMap *map = (UmkaMap *)api->umkaGetResult(params, result)->ptrVal;
    const UmkaType *type = api->umkaGetResultType(params, result);

    if (!fillStringStringMap(umka, api, map, type))
        noteCallbackFailure("hostStringStringMap");
}


static int callIntResult(Umka *umka, const char *name, int64_t expected)
{
    UmkaFuncContext fn = {0};

    if (!umkaGetFunc(umka, NULL, name, &fn))
        return fail("umkaGetFunc failed");

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


static int callWithMap(Umka *umka, const char *name, MapFillFunc fill, int64_t expected)
{
    UmkaFuncContext fn = {0};
    UmkaAPI *api = umkaGetAPI(umka);

    if (!umkaGetFunc(umka, NULL, name, &fn))
        return fail("umkaGetFunc failed");

    const UmkaType *mapType = umkaGetParamType(fn.params, 0);
    UmkaMap map = {0};
    if (!fill(umka, api, &map, mapType))
        return fail("failed to create host map argument");

    *(UmkaMap *)umkaGetParam(fn.params, 0) = map;

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


static int checkUnsupportedMapShape(Umka *umka)
{
    UmkaFuncContext fn = {0};
    UmkaMap map = {0};

    if (!umkaGetFunc(umka, NULL, "unsupportedMapShape", &fn))
        return fail("umkaGetFunc failed");

    if (umkaMakeMap(umka, &map, umkaGetParamType(fn.params, 0)))
        return fail("umkaMakeMap accepted unsupported map[str][]int");

    return 0;
}


int main(void)
{
    int status = 0;
    Umka *umka = umkaAlloc();
    if (!umka)
        return fail("umkaAlloc failed");

    if (!umkaInit(umka, "hostapi_maps.um", source, 1024 * 1024, NULL, 0, NULL, false, false, NULL))
    {
        status = failUmka(umka, "umkaInit");
        umkaFree(umka);
        return status;
    }

    umkaAddFunc(umka, "hostIntMap", hostIntMap);
    umkaAddFunc(umka, "hostStringIntMap", hostStringIntMap);
    umkaAddFunc(umka, "hostStringStringMap", hostStringStringMap);

    if (!umkaCompile(umka))
    {
        status = failUmka(umka, "umkaCompile");
        umkaFree(umka);
        return status;
    }

    status |= callWithMap(umka, "takeIntMap", fillIntMap, 42);
    status |= callWithMap(umka, "takeStringIntMap", fillStringIntMap, 42);
    status |= callWithMap(umka, "takeStringStringMap", fillStringStringMap, 10);

    status |= callIntResult(umka, "useHostIntMap", 42);
    status |= callIntResult(umka, "useHostStringIntMap", 42);
    status |= callIntResult(umka, "useHostStringStringMap", 10);

    status |= checkUnsupportedMapShape(umka);

    if (callbackFailures)
        status = 1;

    umkaFree(umka);
    return status;
}
