# Host API Roadmap For UmkaSharp And BTBrowser

This file captures the fork-level work needed to remove the main embedding limitations currently visible from UmkaSharp and BTBrowser dogfooding.

## 1. Map Construction API

Add public C APIs for host-side map allocation, insertion, assignment/reference-count handling, and ownership transfer. This is the biggest practical blocker because UmkaSharp can currently copy readable map results and callback arguments, but cannot safely create Umka maps from C# values or return maps from C# callbacks.

First fork slice status: `umkaMakeMap` and `umkaSetMapItem` provide additive host-side creation and insertion for fixed-layout non-reference key/item maps plus direct `str` key/item maps. This covers the initial UmkaSharp shapes needed for C# map arguments and callback map results, including `map[int]int`, `map[str]int`, and `map[str]str`.

Remaining map construction work: arbitrary map construction for dynamic arrays, pointers, non-`any` interfaces, closures, fibers, nested maps, arbitrary reference-bearing keys, and structures or arrays that contain unsupported reference-bearing fields.

## 2. Rooted Heap Value Handles

Add a safe host handle model for Umka heap values. The model should define retain, release, root, ownership, thread-affinity, and runtime-shutdown rules for maps, dynamic arrays, strings, interfaces, `any`, closures, fibers, and other heap-backed values before they are exposed through managed wrappers.

Second fork slice status: `UmkaHostHandle`, `umkaMakeHostHandle`, `umkaRetainHostValue`, `umkaRetainHostData`, `umkaClearHostHandle`, `umkaReleaseHostHandle`, `umkaHostHandleValid`, `umkaGetHostHandleType`, and `umkaGetHostHandleValue` provide additive host-side rooting for direct `str`, dynamic arrays, maps, fixed arrays, fixed structures, and plain heap data chunks. Handles retain existing dynamic array and map backing storage rather than deep-copying contents. Handles must be cleared before `umkaFree`; they may be cleared after a runtime error while the owning interpreter still exists.

Remaining handle work: broader policy and API coverage for direct pointers, weak pointers, fibers, thread handoff, and managed wrapper integration.

## 3. Interrupt And Cancellation Support

Add a host-callable VM interruption mechanism so embedded hosts can stop CPU-bound scripts cleanly. BTBrowser can cancel host waits cooperatively today, but a script stuck in pure Umka execution needs native interruption support or process isolation.

Third fork slice status: `UMKA_ERR_INTERRUPTED`, `umkaRequestInterrupt`, `umkaClearInterrupt`, and `umkaInterruptRequested` provide additive interpreter-wide cooperative interruption. The VM checks for a pending request before bytecode dispatch, reports a normal runtime error with the host-supplied message, and unwinds through `umkaRun` or `umkaCall`. Requests are sticky until cleared. An observed interrupt kills the interpreter through the existing runtime error path; clearing the interrupt flag does not make that interpreter runnable again.

Current interruption limits: native C/C++ callbacks and other long-running native operations are not preempted while they are executing. Interruption is per interpreter, not per fiber. The only intended cross-thread API use is requesting interruption; general interpreter operations remain thread-affine.

Remaining interruption work: managed UmkaSharp cancellation-token integration, host timeout policy, and any future upstream hardening around atomic flag/message storage if upstream wants a stricter cross-thread memory model.

## 4. Closure, Interface, `any`, And Fiber Exposure

Fourth fork slice status: `UmkaTypeKind`, `umkaGetTypeKind`, `umkaGetTypeName`, `umkaGetTypeSize`, `umkaGetTypeSpelling`, `umkaTypesEquivalent`, `umkaGetTypeItemCount`, `umkaTypeHasReferences`, `umkaTypeUsesIndirectValueSlot`, `umkaTypeIsVariadicParamList`, `umkaGetFieldCount`, `umkaGetField`, `umkaGetEnumMemberCount`, `umkaGetEnumMember`, `umkaGetFuncParamCount`, `umkaGetFuncParamName`, `umkaGetFuncParamType`, `umkaGetFuncResultType`, `umkaGetFuncDefaultParamCount`, and `umkaGetCallableFuncType` provide additive type reflection for host wrappers. `umkaGetAnySelf` and `umkaGetAnyValue` let hosts inspect the concrete value behind `any` and interface values without depending on private struct layouts.

The type/reflection API now covers the private `Type` facts UmkaSharp was reading directly for kind, spelling, size, item count, reference-containing detection, field metadata, enum members, map key/item types, callable signatures, default parameter count, callable closure unwrapping, variadic parameter-list detection, and direct-slot versus indirect-slot storage. The follow-up UmkaSharp slice should replace broad `Type` layout reads in `native/umka_shim.c` with these public APIs.

Raw function default-value storage remains private because it is represented as internal `Const` data, including pointers to compiler-owned storage for heap-backed defaults. Hosts can query the number of default parameters and can fill supported omitted defaults through `umkaSetDefaultParam` or `umkaSetDefaultParams` without reading private `Signature`, `Param`, or `Const` layout.

`UmkaHostHandle` can retain empty dynamic values and supported non-empty `any` or interface values by copying the full interface cell plus the concrete self value into handle-owned storage. Non-empty interface method-table fields are preserved. Supported retained concrete payloads are ordinal and real values, `str`, dynamic arrays, maps, fixed arrays, structures whose contained fields/items are also supported, direct function values, and closures whose captured upvalue cell is supported.

Unsupported or deliberately deferred dynamic payloads at this point were pointers, weak pointers, nested interfaces, and fibers. Later slices add partial fiber retention while keeping host-side fiber creation and resume out of scope.

Fifth fork slice status: `umkaAssignHostValue`, `umkaReleaseHostValue`, `umkaMakeAny`, and `umkaMakeInterface` provide additive host-side assignment and construction for supported values. `umkaAssignHostValue` gives caller-provided storage a reference-counted value and `umkaReleaseHostValue` releases host-owned storage. `umkaMakeAny` copies supported concrete values to VM heap storage and initializes `any` without exposing private interface layout. `umkaMakeInterface` does the same for non-empty interfaces and fills method-table entries by resolving compatible pointer receiver methods for the concrete type.

Current construction limits: direct host pointer ownership, weak pointers, host-created fibers, nested interface payloads, and containers with unsupported fields/items remain rejected. Direct function values, same-runtime Umka-created fibers, and closures whose captured upvalue cell is supported can be assigned, retained, and boxed into `any`; hosts cannot synthesize arbitrary new closure or fiber entry points. Map creation still has narrower `umkaMakeMap`/`umkaSetMapItem` construction limits outside direct scalar/string maps and `map[str]any`, although existing supported map values can be assigned or boxed. Function parameter slots assigned by the host are consumed by normal Umka call cleanup after a successful `umkaCall`; host-owned storage that is not consumed by a call must be released explicitly.

Sixth fork slice status: `umkaCallableValid`, `umkaMakeCallableContext`, and `umkaCallCallable` provide additive host-side exposure for typed `fn` and closure values. Hosts can validate a callable, create a VM-backed call context, fill source-level arguments through `umkaGetParam`, and invoke it while the API refreshes and reference-counts the hidden closure upvalue parameter. Retained closures and closures deconstructed from `any` can be called through the same path. Runtime errors and interruption use the existing `umkaCall` result model and terminate the interpreter through the normal VM path.

Current callable limits: callable contexts borrow the callable value and do not by themselves retain it. Retain a closure with `UmkaHostHandle` if it must outlive the callback, stack frame, or dynamic value from which it was obtained. Fibers remain rejected by the callable API because a fiber is a heap VM context with its own stack and scheduler parent; `resume` switches the active VM fiber rather than performing an ordinary function call. Cross-thread invocation and cross-interpreter callable transfer remain out of scope.

Seventh fork slice status: `umkaSetDefaultParam` and `umkaSetDefaultParams` provide additive host-side default-parameter materialization for existing `UmkaFuncContext` values. They use source-level parameter indices/counts, preserve hidden VM parameter conventions internally, validate the reflected function parameter type against the context slot type, and support the scalar, pointer, and `str` defaults needed by UmkaSharp. Unsupported defaults currently return `false` for dynamic arrays, maps, interfaces, closures, fibers, `any`, weak pointers, and aggregate values rather than exposing or copying private default storage.

Once UmkaSharp consumes the default-parameter slice, `native/umka_shim.c` should no longer need to read `fnType->sig->param[...]` or `Param.defaultVal` directly.

Eighth fork slice status: `umkaGetHostMapCount`, `umkaGetHostMapEntry`, `umkaGetHostMapEntryKey`, `umkaGetHostMapEntryValue`, `umkaGetHostMapEntryStringKey`, `umkaGetHostMapEntryAnyValue`, `umkaRetainHostMapEntryKey`, and `umkaRetainHostMapEntryValue` provide retained read-only map inspection without exposing `Map`, `MapNode`, heap page, stack, or private `Type` layout. A host can retain a map value, count entries, read entry descriptors by index, read direct `str` keys, read scalar slots, inspect built-in `any` item cells, and retain supported entry keys or values.

The first intended consumer shape is `map[str]any` returned by Umka. Retention now validates actual nested map and array contents, so supported interface, `any`, and closure payloads can be retained when their concrete values are supported. Later fiber retention support also covers same-runtime Umka-created fibers stored as `any` payloads.

Remaining read-only map work: managed UmkaSharp wrappers, mutation/versioning rules for entry descriptors, arbitrary reference-bearing key policies, and clearer long-term support for nested interface payloads. Remaining map construction work is still handled separately by `umkaMakeMap` and `umkaSetMapItem`.

Ninth fork slice status: `umkaMakeMap` and `umkaSetMapItem` now support host-created `map[str]any` in addition to the earlier direct scalar/string map shapes. Hosts create each `any` value with `umkaMakeAny`, insert it with a direct `str` key, and release their temporary `UmkaAny` storage with `umkaReleaseHostValue` if it is not otherwise consumed. The same map can be assigned into Umka parameter storage with `umkaAssignHostValue`, returned from native callbacks, retained with `UmkaHostHandle`, and inspected through the retained read-only map APIs.

Supported `map[str]any` payloads are null, ordinal and real values, `str`, supported structures, supported dynamic arrays, supported maps, same-runtime Umka-created fibers, and closures whose captured upvalue cell is supported. Direct pointers, weak pointers, unsupported nested interfaces, unsupported closure upvalues, arbitrary reference-bearing keys, borrowed map entry pointers, and host-created fibers remain rejected.

Tenth fork slice status: `umkaSetDynArrayItem`, `umkaGetDynArrayItem`, `umkaGetDynArrayAnyItem`, and `umkaRetainHostDynArrayItem` provide fixed-length dynamic-array item construction and inspection without exposing `DynArray` internals beyond the existing public `UmkaDynArray(T)` storage shape. Hosts can create `[]any` with `umkaMakeDynArray`, fill each cell with an `UmkaAny` produced by `umkaMakeAny`, pass the array to Umka with `umkaAssignHostValue`, return it from native callbacks, retain it with `UmkaHostHandle`, inspect item cells, replace items safely, and release host-owned storage with `umkaReleaseHostValue`.

Supported `[]any` payloads are null, ordinal and real values, `str`, supported structures, supported dynamic arrays, supported maps including `map[str]any`, same-runtime Umka-created fibers, and closures whose captured upvalue cell is supported. The APIs also support direct item get/set for existing safe dynamic-array item shapes such as `[]int`.

Remaining dynamic-array work: resizing, append, insert, delete, capacity management, arbitrary managed heap wrappers, mutation/versioning rules for borrowed item pointers, direct pointers, weak pointers, unsupported nested interfaces, unsupported closure upvalues, and host-created fibers.

Eleventh fork slice status: `umkaFiberValid`, `umkaFiberAlive`, `umkaFiberRunning`, and `umkaRetainHostFiber` provide public status and retention support for same-runtime Umka-created fiber values. The general retained host value path now also supports valid fiber values directly and as concrete payloads inside `any`, `map[str]any`, and `[]any`. Hosts can retain a fiber returned by Umka or received by a native callback, query whether it is valid/alive/currently running, pass it back to Umka as a `fiber` parameter, and release the handle safely after the fiber has completed.

Twelfth fork slice status: `umkaResumeFiber` and `umkaResumeFiberValue` provide host-side resume for same-runtime Umka-created fibers. The implementation adds an internal host-boundary parent while a host resume is active, and `vmLoop` now stops cleanly if a resumed child yields back to that boundary through `resume()` or completes through `return`. The public result distinguishes invalid input/state, yielded fibers, completed fibers, and runtime-error/interruption exits.

Host resume is intentionally limited to an interpreter that is alive and idle at the normal host call boundary. The target fiber must be a live child of that idle VM fiber, so native callbacks, hooks, current/running fibers, foreign fibers, non-fiber handles, and fibers owned by another suspended parent are rejected instead of being reparented implicitly. Runtime errors and interruptions use the normal Umka error path and terminate the interpreter; the API restores transient host-resume parent state before returning `UMKA_FIBER_RESUME_ERROR`.

Host-side fiber creation is still not exposed. Creating a fiber requires a closure, parent fiber selection, stack initialization, and upvalue ownership rules that are currently implemented only by Umka `make(fiber, ...)`.

## 5. UmkaSharp Integration

Extend `umka_shim`, managed wrappers, tests, documentation, and BTBrowser adapter code to consume the new C APIs. Each native addition should have managed capability checks, error behavior, lifetime tests, and package-surface tests before becoming part of the public UmkaSharp API.

## 6. Upstream-Friendly Maintenance

Keep fork changes additive where possible. Avoid changing language semantics unless absolutely required. Prefer public C API additions and VM hooks that can plausibly be proposed upstream, so the fork remains maintainable and easier to sync with `vtereshkov/umka-lang`.
