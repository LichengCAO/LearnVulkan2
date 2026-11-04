# Descriptor pools and DescriptorSet ownership

`DescriptorPool` is a reference-counted owner of multiple Vulkan descriptor pools.
Create it with `MakeIntrusive<DescriptorPool>()`. It allocates independent,
move-only `DescriptorSet` objects that keep the pool alive:

```cpp
auto pool = MakeIntrusive<DescriptorPool>();
pool->Create();

{
    auto set = pool->Allocate(layout, description);
    const auto nativeSet = set.GetVkDescriptorSet(); // borrowed
    pool.reset(); // set retains the pool independently
    // Record and submit commands using nativeSet, then wait for their GPU use.
} // frees the set, then destroys its pool if this was the last reference
```

`DescriptorSet` has no `Create` or `Destroy`. A default-constructed or moved-from
set is empty; inspect it with `IsValid()`. Assigning `{}` releases a set early.
Move assignment releases its previous allocation before taking the new one;
self-move leaves the set unchanged. Destruction and moves are `noexcept`.
An unrecoverable release failure reports an error and terminates the process.

The pool object cannot be copied or moved; its `IntrusivePtr` can be copied and
moved. The private destructor requires reference-counted ownership rather than
stack allocation or `unique_ptr`. Sets hold strong references, so dropping the
user's last pool reference leaves existing sets valid. Each set frees its native
allocation before releasing that reference, and the last reference destroys the
now-empty pool. Pool allocation records do not own the set wrappers, so this
creates no reference cycle.

The user pool has no public `Destroy()`. Release an owner's reference with
`pool.reset()`; other owners can continue allocating, and live sets retain the
pool until they are released. The final strong reference destroys all native
pools. There is no implicit GPU wait. The caller must finish GPU use before
releasing a set and must not later submit recorded commands that reference that
freed set.

Each allocation retains the image-view versions in its description until it is
freed. Buffer, buffer-view, sampler and acceleration-structure lifetimes remain
the caller's responsibility, as before. The native getter does not transfer
ownership and must not be used to free the allocation separately.

After `Create()`, allocations and releases of distinct sets in a shared user
pool are synchronized internally. `Create()` and operations on the
same set require caller-side exclusion. Layouts, descriptions and their resources
must remain stable while allocating. This contract does not make other RHI
creation caches concurrently writable.

## Internal dynamic pools

`VulkanDetail::DynamicDescriptorPool` is an internal, exclusively accessed cache
owned by `FrameSlot`. It returns borrowed `VkDescriptorSet` values and reuses
sets with equivalent layout and bindings. It does not create user-owned
`DescriptorSet` objects and never frees a single native set. Its owner completes
recording and GPU use before `Reset()` or `Destroy()`; either operation invalidates
all previously borrowed sets. A failed partial reset makes the pool unusable
until destruction and recreation.

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
