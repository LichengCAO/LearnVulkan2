#pragma once
#include "common.h"
#include "command_buffer.h"
#include "my_vulkan/pipeline/render_pass.h"

#include <unordered_set>

class Buffer;
class Image;
class CommandBuffer;
class Command;
class RenderGraphInstance;
struct RenderGraphTestProbe;

class RenderGraph
{
	friend class RenderGraphInstance;
	friend struct RenderGraphTestProbe;

public:
	struct ImageSubresourceRange
	{
		ImageSubresourceRange() = default;
		ImageSubresourceRange(uint32_t inMipLevel, uint32_t inArrayLayer);

		auto operator==(const ImageSubresourceRange& inOther) const->bool;
		auto Overlap(const ImageSubresourceRange& inOther) const->bool;
		auto Intersect(const ImageSubresourceRange& inOther) const->ImageSubresourceRange;

		uint32_t baseMipLevel = 0;
		uint32_t levelCount = 0;
		uint32_t baseArrayLayer = 0;
		uint32_t layerCount = 0;
	};

private:
	static constexpr uint32_t INVALID_INDEX = ~0u;

	using BufferIndex = uint32_t;
	using ImageIndex = uint32_t;
	using PassIndex = uint32_t;

	enum class PassType
	{
		COMPUTE,  // Owns compute queue commands
		GRAPHICS, // Owns graphics queue commands, manages render pass itself
		SUBPASS,  // Owns graphics queue commands in a subpass, let graph manage render pass for it
	};

	enum class QueueType
	{
		GRAPHICS,
		COMPUTE,
	};

	enum class ResourceType
	{
		BUFFER,
		IMAGE,
	};

	enum class ResourceUsageType
	{
		SAMPLED_IMAGE,
		STORAGE_IMAGE,
		COLOR_ATTACHMENT,
		DEPTH_ATTACHMENT,
		UNIFORM_BUFFER,
		STORAGE_BUFFER,
	};

	enum class HazardType
	{
		RAW,
		WAR,
		WAW,
	};

	struct ImageUsage
	{
		std::string image;
		ImageIndex imageIndex = INVALID_INDEX;
		ImageSubresourceRange subresourceRange;
		ResourceUsageType type = ResourceUsageType::SAMPLED_IMAGE;
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2 stage = 0;
		VkAccessFlags2 access = 0;
		bool reads = false;
		bool writes = false;
		VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	};

	struct BufferUsage
	{
		std::string buffer;
		BufferIndex bufferIndex = INVALID_INDEX;
		VkPipelineStageFlags2 stage = 0;
		VkAccessFlags2 access = 0;
		bool reads = false;
		bool writes = false;
	};

	struct AccessState
	{
		VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2 stage = 0;
		VkAccessFlags2 access = 0;
		bool reads = false;
		bool writes = false;
	};

	struct BarrierPlan
	{
		ResourceType resourceType = ResourceType::IMAGE;
		ImageIndex image = INVALID_INDEX;
		ImageIndex sourceImage = INVALID_INDEX;
		BufferIndex buffer = INVALID_INDEX;
		BufferIndex sourceBuffer = INVALID_INDEX;
		ImageSubresourceRange subresourceRange;
		PassIndex before = INVALID_INDEX;
		PassIndex after = INVALID_INDEX;
		// External barriers use INVALID_INDEX on before/after to mark entering/leaving graph boundaries.
		bool external = false;
	};

	struct QueueSyncPlan
	{
		PassIndex before = INVALID_INDEX;
		PassIndex after = INVALID_INDEX;
		QueueType srcQueue = QueueType::GRAPHICS;
		QueueType dstQueue = QueueType::GRAPHICS;
	};

	struct PassRecord
	{
		std::string name;
		PassType type = PassType::GRAPHICS;
		QueueType queue = QueueType::GRAPHICS;
		bool useDedicatedRenderPass = false;
		bool neverCull = false;
		bool active = false;
		std::vector<ImageUsage> imageUsages;
		std::vector<BufferUsage> bufferUsages;
		std::vector<PassIndex> adjacency;
	};

	struct DependencyEdge
	{
		PassIndex before = INVALID_INDEX;
		PassIndex after = INVALID_INDEX;
	};

	struct SubmitBatch
	{
		struct PassGroupPlan
		{
			QueueType queue = QueueType::GRAPHICS;
			std::vector<PassIndex> passes;
			bool managedRenderPass = false;
			std::vector<BarrierPlan> prologueBarriers;
			std::vector<BarrierPlan> epilogueBarriers;
			std::vector<BarrierPlan> queueReleaseBarriers;
			std::vector<BarrierPlan> subpassDependencies;
			std::vector<QueueSyncPlan> queueSignalPlans;
			std::vector<QueueSyncPlan> queueWaitPlans;
		};

		std::vector<PassGroupPlan> graphicsGroups;
		std::vector<PassGroupPlan> computeGroups;

		template <typename FunctionType>
		void ForEachGroup(FunctionType&& inFunction)
		{
			for (PassGroupPlan& group : graphicsGroups)
			{
				inFunction(group);
			}
			for (PassGroupPlan& group : computeGroups)
			{
				inFunction(group);
			}
		}

		template <typename FunctionType>
		void ForEachGroup(FunctionType&& inFunction) const
		{
			for (const PassGroupPlan& group : graphicsGroups)
			{
				inFunction(group);
			}
			for (const PassGroupPlan& group : computeGroups)
			{
				inFunction(group);
			}
		}
	};

	class BuildResult
	{
		friend class RenderGraph;

	private:
		std::vector<PassRecord> passes;
		std::vector<SubmitBatch> submitBatches;
		std::vector<BufferIndex> bufferAliasRoots;
		std::vector<ImageIndex> imageAliasRoots;

	public:
		auto GetPassCount() const->size_t;
		auto GetPass(PassIndex inPassIndex) const->const PassRecord&;
		auto GetSubmitBatchCount() const->size_t;
		auto GetSubmitBatch(uint32_t inSubmitIndex) const->const SubmitBatch&;
		auto GetBufferAliasRoot(BufferIndex inBufferIndex) const->BufferIndex;
		auto GetImageAliasRoot(ImageIndex inImageIndex) const->ImageIndex;
		auto GetImageAccessState(
			PassIndex inPassIndex,
			ImageIndex inImageIndex,
			const ImageSubresourceRange& inSubresourceRange) const->AccessState;
		auto GetBufferAccessState(PassIndex inPassIndex, BufferIndex inBufferIndex) const->AccessState;
	};

	struct BuildContext
	{
		struct ImageUsageRef
		{
			PassIndex pass = INVALID_INDEX;
			ImageSubresourceRange subresourceRange;
			ResourceUsageType type = ResourceUsageType::SAMPLED_IMAGE;
			bool reads = false;
			bool writes = false;
			VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;

			ImageUsageRef() = default;
			ImageUsageRef(PassIndex inPass, const ImageUsage& inUsage)
				: pass(inPass),
				subresourceRange(inUsage.subresourceRange),
				type(inUsage.type),
				reads(inUsage.reads),
				writes(inUsage.writes),
				loadOp(inUsage.loadOp)
			{
			}
		};

		struct BufferUsageRef
		{
			PassIndex pass = INVALID_INDEX;
			bool reads = false;
			bool writes = false;

			BufferUsageRef() = default;
			BufferUsageRef(PassIndex inPass, const BufferUsage& inUsage)
				: pass(inPass),
				reads(inUsage.reads),
				writes(inUsage.writes)
			{
			}
		};

		struct ImageRecord
		{
			std::vector<ImageUsageRef> usages;
		};

		struct BufferRecord
		{
			std::vector<BufferUsageRef> usages;
		};

		std::vector<PassRecord> passes;
		std::vector<QueueSyncPlan> queueSyncPlans;
		std::vector<ImageRecord> images;
		std::vector<BufferRecord> buffers;

	};

public:
	class BufferInfo final
	{
		friend class RenderGraph;
		friend class RenderGraphInstance;

	private:
		std::string m_name;
		VkDeviceSize m_size = 0;
		VkBufferUsageFlags m_usage = 0;
		VkMemoryPropertyFlags m_memoryProperty = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
		VkSharingMode m_sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		std::optional<VkDeviceSize> m_optAlignment;
		bool m_external = false;

		auto IsAliasCompatible(const BufferInfo& inOther) const->bool;

	public:
		void SetSize(VkDeviceSize inSize)
		{
			CHECK_TRUE(inSize > 0, "Render graph buffer size must be greater than 0!");
			m_size = inSize;
		};
		void AddUsage(VkBufferUsageFlags inUsage)
		{
			CHECK_TRUE(inUsage != 0, "Render graph buffer usage cannot be empty!");
			m_usage |= inUsage;
		};
		void CustomizeMemoryProperty(VkMemoryPropertyFlags inMemoryProperty)
		{
			CHECK_TRUE(inMemoryProperty != 0, "Render graph buffer memory property cannot be empty!");
			m_memoryProperty = inMemoryProperty;
		};
		void CustomizeSharingMode(VkSharingMode inSharingMode)
		{
			m_sharingMode = inSharingMode;
		};
		void CustomizeAlignment(VkDeviceSize inAlignment)
		{
			CHECK_TRUE(inAlignment > 0, "Render graph buffer alignment must be greater than 0!");
			m_optAlignment = inAlignment;
		};
		void SetAsExternal()
		{
			m_external = true;
		};
	};

	class ImageInfo final
	{
		friend class RenderGraph;
		friend class RenderGraphInstance;

	private:
		std::string m_name;
		VkImageUsageFlags m_usage = 0;
		VkImageType m_type = VK_IMAGE_TYPE_2D;
		std::optional<uint32_t> m_optWidth;
		std::optional<uint32_t> m_optHeight;
		std::optional<uint32_t> m_optDepth;
		uint32_t m_mipLevels = 1;
		uint32_t m_arrayLayers = 1;
		std::optional<VkFormat> m_optFormat;
		std::optional<VkImageTiling> m_optTiling;
		std::optional<VkMemoryPropertyFlags> m_optMemoryProperty;
		std::optional<VkSampleCountFlagBits> m_optSampleCount;
		bool m_external = false;

		auto GetWholeSubresourceRange() const->ImageSubresourceRange;
		auto NormalizeSubresourceRange(const ImageSubresourceRange& inRange) const->ImageSubresourceRange;
		auto IsAliasCompatible(const ImageInfo& inOther) const->bool;

	public:
		void AddUsage(VkImageUsageFlags inUsage)
		{
			CHECK_TRUE(inUsage != 0, "Render graph image usage cannot be empty!");
			m_usage |= inUsage;
		};
		void CustomizeSize1D(uint32_t inWidth)
		{
			CHECK_TRUE(inWidth > 0, "Render graph image width must be greater than 0!");
			m_type = VK_IMAGE_TYPE_1D;
			m_optWidth = inWidth;
			m_optHeight.reset();
			m_optDepth.reset();
		};
		void CustomizeSize2D(uint32_t inWidth, uint32_t inHeight)
		{
			CHECK_TRUE(inWidth > 0 && inHeight > 0, "Render graph image size must be greater than 0!");
			m_type = VK_IMAGE_TYPE_2D;
			m_optWidth = inWidth;
			m_optHeight = inHeight;
			m_optDepth.reset();
		};
		void CustomizeSize3D(uint32_t inWidth, uint32_t inHeight, uint32_t inDepth)
		{
			CHECK_TRUE(inWidth > 0 && inHeight > 0 && inDepth > 0, "Render graph image size must be greater than 0!");
			m_type = VK_IMAGE_TYPE_3D;
			m_optWidth = inWidth;
			m_optHeight = inHeight;
			m_optDepth = inDepth;
		};
		void CustomizeMipLevels(uint32_t inMipLevelCount)
		{
			CHECK_TRUE(inMipLevelCount > 0, "Render graph image mip level count must be greater than 0!");
			m_mipLevels = inMipLevelCount;
		};
		void CustomizeArrayLayers(uint32_t inArrayLayerCount)
		{
			CHECK_TRUE(inArrayLayerCount > 0, "Render graph image array layer count must be greater than 0!");
			m_arrayLayers = inArrayLayerCount;
		};
		void CustomizeFormat(VkFormat inFormat)
		{
			CHECK_TRUE(inFormat != VK_FORMAT_UNDEFINED, "Render graph image format cannot be undefined!");
			m_optFormat = inFormat;
		};
		void CustomizeImageTiling(VkImageTiling inTiling)
		{
			m_optTiling = inTiling;
		};
		void CustomizeMemoryProperty(VkMemoryPropertyFlags inMemoryProperty)
		{
			CHECK_TRUE(inMemoryProperty != 0, "Render graph image memory property cannot be empty!");
			m_optMemoryProperty = inMemoryProperty;
		};
		void CustomizeSampleCount(VkSampleCountFlagBits inSampleCount)
		{
			m_optSampleCount = inSampleCount;
		};
		void SetAsExternal()
		{
			m_external = true;
		};
	};

	class SubpassInfo;

	class PassInfo
	{
		friend class RenderGraph;
		friend class SubpassInfo;

	private:
		std::vector<ImageUsage> m_imageUsages;
		std::vector<BufferUsage> m_bufferUsages;
		bool m_neverCull = false;

	public:
		virtual ~PassInfo() = default;
		void SetNeverCull(bool inNeverCull = true);
		void AddSampledImage(
			const std::string& inName,
			VkPipelineStageFlags2 inReadStage,
			const ImageSubresourceRange& inRange = {});
		void AddStorageImage(
			const std::string& inName,
			VkPipelineStageFlags2 inWriteStage,
			const ImageSubresourceRange& inRange = {});
		void AddDescriptorUniformBuffer(const std::string& inName, VkPipelineStageFlags2 inReadStage);
		void AddDescriptorStorageBuffer(const std::string& inName, VkPipelineStageFlags2 inWriteStage);

	private:
		virtual auto GetType() const-> PassType = 0;
	};

	class ComputePassInfo final : public PassInfo
	{
	public:
		explicit ComputePassInfo() = default;

	private:
		virtual auto GetType() const-> PassType override { return PassType::COMPUTE; }
	};

	class GraphicsPassInfo final : public PassInfo
	{
	public:
		explicit GraphicsPassInfo() = default;

	private:
		virtual auto GetType() const-> PassType override { return PassType::GRAPHICS; }
	};

	class SubpassInfo final : public PassInfo
	{
		friend class RenderGraph;

	private:
		bool m_useDedicatedRenderPass = false;

	public:
		explicit SubpassInfo() = default;

	private:
		virtual auto GetType() const-> PassType override { return PassType::SUBPASS; }

	public:
		void UseDedicateRenderPass();
		void AddColorAttachment(
			const std::string& inName,
			VkAttachmentLoadOp inLoadOp,
			VkPipelineStageFlags2 inLoadStage,
			VkAttachmentStoreOp inStoreOp,
			VkPipelineStageFlags2 inStoreStage,
			const ImageSubresourceRange& inRange = {});
		void AddDepthAttachment(
			const std::string& inName,
			VkAttachmentLoadOp inLoadOp,
			VkPipelineStageFlags2 inLoadStage,
			VkAttachmentStoreOp inStoreOp,
			VkPipelineStageFlags2 inStoreStage,
			const ImageSubresourceRange& inRange = {});
	};

private:
	std::vector<BufferInfo> m_buffers;
	std::vector<ImageInfo> m_images;
	std::vector<PassRecord> m_passes;
	std::vector<DependencyEdge> m_extraDependencies;
	std::unordered_map<std::string, BufferIndex> m_nameToBuffer;
	std::unordered_map<std::string, ImageIndex> m_nameToImage;
	std::unordered_map<std::string, PassIndex> m_nameToPass;
	BuildResult m_buildResult;
	bool m_enableResourceAliasing = true;
	bool m_built = false;

private:
	static auto _GetQueueType(PassType inType) -> QueueType;
	static auto _NeedsMemoryDependency(HazardType inHazard, VkImageLayout inOldLayout, VkImageLayout inNewLayout) -> bool;
	auto _GetBufferIndex(const std::string& inName) const->BufferIndex;
	auto _GetImageIndex(const std::string& inName) const->ImageIndex;
	auto _GetPassIndex(const std::string& inName) const->PassIndex;
	void _InvalidateBuild();
	void _LinkPasses(BuildContext& inContext) const;
	void _ResolveDependency(BuildContext& inContext) const;
	void _CullPasses(BuildContext& inContext) const;
	void _BuildScheduleAndBatches(BuildContext& inContext, BuildResult& inoutResult) const;
	void _BuildResourceAliases(BuildContext& inContext, BuildResult& inoutResult) const;
	void _BuildScheduledResourceBarriers(BuildContext& inContext, BuildResult& inoutResult) const;

public:
	void AddBuffer(const std::string& inName, const RenderGraph::BufferInfo& inBufferInfo);
	void AddImage(const std::string& inName, const RenderGraph::ImageInfo& inImageInfo);
	void AddPass(const std::string& inName, const RenderGraph::PassInfo& inPassInfo);
	void AddExtraPassDependency(const std::string& inHappensSooner, const std::string& inHappensLater);
	void EnableResourceAliasing(bool inEnable);
	void Build();
};

class RenderGraphInstance
{
	friend struct RenderGraphTestProbe;

private:
	using BufferIndex = RenderGraph::BufferIndex;
	using ImageIndex = RenderGraph::ImageIndex;
	using PassIndex = RenderGraph::PassIndex;

public:
	struct ExternalBufferInfo
	{
		Buffer* pBuffer{};
		VkPipelineStageFlags2 enteringStage = 0;
		VkAccessFlags2 enteringAccess = 0;
		VkPipelineStageFlags2 leavingStage = 0;
		VkAccessFlags2 leavingAccess = 0;
	};

	struct ExternalImageInfo
	{
		Image* pImage{};
		VkImageLayout enteringLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2 enteringStage = 0;
		VkAccessFlags2 enteringAccess = 0;
		VkImageLayout leavingLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2 leavingStage = 0;
		VkAccessFlags2 leavingAccess = 0;
	};

	class ExecutionContext
	{
		friend class RenderGraphInstance;

	private:
		RenderGraphInstance* m_pInstance = nullptr;
		CommandBuffer* m_pCommandBuffer = nullptr;
		CommandBuffer::PrimaryScope* m_pPrimaryScope = nullptr;
		CommandBuffer::RenderPassScope* m_pRenderPassScope = nullptr;
		std::unordered_map<PassIndex, size_t> m_passToSubpass;

	private:
		ExecutionContext() = default;

	public:
		auto ResolveBuffer(const std::string& inName) -> Buffer*;
		auto ResolveImage(const std::string& inName) -> Image*;
		void FillSubpassCommands(const std::string& inTarget, std::vector<const Command*> inCommands);
		void RecordCommandBuffer(const std::string& inTarget, std::function<void(CommandBuffer*)> inProcess);
	};

	class PassInfo
	{
		friend class RenderGraphInstance;

	private:
		std::function<void(RenderGraphInstance::ExecutionContext&)> m_process;

	public:
		void SetProcess(std::function<void(RenderGraphInstance::ExecutionContext&)> inProcess);
	};

private:
	static constexpr uint32_t INVALID_INDEX = RenderGraph::INVALID_INDEX;

	struct TemporaryRenderPass
	{
		std::vector<PassIndex> passes;
		std::unique_ptr<RenderPass> renderPass;
		std::unique_ptr<Framebuffer> framebuffer;
		VkRect2D renderArea{};
	};

	struct CompiledPassGroup
	{
		RenderGraph::QueueType queue = RenderGraph::QueueType::GRAPHICS;
		std::vector<PassIndex> passes;
		uint32_t temporaryRenderPass = INVALID_INDEX;
		std::vector<std::unique_ptr<Command>> prologueCommands;
		std::vector<std::unique_ptr<Command>> epilogueCommands;
		std::vector<std::unique_ptr<Command>> queueReleaseCommands;
	};

	struct CompiledQueueWait
	{
		uint32_t syncEdge = INVALID_INDEX;
		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	};

	struct CompiledQueueSyncEdge
	{
		uint32_t srcSubmit = INVALID_INDEX;
		uint32_t dstSubmit = INVALID_INDEX;
		RenderGraph::QueueType srcQueue = RenderGraph::QueueType::GRAPHICS;
		RenderGraph::QueueType dstQueue = RenderGraph::QueueType::GRAPHICS;
		VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
	};

	struct CompiledSubmitBatch
	{
		std::vector<CompiledPassGroup> graphicsGroups;
		std::vector<CompiledPassGroup> computeGroups;
		std::vector<uint32_t> graphicsSignalSyncs;
		std::vector<uint32_t> computeSignalSyncs;
		std::vector<CompiledQueueWait> graphicsWaitSyncs;
		std::vector<CompiledQueueWait> computeWaitSyncs;
	};

	struct CompiledGraphPlan
	{
		std::vector<CompiledSubmitBatch> submitBatches;
		std::vector<CompiledQueueSyncEdge> queueSyncEdges;
	};

	enum class BarrierCommandMode
	{
		NORMAL,
		QUEUE_RELEASE,
		QUEUE_ACQUIRE,
	};

	const RenderGraph* m_pRenderGraph = nullptr;
	std::vector<std::unique_ptr<Buffer>> m_internalBuffers;
	std::vector<std::unique_ptr<Image>> m_internalImages;
	std::vector<Buffer*> m_buffers;
	std::vector<Image*> m_images;
	std::vector<std::optional<ExternalBufferInfo>> m_externalBufferInfos;
	std::vector<std::optional<ExternalImageInfo>> m_externalImageInfos;
	std::unordered_map<std::string, Buffer*> m_nameToBuffer;
	std::unordered_map<std::string, Image*> m_nameToImage;
	std::vector<PassInfo> m_passInfos;
	std::vector<TemporaryRenderPass> m_temporaryRenderPasses;
	std::vector<std::vector<uint32_t>> m_graphicsBatchToTemporaryRenderPass;
	CompiledGraphPlan m_compiledPlan;
	std::vector<VkSemaphore> m_freeSemaphores;
	std::vector<VkSemaphore> m_executeSemaphores;
	bool m_compiled = false;

private:
	void _DestroyTemporaryRenderPasses();
	void _DestroyInternalResources();
	void _SetUpPhysicalResources();
	void _CreateTemporaryRenderPasses();
	void _BuildCompiledGraphPlan();
	auto _GetTemporaryRenderPass(uint32_t inSubmitIndex, uint32_t inGraphicsBatchIndex)->TemporaryRenderPass*;
	auto _GetBuffer(const std::string& inName) const->Buffer*;
	auto _GetImage(const std::string& inName) const->Image*;
	void _AppendPassCommands(PassIndex inPassIndex, CommandBuffer::PrimaryScope& inPrimaryScope);
	void _AppendRenderPassCommands(const std::vector<PassIndex>& inPasses, const TemporaryRenderPass& inRenderPass, CommandBuffer& inCommandBuffer);
	void _RecordSubpassCommandBuffer(PassIndex inPassIndex, std::function<void(CommandBuffer*)> inProcess, ExecutionContext& inContext);
	auto _AcquireSemaphore()->VkSemaphore;
	void _RecycleExecuteSemaphores();
	auto _CreateBarrierCommand(
		const std::vector<RenderGraph::BarrierPlan>& inBarrierPlans,
		BarrierCommandMode inMode = BarrierCommandMode::NORMAL)->std::unique_ptr<Command>;

public:
	RenderGraphInstance(const RenderGraph& inRenderGraph);
	~RenderGraphInstance();
	void SetUpExternalBuffer(const std::string& inName, const RenderGraphInstance::ExternalBufferInfo& inBufferInfo);
	void SetUpExternalImage(const std::string& inName, const RenderGraphInstance::ExternalImageInfo& inImageInfo);
	void SetUpPass(const std::string& inName, const RenderGraphInstance::PassInfo& inPassInfo);
	void Compile();
	void Execute();
};
