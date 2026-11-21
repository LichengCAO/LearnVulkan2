# Default inline execution and implicit cluster templates

## Authorized specification

Implement the user's plan from this chat, in the existing working tree without losing pre-existing edits. Default synchronous helpers omit Context/Queue, work from arbitrary threads, and use the COMPUTE role (graphicsAndCompute family). Explicit DeviceContext/Queue overloads remain. Public DeviceContext stays thread-affine. No public BuildTarget or unified public build-type enum.

Global constraints: first-party C++ private functions use one leading underscore; non-void functions use trailing return types; required settings use Set and optional settings with usable defaults use Customize. Resource owners have no thread affinity. Same-instance conflicts and GPU lifetimes remain the caller's responsibility. Preserve existing dirty files. Do not modify external/. Do not stage or commit user WIP.

## Task 1: Queue Host synchronization and default inline execution

First migrate the obsolete ClusterLevelAccelStructBuildInfo identifier to ClusterLevelAccelStructDirectBuildInfo in existing implementation/tests so the known draft-header compile failure no longer masks feature tests. Preserve existing data and behavior during this mechanical migration.

Implement MyDevice private synchronous execution resources (native CommandPool/CommandBuffer/fence), callable from any thread, using COMPUTE. The fixed internal integration function is `void MyDevice::_ExecuteInlineAndWait(std::function<void(VkCommandBuffer)> inRecord)`; Buffer becomes a friend so Task 2 can invoke it. It must not reuse or mutate a public DeviceContext or its frame/submission state. Resources initialize lazily, use a saved native Device, and release before Device destruction. Serialize the executor's own pool/command/fence across the entire reset-record-submit-wait operation. Release queue Host locks while waiting for completion.

Synchronize all first-party queue Host calls by actual VkQueue handle, deduplicating queue-role aliases and including present. The fixed internal integration function is `auto MyDevice::_AcquireQueueHostLock(VkQueue inQueue) const -> std::unique_lock<std::mutex>`; SubmissionManager becomes a friend for its native vkQueueSubmit2 call. MyDevice presentation and default submission use the same guard. WaitIdle locks all unique Device queues in deterministic order before vkDeviceWaitIdle; it must not impose a global lock on independent ordinary operations. External raw VkQueue users remain responsible for avoiding conflict with RHI access.

Use meaningful tests for shared-handle serialization, distinct-handle independence, default-executor reuse and completion before reset, error/retry, and arbitrary-thread use. Native/VMA mocking is allowed where no real device exists. Do not assert source text or mere getters.

## Task 2: Buffer static upload/download

Replace public CopyFromHost and do not add CopyToHost. Public default methods:

```cpp
static void UploadToDeviceInline(Buffer& inDst, VkDeviceSize inOffset, const void* inSrc, size_t inSize);
static void DownloadFromDeviceInline(const Buffer& inSrc, VkDeviceSize inOffset, void* outDst, size_t inSize);
static void UploadToDeviceInline(DeviceContext& inContext, QueueFamilyType inQueue, Buffer& inDst,
    VkDeviceSize inOffset, const void* inSrc, size_t inSize);
static void DownloadFromDeviceInline(DeviceContext& inContext, QueueFamilyType inQueue, const Buffer& inSrc,
    VkDeviceSize inOffset, void* outDst, size_t inSize);
```

Host-visible, host-coherent Buffers use direct mapped copies and must not initialize/use the default GPU executor. Other memory uses a temporary coherent staging Buffer, native GPU copy, necessary memory dependencies, and a completion wait. Default route calls the Task 1 MyDevice private executor. Explicit route uses DeviceContext::ExecuteCommandsAndWait with the requested queue, preserving frame state. Destination requires TRANSFER_DST for staging upload; source requires TRANSFER_SRC for staging download. Do not silently add usage bits. Check valid Buffer, byte range, overflow and non-null Host pointer for nonzero size; zero-size copies submit nothing. Caller ensures conflicting old uses are complete and ownership/producer synchronization is ready. No automatic cross-family transfers.

Migrate all existing first-party CopyFromHost call sites, including SBT Commit, AS uploads and tests, without propagating Context parameters through their public interfaces. Keep staging resources alive through completion. Test map offset/reuse/move/retry, range failures/zero size, missing transfer bits, explicit/default GPU round trips and arbitrary-thread use.

## Task 3: Implicit templates, per-Info encoding and independent indirect arguments

Add encoding settings to Template/Direct Info for index type (default UINT32), index stride (default tightly packed/zero), vertex format (default R32G32B32_SFLOAT); Instantiate Info gains vertex format. Keep vertex stride and final Cluster ID setters for Direct/Instantiate. Template native clusterID is always zero, with no public setter; OMM is disabled but its native index-format discriminator is valid. Geometry index is zero and existing opaque semantics remain. Flags stay zero. No optional reference vertices, bbox, position truncation, micromap, explicit output, compaction or per-template instantiation-size query.

Three independent indirect argument types:

* ClusterLevelAccelStructDirectBuildIndirectInfo
* ClusterLevelAccelStructInstantiateBuildIndirectInfo
* BottomLevelAccelStructBuildIndirectInfo

Each has SetTarget(const matching Batch&), SetBuildInfoAddress(VkDeviceAddress), SetScratchAddress(VkDeviceAddress), CustomizeBuildCountAddress(VkDeviceAddress). SetTarget copies destination addresses/sizes, capacity, scratch/layout requirements and matching native budget values; no Batch/Buffer pointer/reference is retained. Direct and Instantiate validate the target creation mode. No BuildTarget or public unified enum. CommandBuffer has three overloads, freezes all Host values, fixes native pointer members at Record, and dispatches three native task layouts/opTypes with IMPLICIT_DESTINATIONS. Direct and Instantiate select pTriangleClusters, Cluster BLAS selects pClustersBottomLevel. Count address zero builds full capacity; a supplied count is externally bounded and only its output prefix is valid.

CLAS Create snapshots useTemplate, common formats and explicit capacity budgets. Query sizes with matching opType, flags and full capacity, including Instantiate (never count=1). Keep independent instances safe under existing Buffer/VMA contracts. Batch queries provide saved formats/native budgets (by value with pNext=null), capacity/sizes and template per-element metadata.

Static Direct/Instantiate CommitToDeviceInline accepts (span<const matching Info>, const ClusterLevelAccelStructBatch& inTarget, Buffer& inoutTarget, VkDeviceSize inOffset=0). Validate mode, formats, counts, layout/range and declared budgets before one upload through Task 2. Raw template addresses are a borrowed low-level contract: caller guarantees base ID=0 and true metadata fits the declared budget; do not pretend to decode opaque native contents on Host. Existing Batch upload wrappers may delegate to the canonical encoder for compatibility. Existing BLAS source-range setter resolves and stores addresses/counts, not source ownership.

MyDevice gets default and explicit synchronous template builders:

```cpp
void CreateClusterLevelAccelStructTemplatesAndWait(std::span<const ClusterLevelAccelStructTemplateBuildInfo> inInfos,
    ClusterLevelAccelStructTemplateBatch& outBatch);
void CreateClusterLevelAccelStructTemplatesAndWait(DeviceContext& inContext, QueueFamilyType inQueue,
    std::span<const ClusterLevelAccelStructTemplateBuildInfo> inInfos, ClusterLevelAccelStructTemplateBatch& outBatch);
```

Require a nonempty valid input and empty output, a capable queue and ready inputs. Derive exact per-batch budgets, build templates with native BUILD_TRIANGLE_CLUSTER_TEMPLATE/IMPLICIT, wait and read back template addresses with Task 2 Download. Publish outBatch only after success. Own template storage/address table through Buffer RAII and CPU address/metadata vectors. GetReferenceBuffer returns const Buffer*; GetReference(index), GetCount(), GetVertexFormat(), GetIndexType(), GetTriangleCount(index), GetVertexCount(index), GetBuildLimits() are queries. Default execution uses Task 1 private executor; explicit execution uses supplied context. No public CreateAndBuildInline on TemplateBatch.

Tests cover native task bytes/ID semantics, index-format and stride choices, multiple input index Buffers, mode/format/budget failures without destination mutation, full-capacity queries, pointer-snapshot lifetime, GPU count prefix preservation, template rollback/retry and GPU Template -> multiple deformed CLAS -> Cluster BLAS. Extend existing headless GPU fixture and bind only the native functions/queues it needs. Optional GPU suite uses LEARN_VULKAN_CLAS_GPU_TESTS=1.

## Integration and acceptance

Update ownership/concurrency docs and AGENTS.md for the new approved interfaces, preserving unrelated text. Build Debug and run `ctest --test-dir build -C Debug --output-on-failure`. Run the existing headless GPU suite on an NV-capable device and explicitly report hardware/driver blockers rather than claiming unrun GPU checks pass. Complete a whole-change correctness/concurrency review and fix actionable findings. Retain no unrequested Git commits.
