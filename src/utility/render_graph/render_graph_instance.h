#pragma once
#include "render_graph.h"
#include "my_vulkan/command/queue_signal_chain.h"

class GraphicsPipelineStateInfo;

class RenderGraphInstance
{
	friend struct RenderGraphTestProbe;

private:
	using BufferIndex = RenderGraph::BufferIndex;
	using ImageIndex = RenderGraph::ImageIndex;
	using PassIndex = RenderGraph::PassIndex;

public:
	class QueueSyncInfo final
	{
		friend class RenderGraphInstance;
		friend struct RenderGraphTestProbe;

	private:
		QueueSemaphore m_graphicsToCompute;
		QueueSemaphore m_computeToGraphics;
		bool m_enteringUsed = false;
		bool m_leavingUsed = false;

	public:
		QueueSyncInfo() = default;
		QueueSyncInfo(const QueueSyncInfo&) = delete;
		QueueSyncInfo& operator=(const QueueSyncInfo&) = delete;
		QueueSyncInfo(QueueSyncInfo&&) = delete;
		QueueSyncInfo& operator=(QueueSyncInfo&&) = delete;
	};

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
		void FillSubpassCommands(const std::string& inTarget, std::vector<const Command*> inCommands);
		void RecordCommandBuffer(const std::string& inTarget, std::function<void(CommandBuffer*)> inProcess);
	};

	class ExecuteInfo final
	{
		friend class RenderGraphInstance;
		friend struct RenderGraphTestProbe;

	private:
		struct ResourceQueueSyncInfo
		{
			std::string name;
			QueueSyncInfo* entering = nullptr;
			QueueSyncInfo* leaving = nullptr;
		};

		std::vector<QueueSyncInfo*> m_enteringQueueSyncInfos;
		std::vector<QueueSyncInfo*> m_leavingQueueSyncInfos;
		std::vector<ResourceQueueSyncInfo> m_externalBufferQueueSyncInfos;
		std::vector<ResourceQueueSyncInfo> m_externalImageQueueSyncInfos;

	public:
		void AddEnteringQueueSyncInfo(QueueSyncInfo& inQueueSyncInfo);
		void AddLeavingQueueSyncInfo(QueueSyncInfo& inQueueSyncInfo);
		void AddExternalBufferQueueSyncInfo(
			const std::string& inName,
			QueueSyncInfo* inEntering,
			QueueSyncInfo* inLeaving);
		void AddExternalImageQueueSyncInfo(
			const std::string& inName,
			QueueSyncInfo* inEntering,
			QueueSyncInfo* inLeaving);
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
	std::vector<VkSemaphore> m_freeSemaphores;
	std::vector<VkSemaphore> m_executeSemaphores;
	bool m_compiled = false;
	bool m_inFlight = false;
	bool m_submittedGraphicsCommands = false;
	bool m_submittedComputeCommands = false;

private:
	void _DestroyManagedRenderPasses();
	void _DestroyInternalResources();
	void _SetUpPhysicalResources();
	void _CreateManagedRenderPasses();
	void _BuildCompiledGraphPlan();
	auto _GetManagedRenderPass(uint32_t inSubmitIndex, uint32_t inGraphicsBatchIndex)->ManagedRenderPass*;
	auto _GetBuffer(const std::string& inName) const->Buffer*;
	auto _GetImage(const std::string& inName) const->Image*;
	void _AppendPassCommands(PassIndex inPassIndex, CommandBuffer& inCommandBuffer);
	void _AppendRenderPassCommands(const std::vector<PassIndex>& inPasses, const ManagedRenderPass& inRenderPass, CommandBuffer& inCommandBuffer);
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
	void SetUpPass(const std::string& inName, const PassInfo& inPassInfo);
	void Compile();
	void Execute();
	void Execute(const ExecuteInfo& inExecuteInfo);
	void WaitTillDone();
};

using QueueSyncInfo = RenderGraphInstance::QueueSyncInfo;
using ExecuteInfo = RenderGraphInstance::ExecuteInfo;
