# CLAS CommandInput Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [x]`) syntax for tracking.

**Goal:** Replace the old CLAS indirect descriptors with the three approved CommandInput classes, remove public Layout queries, and implement native GPU COMPUTE_SIZES.

**Architecture:** BuildLayouts describe budgets and formats only. CommandInputs own immutable-value operation/layout/capacity snapshots and borrowed GPU bindings. One private query implementation serves both resource Create paths and CommandInput Host queries; native command recording reconstructs the proper input union without allocation or submission.

**Tech Stack:** C++20, VK_NV_cluster_acceleration_structure, VMA, MSVC/CMake and existing native-dispatch/real-GPU regressions.

**Spec:** The human-approved plan in this chat (2026-11-20), reproduced in Task 1 below. Base HEAD db829d1. The pre-existing uncommitted header additions are the user's desired interface draft and must be incorporated, not reverted.

## Global Constraints

- Follow AGENTS.md: private member functions use a single underscore prefix; non-void functions use trailing return syntax; required configuration uses Set and usable-default optional configuration uses Customize.
- Local inspection/search/replacement uses FastCtx directly. No external/ edits.
- Preserve synchronous independent-instance resource creation, RAII, intrusive explicit-batch retention, creation rollback/retry, borrowed GPU addresses and caller synchronization/lifetimes.
- Preserve the existing fix that moves the binding and known destination ranges together, leaving the source unbound. Copies remain independent and self-move remains usable.
- Do not add ordinary Template build commands, MOVE/COPY, a memory pool, ray traversal, implicit barriers, automatic submit/wait, or compatibility wrappers for removed interfaces.
- Native flags remain zero as in the current CLAS interface; ordinary KHR resource behavior remains unchanged.

### Task 1: Implement and migrate all approved CommandInput behavior

**Files:** Modify resource/cluster_acceleration_structure.h and its private implementation/helper, resource/private/acceleration_structure.cpp, command/command_buffer.h, command_buffer_commands.cpp, private native command implementation, and device friends/helpers where needed. Migrate src/test/clas_resource_tests.cpp, command_buffer_tests.cpp, clas_gpu_tests.cpp and related layout tests/fixtures where affected. Update AGENTS.md and current CLAS/GPU-layout/ownership/comparison documentation. Private query/description helper files may be added if needed; reconfigure CMake if a new .cpp must enter its source glob.

**Interfaces:**

Keep and implement ComputeClusterAccelStructSizesCommandInput, BuildExplicitClusterAccelStructCommandInput and BuildImplicitClusterAccelStructCommandInput. Preserve their draft setter names/signatures except the approved changes: all three QueryCurrentTargetSizesInfo methods become const, explicit has no CustomizeOutputSizesArray, and fix inOuputAddress to inOutputAddress. Remove both old BuildIndirectInfo classes and their CommandBuffer overloads. Remove both BuildLayout QuerySizesInfo methods and BuildType enums. Layouts keep budget/format setters only. Resource GetSizesInfo getters remain.

```cpp
auto QueryCurrentTargetSizesInfo() const -> AccelStructSizesInfo;
void CommandBuffer::ComputeClusterAccelStructSizes(
    const ComputeClusterAccelStructSizesCommandInput& inInput);
void CommandBuffer::BuildAccelerationStructureIndirect(
    const BuildExplicitClusterAccelStructCommandInput& inInput);
void CommandBuffer::BuildAccelerationStructureIndirect(
    const BuildImplicitClusterAccelStructCommandInput& inInput);
```

**Binding/query contract:**

- Compute selects CLAS direct, Template, instantiate or Cluster BLAS through its four Set*Target(layout,count) methods; Build Inputs select the existing three operations in their fixed explicit/implicit mode.
- Host query requires a selected valid layout/count, but no argument, scratch or output GPU addresses. It is synchronous, const, and performs no GPU allocation/submission. Private query logic is shared by resource Create and CommandInputs; Create never depends on a CommandInput.
- Batch overloads completely snapshot budgets, operation, capacity, table offset/address, implicit storage and known destination ranges. Reject Batch destination-mode or direct/instantiate mismatches. After a Batch binding, an address setter may repeat an identical address but rejects inconsistent overrides; use a Layout overload for caller-managed addresses.
- Successful target replacement preserves argument, scratch and GPU count bindings. It clears old references, storage and sizes outputs, then applies any complete new Batch binding. Rejected replacement preserves all old state. Layout and Batch mutation/destruction never changes the copied Host snapshot, while actual resources remain caller-borrowed.
- Nonzero queried scratch requires a valid aligned address; zero native scratch permits address 0/default omission. GPU count defaults to full capacity, otherwise selects a valid prefix; no Host reading, clamping or readback is introduced.
- References contain packed VkDeviceAddress and sizes packed uint32_t. Buffers cover maximum capacity; inactive output tails are untouched. Query uses maximum count, never the GPU count's value.
- Compute Host results include whole-batch scratch, scratch alignment and task-array bytes. AS output size/alignment and update scratch are zero. Per-object size values are GPU output data, not values returned by the Host query.
- Validate representable ranges/alignment and known overlaps, including sizes outputs versus arguments, count, scratch, address tables, implicit storage and known explicit destinations where applicable. Raw addresses retain their existing caller usage/capacity/lifetime contract.
- Extend the existing native command to COMPUTE_SIZES and Template sizing. Correctly choose task stride and native limits union, reconstruct pointers at Record, and preserve compute-queue/outside-renderpass and CLAS pipeline checks. Explicit keeps dstSizesArray absent; implicit has optional sizes output; compute requires sizes output and has no AS destination.

**TDD and implementation steps:**

- [x] Add behavioral regressions and migrate test callers first. A representative address-free query is:

```cpp
ClusterLevelAccelStructBuildLayout layout;
layout.SetMaxTriangleCount(3, 6);
layout.SetMaxVertexCount(5, 12);
ComputeClusterAccelStructSizesCommandInput input;
input.SetClusterTarget(layout, 3);
const auto& configured = input;
const auto sizes = configured.QueryCurrentTargetSizesInfo();
CHECK_TRUE(sizes.accelerationStructureSize == 0 && sizes.accelerationStructureAlignment == 0);
CHECK_TRUE(sizes.updateScratchBufferSize == 0);
CHECK_TRUE(sizes.buildInfoBufferSize == 3 * sizeof(VkClusterAccelerationStructureBuildTriangleClusterInfoNV));
```

The fixture must distinguish four compute operations and three operations in each build mode. Check no GPU buffers/submissions occur during Host queries. Include invalid/unbound descriptions, positive independent budgets, zero scratch, mismatched Batch bindings, same/different address overrides, preserved common bindings and cleared target outputs on rebind, full and GPU-prefix counts, snapshot mutation, offset preservation, ranges and overlaps, and all existing copy/move/self-move tests under the new types.

- [x] Build and run the relevant test on the fresh binary to observe RED; record whether the first expected failure is unresolved new symbols, removed API references or a behavioral native-mode assertion. Do not validate with an old cached executable.
- [x] Implement common private descriptions/query logic and migrate every resource Create away from public Layout queries. Implement the three public input types and replace the old command overloads. Reuse existing validation and native command code rather than adding a second dispatch path.
- [x] Run focused tests through GREEN. Then extend the real GPU smoke: initialize size arrays with sentinels; compute for counts 0, 1 and capacity, verify positive active output values and untouched tails. Exercise all four compute operations with valid native task records. Build the source Template before instantiate sizing and source CLAS before Cluster BLAS sizing. Use computed, correctly aligned per-object sizes to allocate raw destinations for a subsequent explicit build and dependent consumption. Preserve existing explicit/implicit/count-prefix smoke cases and add optional implicit size-output verification. No claim about final BLAS contents or ray traversal is required.
- [x] Migrate current documentation and check there are no active references to removed public types, Layout query methods or their nested BuildType enums. Historical plan/lesson artifacts may remain historical; do not rewrite unrelated records.
- [x] Run full final verification from repository root:

```powershell
cmake --build build --config Debug -- /m:2 /v:minimal /fl /flp:logfile=clas-command-inputs-build.log
ctest --test-dir build -C Debug --output-on-failure --output-log build/clas-command-inputs-suite.log
$env:LEARN_VULKAN_CLAS_GPU_TESTS = '1'
ctest --test-dir build -C Debug --output-on-failure -V -R '^learn_vulkan_tests$' --output-log build/clas-command-inputs-gpu.log
Remove-Item Env:LEARN_VULKAN_CLAS_GPU_TESTS
git diff --check
```

- [x] Self-review changes against this spec, commit the implementation and this plan (including the authorized pre-existing header draft), write the implementer report with exact RED/GREEN evidence/log paths, and hand over for independent review. Do not push, merge or spawn your own reviewers.
