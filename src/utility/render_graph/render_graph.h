#pragma once
#include "common.h"

using RenderGraphBufferHandle = uint32_t;
using RenderGraphImageHandle = uint32_t;
using RenderGraphPassHandle = uint32_t;
class Buffer;
class Image;
class CommandBuffer;
class Command;

enum class RenderGraphPassType
{
	COMPUTE,  // Owns compute queue commands
	GRAPHICS, // Owns graphics queue commands, manages render pass itself  
	SUBPASS,  // Owns graphics queue commands in a subpass, let graph manage render pass for it
};

class RenderGraph
{
public:
	class BufferInfo
	{
	public:
		void SetSize(VkDeviceSize inSize);
		void AddUsages(VkBufferUsageFlags inUsage);
		void SetAsExternal();
	};

	class ImageInfo
	{
	public:
		void SetFormat(VkFormat inFormat);
		void CustomizeMipLevel(uint32_t inMipLevelCount);
		void CustomizeArrayLayer(uint32_t inLayerCount);
		void CustomizeSize(uint32_t inWidth, std::optional<uint32_t> inHeight = {}, std::optional<uint32_t> inDepth = {});
		void AddUsages(VkImageUsageFlags inUsage);
		void SetAsExternal();
	};

	class PassInfo
	{
	public:
		virtual ~PassInfo() = default;
		virtual auto GetType() const-> RenderGraphPassType = 0;
		void AddSampledImage(RenderGraphImageHandle inHandle, VkPipelineStageFlags2 inReadStage);
		void AddStorageImage(RenderGraphImageHandle inHandle, VkPipelineStageFlags2 inWriteStage);
		void AddDescriptorUniformBuffer(RenderGraphBufferHandle inHandle, VkPipelineStageFlags2 inReadStage);
		void AddDescriptorStorageBuffer(RenderGraphBufferHandle inHandle, VkPipelineStageFlags2 inWriteStage);
	};

	class ComputePassInfo final : public PassInfo
	{
	public:
		explicit ComputePassInfo() = default;
		virtual auto GetType() const-> RenderGraphPassType override { return RenderGraphPassType::COMPUTE; }
	};

	class GraphicsPassInfo final : public PassInfo
	{
	public:
		explicit GraphicsPassInfo() = default;
		virtual auto GetType() const-> RenderGraphPassType override { return RenderGraphPassType::GRAPHICS; }
	};

	class SubpassInfo final : public PassInfo
	{
	public:
		explicit SubpassInfo() = default;
		virtual auto GetType() const-> RenderGraphPassType override { return RenderGraphPassType::SUBPASS; }
		void UseDedicateRenderPass();
		void AddColorAttachment(
			RenderGraphImageHandle inHandle,
			VkAttachmentLoadOp inLoadOp,
			VkPipelineStageFlags2 inLoadStage,
			VkAttachmentStoreOp inStoreOp,
			VkPipelineStageFlags2 inStoreStage);
		void AddDepthAttachment(
			RenderGraphImageHandle inHandle,
			VkAttachmentLoadOp inLoadOp,
			VkPipelineStageFlags2 inLoadStage,
			VkAttachmentStoreOp inStoreOp,
			VkPipelineStageFlags2 inStoreStage);
		void AddDescriptorInputAttachment(RenderGraphImageHandle inHandle, VkPipelineStageFlags2 inReadStage);
	};

public:
	auto RegisterBuffer(const std::string& inName, const RenderGraph::BufferInfo& inBufferInfo) -> RenderGraphBufferHandle;
	auto GetBufferHandle(const std::string& inName)const -> RenderGraphBufferHandle;
	auto RegisterImage(const std::string& inName, const RenderGraph::ImageInfo& inImageInfo) -> RenderGraphImageHandle;
	auto GetImageHandle(const std::string& inName)const -> RenderGraphImageHandle;
	auto AddPass(const std::string& inName, const RenderGraph::PassInfo& inPassInfo)-> RenderGraphPassHandle;
	void AddExtraDependency(RenderGraphPassHandle inHappensSooner, RenderGraphPassHandle inHappensLater);
	void Build();
	//TODO: Maybe it's unneccessary.
	//void SetAliasingRule(std::function<bool(RenderGraphImageHandle inOrigin, RenderGraphImageHandle inAliasCandidate)> inImageRule, std::function<bool(RenderGraphBufferHandle inOrigin, RenderGraphBufferHandle inAliasCandidate)> inBufferRule);
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
		auto ResolveBuffer(RenderGraphBufferHandle inHandle) -> Buffer*;
		auto ResolveImage(RenderGraphImageHandle inHandle) -> Image*;
		void FillSubpassCommands(RenderGraphPassHandle inTarget, std::vector<const Command*> inCommands);
		void RecordCommandBuffer(RenderGraphPassHandle inTarget, std::function<void(CommandBuffer*)> inProcess);
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
	void SetUpExternalBuffer(RenderGraphBufferHandle inHandle, const RenderGraphInstance::ExternalBufferInfo& inBufferInfo);
	void SetUpExternalImage(RenderGraphImageHandle inHandle, const RenderGraphInstance::ExternalImageInfo& inImageInfo);
	void SetUpPass(RenderGraphPassHandle inHandle, const RenderGraphInstance::PassInfo& inPassInfo);
	void Compile();
	void Execute();
};