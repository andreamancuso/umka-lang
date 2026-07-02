#define __USE_MINGW_ANSI_STDIO 1

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "umka_compiler.h"
#include "umka_api.h"

#define UMKA_VERSION    "1.5.7"


static void compileWarning(Umka *umka, const DebugInfo *debug, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    UmkaError report = {0};
    errorReportInit(&report, &umka->storage,
                    debug ? debug->fileName : umka->lex.fileName,
                    debug ? debug->fnName : umka->debug.fnName,
                    debug ? debug->line : umka->lex.tok.line,
                    debug ? 1 : umka->lex.tok.pos,
                    0,
                    format, args);

    if (umka->error.warningCallback)
        ((UmkaWarningCallback)umka->error.warningCallback)(&report);

    va_end(args);
}


static void compileError(Umka *umka, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    errorReportInit(&umka->error.report, &umka->storage, umka->lex.fileName, umka->debug.fnName, umka->lex.tok.line, umka->lex.tok.pos, 1, format, args);

    vmKill(&umka->vm);

    va_end(args);
    longjmp(umka->error.jumper, 1);
}


static void runtimeError(Umka *umka, int code, const char *format, ...)
{
    va_list args;
    va_start(args, format);

    const DebugInfo *debug = &umka->vm.fiber->debugPerInstr[umka->vm.fiber->ip];
    errorReportInit(&umka->error.report, &umka->storage, debug->fileName, debug->fnName, debug->line, 1, code, format, args);

    vmKill(&umka->vm);

    va_end(args);
    longjmp(umka->error.jumper, 1);
}


// API functions

UMKA_API Umka *umkaAlloc(void)
{
    return malloc(sizeof(Umka));
}


UMKA_API bool umkaInit(Umka *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback)
{
    memset(umka, 0, sizeof(Umka));

    // First set error handlers
    umka->error.handler = compileError;
    umka->error.runtimeHandler = runtimeError;
    umka->error.warningHandler = compileWarning;
    umka->error.warningCallback = warningCallback;
    umka->error.context = umka;

    if (setjmp(umka->error.jumper) == 0)
    {
        compilerInit(umka, fileName, sourceString, stackSize, argc, argv, fileSystemEnabled, implLibsEnabled);
        return true;
    }
    return false;
}


UMKA_API bool umkaCompile(Umka *umka)
{
    if (setjmp(umka->error.jumper) == 0)
    {
        compilerCompile(umka);
        return true;
    }
    return false;
}


UMKA_API int umkaRun(Umka *umka)
{
    if (setjmp(umka->error.jumper) == 0)
    {
        umka->error.jumperNesting++;
        compilerRun(umka);
        umka->error.jumperNesting--;
        return 0;
    }

    return umka->error.report.code;
}


UMKA_API int umkaCall(Umka *umka, UmkaFuncContext *fn)
{
    // Nested calls to umkaCall() should not reset the error jumper
    jmp_buf dummyJumper;
    jmp_buf *jumper = umka->error.jumperNesting == 0 ? &umka->error.jumper : &dummyJumper;

    if (setjmp(*jumper) == 0)
    {
        umka->error.jumperNesting++;
        compilerCall(umka, fn);
        umka->error.jumperNesting--;
        return 0;
    }

    return umka->error.report.code;
}


UMKA_API void umkaRequestInterrupt(Umka *umka, const char *message)
{
    if (umka)
        vmRequestInterrupt(&umka->vm, message);
}


UMKA_API void umkaClearInterrupt(Umka *umka)
{
    if (umka)
        vmClearInterrupt(&umka->vm);
}


UMKA_API bool umkaInterruptRequested(Umka *umka)
{
    return umka && vmInterruptRequested(&umka->vm);
}


UMKA_API void umkaFree(Umka *umka)
{
    compilerFree(umka);
    free(umka);
}


UMKA_API UmkaError *umkaGetError(Umka *umka)
{
    return &umka->error.report;
}


UMKA_API bool umkaAlive(Umka *umka)
{
    return vmAlive(&umka->vm);
}


UMKA_API char *umkaAsm(Umka *umka)
{
    return compilerAsm(umka);
}


UMKA_API bool umkaAddModule(Umka *umka, const char *fileName, const char *sourceString)
{
    return compilerAddModule(umka, fileName, sourceString);
}


UMKA_API bool umkaAddFunc(Umka *umka, const char *name, UmkaExternFunc func)
{
    return compilerAddClosure(umka, name, func, NULL);
}


UMKA_API bool umkaGetFunc(Umka *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn)
{
    return compilerGetFunc(umka, moduleName, fnName, fn);
}


UMKA_API bool umkaGetCallStack(Umka *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line)
{
    const Slot *base = umka->vm.fiber->base;
    int ip = umka->vm.fiber->ip;

    while (depth-- > 0)
        if (!vmUnwindCallStack(&umka->vm, &base, &ip))
            return false;

    if (offset)
        *offset = ip;

    if (fileName)
        snprintf(fileName, nameSize, "%s", umka->vm.fiber->debugPerInstr[ip].fileName);

    if (fnName)
        snprintf(fnName, nameSize, "%s", umka->vm.fiber->debugPerInstr[ip].fnName);

    if (line)
        *line = umka->vm.fiber->debugPerInstr[ip].line;

    return true;
}


UMKA_API void umkaSetHook(Umka *umka, UmkaHookEvent event, UmkaHookFunc hook)
{
    vmSetHook(&umka->vm, event, hook);
}


UMKA_API void *umkaAllocData(Umka *umka, int size, UmkaExternFunc onFree)
{
    return vmAllocData(&umka->vm, size, onFree);
}


UMKA_API void umkaIncRef(Umka *umka, void *ptr)
{
    vmIncRef(&umka->vm, ptr, umka->types.predecl.ptrVoidType);    // We have no actual type info provided by the user, so we can only rely on the type info from the heap chunk header, if any
}


UMKA_API void umkaDecRef(Umka *umka, void *ptr)
{
    vmDecRef(&umka->vm, ptr, umka->types.predecl.ptrVoidType);    // We have no actual type info provided by the user, so we can only rely on the type info from the heap chunk header, if any
}


UMKA_API void *umkaGetMapItem(Umka *umka, UmkaMap *map, UmkaStackSlot key)
{
    const Slot *keyPtr = (Slot *)&key;
    return vmGetMapNodeData(&umka->vm, (Map *)map, *keyPtr);
}


UMKA_API bool umkaMakeMap(Umka *umka, UmkaMap *map, const UmkaType *type)
{
    return vmMakeMap(&umka->vm, (Map *)map, type);
}


UMKA_API bool umkaSetMapItem(Umka *umka, UmkaMap *map, UmkaStackSlot key, UmkaStackSlot item)
{
    const Slot *keyPtr = (Slot *)&key;
    const Slot *itemPtr = (Slot *)&item;
    return vmSetMapNodeData(&umka->vm, (Map *)map, *keyPtr, *itemPtr);
}


UMKA_API void umkaMakeHostHandle(UmkaHostHandle *handle)
{
    vmMakeHostHandle(handle);
}


UMKA_API bool umkaRetainHostValue(Umka *umka, UmkaHostHandle *handle, const UmkaType *type, UmkaStackSlot value)
{
    const Slot *valuePtr = (Slot *)&value;
    return vmRetainHostValue(&umka->vm, handle, type, *valuePtr);
}


UMKA_API bool umkaRetainHostData(Umka *umka, UmkaHostHandle *handle, void *ptr)
{
    return vmRetainHostData(&umka->vm, handle, ptr);
}


UMKA_API bool umkaAssignHostValue(Umka *umka, void *dest, const UmkaType *type, UmkaStackSlot value)
{
    const Slot *valuePtr = (Slot *)&value;
    return umka && vmAssignHostValue(&umka->vm, dest, type, *valuePtr);
}


UMKA_API bool umkaReleaseHostValue(Umka *umka, void *dest, const UmkaType *type)
{
    return umka && vmReleaseHostValue(&umka->vm, dest, type);
}


UMKA_API void umkaClearHostHandle(UmkaHostHandle *handle)
{
    vmClearHostHandle(handle);
}


UMKA_API void umkaReleaseHostHandle(UmkaHostHandle *handle)
{
    vmClearHostHandle(handle);
}


UMKA_API bool umkaHostHandleValid(const UmkaHostHandle *handle)
{
    return vmHostHandleValid(handle);
}


UMKA_API const UmkaType *umkaGetHostHandleType(const UmkaHostHandle *handle)
{
    return vmGetHostHandleType(handle);
}


UMKA_API UmkaStackSlot umkaGetHostHandleValue(const UmkaHostHandle *handle)
{
    return vmGetHostHandleValue(handle).apiSlot;
}


UMKA_API bool umkaGetHostMapCount(Umka *umka, const UmkaHostHandle *mapHandle, int64_t *count)
{
    return umka && vmGetHostMapCount(&umka->vm, mapHandle, count);
}


UMKA_API bool umkaGetHostMapEntry(Umka *umka, const UmkaHostHandle *mapHandle, int64_t index, UmkaHostMapEntry *entry)
{
    return umka && vmGetHostMapEntry(&umka->vm, mapHandle, index, entry);
}


UMKA_API bool umkaGetHostMapEntryKey(Umka *umka, const UmkaHostMapEntry *entry, UmkaStackSlot *key)
{
    Slot result = {0};
    bool ok = umka && vmGetHostMapEntryKey(&umka->vm, entry, &result);
    if (key)
        *key = result.apiSlot;
    return ok;
}


UMKA_API bool umkaGetHostMapEntryValue(Umka *umka, const UmkaHostMapEntry *entry, UmkaStackSlot *value)
{
    Slot result = {0};
    bool ok = umka && vmGetHostMapEntryValue(&umka->vm, entry, &result);
    if (value)
        *value = result.apiSlot;
    return ok;
}


UMKA_API bool umkaGetHostMapEntryStringKey(Umka *umka, const UmkaHostMapEntry *entry, const char **key)
{
    return umka && vmGetHostMapEntryStringKey(&umka->vm, entry, key);
}


UMKA_API bool umkaGetHostMapEntryAnyValue(Umka *umka, const UmkaHostMapEntry *entry, UmkaAny *value)
{
    return umka && vmGetHostMapEntryAnyValue(&umka->vm, entry, value);
}


UMKA_API bool umkaRetainHostMapEntryKey(Umka *umka, const UmkaHostMapEntry *entry, UmkaHostHandle *handle)
{
    return umka && vmRetainHostMapEntryKey(&umka->vm, entry, handle);
}


UMKA_API bool umkaRetainHostMapEntryValue(Umka *umka, const UmkaHostMapEntry *entry, UmkaHostHandle *handle)
{
    return umka && vmRetainHostMapEntryValue(&umka->vm, entry, handle);
}


UMKA_API char *umkaMakeStr(Umka *umka, const char *str)
{
    return vmMakeStr(&umka->vm, str);
}


UMKA_API int umkaGetStrLen(const char *str)
{
    if (!str)
        return 0;
    return getStrDims(str)->len;
}


UMKA_API void umkaMakeDynArray(Umka *umka, void *array, const UmkaType *type, int len)
{
    vmMakeDynArray(&umka->vm, (DynArray *)array, type, len);
}


UMKA_API int umkaGetDynArrayLen(const void *array)
{
    const DynArray *dynArray = (const DynArray *)array;
    if (!dynArray->data)
        return 0;
    return getDims(dynArray)->len;
}


UMKA_API const char *umkaGetVersion(void)
{
    if (sizeof(void *) == 8)
        return "Umka "UMKA_VERSION" ("__DATE__" "__TIME__" 64 bit)";
    else if (sizeof(void *) == 4)
        return "Umka "UMKA_VERSION" ("__DATE__" "__TIME__" 32 bit)";
    else
        return "Umka "UMKA_VERSION" ("__DATE__" "__TIME__")";
}


UMKA_API int64_t umkaGetMemUsage(Umka *umka)
{
    return vmGetMemUsage(&umka->vm);
}


UMKA_API void umkaMakeFuncContext(Umka *umka, const UmkaType *closureType, int entryOffset, UmkaFuncContext *fn)
{
    compilerMakeFuncContext(umka, closureType->field[0]->type, entryOffset, fn);
}


UMKA_API UmkaStackSlot *umkaGetParam(UmkaStackSlot *params, int index)
{
    const ParamLayout *paramLayout = getParamLayout(*vmGetStackFrameLayout(params));
    if (index < 0 || index >= paramLayout->numParams - paramLayout->numResultParams - 1)
        return NULL;
    return params + paramLayout->firstSlotIndex[index + 1];                                                 // + 1 to skip upvalues
}


UMKA_API UmkaAny *umkaGetUpvalue(UmkaStackSlot *params)
{
    const ParamLayout *paramLayout = getParamLayout(*vmGetStackFrameLayout(params));
    return (UmkaAny *)(params + paramLayout->firstSlotIndex[0]);
}


UMKA_API UmkaStackSlot *umkaGetResult(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const ParamLayout *paramLayout = getParamLayout(*vmGetStackFrameLayout(params));
    if (paramLayout->numResultParams == 1)
        result->ptrVal = params[paramLayout->firstSlotIndex[paramLayout->numParams - 1]].ptrVal;
    return result;
}


UMKA_API void *umkaGetMetadata(Umka *umka)
{
    return umka->metadata;
}


UMKA_API void umkaSetMetadata(Umka *umka, void *metadata)
{
    umka->metadata = metadata;
}


UMKA_API void *umkaMakeStruct(Umka *umka, const UmkaType *type)
{
    return vmMakeStruct(&umka->vm, type);
}


static const Type *apiGetPtrType(Umka *umka, const Type *type)
{
    if (!umka || !type)
        return NULL;

    for (const Type *candidate = umka->types.first; candidate; candidate = candidate->next)
        if (candidate->kind == TYPE_PTR && typeEquivalent(candidate->base, type))
            return candidate;

    return typeAddPtrTo(&umka->types, &umka->blocks, type);
}


static bool apiGetInterfaceMethodOffsets(Umka *umka, const Type *interfaceType, const Type *selfType, int64_t *methodOffsets)
{
    if (!umka || !interfaceType || interfaceType->kind != TYPE_INTERFACE || !selfType || selfType->kind != TYPE_PTR)
        return false;

    if (interfaceType->numItems <= 2)
        return true;

    const Type *receiverType = selfType->base;
    if (!receiverType || !receiverType->typeIdent || !methodOffsets)
        return false;

    const int receiverModule = receiverType->typeIdent->module;

    for (int i = 2; i < interfaceType->numItems; i++)
    {
        const char *name = interfaceType->field[i]->name;
        const Ident *method = identFind(&umka->idents, &umka->modules, &umka->blocks, receiverModule, name, selfType, false);
        if (!method || !typeCompatible(interfaceType->field[i]->type, method->type))
            return false;

        methodOffsets[i - 2] = method->offset;
    }

    return true;
}


static bool apiTypeHasFields(const Type *type)
{
    return type && (type->kind == TYPE_STRUCT || type->kind == TYPE_INTERFACE || type->kind == TYPE_CLOSURE);
}


UMKA_API bool umkaMakeAny(Umka *umka, UmkaAny *dest, const UmkaType *type, UmkaStackSlot value)
{
    if (!umka || !dest)
        return false;

    if (!type)
        return vmMakeDynamicValue(&umka->vm, dest, umka->types.predecl.anyType, NULL, NULL, (Slot){0}, NULL);

    const Type *selfType = apiGetPtrType(umka, type);
    const Slot *valuePtr = (Slot *)&value;
    return selfType && vmMakeDynamicValue(&umka->vm, dest, umka->types.predecl.anyType, selfType, type, *valuePtr, NULL);
}


UMKA_API bool umkaMakeInterface(Umka *umka, void *dest, const UmkaType *interfaceType, const UmkaType *type, UmkaStackSlot value)
{
    if (!umka || !dest || !interfaceType || interfaceType->kind != TYPE_INTERFACE)
        return false;

    if (!type)
        return vmMakeDynamicValue(&umka->vm, dest, interfaceType, NULL, NULL, (Slot){0}, NULL);

    const Type *selfType = apiGetPtrType(umka, type);
    if (!selfType)
        return false;

    const int numMethods = interfaceType->numItems > 2 ? interfaceType->numItems - 2 : 0;
    int64_t *methodOffsets = NULL;
    if (numMethods > 0)
    {
        methodOffsets = malloc(numMethods * sizeof(int64_t));
        if (!methodOffsets)
            return false;

        if (!apiGetInterfaceMethodOffsets(umka, interfaceType, selfType, methodOffsets))
        {
            free(methodOffsets);
            return false;
        }
    }

    const Slot *valuePtr = (Slot *)&value;
    bool ok = vmMakeDynamicValue(&umka->vm, dest, interfaceType, selfType, type, *valuePtr, methodOffsets);
    free(methodOffsets);
    return ok;
}


static const Type *apiGetCallableFuncType(const Type *type)
{
    if (!type)
        return NULL;

    if (type->kind == TYPE_FN)
        return type;

    if (type->kind == TYPE_CLOSURE)
    {
        if (type->numItems <= 0 || !type->field[0] || !type->field[0]->type)
            return NULL;

        const Type *fnType = type->field[0]->type;
        return fnType->kind == TYPE_FN ? fnType : NULL;
    }

    return NULL;
}


static const Signature *apiGetFuncSignature(const UmkaType *type)
{
    const Type *fnType = apiGetCallableFuncType(type);
    return fnType ? fnType->sig : NULL;
}


static bool apiTypeUsesIndirectValueSlot(const Type *type)
{
    if (!type)
        return false;

    switch (type->kind)
    {
        case TYPE_ARRAY:
        case TYPE_DYNARRAY:
        case TYPE_MAP:
        case TYPE_STRUCT:
        case TYPE_INTERFACE:
        case TYPE_CLOSURE:
            return true;

        default:
            return false;
    }
}


static int apiGetFuncParamStart(const Signature *sig)
{
    return sig ? 1 : 0;
}


static int apiGetFuncParamCount(const Signature *sig)
{
    if (!sig)
        return 0;

    const int resultParams = typeStructured(sig->resultType) ? 1 : 0;
    const int count = sig->numParams - apiGetFuncParamStart(sig) - resultParams;
    return count > 0 ? count : 0;
}


static bool apiGetCallableInfo(const Type *type, Slot value, const Type **fnType, int64_t *entryOffset, const Interface **upvalue)
{
    if (fnType)
        *fnType = NULL;
    if (entryOffset)
        *entryOffset = 0;
    if (upvalue)
        *upvalue = NULL;

    if (!type)
        return false;

    if (type->kind == TYPE_FN)
    {
        if (!type->sig || value.intVal <= 0)
            return false;

        if (fnType)
            *fnType = type;
        if (entryOffset)
            *entryOffset = value.intVal;
        return true;
    }

    if (type->kind == TYPE_CLOSURE)
    {
        if (type->numItems <= 0 || !type->field[0] || !type->field[0]->type || type->field[0]->type->kind != TYPE_FN)
            return false;

        const Closure *closure = (const Closure *)value.ptrVal;
        if (!closure || closure->entryOffset <= 0)
            return false;

        if (fnType)
            *fnType = type->field[0]->type;
        if (entryOffset)
            *entryOffset = closure->entryOffset;
        if (upvalue)
            *upvalue = &closure->upvalue;
        return true;
    }

    return false;
}


UMKA_API const UmkaType *umkaGetBaseType(const UmkaType *type)
{
    if (type && (type->kind == TYPE_PTR || type->kind == TYPE_WEAKPTR || type->kind == TYPE_ARRAY || type->kind == TYPE_DYNARRAY || type->kind == TYPE_FIBER))
        return type->base;
    return NULL;
}


UMKA_API const UmkaType *umkaGetParamType(UmkaStackSlot *params, int index)
{
    const StackFrameLayout *layout = *vmGetStackFrameLayout(params);
    const ParamLayout *paramLayout = getParamLayout(layout);
    if (index < 0 || index >= paramLayout->numParams - paramLayout->numResultParams - 1)
        return NULL;
    return getParamTypes(layout)->paramType[index + 1]; 
}


UMKA_API const UmkaType *umkaGetResultType(UmkaStackSlot *params, UmkaStackSlot *result)
{
    const StackFrameLayout *layout = *vmGetStackFrameLayout(params);
    return getParamTypes(layout)->resultType;
}


UMKA_API const UmkaType *umkaGetFieldType(const UmkaType *structType, const char *fieldName)
{
    if (structType && structType->kind == TYPE_STRUCT)
    {
        const Field *field = typeFindField(structType, fieldName, NULL);
        if (field)
            return field->type;
    }
    return NULL;
}


UMKA_API const UmkaType *umkaGetMapKeyType(const UmkaType *mapType)
{
    if (mapType && mapType->kind == TYPE_MAP)
        return typeMapKey(mapType);
    return NULL;
}


UMKA_API const UmkaType *umkaGetMapItemType(const UmkaType *mapType)
{
    if (mapType && mapType->kind == TYPE_MAP)
        return typeMapItem(mapType);
    return NULL;
}


UMKA_API UmkaTypeKind umkaGetTypeKind(const UmkaType *type)
{
    return type ? (UmkaTypeKind)type->kind : UMKA_TYPE_NONE;
}


UMKA_API const char *umkaGetTypeName(const UmkaType *type)
{
    return type && type->typeIdent ? type->typeIdent->name : NULL;
}


UMKA_API int umkaGetTypeSize(const UmkaType *type)
{
    return type ? type->size : 0;
}


UMKA_API int umkaGetTypeSpelling(const UmkaType *type, char *buf, int size)
{
    if (!type)
    {
        if (buf && size > 0)
            buf[0] = 0;
        return 0;
    }

    char typeBuf[DEFAULT_STR_LEN + 1];
    typeSpelling(type, typeBuf);

    if (buf && size > 0)
        snprintf(buf, size, "%s", typeBuf);

    return (int)strlen(typeBuf);
}


UMKA_API int umkaGetFieldCount(const UmkaType *type)
{
    return apiTypeHasFields(type) ? type->numItems : 0;
}


UMKA_API bool umkaGetField(const UmkaType *type, int index, const char **name, const UmkaType **fieldType, int *offset)
{
    if (!apiTypeHasFields(type) || index < 0 || index >= type->numItems)
        return false;

    const Field *field = type->field[index];
    if (!field)
        return false;

    if (name)
        *name = field->name;
    if (fieldType)
        *fieldType = field->type;
    if (offset)
        *offset = field->offset;

    return true;
}


UMKA_API int umkaGetFuncParamCount(const UmkaType *type)
{
    return apiGetFuncParamCount(apiGetFuncSignature(type));
}


UMKA_API const char *umkaGetFuncParamName(const UmkaType *type, int index)
{
    const Signature *sig = apiGetFuncSignature(type);
    if (index < 0 || index >= apiGetFuncParamCount(sig))
        return NULL;

    const Param *param = sig->param[index + apiGetFuncParamStart(sig)];
    return param ? param->name : NULL;
}


UMKA_API const UmkaType *umkaGetFuncParamType(const UmkaType *type, int index)
{
    const Signature *sig = apiGetFuncSignature(type);
    if (index < 0 || index >= apiGetFuncParamCount(sig))
        return NULL;

    const Param *param = sig->param[index + apiGetFuncParamStart(sig)];
    return param ? param->type : NULL;
}


UMKA_API const UmkaType *umkaGetFuncResultType(const UmkaType *type)
{
    const Signature *sig = apiGetFuncSignature(type);
    return sig ? sig->resultType : NULL;
}


UMKA_API bool umkaTypesEquivalent(const UmkaType *left, const UmkaType *right)
{
    if (!left || !right)
        return left == right;

    return typeEquivalent(left, right);
}


UMKA_API int umkaGetTypeItemCount(const UmkaType *type)
{
    return type ? type->numItems : 0;
}


UMKA_API bool umkaTypeHasReferences(const UmkaType *type)
{
    return type && type->isGarbageCollected;
}


UMKA_API bool umkaTypeUsesIndirectValueSlot(const UmkaType *type)
{
    return apiTypeUsesIndirectValueSlot(type);
}


UMKA_API int umkaGetEnumMemberCount(const UmkaType *type)
{
    return type && typeEnum(type) ? type->numItems : 0;
}


UMKA_API bool umkaGetEnumMember(const UmkaType *type, int index, const char **name, int64_t *signedValue, uint64_t *unsignedValue)
{
    if (name)
        *name = NULL;
    if (signedValue)
        *signedValue = 0;
    if (unsignedValue)
        *unsignedValue = 0;

    if (!type || !typeEnum(type) || index < 0 || index >= type->numItems)
        return false;

    const EnumConst *member = type->enumConst[index];
    if (!member)
        return false;

    if (name)
        *name = member->name;
    if (signedValue)
        *signedValue = member->val.intVal;
    if (unsignedValue)
        *unsignedValue = member->val.uintVal;

    return true;
}


UMKA_API int umkaGetFuncDefaultParamCount(const UmkaType *type)
{
    const Signature *sig = apiGetFuncSignature(type);
    if (!sig)
        return 0;

    const int paramCount = apiGetFuncParamCount(sig);
    return sig->numDefaultParams < paramCount ? sig->numDefaultParams : paramCount;
}


static bool apiSetDefaultParamValue(Umka *umka, UmkaStackSlot *slot, const Type *type, Const value)
{
    if (!umka || !slot || !type)
        return false;

    switch (type->kind)
    {
        case TYPE_INT8:
        case TYPE_INT16:
        case TYPE_INT32:
        case TYPE_INT:
        case TYPE_BOOL:
        case TYPE_CHAR:
            slot->intVal = value.intVal;
            return true;

        case TYPE_UINT8:
        case TYPE_UINT16:
        case TYPE_UINT32:
        case TYPE_UINT:
            slot->uintVal = value.uintVal;
            return true;

        case TYPE_REAL32:
            slot->real32Val = (float)value.realVal;
            return true;

        case TYPE_REAL:
            slot->realVal = value.realVal;
            return true;

        case TYPE_PTR:
            slot->ptrVal = value.ptrVal;
            return true;

        case TYPE_STR:
            slot->ptrVal = value.ptrVal ? umkaMakeStr(umka, (const char *)value.ptrVal) : NULL;
            return !value.ptrVal || slot->ptrVal != NULL;

        default:
            return false;
    }
}


UMKA_API bool umkaSetDefaultParam(Umka *umka, const UmkaType *type, UmkaFuncContext *fn, int index)
{
    if (!umka || !type || !fn || !fn->params)
        return false;

    const Signature *sig = apiGetFuncSignature(type);
    if (!sig)
        return false;

    const int paramCount = apiGetFuncParamCount(sig);
    const int defaultCount = umkaGetFuncDefaultParamCount(type);
    const int defaultStart = paramCount - defaultCount;

    if (index < defaultStart || index >= paramCount)
        return false;

    const int internalIndex = index + apiGetFuncParamStart(sig);
    const Param *param = sig->param[internalIndex];
    if (!param || !param->type)
        return false;

    const UmkaType *contextParamType = umkaGetParamType(fn->params, index);
    if (!contextParamType || !typeEquivalent(contextParamType, param->type))
        return false;

    UmkaStackSlot *slot = umkaGetParam(fn->params, index);
    return apiSetDefaultParamValue(umka, slot, param->type, param->defaultVal);
}


UMKA_API bool umkaSetDefaultParams(Umka *umka, const UmkaType *type, UmkaFuncContext *fn, int providedCount)
{
    if (!umka || !type || !fn || !fn->params)
        return false;

    const Signature *sig = apiGetFuncSignature(type);
    if (!sig)
        return false;

    const int paramCount = apiGetFuncParamCount(sig);
    const int defaultCount = umkaGetFuncDefaultParamCount(type);
    const int requiredCount = paramCount - defaultCount;

    if (providedCount < requiredCount || providedCount > paramCount)
        return false;

    for (int i = providedCount; i < paramCount; i++)
        if (!umkaSetDefaultParam(umka, type, fn, i))
            return false;

    return true;
}


UMKA_API const UmkaType *umkaGetCallableFuncType(const UmkaType *type)
{
    return apiGetCallableFuncType(type);
}


UMKA_API bool umkaTypeIsVariadicParamList(const UmkaType *type)
{
    return type && type->isVariadicParamList;
}


UMKA_API bool umkaCallableValid(const UmkaType *type, UmkaStackSlot value)
{
    const Slot *valuePtr = (const Slot *)&value;
    return apiGetCallableInfo(type, *valuePtr, NULL, NULL, NULL);
}


UMKA_API bool umkaMakeCallableContext(Umka *umka, const UmkaType *type, UmkaStackSlot value, UmkaFuncContext *fn)
{
    if (!umka || !fn)
        return false;

    const Slot *valuePtr = (const Slot *)&value;
    const Type *fnType = NULL;
    int64_t entryOffset = 0;
    const Interface *upvalue = NULL;

    if (!apiGetCallableInfo(type, *valuePtr, &fnType, &entryOffset, &upvalue))
        return false;

    compilerMakeFuncContext(umka, fnType, entryOffset, fn);

    UmkaAny *destUpvalue = umkaGetUpvalue(fn->params);
    if (destUpvalue)
        *destUpvalue = upvalue ? *(const UmkaAny *)upvalue : (UmkaAny){0};

    return true;
}


UMKA_API int umkaCallCallable(Umka *umka, const UmkaType *type, UmkaStackSlot value, UmkaFuncContext *fn)
{
    if (!umka || !fn)
        return UMKA_ERR_RUNTIME;

    const Slot *valuePtr = (const Slot *)&value;
    const Type *fnType = NULL;
    int64_t entryOffset = 0;
    const Interface *upvalue = NULL;

    if (!apiGetCallableInfo(type, *valuePtr, &fnType, &entryOffset, &upvalue) || fn->entryOffset != entryOffset || !fn->params)
        return UMKA_ERR_RUNTIME;

    UmkaAny *destUpvalue = umkaGetUpvalue(fn->params);
    if (!destUpvalue)
        return UMKA_ERR_RUNTIME;

    *destUpvalue = upvalue ? *(const UmkaAny *)upvalue : (UmkaAny){0};

    if (!vmAlive(&umka->vm) || (typeStructured(fnType->sig->resultType) && (!fn->result || !fn->result->ptrVal)))
        return umkaCall(umka, fn);

    if (upvalue && upvalue->self)
        vmIncRef(&umka->vm, destUpvalue, fnType->sig->param[0]->type);

    return umkaCall(umka, fn);
}


UMKA_API bool umkaGetAnySelf(const UmkaAny *value, const UmkaType **selfType, void **self)
{
    return vmGetAnySelf(value, (const Type **)selfType, self);
}


UMKA_API bool umkaGetAnyValue(const UmkaAny *value, const UmkaType **type, UmkaStackSlot *slot)
{
    Slot result = {0};
    bool ok = vmGetAnyValue(value, (const Type **)type, &result);
    if (slot)
        *slot = result.apiSlot;
    return ok;
}


UMKA_API bool umkaAddClosure(Umka *umka, const char *name, UmkaExternFunc func, void *upvalue)
{
    return compilerAddClosure(umka, name, func, upvalue);
}
