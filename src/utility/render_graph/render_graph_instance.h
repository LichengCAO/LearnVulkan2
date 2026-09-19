#pragma once
#include "render_graph.h"
#include "my_vulkan/command/queue_dependency.h"

class GraphicsPipelineStateInfo;
class HostFence;

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
		QueueDependency* pAcquireDependency = nullptr;
		QueueDependency* pReleaseDependency = nullptr;
		VkPipelineStageFlags2 enteringStage = 0;
		VkAccessFlags2 enteringAccess = 0;
		VkPipelineStageFlags2 leavingStage = 0;
		VkAccessFlags2 leavingAccess = 0;
	};

	struct ExternalImageInfo
	{
		Image* pImage{};
		QueueDependency* pAcquireDependency = nullptr;
		QueueDependency* pReleaseDependency = nullptr;
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
		const RenderPass* m_pRenderPass = nullptr;
		PassIndex m_currentPass = INVALID_INDEX;
		uint32_t m_currentSubpass = INVALID_INDEX;
		std::unordered_map<PassIndex, size_t> m_passToSubpass;

	private:
		ExecutionContext() = default;

	public:
		auto ResolveBuffer(const std::string& inName) -> Buffer*;
		auto ResolveImage(const std::string& inName) -> Image*;
		void ConfigureGraphicsPipelineState(GraphicsPipelineStateInfo& inoutStateInfo) const;
		void RecordCommands(std::function<void(CommandBuffer*)> inProcess);
	};

	using PassProcess = std::function<void(ExecutionContext&)>;

	class PassInfo
	{
		friend class RenderGraphInstance;
		friend struct RenderGraphTestProbe;

	private:
		PassProcess m_process;
		std::unordered_map<uint32_t, VkClearColorValue> m_colorClearValueOverrides;
		std::optional<VkClearDepthStencilValue> m_depthStencilClearValueOverride;

	public:
		void SetProcess(PassProcess inProcess);
		void CustomizeColorClearValue(uint32_t inLocation, const VkClearColorValue& inClearValue);
		void CustomizeDepthStencilClearValue(const VkClearDepthStencilValue& inClearValue);
	};

	class ExecuteInfo final
	{
		friend class RenderGraphInstance;
		friend struct RenderGraphTestProbe;

	private:
		struct PassBinding
		{
			std::string name;
			PassInfo info;
		};

		struct ExternalBufferBinding
		{
			std::string name;
			ExternalBufferInfo info;
		};

		struct ExternalImageBinding
		{
			std::string name;
			ExternalImageInfo info;
		};

		std::vector<PassBinding> m_passes;
		std::vector<ExternalBufferBinding> m_externalBuffers;
		std::vector<ExternalImageBinding> m_externalImages;
		HostFence* m_graphicsCompletionFence = nullptr;
		HostFence* m_computeCompletionFence = nullptr;

	public:
		void SetUpPass(
			const std::string& inName,
			const PassInfo& inPassInfo);
		void SetUpExternalBuffer(
			const std::string& inName,
			const ExternalBufferInfo& inBufferInfo);
		void SetUpExternalImage(
			const std::string& inName,
			const ExternalImageInfo& inImageInfo);
		// The caller owns completion fences and must keep them alive until the
		// associated queue submissions complete.
		void SetGraphicsCompletionFence(HostFence& inCompletionFence);
		void SetComputeCompletionFence(HostFence& inCompletionFence);
	};

private:
	static constexpr uint32_t INVALID_INDEX = RenderGraph::INVALID_INDEX;

	struct ManagedRenderPass
	{
		std::vector<RenderGraph::SubmitBatch::ManagedAttachmentPlan> attachmentPlans;
		std::unique_ptr<RenderPass> renderPass;
		std::unique_ptr<Framebuffer> framebuffer;
		VkRect2D renderArea{};
		std::vector<VkClearValue> defaultClearValues;
	};

	struct CompiledPassGroup
	{
		std::vector<PassIndex> passes;
		uint32_t managedRenderPass = INVALID_INDEX;
		std::vector<std::unique_ptr<Command>> prologueCommands;
		std::vector<std::unique_ptr<Command>> epilogueCommands;
		std::vector<std::unique_ptr<Command>> queueReleaseCommands;
	};

	struct CompiledQueueSyncEdge
	{
		uint32_t srcSubmit = INVALID_INDEX;
		uint32_t dstSubmit = INVALID_INDEX;
		RenderGraph::QueueType srcQueue = RenderGraph::QueueType::GRAPHICS;
		RenderGraph::QueueType dstQueue = RenderGraph::QueueType::GRAPHICS;
		VkPipelineStageFlags waitStage = 0;
	};

	struct CompiledSubmitBatch
	{
		std::vector<CompiledPassGroup> graphicsGroups;
		std::vector<CompiledPassGroup> computeGroups;
		std::vector<uint32_t> graphicsSignalSyncs;
		std::vector<uint32_t> computeSignalSyncs;
		std::vector<uint32_t> graphicsWaitSyncs;
		std::vector<uint32_t> computeWaitSyncs;
	};

	struct CompiledGraphPlan
	{
		std::vector<CompiledSubmitBatch> submitBatches;
		std::vector<CompiledQueueSyncEdge> queueSyncEdges;
	};

	struct ExternalDependencyBinding
	{
		QueueDependency* dependency = nullptr;
		RenderGraph::QueueType queue = RenderGraph::QueueType::GRAPHICS;
		uint32_t waitSubmit = INVALID_INDEX;
		uint32_t signalSubmit = INVALID_INDEX;
	};

	enum class BarrierCommandMode
	{
		NORMAL,
		QUEUE_RELEASE,
		QUEUE_ACQUIRE,
	};

	RenderGraph::BuildResult m_buildResult;
	std::vector<std::unique_ptr<Buffer>> m_internalBuffers;
	std::vector<std::unique_ptr<Image>> m_internalImages;
	std::vector<Buffer*> m_buffers;
	std::vector<Image*> m_images;
	std::vector<std::optional<ExternalBufferInfo>> m_externalBufferInfos;
	std::vector<std::optional<ExternalImageInfo>> m_externalImageInfos;
	std::vector<PassInfo> m_passInfos;
	std::vector<ManagedRenderPass> m_managedRenderPasses;
	std::vector<std::vector<uint32_t>> m_graphicsBatchToManagedRenderPass;
	CompiledGraphPlan m_compiledPlan;

private:
	void _DestroyManagedRenderPasses();
	void _DestroyInternalResources();
	void _ApplyPasses(const ExecuteInfo& inExecuteInfo);
	void _ApplyExternalResources(const ExecuteInfo& inExecuteInfo);
	void _SetUpInternalResources();
	void _BindExternalResources();
	void _CreateManagedRenderPasses();
	void _BuildCompiledGraphPlan();
	auto _BuildExternalDependencyBindings() const->std::vector<ExternalDependencyBinding>;
	auto _GetManagedRenderPass(uint32_t inSubmitIndex, uint32_t inGraphicsBatchIndex)->ManagedRenderPass*;
	auto _GetBuffer(const std::string& inName) const->Buffer*;
	auto _GetImage(const std::string& inName) const->Image*;
	void _AppendPassCommands(PassIndex inPassIndex, CommandBuffer& inCommandBuffer);
	void _AppendRenderPassCommands(const std::vector<PassIndex>& inPasses, const ManagedRenderPass& inRenderPass, CommandBuffer& inCommandBuffer);
	void _RecordSubpassCommandBuffer(PassIndex inPassIndex, std::function<void(CommandBuffer*)> inProcess, ExecutionContext& inContext);
	auto _CreateBarrierCommand(
		const std::vector<RenderGraph::BarrierPlan>& inBarrierPlans,
		BarrierCommandMode inMode = BarrierCommandMode::NORMAL)->std::unique_ptr<Command>;

public:
	RenderGraphInstance(const RenderGraph& inRenderGraph);
	~RenderGraphInstance();
	void Execute(const ExecuteInfo& inExecuteInfo);
};

using ExecuteInfo = RenderGraphInstance::ExecuteInfo;
