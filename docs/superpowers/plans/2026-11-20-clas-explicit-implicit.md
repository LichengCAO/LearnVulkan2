# CLAS Explicit and Implicit Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved CLAS/Cluster BLAS interfaces, including independent explicit owners, managed batches, address-free size queries and borrowed explicit/implicit build targets.

**Architecture:** Layouts describe budgets independently of GPU addresses. Native size queries and immutable command snapshots carry the same operation, destination mode, maximum count and budgets. Implicit batches own contiguous storage; explicit batches retain the supplied reference buffer and independent destination owners.

**Tech Stack:** C++20, Vulkan VK_NV_cluster_acceleration_structure, VMA, MSVC/CMake, existing native-dispatch regression fixtures.

**Spec:** The interface approved in this chat and committed at e23edf9; the design contract below records its acceptance criteria.

## Global Constraints

- Preserve the public interface names and signatures in e23edf9; add private storage/helpers and narrowly necessary additive address/offset accessors.
- Prefix every private member function name with a single underscore.
- Declare every function with a non-void return type using trailing return type syntax.
- Host Create and query operations are synchronous and do not submit GPU builds (the existing TemplatesAndWait factory is the explicit exception).
- Independent resource instances have no thread affinity; callers exclude conflicting access, preserve Device/allocator dependencies and complete GPU use before release.
- Preserve RAII cleanup, failure rollback, retry and empty-target creation contracts. Do not add public Destroy or deferred cleanup.
- Scratch and task-array sizes cover the maximum batch. Implicit accelerationStructureSize covers the whole contiguous output; explicit size bounds each destination. Return operation-specific output and scratch alignments.
- GPU addresses and command inputs remain borrowed; commands copy Host parameter values. Optional GPU count remains a prefix selector.
- Explicit Batch creation retains intrusive strong references to the reference Buffer and every supplied owner. CreateInfo copies pointer lists and borrows the objects until Create returns.
- Do not modify external dependencies or expand scope to COMPUTE_SIZES, MOVE_OBJECTS, template-index extraction or unrelated RHI work.

## Design contract

CLAS layouts query all four EXPLICIT/IMPLICIT DIRECT/INSTANTIATE combinations without GPU addresses or allocations. Cluster BLAS layouts query both destination modes. Invalid counts, budgets, formats, device support and enum values reject. Setters must preserve prior valid descriptions when a new value is rejected.

Individual ClusterLevelAccelStruct objects allocate one explicit destination and retain their format/topology capacity and direct/instantiate policy. BottomLevelAccelStructCreateInfo::SetClusters selects Cluster BLAS capacity instead of triangle/AABB geometry; creation queries NV cluster sizes and owns a suitable aligned Buffer. Its address is usable by cluster builds and TLAS instances; ordinary KHR object build/update paths must reject cluster-only owners rather than treating them as triangle BLAS. Saved dependencies and Buffer RAII govern release.

Implicit batches keep their existing behavior under the new names. Explicit batches validate nonempty valid compatible destinations, derive conservative per-object and total budgets from owners, retain the caller's packed reference table and its byte offset, and publish only after all validation succeeds. Creation never uploads or reads back table contents: callers ensure the table addresses refer to retained destinations and remain valid. Make nonzero offsets observable to consumers with a narrowly scoped accessor if needed.

CLAS indirect info uses one unified type. SetBuildTarget infers its operation/mode from the managed batch; the four raw setters select them explicitly. Replacing a target clears stale mode-specific destination values and snapshots budgets. Invalid replacement preserves the previous snapshot. Recording validates task/count/scratch ranges, alignment and known overlaps and forwards explicit mode as an input address array, implicit mode as storage plus an output array. It does not allocate, upload or submit.

### Task 1: Implement the complete approved CLAS path

**Files:**
- Modify: src/my_vulkan/resource/cluster_acceleration_structure.h and private/cluster_acceleration_structure.cpp.
- Modify: src/my_vulkan/resource/private/cluster_acceleration_structure_build.h.
- Modify: src/my_vulkan/resource/acceleration_structure.h and private/acceleration_structure.cpp where SetClusters changes ordinary BLAS ownership.
- Modify: src/my_vulkan/command/command_buffer.h, command_buffer_commands.cpp and private command types/recording as required by the unified CLAS indirect type.
- Test: src/test/clas_resource_tests.cpp, command_buffer_tests.cpp, clas_gpu_tests.cpp and existing AS tests/fixtures where relevant.
- Docs: docs/acceleration_structures.md and docs/resource_lifetimes.md; update other first-party references to renamed interfaces.
- A focused private helper file may be added if required to share cluster size/validation logic; keep native logic local rather than duplicating it between BLAS and CLAS.

**Interfaces:** Consume the declarations in e23edf9. Produce working definitions for all approved Layout queries, CLAS CreateInfo/owner, explicit/implicit batch creation, BottomLevelAccelStructCreateInfo::SetClusters, raw target setters, and unified CommandBuffer::BuildAccelerationStructureIndirect(CL AS info) overload (the actual type is ClusterLevelAccelStructBuildIndirectInfo).

- [x] Write regression cases before implementation. The size fixture must distinguish EXPLICIT/IMPLICIT modes and direct/instantiate operations; query without target addresses must allocate no buffers. Representative caller setup:

```cpp
ClusterLevelAccelStructBuildLayout layout;
layout.SetMaxTriangleCount(3, 6);
layout.SetMaxVertexCount(5, 12);
auto sizes = layout.QuerySizesInfo(
    ClusterLevelAccelStructBuildLayout::BuildType::EXPLICIT_INSTANTIATE, 3);
CHECK_TRUE(sizes.buildInfoBufferSize == 3 * sizeof(VkClusterAccelerationStructureInstantiateClusterInfoNV));
CHECK_TRUE(sizes.accelerationStructureAlignment == 128);
```

Cover zero/incomplete budgets, unsupported format/feature, invalid enum, independent total budgets, native zero scratch, limits and address overflow. Test implicit batch behavior after renaming. Test explicit owner/batch duplicate creation, failed creation retry, offset/usage/range rejection and strong retention using MakeIntrusive ownership with external refs released before Batch destruction. Verify owners are reclaimed after the batch and no GPU wait occurs.

- [x] Migrate test callers to the approved public interfaces, run the existing Debug build, and record the expected RED missing-definition/type-migration diagnostics. Do not claim old cached executables test the new code.

```powershell
cmake --build build --config Debug --target learn_vulkan -- /m:2 /v:minimal /fl /flp:logfile=clas-implementation-red.log
```

- [x] Implement the private state, shared native queries/validation, owner creation and rollback. Return all six AccelStructSizesInfo fields, including output alignment. Use the existing IntrusivePtr implementation for explicit Batch retention. Keep implicit scratch/storage checks and per-instance exclusion contracts.
- [x] Implement command snapshots and recording. Tests must capture native opType/opMode/count/ranges and fail if mode or operation is hardcoded. Exercise all four CLAS setters and both Cluster BLAS setters, switching modes on one info, snapshot stability after Layout mutation, rejected replacements, zero GPU count, nonzero GPU count addresses, invalid scratch alignment and known overlap.
- [x] Extend the opt-in native CLAS smoke test to execute explicit direct CLAS, explicit instantiation and explicit Cluster BLAS with caller-provided target addresses. Assert addresses match the fixed destinations, preserve the implicit tests and count-prefix checks, and use existing explicit barriers. A missing hardware extension is a reported skip, never proof of GPU success.
- [x] Run GREEN validation after building the new binary:

```powershell
cmake --build build --config Debug --target learn_vulkan -- /m:2 /v:minimal /fl /flp:logfile=clas-implementation-green.log
ctest --test-dir build -C Debug --output-on-failure -R '^learn_vulkan_tests$'
$env:LEARN_VULKAN_CLAS_GPU_TESTS = '1'
ctest --test-dir build -C Debug --output-on-failure -V -R '^learn_vulkan_tests$'
Remove-Item Env:LEARN_VULKAN_CLAS_GPU_TESTS
git diff --check
```

- [x] Update ownership and usage docs to the implemented interface names, query semantics, explicit lifetime/offset contract and observed test coverage. Self-review the diff, commit only this implementation and plan, and write the implementer report with RED/GREEN evidence and material limitations.

## Progress

- Implemented and verified on NVIDIA GeForce RTX 3060 Ti, driver 621.24, extension revision 4.
- Debug host and device suites passed. The native smoke exercised implicit count prefixes,
  explicit direct and instantiate CLAS, and dependent explicit Cluster BLAS.
