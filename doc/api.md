# The Umka Embedding API Reference

The Umka interpreter is a static or shared library that provides the API for embedding into a C/C++ host application.

## Initialization and running

### Types

```
typedef struct tagUmka Umka;
```
Umka interpreter instance handle.

### Functions

```
UMKA_API Umka *umkaAlloc(void);
```
Allocates memory for the interpreter.

Returned value: Interpreter instance handle.

```
UMKA_API bool umkaInit(Umka *umka, const char *fileName, const char *sourceString, 
                       int stackSize, void *reserved, 
                       int argc, char **argv, 
                       bool fileSystemEnabled, bool implLibsEnabled,
                       UmkaWarningCallback warningCallback);
```
Initializes the interpreter instance.

Parameters:

* `umka`: Interpreter instance handle
* `fileName`: Umka source file name
* `sourceString`: Optional string buffer that contains the program source. If not `NULL`, the program source is read from this string rather than from a file. Even in this case, some fake `fileName` should be specified
* `stackSize`: Umka stack size, in slots
* `argc`/`argv`: C/C++ command-line parameter data
* `fileSystemEnabled`: Allows the Umka program to access the file system
* `implLibsEnabled`: Allows the Umka program to use UMIs
* `warningCallback`: Warning callback. If not `NULL`, it will be called on every warning

Returned value: `true` if the source has been successfully loaded.

```
UMKA_API bool umkaAddModule(Umka *umka, const char *fileName, const char *sourceString);
```
Adds an Umka module contained in a string buffer.

Parameters:

* `umka`: Interpreter instance handle
* `sourceString`: String buffer
* `fileName`: A fake file name assigned to the module

Returned value: `true` if the module has been successfully added.

```
UMKA_API bool umkaCompile(Umka *umka);
```
Compiles the Umka program into bytecode.

Parameters:

* `umka`: Interpreter instance handle

Returned value: `true` if the compilation is successful and no compile-time errors are detected.

```
UMKA_API int umkaRun(Umka *umka);
```
Runs the Umka program previously compiled to bytecode, i.e., calls its `main` function, if it exists. 

Parameters:

* `umka`: Interpreter instance handle

Returned value: 0 if the program execution finishes successfully and no run-time errors were detected, otherwise the error code.

```
UMKA_API void umkaFree(Umka *umka);
```
Deallocates memory allocated for the interpreter and program's global variables.

Parameters:

* `umka`: Interpreter instance handle

```
UMKA_API const char *umkaGetVersion(void);
```
Returns Umka interpreter version.

Returned value: Umka interpreter version (build date) string.

```
UMKA_API void umkaSetMetadata(Umka *umka, void *metadata);
```
Saves an arbitrary user data pointer to the Umka instance. Umka does not use the data in any way.

Parameters:

* `umka`: Interpreter instance handle
* `metadata`: User data pointer

```
UMKA_API void *umkaGetMetadata(Umka *umka);
```
Retrieves the user data pointer previously saved to the Umka instance with `umkaSetMetadata`.

Parameters:

* `umka`: Interpreter instance handle

Returned value: User data pointer

## Calling functions

### Types

```
typedef union
{
    int64_t intVal;
    uint64_t uintVal;
    void *ptrVal;
    double realVal;
    float real32Val;
} UmkaStackSlot;
```
Umka stack slot. Used for passing parameters to functions and returning results from functions. Each parameter or result occupies at least one slot. A parameter of a structured type, when passed by value, occupies the minimal number of consecutive slots sufficient to store it.

```
typedef struct
{
    int64_t entryOffset;
    UmkaStackSlot *params;
    UmkaStackSlot *result;
} UmkaFuncContext;
```
Umka function context used to call an Umka function from C/C++. Can be filled in by `umkaGetFunc` or `umkaMakeFuncContext` and then passed to `umkaCall`.

```
typedef void (*UmkaExternFunc)(UmkaStackSlot *params, UmkaStackSlot *result);
```
External C/C++ function that can be called from Umka.

Parameters:

* `params`: Stack slots that store the function parameters passed from Umka to C/C++. Use `umkaGetParam` to access individual parameters and `umkaGetUpvalue` to access captured variables from the slots
* `result`: Stack slots that can store the value returned from C/C++ to Umka. Use `umkaGetResult` to access the stack slot that can actually store the value

### Functions

```
UMKA_API bool umkaAddFunc(Umka *umka, const char *name, UmkaExternFunc func);
```
Adds a C/C++ function to the list of external functions that can be called from Umka. 

Parameters:

* `umka` Interpreter instance handle
* `name` Function name
* `func` Function pointer

Returned value: `true` if the function has been successfully added.

```
UMKA_API bool umkaAddClosure(Umka *umka, const char *name, UmkaExternFunc func, void *upvalue);
```
Adds a C/C++ function to the list of external functions that can be called from Umka. The `upvalue` parameter is a pointer to any user data that should be available inside `func` as a captured variable and accessible via `umkaGetUpvalue`.

Parameters:

* `umka` Interpreter instance handle
* `name` Function name
* `func` Function pointer
* `upvalue` User data pointer

Returned value: `true` if the function has been successfully added.

```
UMKA_API bool umkaGetFunc(Umka *umka, const char *moduleName, const char *fnName, 
                          UmkaFuncContext *fn);
```
Finds an Umka function that can be called from C/C++ using `umkaCall`.

Parameters:

* `umka`: Interpreter instance handle
* `moduleName`: Module name where the function is defined
* `funcName`: Function name
* `fn`: Function context to be filled in

Returned value: `true` if the function was found and its context filled.

```
UMKA_API void umkaMakeFuncContext(Umka *umka, const UmkaType *closureType, int entryOffset, 
                                  UmkaFuncContext *fn);
```
Fills in the function context required by `umkaCall`, if it could not be filled in by `umkaGetFunc`.

Parameters:

* `umka`: Interpreter instance handle
* `closureType`: Umka function type. Can be obtained by calling `umkaGetParamType`
* `entryOffset`: Function entry point offset
* `fn`: Function context to be filled in

``` 
UMKA_API int umkaCall(Umka *umka, UmkaFuncContext *fn);
```
Calls an Umka function. 

Parameters:

* `umka`: Interpreter instance handle
* `fn`: Function context previously filled in by `umkaGetFunc` or `umkaMakeFuncContext`

Returned value: 0 if the Umka function returns successfully and no run-time errors are detected, otherwise the error code.

```
UMKA_API bool umkaSetDefaultParam(Umka *umka, const UmkaType *type,
                                  UmkaFuncContext *fn, int index);
UMKA_API bool umkaSetDefaultParams(Umka *umka, const UmkaType *type,
                                   UmkaFuncContext *fn, int providedCount);
```
Fills default parameter values into an existing function context before calling `umkaCall` or `umkaCallCallable`.

Parameters:

* `umka`: Interpreter instance handle
* `type`: Function or closure type whose signature contains the defaults
* `fn`: Function context whose source-level parameter slots will be written
* `index`: Zero-based source parameter index to fill from its default value
* `providedCount`: Number of leading source parameters already supplied by the host; trailing omitted defaults are filled

Returned value: `true` if the requested default value or values were written, otherwise `false`.

Notes:

* Hidden closure/upvalue, receiver, and structured-result slots are handled internally. `index` and `providedCount` use source-level parameter positions
* `umkaSetDefaultParam` returns `false` for a required parameter or an out-of-range index
* `umkaSetDefaultParams` returns `false` if `providedCount` is smaller than the required parameter count or larger than the source parameter count
* The context parameter type must match the reflected function parameter type
* Supported default values are signed integers, unsigned integers, `bool`, `char`, `real32`, `real`, pointers, and `str`
* String defaults are copied into Umka-owned string storage
* Defaults for dynamic arrays, maps, interfaces, closures, fibers, `any`, weak pointers, and aggregate values are rejected
* Raw default-value storage remains private; callers should not depend on `Const`, `Param`, `Signature`, or other internal layouts

```
UMKA_API bool umkaCallableValid(const UmkaType *type, UmkaStackSlot value);
```
Checks whether a typed value is an initialized host-callable Umka `fn` or closure value.

Parameters:

* `type`: Function or closure type
* `value`: Function entry offset in `intVal` for `fn`, or pointer to `UmkaClosure` storage in `ptrVal` for a closure

Returned value: `true` if the value can be used with `umkaMakeCallableContext` and `umkaCallCallable`, otherwise `false`. Fibers are not callable through this API.

```
UMKA_API bool umkaMakeCallableContext(Umka *umka, const UmkaType *type, UmkaStackSlot value,
                                      UmkaFuncContext *fn);
```
Fills in a function context for a typed `fn` or closure value.

Parameters:

* `umka`: Interpreter instance handle
* `type`: Function or closure type
* `value`: Function entry offset in `intVal` for `fn`, or pointer to `UmkaClosure` storage in `ptrVal` for a closure
* `fn`: Function context to be filled in

Returned value: `true` if the context has been created, otherwise `false`.

Notes:

* The context borrows the callable value. Retain the callable with `umkaRetainHostValue` if it must outlive the current callback, stack frame, or dynamic value being inspected
* Set source-level arguments with `umkaGetParam(fn.params, index)` before calling `umkaCallCallable`
* For structured return values, allocate result storage and put its pointer in `fn.result->ptrVal`, as with `umkaCall`

```
UMKA_API int umkaCallCallable(Umka *umka, const UmkaType *type, UmkaStackSlot value,
                              UmkaFuncContext *fn);
```
Calls a typed `fn` or closure value using a context created by `umkaMakeCallableContext`.

Parameters:

* `umka`: Interpreter instance handle
* `type`: Function or closure type
* `value`: Function entry offset in `intVal` for `fn`, or pointer to `UmkaClosure` storage in `ptrVal` for a closure
* `fn`: Function context with any explicit parameters already filled in

Returned value: 0 if the callable returns successfully, otherwise the error code.

Notes:

* For closures, `umkaCallCallable` refreshes the hidden upvalue parameter and increments its reference before entering the VM. The called function consumes that parameter reference through normal Umka cleanup
* A runtime error or interrupt during the callable follows the same rules as `umkaCall`: the interpreter is terminated, and retained handles may still be cleared before `umkaFree`
* The callable must belong to the same interpreter and thread-affine execution context. It is not valid after `umkaFree`
* Fibers are deliberately rejected. A fiber is a heap VM context with its own stack and scheduler parent, and `resume` changes the active VM fiber rather than performing an ordinary function call

```
UMKA_API UmkaStackSlot *umkaGetParam(UmkaStackSlot *params, int index);
```
Finds function parameter slot.

Parameters:

* `params`: Parameter stack slots
* `index`: Parameter position. The leftmost parameter is at position 0

Returned value: Pointer to the first stack slot occupied by the parameter, `NULL` if there is no such parameter.

Notes:

* Parameters of all ordinal types except `uint` are stored in the `intVal` field of the parameter slot

* Parameters of type `uint` are stored in the `uintVal` field of the parameter slot

* Parameters of type `real` are stored in the `realVal` field of the parameter slot

* Parameters of type `real32` are stored in the `real32Val` field of the parameter slot

* Parameters of all pointer types are stored in the `ptrVal` field of the parameter slot

* Parameters of type `str` are stored in the `ptrVal` field of the parameter value slot, treated as being of type `const unsigned char *`

* Parameters of all structured types `T` occupy as many slots as needed to store `sizeof(T)` bytes, each slot being 8 bytes. The first occupied slot is returned by `umkaGetParam`. It follows that:

  * If a parameter is of type `T`, it is accessible as `*(T *)umkaGetParam(params, index)`

  * If a parameter is of type `^T`, it is accessible as `(T *)umkaGetParam(params, index)->ptrVal`  

```
UMKA_API UmkaAny *umkaGetUpvalue(UmkaStackSlot *params);
```
Finds variables captured by a closure.

Parameters:

* `params`: Parameter stack slots

Returned value: Pointer to the captured variables stored as `any`.

```
UMKA_API UmkaStackSlot *umkaGetResult(UmkaStackSlot *params, UmkaStackSlot *result);
```
Finds the returned value slot.

Parameters:

* `params`: Parameter stack slots
* `result`: Returned value stack slots

Returned value: Pointer to the stack slot allocated for storing the returned value. 

Notes:

* Returned values of all ordinal types except `uint` are stored in the `intVal` field of the returned value slot

* Returned values of type `uint` are stored in the `uintVal` field of the returned value slot

* Returned values of types `real` and `real32` are stored in the `realVal` field of the returned value slot

* Returned values of all pointer types are stored in the `ptrVal` field of the returned value slot

* Returned values of type `str` are stored in the `ptrVal` field of the returned value slot, treated as being of type `const unsigned char *`

* Returned values of all structured types `T` are assumed to be allocated by the caller. The pointer to allocated memory is stored in the `ptrVal` field of the returned value slot, treated as being of type `T *`. In particular:

  * Inside a C function called from Umka, the returned value is allocated by the Umka interpreter. The C function must put the returned value to allocated memory, but must not overwrite the pointer

  * Before calling an Umka function from C, the C program must allocate memory needed for storing the returned value and put the pointer into the `ptrVal` field of the returned value slot

* Multiple returned values of types `(T0, T1 /*...*/)`are treated as a single structure `struct {item0: T0; item1: T1 /*...*/}` and follow the rules for structured types

```
static inline Umka *umkaGetInstance(UmkaStackSlot *result);
```
Returns the interpreter instance handle. Must not be called after the first call to `umkaGetResult`.

Parameters:

* `result`: Returned value stack slots

Returned value: Interpreter instance handle. 

## Debugging and profiling

### Types

```
typedef struct
{
    char *fileName;
    char *fnName;
    int line, pos, code;
    char *msg;
} UmkaError;
```
Umka error or warning description. Returned by `umkaGetError`, passed to `UmkaWarningCallback`.

```
enum
{
    UMKA_ERR_RUNTIME     = -1,
    UMKA_ERR_INTERRUPTED = -2
};
```
Runtime error codes returned by `umkaRun` and `umkaCall`.

```
typedef void (*UmkaWarningCallback)(UmkaError *warning);
```
Umka warning callback that can be set by `umkaInit`.

Parameters:

* `warning`: Warning description

```
typedef enum
{
    UMKA_HOOK_CALL,
    UMKA_HOOK_RETURN,

    UMKA_NUM_HOOKS
} UmkaHookEvent;
```
Umka debug hook event kind. A `UMKA_HOOK_CALL` hook is called after calling any Umka function, `UMKA_HOOK_RETURN` before returning from any Umka function.

```
typedef void (*UmkaHookFunc)(const char *fileName, const char *funcName, int line);
```
Umka debug hook function. A callback that is called each time a hook event occurs.

Parameters:

* `fileName`: Source file in which the event occurred
* `funcName`: Function in which the event occurred
* `line`: Source file line at which the event occurred

### Functions

```
UMKA_API UmkaError *umkaGetError(Umka *umka);
```
Returns the last compile-time or run-time error.

Parameters:

* `umka`: Interpreter instance handle

Returned value: Pointer to the error description. The pointer is valid until either a new error occurs, or `umkaFree` is called.

```
UMKA_API bool umkaAlive(Umka *umka);
```
Checks if the interpreter instance has been initialized and not yet terminated. Termination means that either a runtime error has happened, or `exit` has been called. Neither `umkaRun`, nor `umkaCall` can be called after the termination.

Parameters:

* `umka`: Interpreter instance handle

Returned value: `true` if the interpreter instance has not yet terminated.

### Interrupting execution

```
UMKA_API void umkaRequestInterrupt(Umka *umka, const char *message);
```
Requests cooperative interruption of the interpreter. The request is interpreter-wide and is observed by the VM before dispatching a bytecode instruction. If an interrupt is observed, `umkaRun` or `umkaCall` returns `UMKA_ERR_INTERRUPTED`, and `umkaGetError(umka)->msg` contains the supplied message, or `Execution interrupted` if `message` is `NULL` or empty.

Parameters:

* `umka`: Interpreter instance handle
* `message`: Optional error message copied into the interpreter

Notes:

* Interrupt requests are sticky until `umkaClearInterrupt` is called
* A request made before `umkaRun` or `umkaCall` is observed by the next execution attempt
* If multiple requests are made before the VM observes the interrupt, the latest message is used
* An observed interrupt uses the normal runtime error path, so `umkaAlive` becomes `false`; clearing the interrupt flag does not revive the interpreter
* A request made after normal or runtime termination can still be queried and cleared while the `Umka` instance exists, but it does not make a terminated interpreter runnable again
* No Umka API call is valid after `umkaFree`
* The interrupt check runs before the next bytecode instruction is dispatched. If that instruction would trigger a call or return hook, the interrupt is reported first
* External C/C++ callbacks and long-running native operations are not preempted; a pending interrupt is observed after control returns to the VM
* Interruption is interpreter-wide, not per-fiber. It stops the currently executing VM run/call through the runtime error path
* The only intended cross-thread host operation is requesting interruption; other API operations remain tied to the thread that owns the interpreter

```
UMKA_API void umkaClearInterrupt(Umka *umka);
```
Clears a pending or previously observed interrupt request. This only clears the flag and message; it does not reset other runtime error state or revive a terminated interpreter.

Parameters:

* `umka`: Interpreter instance handle

```
UMKA_API bool umkaInterruptRequested(Umka *umka);
```
Returns whether an interrupt request is currently pending or has been observed and not yet cleared.

Parameters:

* `umka`: Interpreter instance handle

Returned value: `true` if interruption has been requested and not cleared.

Example:

```
umkaRequestInterrupt(umka, "script cancelled");

int err = umkaCall(umka, &fn);
if (err == UMKA_ERR_INTERRUPTED)
    fprintf(stderr, "%s\n", umkaGetError(umka)->msg);
```

```
UMKA_API bool umkaGetCallStack(Umka *umka, int depth, int nameSize, 
                               int *offset, char *fileName, char *fnName, int *line);
```
Finds the Umka call stack entry.

Parameters:

* `umka`: Interpreter instance handle
* `depth`: Call stack unwinding depth. If zero, the current function information is retrieved
* `nameSize`: Size of the string buffers that will be allocated for `fileName` and `fnName`, including the null characters
* `offset`: Bytecode position, in instructions
* `fileName`: Source file name corresponding to the bytecode position
* `fnName`: Function name corresponding to the bytecode position
* `line`: Source file line corresponding to the bytecode position

Returned value: `true` on success.

```
UMKA_API void umkaSetHook(Umka *umka, UmkaHookEvent event, UmkaHookFunc hook);
```
Sets a debug hook function that will be called by the Umka interpreter each time an event occurs.

Parameters:

* `umka`: Interpreter instance handle
* `event`: Event kind that will trigger the hook function call 
* `hook`: Hook function

```
UMKA_API int64_t umkaGetMemUsage(Umka *umka);
```
Returns the allocated heap memory size.

Parameters:

* `umka`: Interpreter instance handle

Returned value: Memory size in bytes.

```
UMKA_API char *umkaAsm(Umka *umka);
```
Generates the Umka assembly listing for the Umka program previously compiled to bytecode. 

Parameters:

* `umka`: Interpreter instance handle

Returned value: String buffer pointer. It stays valid until `umkaFree` is called.

## Accessing Umka data types

### Types

```
typedef struct tagType UmkaType;
```
Umka data type.

```
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
```
Public Umka type category used by the type reflection API.

```
#define UmkaDynArray(T) struct \
{ \
    const UmkaType *type; \
    int64_t itemSize; \
    T *data; \
}
```
Umka dynamic array containing items of type `T`. Can be initialized by calling `umkaMakeDynArray`.

```
typedef struct
{
    const UmkaType *type;
    struct tagMapNode *root;
} UmkaMap;
```
Umka map. Can be created by calling `umkaMakeMap`, modified by calling `umkaSetMapItem`, and accessed by calling `umkaGetMapItem`.

```
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
```
Umka `any` interface.

```
typedef struct
{
    int64_t entryOffset;
    UmkaAny upvalue;
} UmkaClosure;
```
Umka closure.

```
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
```
Host-owned handle for retaining an Umka heap value across native calls. Initialize it by calling `umkaMakeHostHandle`, retain a value or heap data pointer with `umkaRetainHostValue` or `umkaRetainHostData`, and release it with `umkaClearHostHandle` or `umkaReleaseHostHandle`.

```
typedef struct
{
    const UmkaHostHandle *mapHandle;
    int64_t index;
    const UmkaType *keyType;
    const UmkaType *itemType;
} UmkaHostMapEntry;
```
Read-only descriptor for an entry in a retained map host handle. It identifies an entry by map handle, index, key type, and item type. It does not own map storage and does not expose map node internals.

### Functions

```
UMKA_API const UmkaType *umkaGetParamType(UmkaStackSlot *params, int index);
```
Returns function parameter type.

Parameters:

* `params`: Parameter stack slots
* `index`: Parameter position. The leftmost parameter is at position 0

Returned value: Parameter type, or `NULL` if there is no such parameter or if `umkaGetParamType` is called from an `onFree` callback.

```
UMKA_API const UmkaType *umkaGetResultType(UmkaStackSlot *params, UmkaStackSlot *result);
```
Returns function result type.

Parameters:

* `params`: Parameter stack slots
* `result`: Returned value stack slots

Returned value: Function result type, or `NULL` if `umkaGetResultType` is called from an `onFree` callback.

```
UMKA_API UmkaTypeKind umkaGetTypeKind(const UmkaType *type);
```
Returns the public type category, or `UMKA_TYPE_NONE` for `NULL`.

```
UMKA_API const char *umkaGetTypeName(const UmkaType *type);
```
Returns the declared type name, or `NULL` for unnamed types and `NULL` input.

```
UMKA_API int umkaGetTypeSize(const UmkaType *type);
```
Returns the value storage size in bytes, or 0 for `NULL`.

```
UMKA_API int umkaGetTypeSpelling(const UmkaType *type, char *buf, int size);
```
Writes the source-style type spelling to `buf` and returns the full spelling length, not including the terminating null character. If `buf` is `NULL` or `size` is 0, only the length is returned. If the buffer is too small, the written string is truncated and null-terminated.

```
UMKA_API bool umkaTypesEquivalent(const UmkaType *left, const UmkaType *right);
```
Returns `true` if two type handles are equivalent according to Umka type rules. `NULL` inputs are safe; two `NULL` inputs are treated as equivalent, and one `NULL` input is not equivalent to a non-`NULL` type.

```
UMKA_API int umkaGetTypeItemCount(const UmkaType *type);
```
Returns the internal item count for types that have one. For arrays this is the fixed element count. For structures, interfaces, closures, and enumerations this is the number of fields or enum members. Returns 0 for `NULL` and for types without items.

```
UMKA_API bool umkaTypeHasReferences(const UmkaType *type);
```
Returns whether values of the type contain Umka-managed references that need VM ownership or reference-count handling. Returns `false` for `NULL`.

```
UMKA_API bool umkaTypeUsesIndirectValueSlot(const UmkaType *type);
```
Returns whether values of the type are passed through an indirect value slot whose `ptrVal` points to storage. Arrays, dynamic arrays, maps, structures, interfaces, and closures use indirect value slots. Scalar values, strings, pointers, weak pointers, fibers, and direct `fn` values use direct slots. Returns `false` for `NULL`.

```
UMKA_API bool umkaTypeIsVariadicParamList(const UmkaType *type);
```
Returns whether the type is the dynamic-array type created for a variadic `..T` source parameter. Returns `false` for `NULL`.

```
UMKA_API const UmkaType *umkaGetBaseType(const UmkaType *type);
```
For a pointer, weak pointer, or fiber type, returns the base type. For an array or dynamic array type, returns the item type.

Parameters:

* `type`: Pointer, weak pointer, fiber, array, or dynamic array type

Returned value: Base type of a pointer, weak pointer, or fiber type; item type of an array or dynamic array type; `NULL` otherwise.

```
UMKA_API int umkaGetFieldCount(const UmkaType *type);
```
Returns the number of fields in a structure, interface, or closure type, or 0 for other types and `NULL`.

```
UMKA_API bool umkaGetField(const UmkaType *type, int index, const char **name, const UmkaType **fieldType, int *offset);
```
Returns field metadata by index for structure, interface, and closure types.

Parameters:

* `type`: Structure, interface, or closure type
* `index`: Zero-based field index
* `name`: Optional output pointer for the field name
* `fieldType`: Optional output pointer for the field type
* `offset`: Optional output pointer for the byte offset within the value storage

Returned value: `true` if the field exists, otherwise `false`.

```
UMKA_API const UmkaType *umkaGetFieldType(const UmkaType *structType, const char *fieldName);
```
Returns structure field type.

Parameters:

* `structType`: Structure type
* `fieldName`: Field name

Returned value: Field type; `NULL` if `structType` is not a structure type or has no field `fieldName`.

```
UMKA_API const UmkaType *umkaGetMapKeyType(const UmkaType *mapType);
```
Returns map key type.

Parameters:

* `mapType`: Map type

Returned value: Key type; `NULL` if `mapType` is not a map type.

```
UMKA_API const UmkaType *umkaGetMapItemType(const UmkaType *mapType);
```
Returns map item type.

Parameters:

* `mapType`: Map type

Returned value: Item type; `NULL` if `mapType` is not a map type.

```
UMKA_API int umkaGetFuncParamCount(const UmkaType *type);
UMKA_API const char *umkaGetFuncParamName(const UmkaType *type, int index);
UMKA_API const UmkaType *umkaGetFuncParamType(const UmkaType *type, int index);
UMKA_API const UmkaType *umkaGetFuncResultType(const UmkaType *type);
UMKA_API int umkaGetFuncDefaultParamCount(const UmkaType *type);
UMKA_API const UmkaType *umkaGetCallableFuncType(const UmkaType *type);
```
Returns source-level function signature metadata for `fn` and closure types. Hidden VM parameters are not exposed.

Parameters:

* `type`: Function or closure type
* `index`: Zero-based source parameter index

Returned values:

* `umkaGetFuncParamCount`: Number of source parameters, or 0 for non-function types
* `umkaGetFuncParamName`: Parameter name, or `NULL` if unavailable
* `umkaGetFuncParamType`: Parameter type, or `NULL` if unavailable
* `umkaGetFuncResultType`: Result type, or `NULL` for non-function types
* `umkaGetFuncDefaultParamCount`: Number of source parameters with defaults, or 0 for non-function types
* `umkaGetCallableFuncType`: Underlying direct `fn` type for a direct function or closure type, or `NULL` for non-callable types

```
UMKA_API int umkaGetEnumMemberCount(const UmkaType *type);
UMKA_API bool umkaGetEnumMember(const UmkaType *type, int index, const char **name, int64_t *signedValue, uint64_t *unsignedValue);
```
Returns enumeration member metadata.

Parameters:

* `type`: Enumeration type
* `index`: Zero-based member index
* `name`: Optional output pointer for the member name
* `signedValue`: Optional output pointer for the member value viewed as signed
* `unsignedValue`: Optional output pointer for the member value viewed as unsigned

Returned values:

* `umkaGetEnumMemberCount`: Number of enum members, or 0 for non-enum types and `NULL`
* `umkaGetEnumMember`: `true` if the member exists, otherwise `false`; output pointers are cleared for missing members

```
UMKA_API bool umkaGetAnySelf(const UmkaAny *value, const UmkaType **selfType, void **self);
```
Returns the raw self pointer and self pointer type stored in an `any` or interface value. This is for inspection only; callers should not depend on the private interface layout beyond this API.

Parameters:

* `value`: Pointer to an `UmkaAny` value or to the leading `UmkaAny` part of an interface value
* `selfType`: Optional output pointer for the stored self type
* `self`: Optional output pointer for the stored self value storage

Returned value: `true` for a non-empty value with both self pointer and self type, otherwise `false`. Output pointers are still cleared or filled with the available raw fields when possible.

```
UMKA_API bool umkaGetAnyValue(const UmkaAny *value, const UmkaType **type, UmkaStackSlot *slot);
```
Deconstructs an `any` or interface value into its concrete type and value slot.

Parameters:

* `value`: Pointer to an `UmkaAny` value or to the leading `UmkaAny` part of an interface value
* `type`: Optional output pointer for the concrete value type
* `slot`: Optional output slot for the concrete value

Returned value: `true` if the dynamic value has a concrete self value that can be deconstructed, otherwise `false`.

Notes:

* Scalar concrete values are returned in the matching `UmkaStackSlot` field
* Direct `str`, pointer, weak pointer, fiber, and function concrete values are returned in their usual slot field
* For dynamic arrays, maps, fixed arrays, structures, interfaces, and closures, `slot->ptrVal` points to the concrete value storage
* The returned pointer is only as stable as the original Umka value unless the dynamic value has been retained in an `UmkaHostHandle`

Example:

```
UmkaAny *value = (UmkaAny *)umkaGetParam(params, 0);
const UmkaType *type = NULL;
UmkaStackSlot slot = {0};

if (umkaGetAnyValue(value, &type, &slot) && umkaGetTypeKind(type) == UMKA_TYPE_INT)
    printf("%lld\n", (long long)slot.intVal);
```

```
UMKA_API void *umkaAllocData(Umka *umka, int size, UmkaExternFunc onFree);
```
Allocates an untyped chunk of heap memory.

Parameters:

* `umka`: Interpreter instance handle
* `size`: Chunk size in bytes
* `onFree`: Optional callback function that will be called when the chunk reference count reaches zero. It accepts one parameter, the chunk pointer

Returned value: Pointer to the allocated chunk. 

```
UMKA_API void umkaIncRef(Umka *umka, void *ptr);
```
Increments the reference count of a memory chunk.

Parameters:

* `umka`: Interpreter instance handle
* `ptr`: Chunk pointer

```
UMKA_API void umkaDecRef(Umka *umka, void *ptr);
```
Decrements the reference count of a memory chunk.

Parameters:

* `umka`: Interpreter instance handle
* `ptr`: Chunk pointer

```
UMKA_API char *umkaMakeStr(Umka *umka, const char *str);
```
Creates an Umka string from a C string. Every string passed from C/C++ to Umka should be created by calling this function.

Parameters:

* `umka`: Interpreter instance handle
* `str`: C string pointer

Returned value: Umka string pointer

```
UMKA_API int umkaGetStrLen(const char *str);
```
Returns the length of an Umka string. The result is always identical to that of `strlen`, but does not imply searching for the terminating null character.

Parameters:

* `str`: Umka string pointer

Returned value: String length, in bytes, not including the null character.

```
UMKA_API void umkaMakeDynArray(Umka *umka, void *array, const UmkaType *type, int len);
```
Creates a dynamic array. Equivalent to `array = make(type, len)` in Umka.

Parameters:

* `umka`: Interpreter instance handle
* `array`: Pointer to the dynamic array, actually of type `UmkaDynArray(ItemType)`
* `type`: Dynamic array type. Can be obtained by calling `UmkaGetParamType`
* `len`: Dynamic array length 

```
UMKA_API int umkaGetDynArrayLen(const void *array);
```
Returns the length of a dynamic array. Equivalent to `len(array)` in Umka.

Parameters:

* `array`: Pointer to the dynamic array, actually of type `UmkaDynArray(ItemType)`

Returned value: Dynamic array length

```
UMKA_API bool umkaSetDynArrayItem(Umka *umka, void *array, int64_t index, UmkaStackSlot item);
```
Assigns a dynamic array item using Umka reference-counting rules.

Parameters:

* `umka`: Interpreter instance handle
* `array`: Dynamic array previously initialized by `umkaMakeDynArray`, returned by Umka, or retained in host storage
* `index`: Zero-based item index
* `item`: Source item value. For `str`, store the string pointer in `ptrVal`; for dynamic arrays, maps, fixed arrays, structures, `any`, interfaces, and closures, store a pointer to source value storage in `ptrVal`

Returned value: `true` if the item has been assigned, otherwise `false`.

```
UMKA_API bool umkaGetDynArrayItem(Umka *umka, const void *array, int64_t index, UmkaStackSlot *item);
```
Reads a dynamic array item into a stack slot.

Parameters:

* `umka`: Interpreter instance handle
* `array`: Dynamic array storage
* `index`: Zero-based item index
* `item`: Output stack slot

Returned value: `true` if the item has been read, otherwise `false`.

```
UMKA_API bool umkaGetDynArrayAnyItem(Umka *umka, const void *array, int64_t index, UmkaAny *item);
```
Reads an item from a built-in `[]any` dynamic array.

Parameters:

* `umka`: Interpreter instance handle
* `array`: Dynamic array storage whose item type is built-in `any`
* `index`: Zero-based item index
* `item`: Output `UmkaAny` cell

Returned value: `true` if the `any` item has been read, otherwise `false`.

```
UMKA_API bool umkaRetainHostDynArrayItem(Umka *umka, const void *array, int64_t index, UmkaHostHandle *handle);
```
Retains a supported dynamic array item in a host handle.

Parameters:

* `umka`: Interpreter instance handle
* `array`: Dynamic array storage
* `index`: Zero-based item index
* `handle`: Initialized host handle that receives the retained item

Returned value: `true` if the item has been retained, otherwise `false`.

Notes:

* These APIs operate on the current fixed length of a dynamic array. They do not resize, append, insert, delete, or expose capacity
* `umkaSetDynArrayItem` increments references held by the new item, decrements references held by the old item, then copies the new item bytes
* `[]any` construction is supported by creating each concrete value with `umkaMakeAny`, passing a pointer to that `UmkaAny` in `item.ptrVal`, and releasing the temporary `UmkaAny` with `umkaReleaseHostValue` when it is not consumed elsewhere
* Supported `[]any` payloads are null, ordinal and real values, `str`, supported structures, supported dynamic arrays, supported maps including `map[str]any`, closures whose captured upvalue cell is supported, and same-runtime Umka-created fibers
* `umkaGetDynArrayItem` returns borrowed pointers for reference-bearing or structured values. Retain the item with `umkaRetainHostDynArrayItem` if it must outlive the array storage from which it was read
* `false` is returned for `NULL` arguments, uninitialized arrays, non-dynamic-array storage, invalid indices, wrong item types, unsupported payloads, or `umkaGetDynArrayAnyItem` used on a non-`[]any` array
* Unsupported cases include dynamic-array resizing, append, insert, delete, host-created fibers, pointers, weak pointers, unsupported nested interfaces or closure upvalues, and arbitrary managed heap wrappers

```
UMKA_API void *umkaMakeStruct(Umka *umka, const UmkaType *type);
```
Creates a structure or array in heap memory.

Parameters:

* `umka`: Interpreter instance handle
* `type`: Structure or array type

Returned value: Pointer to the created structure or array.

```
UMKA_API void *umkaGetMapItem(Umka *umka, UmkaMap *map, UmkaStackSlot key);
```
Finds the map item by the given key.

Parameters:

* `umka`: Interpreter instance handle
* `map`: Umka map
* `key`: Key value

Returned value: Pointer to the map item, `NULL` if the item does not exist.

```
UMKA_API bool umkaMakeMap(Umka *umka, UmkaMap *map, const UmkaType *type);
```
Creates an Umka map in caller-provided `UmkaMap` storage. Equivalent to `map = make(type)` in Umka for the supported host API subset.

Parameters:

* `umka`: Interpreter instance handle
* `map`: Pointer to `UmkaMap` storage. Before the first call, the storage must be zero-initialized. Calling `umkaMakeMap` again for an existing valid map releases the old map contents first
* `type`: Map type. Can be obtained by calling `umkaGetParamType` or `umkaGetResultType`

Returned value: `true` if the map has been created, otherwise `false`.

Notes:

* The allocated map nodes, keys, and items are owned by the Umka interpreter heap and must not be accessed after `umkaFree` or after the interpreter has terminated with a runtime error
* This host API supports map key and item types that are fixed-layout non-reference values, plus direct `str`. Fixed-layout non-reference values include ordinal and real types, fixed arrays, and structures that contain no pointers, weak pointers, strings, maps, dynamic arrays, interfaces, closures, or fibers
* This host API also supports `map[str]any` construction. Use `umkaMakeAny` to initialize an `UmkaAny`, pass a pointer to it in `item.ptrVal` to `umkaSetMapItem`, then release the temporary `UmkaAny` with `umkaReleaseHostValue` if it is not consumed elsewhere
* Unsupported map construction shapes include nested maps as direct items, dynamic arrays as direct items, pointers, weak pointers, non-`any` interfaces, direct closure items, direct fiber items, arbitrary reference-bearing map keys, and structures or arrays that contain unsupported references. Supported structures, dynamic arrays, maps, and closures may still be boxed into `any` and inserted into `map[str]any`
* `false` is returned for `NULL` arguments, non-map `type`, unsupported key/item types, or invalid non-zero `map` storage

```
UMKA_API bool umkaSetMapItem(Umka *umka, UmkaMap *map, UmkaStackSlot key, UmkaStackSlot item);
```
Inserts or replaces a map item.

Parameters:

* `umka`: Interpreter instance handle
* `map`: Map previously initialized by `umkaMakeMap`
* `key`: Key value. For fixed-layout structured keys, store a pointer to the source bytes in `ptrVal`
* `item`: Item value. For fixed-layout structured items, store a pointer to the source bytes in `ptrVal`

Returned value: `true` if the item has been inserted or replaced, otherwise `false`.

Notes:

* Direct `str` keys and items must be `NULL` or Umka strings created by `umkaMakeStr`
* The map stores its own references to direct `str` keys and items. The caller remains responsible for any reference it keeps, and may call `umkaDecRef` after successful insertion to transfer sole ownership to the map
* Replacing a direct `str` item decrements the old stored string reference
* For `map[str]any`, `item.ptrVal` must point to an `UmkaAny` initialized by `umkaMakeAny`. The map stores its own references to the boxed concrete value. The caller remains responsible for releasing the temporary `UmkaAny` value it passed in
* Fixed-layout non-reference keys and items are copied by value
* `false` is returned for `NULL` arguments, uninitialized maps, unsupported map shapes, unsupported `any` payloads, or direct string pointers that do not belong to the Umka heap. Allocation failures and other VM runtime errors still use the normal Umka error mechanism

```
UMKA_API bool umkaAssignHostValue(Umka *umka, void *dest, const UmkaType *type, UmkaStackSlot value);
```
Assigns a typed value to caller-provided storage using Umka reference-counting rules.

Parameters:

* `umka`: Interpreter instance handle
* `dest`: Destination storage for a value of `type`. The storage must be zero-initialized or already contain a valid value of the same type
* `type`: Destination value type
* `value`: Source value. For `str`, store the string pointer in `ptrVal`; for `fn`, store the entry offset in `intVal`; for dynamic arrays, maps, fixed arrays, structures, `any`, interfaces, and closures, store a pointer to source value storage in `ptrVal`

Returned value: `true` if the value has been assigned, otherwise `false`.

Notes:

* The assignment increments references held by the new value, decrements references held by the old destination value, then copies the new value bytes
* Supported types are ordinal and real values, `bool`, `char`, direct `str`, `fn`, same-runtime Umka-created fibers, closures with supported captured upvalues, dynamic arrays, maps, fixed arrays, structures, and direct `any` or interface values whose concrete self value is supported
* Fixed arrays and structures may contain ordinal, real, `str`, dynamic array, map, fixed array, and structure fields/items. Pointers, weak pointers, interfaces, closures, fibers, and function values are rejected as directly assigned fields/items. `map[str]any` and `[]any` values are accepted when every inserted `any` payload is supported by `umkaMakeAny`
* Dynamic values whose concrete self is an ordinal, real, `str`, dynamic array, map, fixed array, structure, `fn`, same-runtime Umka-created fiber, or supported closure are accepted. Pointer, weak pointer, and nested interface concrete values are rejected
* Direct `str` values must be `NULL` or Umka strings created by `umkaMakeStr`
* If `dest` is a parameter slot in an `UmkaFuncContext`, a successful `umkaCall` consumes that assigned parameter reference through normal function cleanup. Do not call `umkaReleaseHostValue` on that same parameter slot after the call returns. For host-owned storage that is not consumed by an Umka call, release it explicitly

```
UMKA_API bool umkaReleaseHostValue(Umka *umka, void *dest, const UmkaType *type);
```
Releases references held by a value in caller-provided storage and zeroes the storage.

Parameters:

* `umka`: Interpreter instance handle
* `dest`: Storage containing a valid value of `type`
* `type`: Value type

Returned value: `true` if the value has been released and zeroed, otherwise `false`.

Notes:

* Use this for host-owned storage initialized by `umkaAssignHostValue`, `umkaMakeAny`, `umkaMakeInterface`, `umkaMakeDynArray`, or `umkaMakeMap`, when that storage is not consumed by `umkaCall`
* A value may be released after a runtime error as long as the owning `Umka` instance has not been freed

```
UMKA_API bool umkaMakeAny(Umka *umka, UmkaAny *dest, const UmkaType *type, UmkaStackSlot value);
```
Constructs an `any` value in caller-provided `UmkaAny` storage.

Parameters:

* `umka`: Interpreter instance handle
* `dest`: Destination `UmkaAny` storage. The storage must be zero-initialized or already contain a valid `any` value
* `type`: Concrete value type, or `NULL` to construct a null `any`
* `value`: Concrete source value. For structured values, store a pointer to source value storage in `ptrVal`

Returned value: `true` if the `any` value has been constructed, otherwise `false`.

Notes:

* Non-null concrete values are copied to VM heap storage, and the constructed `any` stores a self pointer to that heap copy
* The constructed `any` owns that self reference until it is overwritten, consumed by an Umka call, or released with `umkaReleaseHostValue`
* For `str`, dynamic array, and map sources, the constructor retains the source backing storage. The caller still owns any original source reference and may release it after successful construction if it no longer needs it
* Supported and rejected concrete shapes are the same as for `umkaAssignHostValue`

Example:

```
UmkaAny value = {0};
UmkaStackSlot slot = {0};
slot.intVal = 42;

umkaMakeAny(umka, &value, intType, slot);

slot.ptrVal = &value;
umkaAssignHostValue(umka, umkaGetParam(fn.params, 0), anyType, slot);
umkaCall(umka, &fn);

umkaReleaseHostValue(umka, &value, anyType);
```

```
UMKA_API bool umkaMakeInterface(Umka *umka, void *dest, const UmkaType *interfaceType, const UmkaType *type, UmkaStackSlot value);
```
Constructs an interface value in caller-provided storage.

Parameters:

* `umka`: Interpreter instance handle
* `dest`: Destination interface storage. Allocate at least `umkaGetTypeSize(interfaceType)` bytes
* `interfaceType`: Destination interface type
* `type`: Concrete value type, or `NULL` to construct a null interface value
* `value`: Concrete source value. For structured values, store a pointer to source value storage in `ptrVal`

Returned value: `true` if the interface value has been constructed, otherwise `false`.

Notes:

* The constructor copies the concrete value to VM heap storage, stores a `^T` self type, and fills the interface method table by resolving methods for `^T`
* This supports Umka's value-to-interface model for declared concrete types with pointer receiver methods
* `false` is returned if `interfaceType` is not an interface, the concrete type does not implement every required method with a compatible signature, or the concrete shape is unsupported
* Direct host pointer ownership is not implemented by this API. Pass a concrete value of type `T`; the constructor creates the `^T` self storage used by the interface

```
UMKA_API void umkaMakeHostHandle(UmkaHostHandle *handle);
```
Initializes a host handle. Call this before any retain, clear, release, or read operation unless the handle storage has been zero-initialized.

Parameters:

* `handle`: Host handle storage

```
UMKA_API bool umkaRetainHostValue(Umka *umka, UmkaHostHandle *handle, const UmkaType *type, UmkaStackSlot value);
```
Retains a typed Umka value in a host handle. If the handle already contains a value, the old value is released after the new value has been retained.

Parameters:

* `umka`: Interpreter instance handle
* `handle`: Initialized host handle
* `type`: Value type. Use `umkaGetParamType`, `umkaGetResultType`, `umkaGetFieldType`, `umkaGetMapKeyType`, `umkaGetMapItemType`, or the type returned by `umkaGetHostHandleType`
* `value`: Value to retain. For `str`, store the string pointer in `ptrVal`; for `fn`, store the entry offset in `intVal`; for structured values such as dynamic arrays, maps, arrays, structures, `any`, interfaces, and closures, store a pointer to the value storage in `ptrVal`

Returned value: `true` if the value has been retained, otherwise `false`.

Notes:

* Supported root value types are direct `str`, `fn`, same-runtime Umka-created fibers, closures with supported captured upvalues, dynamic arrays, maps, fixed arrays, structures, and `any` or interface values whose concrete self value is supported
* Fixed arrays, structures, dynamic arrays, and maps may contain ordinal, real, `str`, dynamic array, map, fixed array, structure, interface, fiber, and closure fields/items when the actual retained values are supported. Pointers, weak pointers, unsupported nested interfaces, and unsupported closure upvalues are rejected
* Retained `any` and interface values copy the full interface cell and the concrete self value into handle-owned storage. Concrete ordinal, real, `str`, dynamic array, map, fixed array, structure, `fn`, same-runtime Umka-created fiber, and supported closure values are supported
* Non-empty interface method-table fields are preserved in the copied interface cell
* Dynamic values whose concrete self is a pointer, weak pointer, or nested interface are rejected by retention, although `umkaGetAnyValue` can still inspect them
* Empty dynamic values may be retained. `umkaGetAnyValue` returns `false` for them
* For dynamic arrays and maps, the handle retains the existing backing heap storage; it does not deep-copy the array items or map nodes
* For fixed arrays and structures, the handle stores a C-side byte copy and adjusts recursive reference counts for supported reference-bearing fields
* For closures, the handle stores a C-side closure copy and retains the captured upvalue cell if it is non-empty and supported. A retained closure can be called with `umkaMakeCallableContext` and `umkaCallCallable`
* For fibers, the handle retains the existing fiber context. Clear a retained live fiber only after it has completed or while another Umka value still owns it; destroying the last reference to a live fiber is rejected by the VM because its stack may still own references
* `NULL` is a valid retained `str` value. Structured values require a non-`NULL` `ptrVal`

```
UMKA_API bool umkaFiberValid(Umka *umka, UmkaStackSlot fiber);
UMKA_API bool umkaFiberAlive(Umka *umka, UmkaStackSlot fiber);
UMKA_API bool umkaFiberRunning(Umka *umka, UmkaStackSlot fiber);
```
Queries the status of an Umka-created fiber value.

Parameters:

* `umka`: Interpreter instance handle
* `fiber`: Fiber value in `ptrVal`

Returned value: `true` if the queried condition holds, otherwise `false`.

Notes:

* `umkaFiberValid` returns `true` only for a non-null fiber allocated by the same interpreter and still present in that interpreter heap
* `umkaFiberAlive` is equivalent to Umka `valid(fiber)` for a valid same-runtime fiber
* `umkaFiberRunning` returns `true` only for the currently executing VM fiber. It normally returns `false` after control has returned to the host
* These functions return `false` for null runtimes, null fibers, foreign fibers, invalid pointers, and interpreters that are no longer alive

```
UMKA_API bool umkaRetainHostFiber(Umka *umka, UmkaHostHandle *handle, UmkaStackSlot fiber);
```
Retains a same-runtime Umka-created fiber in a host handle.

Parameters:

* `umka`: Interpreter instance handle
* `handle`: Initialized host handle
* `fiber`: Fiber value in `ptrVal`

Returned value: `true` if the fiber has been retained, otherwise `false`.

Notes:

* This is a convenience wrapper around `umkaRetainHostValue` with the built-in fiber type
* Fibers can also be retained when they are the concrete value inside `any`, `map[str]any`, or `[]any`, provided the fiber belongs to the same interpreter
* Host-created fibers are not exposed. Hosts can obtain fibers only from Umka code, callback arguments, results, or retained containers

```
typedef enum
{
    UMKA_FIBER_RESUME_INVALID,
    UMKA_FIBER_RESUME_YIELDED,
    UMKA_FIBER_RESUME_DONE,
    UMKA_FIBER_RESUME_ERROR
} UmkaFiberResumeStatus;

UMKA_API UmkaFiberResumeStatus umkaResumeFiber(Umka *umka, UmkaHostHandle *handle);
UMKA_API UmkaFiberResumeStatus umkaResumeFiberValue(Umka *umka, UmkaStackSlot fiber);
```
Resumes a same-runtime Umka-created fiber from the host.

Parameters:

* `umka`: Interpreter instance handle
* `handle`: Retained host handle containing a fiber value
* `fiber`: Direct fiber value in `ptrVal`

Returned value:

* `UMKA_FIBER_RESUME_YIELDED`: The fiber yielded back to the host by calling `resume()` with no child
* `UMKA_FIBER_RESUME_DONE`: The fiber has completed or was already dead
* `UMKA_FIBER_RESUME_ERROR`: The resumed fiber raised a runtime error or interruption. Use `umkaGetError` for details
* `UMKA_FIBER_RESUME_INVALID`: The input or interpreter state is not resumable

Notes:

* `umkaResumeFiber` accepts only a non-empty same-runtime `UmkaHostHandle` whose retained value type is `fiber`
* `umkaResumeFiberValue` accepts a direct same-runtime fiber slot while the value is still valid
* The interpreter must be alive and idle at the normal host call boundary. Resume from native callbacks, hooks, or other reentrant VM execution is rejected
* The target fiber must be a live child of the idle VM fiber. This avoids stealing a fiber from a suspended parent fiber
* While resuming, the VM installs an internal host-boundary parent. A child `resume()` yields to the host and returns `UMKA_FIBER_RESUME_YIELDED`; child completion returns `UMKA_FIBER_RESUME_DONE`
* Null runtimes, null handles, empty handles, non-fiber handles, null fibers, invalid or foreign fibers, current/running fibers, and non-child fibers return `UMKA_FIBER_RESUME_INVALID`
* Runtime errors and interruptions follow the normal Umka error path and terminate the interpreter. `UMKA_FIBER_RESUME_ERROR` is returned only after the public API catches that error
* Host-created fibers, cross-interpreter fiber transfer, cross-thread scheduling, and resuming arbitrary suspended fiber graphs are not exposed

```
UMKA_API bool umkaRetainHostData(Umka *umka, UmkaHostHandle *handle, void *ptr);
```
Retains a heap chunk pointer, usually returned by `umkaAllocData` or `umkaMakeStruct`.

Parameters:

* `umka`: Interpreter instance handle
* `handle`: Initialized host handle
* `ptr`: Pointer to the start of a non-stack Umka heap chunk

Returned value: `true` if the heap chunk has been retained, otherwise `false`.

Notes:

* `umkaRetainHostData` rejects `NULL`, non-Umka pointers, stack chunks, and interior pointers
* Do not use `umkaRetainHostData` for `str`, dynamic array values, map values, or stack/local structured values. Use `umkaRetainHostValue` with the correct type metadata instead

```
UMKA_API void umkaClearHostHandle(UmkaHostHandle *handle);
UMKA_API void umkaReleaseHostHandle(UmkaHostHandle *handle);
```
Releases the value retained by a host handle and resets the handle to empty. `umkaReleaseHostHandle` is an alias for `umkaClearHostHandle`.

Parameters:

* `handle`: Host handle

Notes:

* Clearing or releasing an empty initialized handle is a no-op
* A retained handle may be cleared after a runtime error as long as the owning `Umka` instance has not been freed
* No operation on a host handle is valid after the owning interpreter has been freed with `umkaFree`; clear all active handles before calling `umkaFree`
* Host handles are not thread-safe and must be used on the same thread as the owning interpreter

```
UMKA_API bool umkaHostHandleValid(const UmkaHostHandle *handle);
```
Returns `true` if the handle currently retains a value or heap chunk.

```
UMKA_API const UmkaType *umkaGetHostHandleType(const UmkaHostHandle *handle);
```
Returns the retained value type, or `NULL` for empty handles and handles created by `umkaRetainHostData`.

```
UMKA_API UmkaStackSlot umkaGetHostHandleValue(const UmkaHostHandle *handle);
```
Returns the retained value. For structured values, `ptrVal` points to stable handle-owned value storage until the handle is cleared. For retained `any` and interface values, `ptrVal` points to the copied interface cell; use `umkaGetAnyValue((UmkaAny *)slot.ptrVal, ...)` to inspect the concrete value. When passing a retained non-empty interface value back to Umka, copy `umkaGetTypeSize(umkaGetHostHandleType(handle))` bytes from `slot.ptrVal` into the destination interface storage. For empty handles, the returned slot is zeroed.

```
UMKA_API bool umkaGetHostMapCount(Umka *umka, const UmkaHostHandle *mapHandle, int64_t *count);
```
Returns the number of entries in a retained map.

Parameters:

* `umka`: Interpreter instance handle
* `mapHandle`: Retained host handle whose value type is a map
* `count`: Output entry count

Returned value: `true` if the count has been read, otherwise `false`.

```
UMKA_API bool umkaGetHostMapEntry(Umka *umka, const UmkaHostHandle *mapHandle, int64_t index, UmkaHostMapEntry *entry);
```
Returns a read-only entry descriptor for a retained map entry.

Parameters:

* `umka`: Interpreter instance handle
* `mapHandle`: Retained host handle whose value type is a map
* `index`: Zero-based entry index
* `entry`: Output entry descriptor

Returned value: `true` if the entry descriptor has been filled, otherwise `false`.

```
UMKA_API bool umkaGetHostMapEntryKey(Umka *umka, const UmkaHostMapEntry *entry, UmkaStackSlot *key);
UMKA_API bool umkaGetHostMapEntryValue(Umka *umka, const UmkaHostMapEntry *entry, UmkaStackSlot *value);
```
Returns a snapshot of an entry key or value slot.

Parameters:

* `umka`: Interpreter instance handle
* `entry`: Entry descriptor returned by `umkaGetHostMapEntry`
* `key`, `value`: Output stack slot

Returned value: `true` if the slot has been read, otherwise `false`.

```
UMKA_API bool umkaGetHostMapEntryStringKey(Umka *umka, const UmkaHostMapEntry *entry, const char **key);
```
Returns a retained map entry key as a string pointer.

Parameters:

* `umka`: Interpreter instance handle
* `entry`: Entry descriptor returned by `umkaGetHostMapEntry`
* `key`: Output string pointer

Returned value: `true` if the entry key type is `str` and the key has been read, otherwise `false`.

```
UMKA_API bool umkaGetHostMapEntryAnyValue(Umka *umka, const UmkaHostMapEntry *entry, UmkaAny *value);
```
Returns a retained map entry value as an `any` interface cell.

Parameters:

* `umka`: Interpreter instance handle
* `entry`: Entry descriptor returned by `umkaGetHostMapEntry`
* `value`: Output `any` value

Returned value: `true` if the entry item type is the built-in `any` shape and the value has been read, otherwise `false`.

```
UMKA_API bool umkaRetainHostMapEntryKey(Umka *umka, const UmkaHostMapEntry *entry, UmkaHostHandle *handle);
UMKA_API bool umkaRetainHostMapEntryValue(Umka *umka, const UmkaHostMapEntry *entry, UmkaHostHandle *handle);
```
Retains a map entry key or value in a host handle.

Parameters:

* `umka`: Interpreter instance handle
* `entry`: Entry descriptor returned by `umkaGetHostMapEntry`
* `handle`: Initialized host handle that receives the retained key or value

Returned value: `true` if the key or value has been retained, otherwise `false`.

Notes:

* These APIs require a retained `UmkaHostHandle` whose value type is a map. They reject `NULL` runtimes, `NULL` outputs, empty handles, handles from another interpreter, non-map handles, and invalid entry indices
* `UmkaHostMapEntry` is a descriptor, not an owning handle. It is valid only while the owning interpreter and retained map handle remain alive and the map is not mutated
* Entry descriptors re-resolve the entry by index and type metadata. They do not expose `Map`, `MapNode`, heap page, stack, or private `Type` layout
* `umkaGetHostMapEntryStringKey` is intended for direct `str` keys. Other key kinds return `false`
* `umkaGetHostMapEntryAnyValue` is intended for built-in `any` map item values. Other value kinds return `false`
* `umkaRetainHostMapEntryKey` and `umkaRetainHostMapEntryValue` use the same support and ownership rules as `umkaRetainHostValue`
* String pointers and structured pointers returned through snapshot slots are borrowed from retained map storage. Retain the entry key or value in a separate host handle if it must outlive the map handle
* Unsupported cases include map mutation through these APIs, arbitrary reference-bearing map keys, unsupported `any` payloads, borrowed map entry pointers, and host-created fibers

## Accessing Umka API dynamically

Using the Umka API functions generally requires linking against the Umka interpreter library. This dependency is undesirable when implementing UMIs. In such cases, the same Umka API functions can be accessed dynamically, through the Umka interpreter instance handle passed to the UMI functions.

### Types

```
typedef Umka *(*UmkaAlloc) (void);
// ... all other API function pointer types

typedef struct
{
    UmkaAlloc umkaAlloc;
    // ... all other API function pointers
} UmkaAPI;
```
Collection of pointers to all the Umka API functions, except those declared as `inline`. For any API function, there is a corresponding field with the same name in `UmkaAPI`.

### Functions

```
static inline UmkaAPI *umkaGetAPI(Umka *umka);
```
Returns Umka API function pointers. 

Parameters:

* `umka`: Interpreter instance handle

Returned value: Collection of Umka API function pointers

## Appendix: UMI example

```
// lib.um - UMI interface

fn add*(a, b: real): real
fn mulVec*(a: real, v: [2]real): [2]real
fn hello*(): str
fn squares*(n: int): []int
```
```
// lib.c - UMI implementation

#include "umka_api.h"

UMKA_EXPORT void add(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);

    const double a = api->umkaGetParam(params, 0)->realVal;
    const double b = api->umkaGetParam(params, 1)->realVal;
    api->umkaGetResult(params, result)->realVal = a + b;
}

UMKA_EXPORT void mulVec(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka); 

    const double a = api->umkaGetParam(params, 0)->realVal;
    const double* v = (const double *)api->umkaGetParam(params, 1);
    double* out = api->umkaGetResult(params, result)->ptrVal;

    out[0] = a * v[0];
    out[1] = a * v[1];
}

UMKA_EXPORT void hello(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);
    
    api->umkaGetResult(params, result)->ptrVal = api->umkaMakeStr(umka, "Hello");
}

UMKA_EXPORT void squares(UmkaStackSlot *params, UmkaStackSlot *result)
{
    Umka *umka = umkaGetInstance(result);
    UmkaAPI *api = umkaGetAPI(umka);

    const int n = api->umkaGetParam(params, 0)->intVal;

    typedef UmkaDynArray(int64_t) IntArray;
    IntArray *array = api->umkaGetResult(params, result)->ptrVal;
    const UmkaType *arrayType = api->umkaGetResultType(params, result); 

    api->umkaMakeDynArray(umka, array, arrayType, n);

    for (int i = 0; i < n; i++)
        array->data[i] = i * i;
}
```
