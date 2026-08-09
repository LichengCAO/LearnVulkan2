#pragma once
#include "common.h"

using RenderGraphBufferHandle = uint32_t;
using RenderGraphImageHandle = uint32_t;
using RenderGraphPassHandle = uint32_t;
class Buffer;
class Image;

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
		void CustomizArrayLayer(uint32_t inLayerCount);
		void CustomizeSize(uint32_t inWidth, std::optional<uint32_t> inHeight = {}, std::optional<uint32_t> inDepth = {});
		void AddUsages(VkImageUsageFlags inUsage);
		void SetAsExternal();
	};

	class PassInfo
	{
	public:
		void AddColorAttachment(
			RenderGraphImageHandle inHandle, 
			VkAttachmentLoadOp inLoadOp,
			VkImageLayout inLoadLayout, 
			VkPipelineStageFlags2 inLoadStage,
			VkAttachmentStoreOp inStoreOp,
			VkImageLayout inStoreLayout, 
			VkPipelineStageFlags2 inStoreStage);
		void AddDepthAttachment(
			RenderGraphImageHandle inHandle, 
			VkAttachmentLoadOp inLoadOp,
			VkImageLayout inLoadLayout,
			VkPipelineStageFlags2 inLoadStage,
			VkAttachmentStoreOp inStoreOp,
			VkImageLayout inStoreLayout,
			VkPipelineStageFlags2 inStoreStage);
		void AddDescriptorInputAttachment(RenderGraphImageHandle inHandle, VkPipelineStageFlags2 inReadStage);
		void AddDescriptorReadOnlyImage(RenderGraphImageHandle inHandle, VkPipelineStageFlags2 inReadStage);
		void AddDescriptorWriteOnlyImage(RenderGraphImageHandle inHandle, VkPipelineStageFlags2 inWriteStage);
		void AddDescriptorReadWriteImage(RenderGraphImageHandle inHandle, VkPipelineStageFlags2 inReadStage, VkPipelineStageFlags2 inWriteStage);
		void AddDescriptorUniformBuffer(RenderGraphBufferHandle inHandle, VkPipelineStageFlags2 inReadStage);
		void AddDescriptorStorageBuffer(RenderGraphBufferHandle inHandle, VkPipelineStageFlags2 inReadStage, VkPipelineStageFlags2 inWriteStage);
	};

public:
	auto RegisterBuffer(const std::string& inName, const RenderGraph::BufferInfo& inBufferInfo) -> RenderGraphBufferHandle;
	auto GetBufferHandle(const std::string& inName)const -> RenderGraphBufferHandle;
	auto RegisterImage(const std::string& inName, const RenderGraph::ImageInfo& inImageInfo) -> RenderGraphImageHandle;
	auto GetImageHandle(const std::string& inName)const -> RenderGraphImageHandle;
	auto AddPass(const std::string& inName, const RenderGraph::PassInfo& inPassInfo)-> RenderGraphPassHandle;
};

class RenderGraphInstance
{
public:
	struct ExternalBufferInfo
	{
		Buffer* pBuffer;
		VkPipelineStageFlags2 enteringStage;
		VkAccessFlags2 enteringAccess;
		VkPipelineStageFlags2 leavingStage;
		VkAccessFlags2 leavingAccess;
	};

	struct ExternalImageInfo
	{
		Image* pImage;
		VkImageLayout enteringLayout; 
		VkPipelineStageFlags2 enteringStage; 
		VkAccessFlags2 enteringAccess;
		VkImageLayout leavingLayout;
		VkPipelineStageFlags2 leavingStage;
		VkAccessFlags2 leavingAccess;
	};

	class ExecutionContext
	{

	};

	class PassInfo
	{
	public:
		void SetProcess(std::function<void(RenderGraphInstance::ExecutionContext&)> inProcess);
	};

public:
	RenderGraphInstance(const RenderGraph& inRenderGraph);
	void SetUpExternalBuffer(RenderGraphBufferHandle inHandle, const RenderGraphInstance::ExternalBufferInfo& inBufferInfo);
	void SetUpExternalImage(RenderGraphImageHandle inHandle, const RenderGraphInstance::ExternalImageInfo& inImageInfo);
	void SetUpPass(RenderGraphPassHandle inHandle, const RenderGraphInstance::PassInfo& inPassInfo);
	auto ResolveBuffer(RenderGraphBufferHandle inHandle) -> Buffer*;
	auto ResolveImage(RenderGraphImageHandle inHandle) -> Image*;
};