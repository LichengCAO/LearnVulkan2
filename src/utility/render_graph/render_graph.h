#pragma once
#include "common.h"

class Buffer;
class Image;
class CommandBuffer;
class Command;
class RenderGraphInstance;

class RenderGraph
{
	friend class RenderGraphInstance;

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

	enum class ImageUsageType
	{
		SAMPLED,
		STORAGE,
		COLOR_ATTACHMENT,
		DEPTH_ATTACHMENT,
		INPUT_ATTACHMENT,
	};

	enum class BufferUsageType
	{
		UNIFORM,
		STORAGE,
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
		ImageUsageType type = ImageUsageType::SAMPLED;
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
		BufferUsageType type = BufferUsageType::UNIFORM;
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
		PassIndex before = INVALID_INDEX;
		PassIndex after = INVALID_INDEX;
		HazardType hazard = HazardType::RAW;
		VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkPipelineStageFlags2 srcStage = 0;
		VkAccessFlags2 srcAccess = 0;
		VkPipelineStageFlags2 dstStage = 0;
		VkAccessFlags2 dstAccess = 0;
		bool executionOnly = false;
		bool needsQueueSync = false;
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
		std::vector<ImageUsage> imageUsages;
		std::vector<BufferUsage> bufferUsages;
	};

	struct DependencyEdge
	{
		PassIndex before = INVALID_INDEX;
		PassIndex after = INVALID_INDEX;
	};

public:
	class BufferInfo
	{
		friend class RenderGraph;

	private:
		std::string m_name;
		VkBufferUsageFlags m_usage = 0;
		bool m_external = false;

	public:
		void AddUsage(VkBufferUsageFlags inUsage);
		void SetAsExternal();
	};

	class ImageInfo
	{
		friend class RenderGraph;

	private:
		std::string m_name;
		VkImageUsageFlags m_usage = 0;
		bool m_external = false;

	public:
		void AddUsage(VkImageUsageFlags inUsage);
		void SetAsExternal();
	};

	class SubpassInfo;

	class PassInfo
	{
		friend class RenderGraph;
		friend class SubpassInfo;

	private:
		std::vector<ImageUsage> m_imageUsages;
		std::vector<BufferUsage> m_bufferUsages;

	public:
		virtual ~PassInfo() = default;
		void AddSampledImage(const std::string& inName, VkPipelineStageFlags2 inReadStage);
		void AddStorageImage(const std::string& inName, VkPipelineStageFlags2 inWriteStage);
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
			VkPipelineStageFlags2 inStoreStage);
		void AddDepthAttachment(
			const std::string& inName,
			VkAttachmentLoadOp inLoadOp,
			VkPipelineStageFlags2 inLoadStage,
			VkAttachmentStoreOp inStoreOp,
			VkPipelineStageFlags2 inStoreStage);
		void AddDescriptorInputAttachment(const std::string& inName, VkPipelineStageFlags2 inReadStage);
	};

private:
	std::vector<BufferInfo> m_buffers;
	std::vector<ImageInfo> m_images;
	std::vector<PassRecord> m_passes;
	std::vector<DependencyEdge> m_extraDependencies;
	std::unordered_map<std::string, BufferIndex> m_nameToBuffer;
	std::unordered_map<std::string, ImageIndex> m_nameToImage;
	std::unordered_map<std::string, PassIndex> m_nameToPass;
	std::vector<DependencyEdge> m_dependencyEdges;
	std::vector<PassIndex> m_sortedPasses;
	std::vector<std::string> m_passesInExecutionOrder;
	std::vector<std::vector<std::string>> m_passBatches;
	std::vector<BarrierPlan> m_barrierPlans;
	std::vector<QueueSyncPlan> m_queueSyncPlans;
	bool m_built = false;

private:
	static auto _GetQueueType(PassType inType) -> QueueType;
	static auto _NeedsMemoryDependency(HazardType inHazard, VkImageLayout inOldLayout, VkImageLayout inNewLayout) -> bool;
	auto _GetBufferIndex(const std::string& inName) const->BufferIndex;
	auto _GetImageIndex(const std::string& inName) const->ImageIndex;
	auto _GetPassIndex(const std::string& inName) const->PassIndex;
	void _InvalidateBuild();

public:
	void AddBuffer(const std::string& inName, const RenderGraph::BufferInfo& inBufferInfo);
	void AddImage(const std::string& inName, const RenderGraph::ImageInfo& inImageInfo);
	void AddPass(const std::string& inName, const RenderGraph::PassInfo& inPassInfo);
	void AddExtraPassDependency(const std::string& inHappensSooner, const std::string& inHappensLater);
	void Build();
	//TODO: Maybe it's unneccessary.
	//void SetAliasingRule(std::function<bool(const std::string& inOrigin, const std::string& inAliasCandidate)> inImageRule, std::function<bool(const std::string& inOrigin, const std::string& inAliasCandidate)> inBufferRule);
};

class RenderGraphInstance
{
public:
	struct ExternalBufferInfo
	{
		Buffer* pBuffer{};
		VkPipelineStageFlags2 enteringStage;
		VkAccessFlags2 enteringAccess;
		VkPipelineStageFlags2 leavingStage;
		VkAccessFlags2 leavingAccess;
	};

	struct ExternalImageInfo
	{
		Image* pImage{};
		VkImageLayout enteringLayout; 
		VkPipelineStageFlags2 enteringStage; 
		VkAccessFlags2 enteringAccess;
		VkImageLayout leavingLayout;
		VkPipelineStageFlags2 leavingStage;
		VkAccessFlags2 leavingAccess;
	};

	class ExecutionContext
	{
	public:
		auto ResolveBuffer(const std::string& inName) -> Buffer*;
		auto ResolveImage(const std::string& inName) -> Image*;
		void FillSubpassCommands(const std::string& inTarget, std::vector<const Command*> inCommands);
		void RecordCommandBuffer(const std::string& inTarget, std::function<void(CommandBuffer*)> inProcess);
	};

	class PassInfo
	{
	public:
		void SetProcess(std::function<void(RenderGraphInstance::ExecutionContext&)> inProcess);
	};

private:
	void _SetUpPhysicalResources();

public:
	RenderGraphInstance(const RenderGraph& inRenderGraph);
	void SetUpExternalBuffer(const std::string& inName, const RenderGraphInstance::ExternalBufferInfo& inBufferInfo);
	void SetUpExternalImage(const std::string& inName, const RenderGraphInstance::ExternalImageInfo& inImageInfo);
	void SetUpPass(const std::string& inName, const RenderGraphInstance::PassInfo& inPassInfo);
	void Compile();
	void Execute();
};
