#pragma once
#include "pipeline_common.h"
#include <pipeline_state.h>
class RenderPass;
class Framebuffer;
class ImageView;
class GraphicsPipeline;
class GraphicsProgram;
class PushConstantManager;
class CommandSubmission;
class CommandBuffer;
class DescriptorSetLayout;

class IRenderPassInitializer
{
public:
	virtual void InitRenderPass(RenderPass* pRenderPass) const = 0;
};

class IFramebufferInitializer
{
public:
	virtual void InitFramebuffer(Framebuffer* pFramebuffer) const = 0;
};

class AttachmentDescriptionBuilder
{
private:
	VkFormat m_format = VK_FORMAT_MAX_ENUM;
	std::optional<VkAttachmentLoadOp> m_loadOp;
	std::optional<VkAttachmentStoreOp> m_storeOp;
	std::optional<VkImageLayout> m_initialLayout;
	std::optional<VkImageLayout> m_finalLayout;

public:
	AttachmentDescriptionBuilder& Reset();
	AttachmentDescriptionBuilder& SetFormat(VkFormat inFormat);
	AttachmentDescriptionBuilder& CustomizeLoadOperationAndInitialLayout(VkAttachmentLoadOp inOperation, VkImageLayout inLayout);
	AttachmentDescriptionBuilder& CustomizeStoreOperationAndFinalLayout(VkAttachmentStoreOp inOperation, VkImageLayout inLayout);
	VkAttachmentDescription Build() const;
};

class RenderPass final
{
public:
	enum class AttachmentPreset
	{
		SWAPCHAIN,
		DEPTH,
		GBUFFER_UV,
		GBUFFER_NORMAL,
		GBUFFER_POSITION,
		GBUFFER_ALBEDO,
		GBUFFER_DEPTH,
		COLOR_OUTPUT,
	};

	struct Attachment
	{
		VkAttachmentDescription attachmentDescription{};
		VkClearValue clearValue{};
	};

	class AttachmentBuilder
	{
	public:
		static RenderPass::Attachment BuildByPreset(RenderPass::AttachmentPreset inPreset);
	};

	class Subpass final
	{
	private:
		std::vector<VkAttachmentReference> colorAttachments;
		std::vector<VkAttachmentReference> resolveAttachments;
		std::optional<VkAttachmentReference> optDepthStencilAttachment;

	public:
		RenderPass::Subpass& AddColorAttachment(
			uint32_t inLocationInShader,
			uint32_t inAttachmentIndexInRenderPass,
			VkImageLayout inLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		RenderPass::Subpass& AddResolveAttachment(
			uint32_t inColorReferenceLocation,
			uint32_t inAttachmentIndexInRenderPass,
			VkImageLayout inLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

		RenderPass::Subpass& AddDepthStencilAttachment(
			uint32_t inAttachmentIndexInRenderPass,
			VkImageLayout inLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);

		VkSubpassDescription GetSubpassDescription() const;

		friend class RenderPass;
		friend class GraphicsPipeline;
	};

	class SingleSubpassInit : public IRenderPassInitializer
	{
	private:
		Subpass m_subpass;
		std::vector<Attachment> m_attachments;

	public:
		virtual void InitRenderPass(RenderPass* pRenderPass) const override;

		RenderPass::SingleSubpassInit& Reset();
		RenderPass::SingleSubpassInit& AddColorAttachment(const Attachment& inAttachment);
		RenderPass::SingleSubpassInit& AddDepthStencilAttachment(const Attachment& inAttachment, bool inReadOnly = false);
	};

private:
	std::vector<VkClearValue> m_clearValues;
	std::vector<Subpass> m_subpasses;
	VkRenderPass m_vkRenderPass = VK_NULL_HANDLE;

public:
	~RenderPass();

	void Create(const IRenderPassInitializer* inIntializerPtr);
	void Destroy();
	VkRenderPass GetVkRenderPass() const;
	const Subpass& GetSubpass(uint32_t index = 0) const;
	void StartRenderPass(CommandSubmission* pCmd, const Framebuffer* pFramebuffer) const;

	static VkRenderPassBeginInfo GetRenderPassBeginInfo(
		const RenderPass* inRenderPassPtr,
		const Framebuffer* inFramebuferrPtr);
};

class Framebuffer final
{
private:
	const RenderPass* m_pRenderPass = nullptr;
	VkFramebuffer m_vkFramebuffer = VK_NULL_HANDLE;
	VkExtent2D m_imageSize{};

public:
	class FramebufferInit : public IFramebufferInitializer
	{
	private:
		uint32_t m_width = 0;
		uint32_t m_height = 0;
		std::vector<VkImageView> m_views;
		const RenderPass* m_pRenderPass = nullptr;

	public:
		virtual void InitFramebuffer(Framebuffer* pFramebuffer) const override;

		Framebuffer::FramebufferInit& Reset();
		Framebuffer::FramebufferInit& SetImageView(uint32_t inAttachmentIndex, const ImageView* inViewPtr);
		Framebuffer::FramebufferInit& SetRenderPass(const RenderPass* inRenderPassPtr);
	};

public:
	~Framebuffer();

	void Create(const IFramebufferInitializer* inInitializerPtr);
	void Destroy();
	VkExtent2D GetImageSize() const;
	VkFramebuffer GetVkFramebuffer() const;

	friend class RenderPass;
	friend class GraphicsProgram;
};

class IGraphicsPipelineInitializer
{
public:
	virtual void InitGraphicsPipeline(GraphicsPipeline* pPipeline) const = 0;
};

class GraphicsPipeline
{
public:
	// For pipelines that don't bind vertex buffers, e.g. pass through
	struct PipelineInput_Draw
	{
		VkExtent2D imageSize{};
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		uint32_t vertexCount = 0u;

		std::vector<VkBuffer>						vertexBuffers;			// can be empty
		std::optional<std::vector<VkDeviceSize>>	optVertexBufferOffsets; // must have the same length as vertexBuffers
	};

	// For general graphics pipelines that use vertex buffers
	struct PipelineInput_DrawIndexed
	{
		VkExtent2D imageSize{};
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		std::vector<VkBuffer>	vertexBuffers;								// not sure, but I think order matters
		VkBuffer				indexBuffer = VK_NULL_HANDLE;
		VkIndexType				vkIndexType = VK_INDEX_TYPE_UINT32;
		uint32_t				indexCount = 0u;

		std::optional<std::vector<VkDeviceSize>>	optVertexBufferOffsets; // must have the same length as vertexBuffers
		std::optional<VkDeviceSize>					optIndexBufferOffset;
	};

	// For mesh shader pipelines
	struct PipelineInput_Mesh
	{
		VkExtent2D imageSize{};
		std::vector<VkDescriptorSet> vkDescriptorSets;
		std::vector<uint32_t> optDynamicOffsets;
		std::vector<std::pair<VkShaderStageFlags, const void*>> pushConstants;

		uint32_t groupCountX = 1;
		uint32_t groupCountY = 1;
		uint32_t groupCountZ = 1;
	};

	// For pipeline initialization
	class BaseInit : public IGraphicsPipelineInitializer
	{
	private:
		std::vector<VkPipelineShaderStageCreateInfo> m_shaderStageInfos;
		std::vector<VkVertexInputBindingDescription> m_vertBindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> m_vertAttributeDescriptions;
		std::vector<VkDynamicState> m_dynamicStates;
		std::vector<VkPipelineColorBlendAttachmentState> m_colorBlendAttachmentStates;
		std::vector<VkDescriptorSetLayout> m_descriptorSetLayouts;
		std::vector<VkPushConstantRange> m_pushConstantInfos;
		VkPipelineInputAssemblyStateCreateInfo m_inputAssemblyStateInfo{};
		VkPipelineRasterizationStateCreateInfo m_rasterizerStateInfo{};
		VkPipelineMultisampleStateCreateInfo m_multisampleStateInfo{};
		VkPipelineViewportStateCreateInfo m_viewportStateInfo{};
		VkPipelineDepthStencilStateCreateInfo m_depthStencilInfo{};
		VkPipelineCache m_cache = VK_NULL_HANDLE;
		VkRenderPass m_renderPass = VK_NULL_HANDLE;
		uint32_t m_subpassIndex = 0;

		void _InitCreateInfos();

		VkPipelineColorBlendAttachmentState _GetDefaultColorBlendAttachmentState() const;

	public:
		BaseInit();

		virtual void InitGraphicsPipeline(GraphicsPipeline* pPipeline) const override;

		GraphicsPipeline::BaseInit& Reset();

		GraphicsPipeline::BaseInit& AddShader(const VkPipelineShaderStageCreateInfo& inShaderInfo);

		GraphicsPipeline::BaseInit& AddVertexInputDescription(
			const VkVertexInputBindingDescription& inBindingDescription,
			const std::vector<VkVertexInputAttributeDescription>& inAttributeDescriptions);

		GraphicsPipeline::BaseInit& AddDescriptorSetLayout(VkDescriptorSetLayout inDescriptorSetLayout);

		GraphicsPipeline::BaseInit& CustomizeColorAttachmentAsAdd(uint32_t inAttachmentIndex);

		GraphicsPipeline::BaseInit& AddPushConstant(VkShaderStageFlags inStages, uint32_t inOffset, uint32_t inSize);

		// Optional, default: VK_NULL_HANDLE.
		// In cases this is set: 
		// if cache is not empty, this will facilitate pipeline creation,
		// if not, cache will be filled with pipeline information for next creation
		GraphicsPipeline::BaseInit& CustomizePipelineCache(VkPipelineCache inCache);

		GraphicsPipeline::BaseInit& SetRenderPassSubpass(const RenderPass* inRenderPassPtr, uint32_t inSubpassIndex = 0);
	};

private:
	std::unique_ptr<PushConstantManager> m_uptrPushConstant;
	VkPipelineLayout m_vkPipelineLayout = VK_NULL_HANDLE;
	VkPipeline m_vkPipeline = VK_NULL_HANDLE;

private:
	void _DoCommon(
		VkCommandBuffer _cmd,
		const VkExtent2D& _imageSize,
		const std::vector<VkDescriptorSet>& _vkDescriptorSets,
		const std::vector<uint32_t>& _dynamicOffsets,
		const std::vector<std::pair<VkShaderStageFlags, const void*>>& _pushConstants);

public:
	~GraphicsPipeline();

	void Create(const IGraphicsPipelineInitializer* pInitializer);
	
	void Destroy();

	void Do(VkCommandBuffer commandBuffer, const PipelineInput_DrawIndexed& input);
	void Do(VkCommandBuffer commandBuffer, const PipelineInput_Mesh& input);
	void Do(VkCommandBuffer commandBuffer, const PipelineInput_Draw& input);
};

class GraphicsProgramCreateInfo final
{
	friend class GraphicsProgram;
private:
	std::set<std::string> m_spirvFiles;
	std::unordered_map<VkShaderStageFlagBits, std::string> m_mapStageToEntry;
	VkSampleCountFlagBits m_sampleCount = VkSampleCountFlagBits::VK_SAMPLE_COUNT_1_BIT;
	bool m_depthStencil = false;

public:
	void AddShaderModuleInfo(const ShaderModuleCreateInfo& inShaderModule);
	void AddDepthStencilAttachment() { m_depthStencil = true; };
	void CustomizeMultiSample(VkSampleCountFlagBits inSample) { m_sampleCount = inSample; };
};

class GraphicsPipelineLayout final
{
private:
	std::vector<std::unique_ptr<DescriptorSetLayout>> m_descriptorSetLayouts;
	std::unordered_map<std::string, VkFormat> m_mapAttachmentNameToFormat;
	std::unordered_map<uint32_t, VkFormat> m_mapAttachmentIndexToFormat;
	bool m_depthStencil = false;

public:
	void Create(const GraphicsProgramCreateInfo& inCreateInfo);

	void Destroy();
};

class RenderTarget final
{
private:
	bool m_isDepth = false;
	const ImageView* m_view{};
	const ImageView* m_resolveView{};

public:
	void SetColorImageView(const ImageView* inImageView);
	void SetResolveColorImageView(const ImageView* inMultiSampleImageView, const ImageView* in1SampleImageView);
	void SetDepthStencilImageView(const ImageView* inDepthStencilImageView);
};

class Viewport final
{
public:
	void SetViewport(float inX, float inY, float inWidth, float inHeight);
	void SetScissor(uint32_t inX, uint32_t inY, uint32_t inWidth, uint32_t inHeight);
};

class DrawState final
{
public:
	void SetVertexBuffer();
};

class IndexedDrawState final
{
public:
	void SetVertexBuffer();
	void SetIndexBuffer();
};

class MeshTaskState final
{
public:
	void SetDispatchSize();
};

class GraphicsPipelineState final : public PipelineState
{
public:
	void CustomizeViewportAndScissor(const Viewport* inViewports, size_t inCount);
	void SetGraphicsPipelineLayout(const GraphicsPipelineLayout* inLayout);
	void SetDrawState();
	void SetIndexedDrawState();
	void SetMeshTaskState();
};

class SubpassState final
{
	void SetColorAttachment(std::variant<uint32_t, std::string> inAttachmentIdentifier, const RenderTarget& inTarget);
	void SetDepthStencilAttachment(const RenderTarget& inTarget);
};

class RenderPassState final
{
public:
	void AddSubpass(SubpassState, GraphicsPipelineState*, size_t);
};

class PipelineStateDispatcher final
{
public:
	void DispatchRenderPassState();
	void DispatchComputePipelineState();
	void DispatchRayTracingPipelineState();
};