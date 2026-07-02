#ifndef UMKA_API_H_INCLUDED
#define UMKA_API_H_INCLUDED


#ifdef _WIN32
    #if defined(UMKA_STATIC)
        #define UMKA_EXPORT
        #define UMKA_IMPORT
    #else
        #define UMKA_EXPORT __declspec(dllexport)
        #define UMKA_IMPORT __declspec(dllimport)
    #endif
#else
    #define UMKA_EXPORT __attribute__((visibility("default")))
    #define UMKA_IMPORT __attribute__((visibility("default")))
#endif

#ifdef UMKA_BUILD
    #define UMKA_API UMKA_EXPORT
#else
    #define UMKA_API UMKA_IMPORT
#endif


#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>


#if defined(__cplusplus)
extern "C" {
#endif


typedef struct tagUmka Umka;


typedef union
{
    int64_t intVal;
    uint64_t uintVal;
    void *ptrVal;
    double realVal;
    float real32Val;   // Not used in result slots
} UmkaStackSlot;


typedef struct
{
    int64_t entryOffset;
    UmkaStackSlot *params;
    UmkaStackSlot *result;
} UmkaFuncContext;


typedef void (*UmkaExternFunc)(UmkaStackSlot *params, UmkaStackSlot *result);


typedef enum
{
    UMKA_HOOK_CALL,
    UMKA_HOOK_RETURN,

    UMKA_NUM_HOOKS
} UmkaHookEvent;


typedef void (*UmkaHookFunc)(const char *fileName, const char *funcName, int line);


typedef struct tagType UmkaType;


typedef enum
{
    UMKA_TYPE_NONE,
    UMKA_TYPE_FORWARD,
    UMKA_TYPE_VOID,
    UMKA_TYPE_NULL,
    UMKA_TYPE_INT8,
    UMKA_TYPE_INT16,
    UMKA_TYPE_INT32,
    UMKA_TYPE_INT,
    UMKA_TYPE_UINT8,
    UMKA_TYPE_UINT16,
    UMKA_TYPE_UINT32,
    UMKA_TYPE_UINT,
    UMKA_TYPE_BOOL,
    UMKA_TYPE_CHAR,
    UMKA_TYPE_REAL32,
    UMKA_TYPE_REAL,
    UMKA_TYPE_PTR,
    UMKA_TYPE_WEAKPTR,
    UMKA_TYPE_ARRAY,
    UMKA_TYPE_DYNARRAY,
    UMKA_TYPE_STR,
    UMKA_TYPE_MAP,
    UMKA_TYPE_STRUCT,
    UMKA_TYPE_INTERFACE,
    UMKA_TYPE_CLOSURE,
    UMKA_TYPE_FIBER,
    UMKA_TYPE_FN
} UmkaTypeKind;


#define UmkaDynArray(T) struct \
{ \
    const UmkaType *type; \
    int64_t itemSize; \
    T *data; \
}


typedef struct
{
    const UmkaType *type;
    struct tagMapNode *root;
} UmkaMap;


typedef struct
{
    // Different field names are allowed for backward compatibility
    union
    {
        void *data;
        void *self;
    };
    union
    {
        const UmkaType *type;
        const UmkaType *selfType;        
    };
} UmkaAny;


typedef struct
{
    int64_t entryOffset;
    UmkaAny upvalue;
} UmkaClosure;


typedef enum
{
    UMKA_HOST_HANDLE_EMPTY,
    UMKA_HOST_HANDLE_VALUE,
    UMKA_HOST_HANDLE_DATA
} UmkaHostHandleKind;


typedef struct
{
    void *runtime;
    const UmkaType *type;
    UmkaStackSlot value;
    void *storage;
    int64_t storageSize;
    UmkaHostHandleKind kind;
} UmkaHostHandle;


typedef struct
{
    const char *fileName;
    const char *fnName;
    int line, pos, code;
    const char *msg;
} UmkaError;


enum
{
    UMKA_ERR_RUNTIME     = -1,
    UMKA_ERR_INTERRUPTED = -2
};


typedef void (*UmkaWarningCallback)(UmkaError *warning);


typedef Umka *(*UmkaAlloc)                      (void);
typedef bool (*UmkaInit)                        (Umka *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback);
typedef bool (*UmkaCompile)                     (Umka *umka);
typedef int  (*UmkaRun)                         (Umka *umka);
typedef int  (*UmkaCall)                        (Umka *umka, UmkaFuncContext *fn);
typedef void (*UmkaRequestInterrupt)            (Umka *umka, const char *message);
typedef void (*UmkaClearInterrupt)              (Umka *umka);
typedef bool (*UmkaInterruptRequested)          (Umka *umka);
typedef void (*UmkaFree)                        (Umka *umka);
typedef UmkaError *(*UmkaGetError)              (Umka *umka);
typedef bool (*UmkaAlive)                       (Umka *umka);
typedef char *(*UmkaAsm)                        (Umka *umka);
typedef bool (*UmkaAddModule)                   (Umka *umka, const char *fileName, const char *sourceString);
typedef bool (*UmkaAddFunc)                     (Umka *umka, const char *name, UmkaExternFunc func);
typedef bool (*UmkaGetFunc)                     (Umka *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn);
typedef bool (*UmkaGetCallStack)                (Umka *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line);
typedef void (*UmkaSetHook)                     (Umka *umka, UmkaHookEvent event, UmkaHookFunc hook);
typedef void *(*UmkaAllocData)                  (Umka *umka, int size, UmkaExternFunc onFree);
typedef void (*UmkaIncRef)                      (Umka *umka, void *ptr);
typedef void (*UmkaDecRef)                      (Umka *umka, void *ptr);
typedef void *(*UmkaGetMapItem)                 (Umka *umka, UmkaMap *map, UmkaStackSlot key);
typedef bool (*UmkaMakeMap)                     (Umka *umka, UmkaMap *map, const UmkaType *type);
typedef bool (*UmkaSetMapItem)                  (Umka *umka, UmkaMap *map, UmkaStackSlot key, UmkaStackSlot item);
typedef char *(*UmkaMakeStr)                    (Umka *umka, const char *str);
typedef int  (*UmkaGetStrLen)                   (const char *str);
typedef void (*UmkaMakeDynArray)                (Umka *umka, void *array, const UmkaType *type, int len);
typedef int  (*UmkaGetDynArrayLen)              (const void *array);
typedef const char *(*UmkaGetVersion)           (void);
typedef int64_t (*UmkaGetMemUsage)              (Umka *umka);
typedef void (*UmkaMakeFuncContext)             (Umka *umka, const UmkaType *closureType, int entryOffset, UmkaFuncContext *fn);
typedef UmkaStackSlot *(*UmkaGetParam)          (UmkaStackSlot *params, int index);
typedef UmkaAny *(*UmkaGetUpvalue)              (UmkaStackSlot *params);
typedef UmkaStackSlot *(*UmkaGetResult)         (UmkaStackSlot *params, UmkaStackSlot *result);
typedef void *(*UmkaGetMetadata)                (Umka *umka);
typedef void (*UmkaSetMetadata)                 (Umka *umka, void *metadata);
typedef void *(*UmkaMakeStruct)                 (Umka *umka, const UmkaType *type);
typedef const UmkaType *(*UmkaGetBaseType)      (const UmkaType *type);
typedef const UmkaType *(*UmkaGetParamType)     (UmkaStackSlot *params, int index);
typedef const UmkaType *(*UmkaGetResultType)    (UmkaStackSlot *params, UmkaStackSlot *result);
typedef const UmkaType *(*UmkaGetFieldType)     (const UmkaType *structType, const char *fieldName);
typedef const UmkaType *(*UmkaGetMapKeyType)    (const UmkaType *mapType);
typedef const UmkaType *(*UmkaGetMapItemType)   (const UmkaType *mapType);
typedef UmkaTypeKind (*UmkaGetTypeKind)         (const UmkaType *type);
typedef const char *(*UmkaGetTypeName)          (const UmkaType *type);
typedef int (*UmkaGetTypeSize)                  (const UmkaType *type);
typedef int (*UmkaGetTypeSpelling)              (const UmkaType *type, char *buf, int size);
typedef int (*UmkaGetFieldCount)                (const UmkaType *type);
typedef bool (*UmkaGetField)                    (const UmkaType *type, int index, const char **name, const UmkaType **fieldType, int *offset);
typedef int (*UmkaGetFuncParamCount)            (const UmkaType *type);
typedef const char *(*UmkaGetFuncParamName)     (const UmkaType *type, int index);
typedef const UmkaType *(*UmkaGetFuncParamType) (const UmkaType *type, int index);
typedef const UmkaType *(*UmkaGetFuncResultType)(const UmkaType *type);
typedef bool (*UmkaTypesEquivalent)             (const UmkaType *left, const UmkaType *right);
typedef int (*UmkaGetTypeItemCount)             (const UmkaType *type);
typedef bool (*UmkaTypeHasReferences)           (const UmkaType *type);
typedef bool (*UmkaTypeUsesIndirectValueSlot)   (const UmkaType *type);
typedef int (*UmkaGetEnumMemberCount)           (const UmkaType *type);
typedef bool (*UmkaGetEnumMember)               (const UmkaType *type, int index, const char **name, int64_t *signedValue, uint64_t *unsignedValue);
typedef int (*UmkaGetFuncDefaultParamCount)     (const UmkaType *type);
typedef bool (*UmkaSetDefaultParam)             (Umka *umka, const UmkaType *type, UmkaFuncContext *fn, int index);
typedef bool (*UmkaSetDefaultParams)            (Umka *umka, const UmkaType *type, UmkaFuncContext *fn, int providedCount);
typedef const UmkaType *(*UmkaGetCallableFuncType)(const UmkaType *type);
typedef bool (*UmkaTypeIsVariadicParamList)     (const UmkaType *type);
typedef bool (*UmkaGetAnySelf)                  (const UmkaAny *value, const UmkaType **selfType, void **self);
typedef bool (*UmkaGetAnyValue)                 (const UmkaAny *value, const UmkaType **type, UmkaStackSlot *slot);
typedef bool (*UmkaAddClosure)                  (Umka *umka, const char *name, UmkaExternFunc func, void *upvalue);
typedef bool (*UmkaAssignHostValue)             (Umka *umka, void *dest, const UmkaType *type, UmkaStackSlot value);
typedef bool (*UmkaReleaseHostValue)            (Umka *umka, void *dest, const UmkaType *type);
typedef bool (*UmkaMakeAny)                     (Umka *umka, UmkaAny *dest, const UmkaType *type, UmkaStackSlot value);
typedef bool (*UmkaMakeInterface)               (Umka *umka, void *dest, const UmkaType *interfaceType, const UmkaType *type, UmkaStackSlot value);
typedef bool (*UmkaCallableValid)               (const UmkaType *type, UmkaStackSlot value);
typedef bool (*UmkaMakeCallableContext)         (Umka *umka, const UmkaType *type, UmkaStackSlot value, UmkaFuncContext *fn);
typedef int  (*UmkaCallCallable)                (Umka *umka, const UmkaType *type, UmkaStackSlot value, UmkaFuncContext *fn);
typedef void (*UmkaMakeHostHandle)              (UmkaHostHandle *handle);
typedef bool (*UmkaRetainHostValue)             (Umka *umka, UmkaHostHandle *handle, const UmkaType *type, UmkaStackSlot value);
typedef bool (*UmkaRetainHostData)              (Umka *umka, UmkaHostHandle *handle, void *ptr);
typedef void (*UmkaClearHostHandle)             (UmkaHostHandle *handle);
typedef void (*UmkaReleaseHostHandle)           (UmkaHostHandle *handle);
typedef bool (*UmkaHostHandleValid)             (const UmkaHostHandle *handle);
typedef const UmkaType *(*UmkaGetHostHandleType)(const UmkaHostHandle *handle);
typedef UmkaStackSlot (*UmkaGetHostHandleValue) (const UmkaHostHandle *handle);


typedef struct
{
    UmkaAlloc           umkaAlloc;
    UmkaInit            umkaInit;
    UmkaCompile         umkaCompile;
    UmkaRun             umkaRun;
    UmkaCall            umkaCall;
    UmkaFree            umkaFree;
    UmkaGetError        umkaGetError;
    UmkaAlive           umkaAlive;
    UmkaAsm             umkaAsm;
    UmkaAddModule       umkaAddModule;
    UmkaAddFunc         umkaAddFunc;
    UmkaGetFunc         umkaGetFunc;
    UmkaGetCallStack    umkaGetCallStack;
    UmkaSetHook         umkaSetHook;
    UmkaAllocData       umkaAllocData;
    UmkaIncRef          umkaIncRef;
    UmkaDecRef          umkaDecRef;
    UmkaGetMapItem      umkaGetMapItem;
    UmkaMakeStr         umkaMakeStr;
    UmkaGetStrLen       umkaGetStrLen;
    UmkaMakeDynArray    umkaMakeDynArray;
    UmkaGetDynArrayLen  umkaGetDynArrayLen;
    UmkaGetVersion      umkaGetVersion;
    UmkaGetMemUsage     umkaGetMemUsage;
    UmkaMakeFuncContext umkaMakeFuncContext;
    UmkaGetParam        umkaGetParam;
    UmkaGetUpvalue      umkaGetUpvalue;
    UmkaGetResult       umkaGetResult;
    UmkaGetMetadata     umkaGetMetadata;
    UmkaSetMetadata     umkaSetMetadata;
    UmkaMakeStruct      umkaMakeStruct;
    UmkaGetBaseType     umkaGetBaseType;
    UmkaGetParamType    umkaGetParamType;
    UmkaGetResultType   umkaGetResultType; 
    UmkaGetFieldType    umkaGetFieldType;
    UmkaGetMapKeyType   umkaGetMapKeyType;
    UmkaGetMapItemType  umkaGetMapItemType;
    UmkaAddClosure      umkaAddClosure;
    UmkaMakeMap         umkaMakeMap;
    UmkaSetMapItem      umkaSetMapItem;
    UmkaMakeHostHandle  umkaMakeHostHandle;
    UmkaRetainHostValue umkaRetainHostValue;
    UmkaRetainHostData  umkaRetainHostData;
    UmkaClearHostHandle umkaClearHostHandle;
    UmkaReleaseHostHandle umkaReleaseHostHandle;
    UmkaHostHandleValid umkaHostHandleValid;
    UmkaGetHostHandleType umkaGetHostHandleType;
    UmkaGetHostHandleValue umkaGetHostHandleValue;
    UmkaRequestInterrupt umkaRequestInterrupt;
    UmkaClearInterrupt umkaClearInterrupt;
    UmkaInterruptRequested umkaInterruptRequested;
    UmkaGetTypeKind    umkaGetTypeKind;
    UmkaGetTypeName    umkaGetTypeName;
    UmkaGetTypeSize    umkaGetTypeSize;
    UmkaGetTypeSpelling umkaGetTypeSpelling;
    UmkaGetFieldCount  umkaGetFieldCount;
    UmkaGetField       umkaGetField;
    UmkaGetFuncParamCount umkaGetFuncParamCount;
    UmkaGetFuncParamName umkaGetFuncParamName;
    UmkaGetFuncParamType umkaGetFuncParamType;
    UmkaGetFuncResultType umkaGetFuncResultType;
    UmkaGetAnySelf     umkaGetAnySelf;
    UmkaGetAnyValue    umkaGetAnyValue;
    UmkaAssignHostValue umkaAssignHostValue;
    UmkaReleaseHostValue umkaReleaseHostValue;
    UmkaMakeAny        umkaMakeAny;
    UmkaMakeInterface  umkaMakeInterface;
    UmkaCallableValid  umkaCallableValid;
    UmkaMakeCallableContext umkaMakeCallableContext;
    UmkaCallCallable   umkaCallCallable;
    UmkaTypesEquivalent umkaTypesEquivalent;
    UmkaGetTypeItemCount umkaGetTypeItemCount;
    UmkaTypeHasReferences umkaTypeHasReferences;
    UmkaTypeUsesIndirectValueSlot umkaTypeUsesIndirectValueSlot;
    UmkaGetEnumMemberCount umkaGetEnumMemberCount;
    UmkaGetEnumMember umkaGetEnumMember;
    UmkaGetFuncDefaultParamCount umkaGetFuncDefaultParamCount;
    UmkaGetCallableFuncType umkaGetCallableFuncType;
    UmkaTypeIsVariadicParamList umkaTypeIsVariadicParamList;
    UmkaSetDefaultParam umkaSetDefaultParam;
    UmkaSetDefaultParams umkaSetDefaultParams;
} UmkaAPI;


UMKA_API Umka *umkaAlloc                    (void);
UMKA_API bool umkaInit                      (Umka *umka, const char *fileName, const char *sourceString, int stackSize, void *reserved, int argc, char **argv, bool fileSystemEnabled, bool implLibsEnabled, UmkaWarningCallback warningCallback);
UMKA_API bool umkaCompile                   (Umka *umka);
UMKA_API int  umkaRun                       (Umka *umka);
UMKA_API int  umkaCall                      (Umka *umka, UmkaFuncContext *fn);
UMKA_API void umkaRequestInterrupt          (Umka *umka, const char *message);
UMKA_API void umkaClearInterrupt            (Umka *umka);
UMKA_API bool umkaInterruptRequested        (Umka *umka);
UMKA_API void umkaFree                      (Umka *umka);
UMKA_API UmkaError *umkaGetError            (Umka *umka);
UMKA_API bool umkaAlive                     (Umka *umka);
UMKA_API char *umkaAsm                      (Umka *umka);
UMKA_API bool umkaAddModule                 (Umka *umka, const char *fileName, const char *sourceString);
UMKA_API bool umkaAddFunc                   (Umka *umka, const char *name, UmkaExternFunc func);
UMKA_API bool umkaGetFunc                   (Umka *umka, const char *moduleName, const char *fnName, UmkaFuncContext *fn);
UMKA_API bool umkaGetCallStack              (Umka *umka, int depth, int nameSize, int *offset, char *fileName, char *fnName, int *line);
UMKA_API void umkaSetHook                   (Umka *umka, UmkaHookEvent event, UmkaHookFunc hook);
UMKA_API void *umkaAllocData                (Umka *umka, int size, UmkaExternFunc onFree);
UMKA_API void umkaIncRef                    (Umka *umka, void *ptr);
UMKA_API void umkaDecRef                    (Umka *umka, void *ptr);
UMKA_API void *umkaGetMapItem               (Umka *umka, UmkaMap *map, UmkaStackSlot key);
UMKA_API bool umkaMakeMap                   (Umka *umka, UmkaMap *map, const UmkaType *type);
UMKA_API bool umkaSetMapItem                (Umka *umka, UmkaMap *map, UmkaStackSlot key, UmkaStackSlot item);
UMKA_API char *umkaMakeStr                  (Umka *umka, const char *str);
UMKA_API int  umkaGetStrLen                 (const char *str);
UMKA_API void umkaMakeDynArray              (Umka *umka, void *array, const UmkaType *type, int len);
UMKA_API int  umkaGetDynArrayLen            (const void *array);
UMKA_API const char *umkaGetVersion         (void);
UMKA_API int64_t umkaGetMemUsage            (Umka *umka);
UMKA_API void umkaMakeFuncContext           (Umka *umka, const UmkaType *closureType, int entryOffset, UmkaFuncContext *fn);
UMKA_API UmkaStackSlot *umkaGetParam        (UmkaStackSlot *params, int index);
UMKA_API UmkaAny *umkaGetUpvalue            (UmkaStackSlot *params);
UMKA_API UmkaStackSlot *umkaGetResult       (UmkaStackSlot *params, UmkaStackSlot *result);
UMKA_API void *umkaGetMetadata              (Umka *umka);
UMKA_API void umkaSetMetadata               (Umka *umka, void *metadata);
UMKA_API void *umkaMakeStruct               (Umka *umka, const UmkaType *type);
UMKA_API const UmkaType *umkaGetBaseType    (const UmkaType *type);
UMKA_API const UmkaType *umkaGetParamType   (UmkaStackSlot *params, int index);
UMKA_API const UmkaType *umkaGetResultType  (UmkaStackSlot *params, UmkaStackSlot *result);
UMKA_API const UmkaType *umkaGetFieldType   (const UmkaType *structType, const char *fieldName);
UMKA_API const UmkaType *umkaGetMapKeyType  (const UmkaType *mapType);
UMKA_API const UmkaType *umkaGetMapItemType (const UmkaType *mapType);
UMKA_API UmkaTypeKind umkaGetTypeKind       (const UmkaType *type);
UMKA_API const char *umkaGetTypeName        (const UmkaType *type);
UMKA_API int  umkaGetTypeSize               (const UmkaType *type);
UMKA_API int  umkaGetTypeSpelling           (const UmkaType *type, char *buf, int size);
UMKA_API int  umkaGetFieldCount             (const UmkaType *type);
UMKA_API bool umkaGetField                  (const UmkaType *type, int index, const char **name, const UmkaType **fieldType, int *offset);
UMKA_API int  umkaGetFuncParamCount         (const UmkaType *type);
UMKA_API const char *umkaGetFuncParamName   (const UmkaType *type, int index);
UMKA_API const UmkaType *umkaGetFuncParamType(const UmkaType *type, int index);
UMKA_API const UmkaType *umkaGetFuncResultType(const UmkaType *type);
UMKA_API bool umkaTypesEquivalent           (const UmkaType *left, const UmkaType *right);
UMKA_API int  umkaGetTypeItemCount          (const UmkaType *type);
UMKA_API bool umkaTypeHasReferences         (const UmkaType *type);
UMKA_API bool umkaTypeUsesIndirectValueSlot (const UmkaType *type);
UMKA_API int  umkaGetEnumMemberCount        (const UmkaType *type);
UMKA_API bool umkaGetEnumMember             (const UmkaType *type, int index, const char **name, int64_t *signedValue, uint64_t *unsignedValue);
UMKA_API int  umkaGetFuncDefaultParamCount  (const UmkaType *type);
UMKA_API bool umkaSetDefaultParam           (Umka *umka, const UmkaType *type, UmkaFuncContext *fn, int index);
UMKA_API bool umkaSetDefaultParams          (Umka *umka, const UmkaType *type, UmkaFuncContext *fn, int providedCount);
UMKA_API const UmkaType *umkaGetCallableFuncType(const UmkaType *type);
UMKA_API bool umkaTypeIsVariadicParamList   (const UmkaType *type);
UMKA_API bool umkaGetAnySelf                (const UmkaAny *value, const UmkaType **selfType, void **self);
UMKA_API bool umkaGetAnyValue               (const UmkaAny *value, const UmkaType **type, UmkaStackSlot *slot);
UMKA_API bool umkaAddClosure                (Umka *umka, const char *name, UmkaExternFunc func, void *upvalue);
UMKA_API bool umkaAssignHostValue           (Umka *umka, void *dest, const UmkaType *type, UmkaStackSlot value);
UMKA_API bool umkaReleaseHostValue          (Umka *umka, void *dest, const UmkaType *type);
UMKA_API bool umkaMakeAny                   (Umka *umka, UmkaAny *dest, const UmkaType *type, UmkaStackSlot value);
UMKA_API bool umkaMakeInterface             (Umka *umka, void *dest, const UmkaType *interfaceType, const UmkaType *type, UmkaStackSlot value);
UMKA_API bool umkaCallableValid             (const UmkaType *type, UmkaStackSlot value);
UMKA_API bool umkaMakeCallableContext       (Umka *umka, const UmkaType *type, UmkaStackSlot value, UmkaFuncContext *fn);
UMKA_API int  umkaCallCallable              (Umka *umka, const UmkaType *type, UmkaStackSlot value, UmkaFuncContext *fn);
UMKA_API void umkaMakeHostHandle            (UmkaHostHandle *handle);
UMKA_API bool umkaRetainHostValue           (Umka *umka, UmkaHostHandle *handle, const UmkaType *type, UmkaStackSlot value);
UMKA_API bool umkaRetainHostData            (Umka *umka, UmkaHostHandle *handle, void *ptr);
UMKA_API void umkaClearHostHandle           (UmkaHostHandle *handle);
UMKA_API void umkaReleaseHostHandle         (UmkaHostHandle *handle);
UMKA_API bool umkaHostHandleValid           (const UmkaHostHandle *handle);
UMKA_API const UmkaType *umkaGetHostHandleType(const UmkaHostHandle *handle);
UMKA_API UmkaStackSlot umkaGetHostHandleValue(const UmkaHostHandle *handle);


static inline UmkaAPI *umkaGetAPI(Umka *umka)
{
    return (UmkaAPI *)umka;
}


static inline Umka *umkaGetInstance(UmkaStackSlot *result)
{
    return (Umka *)result->ptrVal;
}


#if defined(__cplusplus)
}
#endif

#endif // UMKA_API_H_INCLUDED
