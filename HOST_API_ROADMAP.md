# Host API Roadmap For UmkaSharp And BTBrowser

This file captures the fork-level work needed to remove the main embedding limitations currently visible from UmkaSharp and BTBrowser dogfooding.

## 1. Map Construction API

Add public C APIs for host-side map allocation, insertion, assignment/reference-count handling, and ownership transfer. This is the biggest practical blocker because UmkaSharp can currently copy readable map results and callback arguments, but cannot safely create Umka maps from C# values or return maps from C# callbacks.

First fork slice status: `umkaMakeMap` and `umkaSetMapItem` provide additive host-side creation and insertion for fixed-layout non-reference key/item maps plus direct `str` key/item maps. This covers the initial UmkaSharp shapes needed for C# map arguments and callback map results, including `map[int]int`, `map[str]int`, and `map[str]str`.

Remaining map work: arbitrary map construction for dynamic arrays, pointers, interfaces, closures, fibers, `any`, nested maps, and structures or arrays that contain unsupported reference-bearing fields.

## 2. Rooted Heap Value Handles

Add a safe host handle model for Umka heap values. The model should define retain, release, root, ownership, thread-affinity, and runtime-shutdown rules for maps, dynamic arrays, strings, interfaces, `any`, closures, fibers, and other heap-backed values before they are exposed through managed wrappers.

Second fork slice status: `UmkaHostHandle`, `umkaMakeHostHandle`, `umkaRetainHostValue`, `umkaRetainHostData`, `umkaClearHostHandle`, `umkaReleaseHostHandle`, `umkaHostHandleValid`, `umkaGetHostHandleType`, and `umkaGetHostHandleValue` provide additive host-side rooting for direct `str`, dynamic arrays, maps, fixed arrays, fixed structures, and plain heap data chunks. Handles retain existing dynamic array and map backing storage rather than deep-copying contents. Handles must be cleared before `umkaFree`; they may be cleared after a runtime error while the owning interpreter still exists.

Remaining handle work: broader policy and API coverage for direct pointers, weak pointers, closures, fibers, function values, thread handoff, and managed wrapper integration.

## 3. Interrupt And Cancellation Support

Add a host-callable VM interruption mechanism so embedded hosts can stop CPU-bound scripts cleanly. BTBrowser can cancel host waits cooperatively today, but a script stuck in pure Umka execution needs native interruption support or process isolation.

Third fork slice status: `UMKA_ERR_INTERRUPTED`, `umkaRequestInterrupt`, `umkaClearInterrupt`, and `umkaInterruptRequested` provide additive interpreter-wide cooperative interruption. The VM checks for a pending request before bytecode dispatch, reports a normal runtime error with the host-supplied message, and unwinds through `umkaRun` or `umkaCall`. Requests are sticky until cleared. An observed interrupt kills the interpreter through the existing runtime error path; clearing the interrupt flag does not make that interpreter runnable again.

Current interruption limits: native C/C++ callbacks and other long-running native operations are not preempted while they are executing. Interruption is per interpreter, not per fiber. The only intended cross-thread API use is requesting interruption; general interpreter operations remain thread-affine.

Remaining interruption work: managed UmkaSharp cancellation-token integration, host timeout policy, and any future upstream hardening around atomic flag/message storage if upstream wants a stricter cross-thread memory model.

## 4. Closure, Interface, `any`, And Fiber Exposure

Fourth fork slice status: `UmkaTypeKind`, `umkaGetTypeKind`, `umkaGetTypeName`, `umkaGetTypeSize`, `umkaGetTypeSpelling`, `umkaGetFieldCount`, `umkaGetField`, `umkaGetFuncParamCount`, `umkaGetFuncParamName`, `umkaGetFuncParamType`, and `umkaGetFuncResultType` provide additive type reflection for host wrappers. `umkaGetAnySelf` and `umkaGetAnyValue` let hosts inspect the concrete value behind `any` and interface values without depending on private struct layouts.

`UmkaHostHandle` can retain empty dynamic values and supported non-empty `any` or interface values by copying the full interface cell plus the concrete self value into handle-owned storage. Non-empty interface method-table fields are preserved. Supported retained concrete payloads are ordinal and real values, `str`, dynamic arrays, maps, fixed arrays, and structures whose contained fields/items are also supported.

Unsupported or deliberately deferred dynamic payloads are pointers, weak pointers, nested interfaces, closures, fibers, and function values. `umkaGetAnyValue` can inspect these where the VM can deconstruct them, but `umkaRetainHostValue` rejects them because this fork does not yet define safe ownership, call/resume, reentrancy, or cross-frame lifetime rules for those shapes.

Host construction of `any` and interface values remains out of scope. A safe constructor should reuse VM assignment, reference-counting, type assertion, and interface method-table setup semantics rather than requiring hosts to synthesize private interface cells.

## 5. UmkaSharp Integration

Extend `umka_shim`, managed wrappers, tests, documentation, and BTBrowser adapter code to consume the new C APIs. Each native addition should have managed capability checks, error behavior, lifetime tests, and package-surface tests before becoming part of the public UmkaSharp API.

## 6. Upstream-Friendly Maintenance

Keep fork changes additive where possible. Avoid changing language semantics unless absolutely required. Prefer public C API additions and VM hooks that can plausibly be proposed upstream, so the fork remains maintainable and easier to sync with `vtereshkov/umka-lang`.
