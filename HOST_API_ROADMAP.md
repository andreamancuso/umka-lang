# Host API Roadmap For UmkaSharp And BTBrowser

This file captures the fork-level work needed to remove the main embedding limitations currently visible from UmkaSharp and BTBrowser dogfooding.

## 1. Map Construction API

Add public C APIs for host-side map allocation, insertion, assignment/reference-count handling, and ownership transfer. This is the biggest practical blocker because UmkaSharp can currently copy readable map results and callback arguments, but cannot safely create Umka maps from C# values or return maps from C# callbacks.

First fork slice status: `umkaMakeMap` and `umkaSetMapItem` provide additive host-side creation and insertion for fixed-layout non-reference key/item maps plus direct `str` key/item maps. This covers the initial UmkaSharp shapes needed for C# map arguments and callback map results, including `map[int]int`, `map[str]int`, and `map[str]str`.

Remaining map work: rooted host handles, long-lived ownership transfer, arbitrary nested maps, dynamic arrays, pointers, interfaces, closures, fibers, `any`, and structures or arrays that contain reference-bearing fields.

## 2. Rooted Heap Value Handles

Add a safe host handle model for Umka heap values. The model should define retain, release, root, ownership, thread-affinity, and runtime-shutdown rules for maps, dynamic arrays, strings, interfaces, `any`, closures, fibers, and other heap-backed values before they are exposed through managed wrappers.

## 3. Interrupt And Cancellation Support

Add a host-callable VM interruption mechanism so embedded hosts can stop CPU-bound scripts cleanly. BTBrowser can cancel host waits cooperatively today, but a script stuck in pure Umka execution needs native interruption support or process isolation.

## 4. Closure, Interface, `any`, And Fiber Exposure

Expose these only after rooted heap handles and lifetime rules exist. Closures need captured-upvalue ownership and reentrant call behavior. Interfaces and `any` need self-value rooting, type assertions, and method-table retention. Fibers need host-side creation/resume/status ownership rules.

## 5. UmkaSharp Integration

Extend `umka_shim`, managed wrappers, tests, documentation, and BTBrowser adapter code to consume the new C APIs. Each native addition should have managed capability checks, error behavior, lifetime tests, and package-surface tests before becoming part of the public UmkaSharp API.

## 6. Upstream-Friendly Maintenance

Keep fork changes additive where possible. Avoid changing language semantics unless absolutely required. Prefer public C API additions and VM hooks that can plausibly be proposed upstream, so the fork remains maintainable and easier to sync with `vtereshkov/umka-lang`.
