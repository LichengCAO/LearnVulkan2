# Repository Guidelines

## C++ Code Style

- Apply these rules to first-party C++ code. Do not modify code under `external/` to enforce them.
- Prefix every private member function name with a single underscore, for example `_CreateBuffer`.
- Declare every function with a non-`void` return type using trailing return type syntax: `auto Function(...) -> ReturnType`.
- Functions returning `void` are exempt from the trailing return type rule.

## RHI Creation and Threading

These requirements apply to new and migrated first-party RHI interfaces. Existing interfaces retain their current threading restrictions until audited and migrated; this policy alone does not make them thread-safe.

Objects and allocators have no thread affinity. Unless an interface explicitly documents internal synchronization, callers must exclude conflicting access to the same instance and to any related shared mutable state. Independent instances may be used concurrently.

### Creation

- After device initialization, public object-level `Create()` supports concurrent creation of independent instances by default. The caller must exclude concurrent creation, modification, and destruction of the same instance, and keep creation inputs and dependencies stable for the duration of the call.
- Multiple threads may read the same finalized CreateInfo. Modification, `Reset()`, and `Finalize()` require exclusive access relative to readers and other writers.
- Creation requiring coordination of shared mutable state belongs in a MyDevice creation entry point: `CreateXxx()`, or `GetOrCreateXxx()` for cached objects. The corresponding object does not expose public `Create()`; its implementation may use private `_Create()`.
- Document which shared state each MyDevice creation entry point accesses. Callers synchronize it with all other accesses to that state, including indirect release paths. Operations using independent shared state may run concurrently; synchronization is not automatically global to MyDevice.
- `Create()` completes creation before returning, without implicitly enqueueing work. Keep object-level `Create()` according to its threading contract, regardless of whether it calls Vulkan or how long it takes.

### Release and Shared State

- Destruction and final strong-reference release may run on any thread when callers satisfy the synchronization requirements of every related object. Atomic reference counting does not synchronize object contents or the final release path.
- Callers synchronize creation, lookup, release, and whole-cache destruction that access the same MyDevice-managed cache or allocation records. Allocators do not provide internal locking unless explicitly documented.
- Borrowed handle values do not own native resources. Copying, moving, clearing, or destroying such a value does not access its owner or release its native handle; validity ends when the owner reclaims the resource.
- Actual resource release completes synchronously, without introducing a deferred reclamation queue. Synchronize according to the shared state accessed, rather than requiring all operations on a Device to be mutually exclusive.
- MyDevice initialization, destruction, and device recreation require exclusive access relative to related resource operations. Host thread safety does not replace GPU completion synchronization or resource lifetime requirements.
- Document threading contracts for `Allocate()`, `Free()`, `Reset()`, recording, and submission independently of `Create()`. DescriptorPool has no internal locking: callers serialize each pool's allocation, explicit Free and final destruction; independent pools may run concurrently subject to their dependencies' contracts. It returns borrowed VkDescriptorSet handles; copying or clearing a handle does not retain or access its pool. See [descriptor pools](docs/descriptor_pool.md#synchronization) for dependency release requirements.

### Migration Priorities

- Compute, Graphics and RayTracing ShaderProgram retain their existing creation restrictions until individually migrated. Callers coordinate their Create/Destroy paths and GraphicsShaderProgram lazy GetVkPipeline with all other accesses to the same shared caches and dependencies. Program creation accesses both descriptor-layout and pipeline-layout caches; independent instances do not yet support unsynchronized concurrent Create.

- Audit shared-cache creation paths, Buffer/Image memory allocation records, and the CommandPool registry first. Move creation into MyDevice or restructure the shared state according to the actual concurrency requirements.
