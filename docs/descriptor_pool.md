# Descriptor pools and native descriptor sets

`DescriptorPool` is a reference-counted owner of multiple Vulkan descriptor pools.
Create it with `MakeIntrusive<DescriptorPool>()`. It allocates independent
`VkDescriptorSet` handles. Release individual allocations explicitly:

```cpp
auto pool = MakeIntrusive<DescriptorPool>();
pool->Create();

auto set = pool->Allocate(layout, description);
// Record and submit commands using set, then complete their GPU use.
pool->Free(set); // frees this allocation and clears set on success
pool.reset(); // destroys native pools if this was their last owner
```

There is no `DescriptorSet` wrapper. Initialize empty variables with
`VK_NULL_HANDLE`. Copying, moving, assigning or discarding a handle does not
access the pool or release resources; moving a handle does not clear its source.
Overwriting or discarding a value leaves its allocation in the pool. Call
`pool->Free(set)` first when individual reclamation is required.

`Free(VkDescriptorSet&)` looks up the allocation in this pool's records and
returns it to its source native pool. A null handle is a no-op, including a
variable already cleared by a successful Free. Handles not tracked by this pool
are rejected before native calls. Native release failure throws and leaves the
handle, allocation record and retained resources unchanged, permitting retry.
Success clears only the supplied variable; every other copy becomes invalid.
An old copy is rejected while its handle value is unregistered. There is no
generation tracking: if the driver reuses the same value for a new allocation,
the pool cannot distinguish the stale copy. Callers must stop using all copies
when an allocation is freed.
Publication rollback during failed Allocate remains automatic; failure of that
rollback is unrecoverable and terminates the process.

The pool object cannot be copied or moved; its `IntrusivePtr` can be copied and
moved. The private destructor requires reference-counted ownership rather than
stack allocation or `unique_ptr`. Native handles do not hold strong references.
The caller keeps a pool owner alive for every set use and explicit Free. Releasing the last
pool owner destroys all native pages, including their outstanding allocations,
before releasing the allocations' retained image views. This does not call
`vkFreeDescriptorSets()` individually.

The user pool has no public `Destroy()`. Release an owner's reference with
`pool.reset()`; other owners can continue using it. Final pool destruction
invalidates all remaining borrowed sets, including handles still in scope.
Comparing a handle with `VK_NULL_HANDLE` only checks whether the variable is
empty; it does not track allocation validity or pool destruction. Old values
must not be used with Vulkan or passed to another pool's Free.
There is no implicit GPU wait. Complete all related GPU use before Free or final
pool destruction, and never later submit commands referencing invalidated sets.

Each allocation retains the image-view versions in its description until it is
freed. Buffer, buffer-view and acceleration-structure lifetimes remain the
caller's responsibility. Samplers borrow handles owned by the Device cache;
their wrappers must survive description parsing. Use the owning pool's Free
instead of directly freeing its native descriptor sets through Vulkan.

## Synchronization

`DescriptorPool` has no internal locks or thread affinity. Callers exclude
conflicting operations on the same pool: `Create()`, `Allocate()`, `Free()`, and
destruction through the last strong reference. Freeing different sets from one
pool still accesses the same allocation records and native pools. Atomic pool
reference counting does not synchronize these operations. Handle copies,
assignment and scope exit access no pool state; independent variables can be
manipulated on other threads without synchronizing pool access. Modification
of the same variable still requires exclusion.

Use the same external synchronization for allocation and release, for example:

```cpp
std::mutex poolMutex; // shared by every user of this pool
VkDescriptorSet set = VK_NULL_HANDLE;
{
    std::lock_guard lock(poolMutex);
    set = pool->Allocate(layout, description);
}

// Record, submit and complete all GPU uses of set before releasing it.
{
    std::lock_guard lock(poolMutex);
    pool->Free(set);
}
```

Scope exit does not release the allocation. Handing a pool or set to a different
thread is allowed once prior conflicting operations have finished. Keep an
explicit pool owner through that handoff; the borrowed set alone does not keep
the pool alive. The final owner may be released on the receiving thread.

Independent pool instances can create, allocate and release concurrently;
`Create()` stays on the pool because it uses no shared allocation cache. Keep
the Device initialized and layouts, descriptions and referenced wrappers stable
while allocating. Creating a layout through
`MyDevice::GetOrCreateDescriptorSetLayout()` requires caller exclusion on its
separate Device cache. Copying or clearing a layout value does not access that
cache; it preserves native ownership in the Device and binding metadata in the
other values.

Freeing a set also drops its retained image views. If this triggers final native
resource or memory release, callers must satisfy the synchronization contracts
of that resource and its allocator as well. A lock for one pool does not protect
other shared dependency state. Device teardown requires exclusion from all
related pool and resource operations, and the Device must outlive them.

## Internal dynamic pools

`VulkanDetail::DynamicDescriptorPool` is an internal, exclusively accessed cache
owned by `FrameSlot`. Its `GetOrAllocateTransient()` returns borrowed
`VkDescriptorSet` values and reuses
sets with equivalent layout and bindings. Its handles cannot be individually
freed through the user pool. It never frees a single native set. Its owner
completes recording and GPU use before `Reset()` or `Destroy()`; either operation invalidates
all previously borrowed sets. A failed partial reset makes the pool unusable
until destruction and recreation.

User-pool `Allocate()` creates an independent allocation for explicit `Free()`.
Dynamic-pool `GetOrAllocateTransient()` may reuse a cached allocation and returns
a handle valid until whole-pool recycling. `Transient` follows the pool's
lifecycle; it does not prescribe a one-frame lifetime.

Both types grow across multiple native pools, preserve the previous default
budgets, and enlarge a new pool to accommodate an oversized layout. User pools
enable `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`; dynamic pools do not.
Capacity/fragmentation errors try existing available pools and at most one fresh
pool per request. Other device errors propagate immediately.

Allocation validates writes and counts before calling the device. If user
allocation publication fails, it is individually rolled back. An unpublished
dynamic allocation is reclaimed at the next whole-pool reset or destruction.

The current serial preparation and parallel command recording flow is retained.
Moving the dynamic pool into `CommandPool` is a separate change.

## Header organization

The user API is declared in `my_vulkan/resource/descriptor.h`. Internal pool
pages and the dynamic pool live in their module's `private/` directory. The same
layout applies to allocators, command implementations, frame/submission internals,
and native resource nodes; private directories are not implicit include paths.
User-facing `HostFence` and `QueueDependency` are declared in
`my_vulkan/render_context/synchronization.h`.
