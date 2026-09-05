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

	// These nested resource descriptors are defined below, after the build-only
	// structures that store them by value. Keep their forward declarations public
	// so their access level is consistent with the definitions.
	class BufferInfo;
	class ImageInfo;

private:
	static constexpr uint32_t INVALID_INDEX = ~0u;

	using BufferIndex = uint32_t;
	using ImageIndex = uint32_t;
	using PassIndex = uint32_t;

	enum class PassType
	{
		COMPUTE,  // Owns compute queue commands
		GRAPHICS, // Owns opaque graphics queue commands and manages any render pass itself
		RENDER_PASS, // Owns one graph-managed render pass
		SUBPASS,  // Owns graphics commands in a graph-managed, mergeable subpass
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
		DEPTH_STENCIL_ATTACHMENT,
		RESOLVE_ATTACHMENT,
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
		uint32_t attachmentSlot = INVALID_INDEX;
		VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		VkClearColorValue clearColor{};
		VkClearDepthStencilValue clearDepthStencil{ 1.0f, 0 };
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
		BufferIndex buffer = INVALID_INDEX;
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
		struct ManagedAttachmentPlan
		{
			ImageIndex image = INVALID_INDEX;
			ImageSubresourceRange subresourceRange;
			ResourceUsageType role = ResourceUsageType::COLOR_ATTACHMENT;
			VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
			VkAttachmentStoreOp stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
			VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkImageLayout finalLayout = VK_IMAGE_LAYOUT_UNDEFINED;
			VkClearValue clearValue{};
		};

		struct ManagedSubpassPlan
		{
			PassIndex pass = INVALID_INDEX;
			std::vector<uint32_t> colorAttachments;
			std::vector<uint32_t> resolveAttachments;
			std::optional<uint32_t> depthStencilAttachment;
			std::vector<uint32_t> preserveAttachments;
		};

		struct ManagedRenderPassPlan
		{
			std::vector<ManagedAttachmentPlan> attachments;
			std::vector<ManagedSubpassPlan> subpasses;
			std::vector<BarrierPlan> dependencies;
		};

		struct PassGroupPlan
		{
			QueueType queue = QueueType::GRAPHICS;
			std::vector<PassIndex> passes;
			bool managedRenderPass = false;
			std::optional<ManagedRenderPassPlan> renderPassPlan;
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
		friend struct RenderGraphTestProbe;

	private:
		std::vector<PassRecord> passes;
		std::vector<SubmitBatch> submitBatches;
		std::vector<BufferInfo> buffers;
		std::vector<ImageInfo> images;
		std::unordered_map<std::string, BufferIndex> nameToBuffer;
		std::unordered_map<std::string, ImageIndex> nameToImage;
		std::unordered_map<std::string, PassIndex> nameToPass;
		bool valid = false;

	public:
		auto IsValid() const->bool;
		auto GetPassCount() const->size_t;
		auto GetPass(PassIndex inPassIndex) const->const PassRecord&;
		auto GetSubmitBatchCount() const->size_t;
		auto GetSubmitBatch(uint32_t inSubmitIndex) const->const SubmitBatch&;
		auto GetBufferCount() const->size_t;
		auto GetImageCount() const->size_t;
		auto GetBufferInfo(BufferIndex inBufferIndex) const->const BufferInfo&;
		auto GetImageInfo(ImageIndex inImageIndex) const->const ImageInfo&;
		auto GetBufferIndex(const std::string& inName) const->BufferIndex;
		auto GetImageIndex(const std::string& inName) const->ImageIndex;
		auto GetPassIndex(const std::string& inName) const->PassIndex;
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
			VkAttachmentLoadOp stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;

			ImageUsageRef() = default;
			ImageUsageRef(PassIndex inPass, const ImageUsage& inUsage)
				: pass(inPass),
				subresourceRange(inUsage.subresourceRange),
				type(inUsage.type),
				reads(inUsage.reads),
				writes(inUsage.writes),
				loadOp(inUsage.loadOp),
				stencilLoadOp(inUsage.stencilLoadOp)
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
		std::vector<ImageInfo> imageInfos;
		std::vector<BufferInfo> bufferInfos;
		std::unordered_map<std::string, ImageIndex> nameToImage;
		std::unordered_map<std::string, BufferIndex> nameToBuffer;
		std::vector<DependencyEdge> extraDependencies;
		std::vector<ImageIndex> imageAliasRoots;
		std::vector<BufferIndex> bufferAliasRoots;
		std::vector<ImageIndex> logicalToPhysicalImages;
		std::vector<BufferIndex> logicalToPhysicalBuffers;
		bool enableResourceAliasing = true;
		bool aliasesMaterialized = false;

	};

public:
	class AttachmentPassInfo;

	class AttachmentInfo final
	{
		friend class AttachmentPassInfo;

	private:
		ImageSubresourceRange m_subresourceRange;
		VkAttachmentLoadOp m_loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VkAttachmentStoreOp m_storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		VkAttachmentLoadOp m_stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		VkAttachmentStoreOp m_stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		VkClearColorValue m_clearColor{};
		VkClearDepthStencilValue m_clearDepthStencil{ 1.0f, 0 };

	public:
		void SetSubresourceRange(const ImageSubresourceRange& inRange) { m_subresourceRange = inRange; }
		void SetLoadStoreOperations(VkAttachmentLoadOp inLoadOp, VkAttachmentStoreOp inStoreOp)
		{
			m_loadOp = inLoadOp;
			m_storeOp = inStoreOp;
		}
		void SetStencilLoadStoreOperations(VkAttachmentLoadOp inLoadOp, VkAttachmentStoreOp inStoreOp)
		{
			m_stencilLoadOp = inLoadOp;
			m_stencilStoreOp = inStoreOp;
		}
		void SetClearColor(const VkClearColorValue& inClearColor) { m_clearColor = inClearColor; }
		void SetClearDepthStencil(const VkClearDepthStencilValue& inClearDepthStencil) { m_clearDepthStencil = inClearDepthStencil; }
	};

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
		friend struct RenderGraphTestProbe;

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

	class PassInfo
	{
		friend class RenderGraph;
		friend class AttachmentPassInfo;

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

	class AttachmentPassInfo : public PassInfo
	{
	public:
		void AddColorAttachment(
			uint32_t inLocation,
			const std::string& inName,
			const AttachmentInfo& inAttachmentInfo = {});
		void SetDepthStencilAttachment(
			const std::string& inName,
			const AttachmentInfo& inAttachmentInfo = {});
		void SetResolveAttachment(
			uint32_t inLocation,
			const std::string& inName,
			const AttachmentInfo& inAttachmentInfo = {});
	};

	class RenderPassInfo final : public AttachmentPassInfo
	{
	private:
		virtual auto GetType() const-> PassType override { return PassType::RENDER_PASS; }
	};

	class SubpassInfo final : public AttachmentPassInfo
	{
	private:
		virtual auto GetType() const-> PassType override { return PassType::SUBPASS; }
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
	auto _CreateBuildContext() const->BuildContext;
	void _LinkPasses(BuildContext& inContext) const;
	void _ResolveDependency(BuildContext& inContext) const;
	void _CullPasses(BuildContext& inContext) const;
	void _BuildScheduleAndBatches(BuildContext& inContext, BuildResult& inoutResult) const;
	void _BuildResourceAliases(BuildContext& inoutContext, const BuildResult& inResult) const;
	void _MaterializeResourceAliases(BuildContext& inoutContext) const;
	void _BuildScheduledResourceBarriers(BuildContext& inContext, BuildResult& inoutResult) const;
	void _BuildManagedRenderPassPlans(const BuildContext& inContext, BuildResult& inoutResult) const;

public:
	void AddBuffer(const std::string& inName, const RenderGraph::BufferInfo& inBufferInfo);
	void AddImage(const std::string& inName, const RenderGraph::ImageInfo& inImageInfo);
	void AddPass(const std::string& inName, const RenderGraph::PassInfo& inPassInfo);
	void AddExtraPassDependency(const std::string& inHappensSooner, const std::string& inHappensLater);
	void EnableResourceAliasing(bool inEnable);
	const BuildResult& Build();
	const BuildResult& GetBuildResult() const;
};
