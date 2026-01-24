#include "frame_graph_node.h"
#include "frame_graph.h"
#include "device.h"
#include <forward_list>


namespace
{
	uint32_t& _GetResourceStateQueueFamilyIndex(FRAME_GRAPH_RESOURCE_STATE& inoutState)
	{
		if (std::holds_alternative<FrameGraphImageSubResourceState>(inoutState))
		{
			FrameGraphImageSubResourceState& stateToModify = std::get<FrameGraphImageSubResourceState>(inoutState);
			return stateToModify.queueFamily;
		}
		else
		{
			CHECK_TRUE(std::holds_alternative<FrameGraphBufferSubResourceState>(inoutState));
			FrameGraphBufferSubResourceState& stateToModify = std::get<FrameGraphBufferSubResourceState>(inoutState);
			return stateToModify.queueFamily;
		}
	}
}

#pragma region Initializer

FrameGraphNodeInputInitializer& FrameGraphNodeInputInitializer::Reset()
{
    m_name.clear();
    m_access = 0;
    m_stages = 0;
    m_layout.reset();
    m_queueFamilyIndex.reset(); 
    
	return *this;    
}

FrameGraphNodeInputInitializer& FrameGraphNodeInputInitializer::SetName(const std::string& inName)
{
    m_name = inName;
    
	return *this;
}

FrameGraphNodeInputInitializer& FrameGraphNodeInputInitializer::SetAccessState(VkAccessFlags inAccessFlags)
{
    m_access = inAccessFlags;
    
	return *this;
}

FrameGraphNodeInputInitializer& FrameGraphNodeInputInitializer::SetVisiblePipelineStage(VkPipelineStageFlags inPipelineStageFlags)
{
    m_stages = inPipelineStageFlags;
    
	return *this;
}

FrameGraphNodeInputInitializer& FrameGraphNodeInputInitializer::SetImageLayout(VkImageLayout inImageLayout)
{
    m_layout = inImageLayout;
    
    return *this;
}

FrameGraphNodeInputInitializer& FrameGraphNodeInputInitializer::CustomizeQueueFamilyIndex(uint32_t inQueueFamilyIndex)
{
    m_queueFamilyIndex = inQueueFamilyIndex;
    
    return *this;
}

void FrameGraphNodeInputInitializer::InitializeInput(FrameGraphNodeInput* inoutNodeInput) const
{
	bool isImage = m_layout.has_value();
	uint32_t queueFamilyIndex = m_queueFamilyIndex.value_or(VK_QUEUE_FAMILY_IGNORED);

	inoutNodeInput->m_name = m_name;
	inoutNodeInput->m_connectedOutput = nullptr;
	inoutNodeInput->m_correlatedOutput = nullptr;
	inoutNodeInput->m_owner = nullptr;
	if (isImage)
	{
		FrameGraphImageSubResourceState state{};
		state.access = m_access;
		state.layout = m_layout.value();
		state.queueFamily = queueFamilyIndex;
		state.stage = m_stages;
		//state.range = {}; TODO;
		inoutNodeInput->m_resourceState = state;
	}
	else
	{
		FrameGraphBufferSubResourceState state{};
		state.access = m_access;
		state.queueFamily = queueFamilyIndex;
		state.stage = m_stages;
		// state.offset = 0; TODO
		// state.range = 0;
		inoutNodeInput->m_resourceState = state;
	}
}

FrameGraphNodeOutputInitializer& FrameGraphNodeOutputInitializer::Reset()
{
    m_name.clear();
    m_access = 0;
    m_stages = 0;
    m_layout.reset();
    m_queueFamilyIndex.reset();
    return *this;
}

FrameGraphNodeOutputInitializer& FrameGraphNodeOutputInitializer::SetName(const std::string& inName)
{
    m_name = inName;
    return *this;
}

FrameGraphNodeOutputInitializer& FrameGraphNodeOutputInitializer::SetAccessState(VkAccessFlags inAccessFlags)
{
    m_access = inAccessFlags;
    return *this;
}

FrameGraphNodeOutputInitializer& FrameGraphNodeOutputInitializer::SetAvailablePipelineStage(VkPipelineStageFlags inPipelineStageFlags)
{
    m_stages = inPipelineStageFlags;
    return *this;
}

FrameGraphNodeOutputInitializer& FrameGraphNodeOutputInitializer::SetResourceHandle(FRAME_GRAPH_RESOURCE_HANDLE inResourceHandle)
{
	m_resourceHandle = inResourceHandle;
	return *this;
}

FrameGraphNodeOutputInitializer& FrameGraphNodeOutputInitializer::SetImageLayout(VkImageLayout inImageLayout)
{
	m_layout = inImageLayout;
	
	return *this;
}

FrameGraphNodeOutputInitializer& FrameGraphNodeOutputInitializer::CustomizeQueueFamilyIndex(uint32_t inQueueFamilyIndex)
{
	m_queueFamilyIndex = inQueueFamilyIndex;
	
	return *this;
}

void FrameGraphNodeOutputInitializer::InitializeOutput(FrameGraphNodeOutput* inoutNodeOutput) const
{
	bool isImage = m_layout.has_value();
	uint32_t queueFamilyIndex = m_queueFamilyIndex.value_or(VK_QUEUE_FAMILY_IGNORED);

	inoutNodeOutput->m_name = m_name;
	inoutNodeOutput->m_connectedInputs.clear();
	inoutNodeOutput->m_resourceHandle = m_resourceHandle;
	inoutNodeOutput->m_owner = nullptr;
	if (isImage)
	{
		FrameGraphImageSubResourceState imgState{};
		imgState.access = m_access;
		imgState.layout = m_layout.value();
		imgState.queueFamily = queueFamilyIndex;
		imgState.stage = m_stages;
		// imgState.range = {}; // TODO
		inoutNodeOutput->m_resourceState = imgState;
	}
	else
	{
		FrameGraphBufferSubResourceState bufState{};
		bufState.access = m_access;
		bufState.queueFamily = queueFamilyIndex;
		bufState.stage = m_stages;
		// bufState.offset = 0; // TODO
		// bufState.range = 0;  // TODO
		inoutNodeOutput->m_resourceState = bufState;
	}
}

FrameGraphNodeInoutInitializer& FrameGraphNodeInoutInitializer::Reset()
{
	m_name.clear();
	m_inAccess = 0;
	m_outAccess = 0;
	m_inStages = 0;
	m_outStages = 0;
	m_initialLayout.reset();
	m_finalLayout.reset();
	m_initialQueueFamilyIndex.reset();
	m_finalQueueFamilyIndex.reset();
	return *this;
}

FrameGraphNodeInoutInitializer& FrameGraphNodeInoutInitializer::SetName(const std::string& inName)
{
	m_name = inName;
	return *this;
}

FrameGraphNodeInoutInitializer& FrameGraphNodeInoutInitializer::SetInputAccessState(const VkAccessFlags inAccessFlags)
{
	m_inAccess = inAccessFlags;
	return *this;
}

FrameGraphNodeInoutInitializer& FrameGraphNodeInoutInitializer::SetInputPipelineStage(const VkPipelineStageFlags inPipelineStageFlags)
{
	m_inStages = inPipelineStageFlags;
	return *this;
}

FrameGraphNodeInoutInitializer& FrameGraphNodeInoutInitializer::SetOutputAccessState(const VkAccessFlags inAccessFlags)
{
	m_outAccess = inAccessFlags;
	return *this;
}

FrameGraphNodeInoutInitializer& FrameGraphNodeInoutInitializer::SetOutputPipelineStage(const VkPipelineStageFlags inPipelineStageFlags)
{
	m_outStages = inPipelineStageFlags;
	return *this;
}

FrameGraphNodeInoutInitializer& FrameGraphNodeInoutInitializer::SetImageLayouts(
	const VkImageLayout inInitialImageLayout, 
	const VkImageLayout inFinalImageLayout)
{
	if (inInitialImageLayout >= VK_IMAGE_LAYOUT_UNDEFINED && inInitialImageLayout <= VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
	{
		m_initialLayout = inInitialImageLayout;
	}
	if (inFinalImageLayout >= VK_IMAGE_LAYOUT_UNDEFINED && inFinalImageLayout <= VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
	{
		m_finalLayout = inFinalImageLayout;
	}
	return *this;
}

FrameGraphNodeInoutInitializer& FrameGraphNodeInoutInitializer::CustomizeQueueFamilyIndices(
	uint32_t inInitialQueueFamilyIndex, 
	uint32_t inFinalQueueFamilyIndex)
{
	if (inInitialQueueFamilyIndex < UINT32_MAX)
	{
		m_initialQueueFamilyIndex = inInitialQueueFamilyIndex;
	}
	if (inFinalQueueFamilyIndex < UINT32_MAX)
	{
		m_finalQueueFamilyIndex = inFinalQueueFamilyIndex;
	}
	return *this;
}

void FrameGraphNodeInoutInitializer::InitializeInout(
	FrameGraphNodeInput* inoutNodeInput, 
	FrameGraphNodeOutput* inoutNodeOutput) const
{
	CHECK_TRUE(!inoutNodeInput || !inoutNodeOutput);

	// ====================== FrameGraphNodeInput ======================
	bool isImage = m_initialLayout.has_value();
	uint32_t initialQueueFamily = m_initialQueueFamilyIndex.value_or(VK_QUEUE_FAMILY_IGNORED);

	inoutNodeInput->m_name = m_name;
	inoutNodeInput->m_correlatedOutput = inoutNodeOutput;
	inoutNodeInput->m_owner = nullptr;
	if (isImage)
	{
		FrameGraphImageSubResourceState imgInState{};
		imgInState.access = m_inAccess;
		imgInState.layout = m_initialLayout.value();
		imgInState.queueFamily = initialQueueFamily;
		imgInState.stage = m_inStages;
		// imgState.range = {}; // TODO
		inoutNodeInput->m_resourceState = imgInState;
	}
	else
	{
		FrameGraphBufferSubResourceState bufInState{};
		bufInState.access = m_inAccess;
		bufInState.queueFamily = initialQueueFamily;
		bufInState.stage = m_inStages;
		// bufState.offset = 0; // TODO
		// bufState.range = 0;  // TODO
		inoutNodeInput->m_resourceState = bufInState;
	}

	// ====================== FrameGraphNodeOutput ======================
	uint32_t finalQueueFamily = m_finalQueueFamilyIndex.value_or(VK_QUEUE_FAMILY_IGNORED);

	inoutNodeOutput->m_name = m_name;
	inoutNodeOutput->m_connectedInputs.clear();
	inoutNodeOutput->m_owner = nullptr;
	inoutNodeOutput->m_correlatedInput = inoutNodeInput;
	if (isImage)
	{
		FrameGraphImageSubResourceState imgOutState{};
		imgOutState.access = m_outAccess;
		imgOutState.layout = m_finalLayout.value();
		imgOutState.queueFamily = finalQueueFamily;
		imgOutState.stage = m_outStages;
		// imgState.range = {}; // TODO
		inoutNodeOutput->m_resourceState = imgOutState;
	}
	else
	{
		FrameGraphBufferSubResourceState bufOutState{};
		bufOutState.access = m_outAccess;
		bufOutState.queueFamily = finalQueueFamily;
		bufOutState.stage = m_outStages;
		// bufState.offset = 0; // TODO
		// bufState.range = 0;  // TODO
		inoutNodeOutput->m_resourceState = bufOutState;
	}
}

FrameGraphNodeInitializer& FrameGraphNodeInitializer::Reset()
{
	m_tmpInput.clear();
	m_tmpOutput.clear();
	m_queueType = FrameGraphQueueType::GRAPHICS_ONLY;
	m_frameGraph = nullptr;
	return *this;
}

FrameGraphNodeInitializer& FrameGraphNodeInitializer::AddInput(const IFrameGraphNodeInputInitializer* inInitializer)
{
	std::unique_ptr<FrameGraphNodeInput> pInput = std::make_unique<FrameGraphNodeInput>();
	
	inInitializer->InitializeInput(pInput.get());
	m_tmpInput.push_back(std::move(pInput));

	return *this;
}

FrameGraphNodeInitializer& FrameGraphNodeInitializer::AddOutput(const IFrameGraphNodeOutputInitializer* inInitializer)
{
	std::unique_ptr<FrameGraphNodeOutput> pOutput = std::make_unique<FrameGraphNodeOutput>();

	inInitializer->InitializeOutput(pOutput.get());
	m_tmpOutput.push_back(std::move(pOutput));

	return *this;
}

FrameGraphNodeInitializer& FrameGraphNodeInitializer::AddInout(const IFrameGraphNodeInoutInitializer* inInitializer)
{
	std::unique_ptr<FrameGraphNodeInput> pInput = std::make_unique<FrameGraphNodeInput>();
	std::unique_ptr<FrameGraphNodeOutput> pOutput = std::make_unique<FrameGraphNodeOutput>();

	inInitializer->InitializeInout(pInput.get(), pOutput.get());
	m_tmpInput.push_back(std::move(pInput));
	m_tmpOutput.push_back(std::move(pOutput));

	return *this;
}

FrameGraphNodeInitializer& FrameGraphNodeInitializer::SetQueueType(FrameGraphQueueType inQueueType)
{
	m_queueType = inQueueType;

	return *this;
}

FrameGraphNodeInitializer& FrameGraphNodeInitializer::SetOwner(FrameGraph* inFrameGraph)
{
	m_frameGraph = inFrameGraph;

	return *this;
}

void FrameGraphNodeInitializer::InitializeFrameGraphNode(FrameGraphNode* inFrameGraphNode)
{
	auto& device = MyDevice::GetInstance();
	uint32_t graphicsQueue = device.GetQueueFamilyIndexOfType(QueueFamilyType::GRAPHICS);
	uint32_t computeQueue = device.GetQueueFamilyIndexOfType(QueueFamilyType::COMPUTE);
	uint32_t defaultQueue = m_queueType == FrameGraphQueueType::COMPUTE_ONLY ? computeQueue : graphicsQueue;

	for (auto& uptrToMove : m_tmpInput)
	{
		uptrToMove->m_owner = inFrameGraphNode;
		auto& queueFamilyIndex = _GetResourceStateQueueFamilyIndex(uptrToMove->m_resourceState);
		if (queueFamilyIndex == VK_QUEUE_FAMILY_IGNORED)
		{
			queueFamilyIndex = defaultQueue;
		}
		inFrameGraphNode->m_inputs.push_back(std::move(uptrToMove));
	}
	m_tmpInput.clear();

	for (auto& uptrToMove : m_tmpOutput)
	{
		uptrToMove->m_owner = inFrameGraphNode;
		auto& queueFamilyIndex = _GetResourceStateQueueFamilyIndex(uptrToMove->m_resourceState);
		if (queueFamilyIndex == VK_QUEUE_FAMILY_IGNORED)
		{
			queueFamilyIndex = defaultQueue;
		}
		inFrameGraphNode->m_outputs.push_back(std::move(uptrToMove));
	}
	m_tmpOutput.clear();

	inFrameGraphNode->m_queueType = m_queueType;
	inFrameGraphNode->m_graph = m_frameGraph;
}

#pragma endregion

#pragma region InputOutput

auto FrameGraphNodeInput::GetConnectedOutput() const -> FrameGraphNodeOutput*
{
	return m_connectedOutput;
}

auto FrameGraphNodeInput::GetResourceHandle() const -> const FRAME_GRAPH_RESOURCE_HANDLE&
{
	auto pOutput = GetConnectedOutput();
	
	CHECK_TRUE(pOutput != nullptr);

	return pOutput->GetResourceHandle();
}

auto FrameGraphNodeInput::GetRequiredResourceState() const -> const FRAME_GRAPH_RESOURCE_STATE&
{
	return m_resourceState;
}

auto FrameGraphNodeInput::GetOwner() const -> const FrameGraphNode*
{
	return m_owner;
}

auto FrameGraphNodeInput::GetOwner() -> FrameGraphNode*
{
	return m_owner;
}

auto FrameGraphNodeInput::IsMutableReference() const -> bool
{
	return m_correlatedOutput != nullptr;
}

auto FrameGraphNodeOutput::GetConnectedInputs() const -> const std::vector<FrameGraphNodeInput*>&
{
	return m_connectedInputs;
}

auto FrameGraphNodeOutput::GetResourceHandle() const -> const FRAME_GRAPH_RESOURCE_HANDLE&
{
	return m_resourceHandle;
}

auto FrameGraphNodeOutput::GetProcessedResourceState() const -> const FRAME_GRAPH_RESOURCE_STATE&
{
	return m_resourceState;
}

auto FrameGraphNodeOutput::GetOwner() const -> const FrameGraphNode*
{
	return m_owner;
}

auto FrameGraphNodeOutput::GetOwner() -> FrameGraphNode*
{
	return m_owner;
}

auto FrameGraphNodeOutput::IsReference() const -> bool
{
	return m_correlatedInput != nullptr;
}

void FrameGraphNodeOutput::ConnectToInput(FrameGraphNodeInput* inoutFrameGraphInputPtr)
{
	bool hasThisConnected = false;

	for (size_t i = 0; i < m_connectedInputs.size(); ++i)
	{
		hasThisConnected = (m_connectedInputs[i] == inoutFrameGraphInputPtr);
		if (hasThisConnected)
		{
			break;
		}
	}

	// After contemplation, I decide to allow connecting mutable inputs and const inputs
	// concurrently, in this case, we need extra dependencies to make sure that mutable
	// pass happens after const pass
	//if (inoutFrameGraphInputPtr->IsMutableReference())
	//{
	//	CHECK_TRUE(m_connectedInputs.empty() || (hasThisConnected && m_connectedInputs.size() == 1));
	//}

	if (!hasThisConnected)
	{
		m_connectedInputs.push_back(inoutFrameGraphInputPtr);
	}
}

#pragma endregion

#pragma region Node
void FrameGraphNode::_DoForEachNextInput(std::function<void(FrameGraphNodeInput*)> inFuncToDo) const
{
	for (auto& pOutput : m_outputs)
	{
		auto& nextInputs = pOutput->GetConnectedInputs();

		for (auto pNextInput : nextInputs)
		{
			inFuncToDo(pNextInput);
		}
	}
}

void FrameGraphNode::Init(FrameGraphNodeInitializer* inInitializer)
{
	inInitializer->InitializeFrameGraphNode(this);
	CHECK_TRUE(m_graph != nullptr);
}

void FrameGraphNode::AddExtraDependency(FrameGraphNode* inNodeThisDependsOn)
{
	m_extraDependencies.insert(inNodeThisDependsOn);
}

void FrameGraphNode::GetPreGraphNodes(std::set<FrameGraphNode*>& outFrameGraphNodes) const
{
	outFrameGraphNodes.insert(m_extraDependencies.begin(), m_extraDependencies.end());
	for (auto& uptrInput : m_inputs)
	{
		outFrameGraphNodes.insert(uptrInput->GetOwner());
	}
}

void FrameGraphNode::RequireInputResourceState() const
{
	FrameGraphCompileContext compileContext{}; // TODO

	for (auto& uptrInput : m_inputs)
	{
		auto& handle = uptrInput->GetResourceHandle();

		if (std::holds_alternative<FrameGraphImageHandle>(handle))
		{
			const FrameGraphImageHandle& imgHandle = 
				std::get<FrameGraphImageHandle>(handle);
			const FrameGraphImageSubResourceState& imgResource = 
				std::get<FrameGraphImageSubResourceState>(uptrInput->GetRequiredResourceState());
			
			compileContext.RequireSubResourceStateBeforePass(imgHandle, imgResource);
		}
		else if (std::holds_alternative<FrameGraphBufferHandle>(handle))
		{
			const FrameGraphBufferHandle& bufHandle = 
				std::get<FrameGraphBufferHandle>(handle);
			const FrameGraphBufferSubResourceState& bufResource = 
				std::get<FrameGraphBufferSubResourceState>(uptrInput->GetRequiredResourceState());
			
			compileContext.RequireSubResourceStateBeforePass(bufHandle, bufResource);
		}
		else
		{
			CHECK_TRUE(false);
		}
	}
}

void FrameGraphNode::MergeOutputResourceState() const
{
	FrameGraphCompileContext compileContext{}; // TODO

	for (auto& uptrOutput : m_outputs)
	{
		auto& handle = uptrOutput->GetResourceHandle();
		if (std::holds_alternative<FrameGraphImageHandle>(handle))
		{
			const FrameGraphImageHandle& imgHandle = 
				std::get<FrameGraphImageHandle>(handle);
			const FrameGraphImageSubResourceState& imgResource = 
				std::get<FrameGraphImageSubResourceState>(uptrOutput->GetProcessedResourceState());
			
			compileContext.MergeSubResourceStateAfterPass(imgHandle, imgResource);
		}
		else if (std::holds_alternative<FrameGraphBufferHandle>(handle))
		{
			const FrameGraphBufferHandle& bufHandle = 
				std::get<FrameGraphBufferHandle>(handle);
			const FrameGraphBufferSubResourceState& bufResource = 
				std::get<FrameGraphBufferSubResourceState>(uptrOutput->GetProcessedResourceState());
			
			compileContext.MergeSubResourceStateAfterPass(bufHandle, bufResource);
		}
		else
		{
			CHECK_TRUE(false);
		}
	}
}

void FrameGraphNode::PresageResourceStateTransfer() const
{
	FrameGraphCompileContext compileContext{}; // TODO
	auto funcInformNextState = [&](FrameGraphNodeInput* pInput)
	{
		auto& handle = pInput->GetResourceHandle();
		if (std::holds_alternative<FrameGraphImageHandle>(handle))
		{
			const FrameGraphImageHandle& imgHandle =
				std::get<FrameGraphImageHandle>(pInput->GetResourceHandle());
			const FrameGraphImageSubResourceState& imgResource =
				std::get<FrameGraphImageSubResourceState>(pInput->GetRequiredResourceState());

			compileContext.PresageSubResourceStateNextPass(imgHandle, imgResource);
		}
		else if (std::holds_alternative<FrameGraphBufferHandle>(handle))
		{
			const FrameGraphBufferHandle& bufHandle =
				std::get<FrameGraphBufferHandle>(pInput->GetResourceHandle());
			const FrameGraphBufferSubResourceState& bufResource =
				std::get<FrameGraphBufferSubResourceState>(pInput->GetRequiredResourceState());

			compileContext.PresageSubResourceStateNextPass(bufHandle, bufResource);
		}
		else
		{
			CHECK_TRUE(false);
		}
	};
	_DoForEachNextInput(funcInformNextState);
}

void FrameGraphNode::UpdateResourceReferenceCount() const
{
	FrameGraphCompileContext compileContext{}; // TODO

	for (auto& uptrInput : m_inputs)
	{
		auto& handle = uptrInput->GetResourceHandle();

		if (std::holds_alternative<FrameGraphImageHandle>(handle))
		{
			const FrameGraphImageHandle& imgHandle = 
				std::get<FrameGraphImageHandle>(handle);
			const FrameGraphImageSubResourceState& imgResource = 
				std::get<FrameGraphImageSubResourceState>(uptrInput->GetRequiredResourceState());
			
			compileContext.ReleaseSubResourceReference(imgHandle, imgResource.range);
		}
		else if (std::holds_alternative<FrameGraphBufferHandle>(handle))
		{
			const FrameGraphBufferHandle& bufHandle = 
				std::get<FrameGraphBufferHandle>(handle);
			const FrameGraphBufferSubResourceState& bufResource = 
				std::get<FrameGraphBufferSubResourceState>(uptrInput->GetRequiredResourceState());
			
			compileContext.ReleaseSubResourceReference(bufHandle, bufResource.offset, bufResource.size);
		}
		else
		{
			CHECK_TRUE(false);
		}
	}

	auto funcAddReference = [&](FrameGraphNodeInput* pInput)
	{
		auto& handle = pInput->GetResourceHandle();
		if (std::holds_alternative<FrameGraphImageHandle>(handle))
		{
			const FrameGraphImageHandle& imgHandle =
				std::get<FrameGraphImageHandle>(pInput->GetResourceHandle());
			const FrameGraphImageSubResourceState& imgResource =
				std::get<FrameGraphImageSubResourceState>(pInput->GetRequiredResourceState());

			compileContext.AddSubResourceReference(imgHandle, imgResource.range);
		}
		else if (std::holds_alternative<FrameGraphBufferHandle>(handle))
		{
			const FrameGraphBufferHandle& bufHandle =
				std::get<FrameGraphBufferHandle>(pInput->GetResourceHandle());
			const FrameGraphBufferSubResourceState& bufResource =
				std::get<FrameGraphBufferSubResourceState>(pInput->GetRequiredResourceState());

			compileContext.AddSubResourceReference(bufHandle, bufResource.offset, bufResource.size);
		}
		else
		{
			CHECK_TRUE(false);
		}
	};
	_DoForEachNextInput(funcAddReference);
}
#pragma endregion

#undef FRAME_GRAPH_RESOURCE_STATE
#undef FRAME_GRAPH_RESOURCE_HANDLE

FrameGraphPass& FrameGraphPass::AddInAttachment(const FrameGraphImageSubResourceState* inRequireState)
{
	AttachmentInfo info{};

	info.imageState = *inRequireState;
	info.shouldHoldResource = false;
	info.hasInput = true;
	info.hasOutput = false;
	attachments.push_back(std::move(info));

	return *this;
}

FrameGraphPass& FrameGraphPass::AddOutAttachment(const FrameGraphImageSubResourceState* inFinalState)
{
	AttachmentInfo info{};

	info.imageState = *inFinalState;
	info.shouldHoldResource = true;
	info.hasInput = false;
	info.hasOutput = true;
	attachments.push_back(std::move(info));

	return *this;
}

FrameGraphPass& FrameGraphPass::AddInoutAttachment(
	const FrameGraphImageSubResourceState* inInitialState, 
	const FrameGraphImageSubResourceState* inFinalState)
{
	AttachmentInfo info{};

	info.imageState = *inInitialState;
	info.imageState2 = *inFinalState;
	info.shouldHoldResource = false;
	info.hasInput = true;
	info.hasOutput = true;
	attachments.push_back(std::move(info));

	return *this;
}

FrameGraphPass& FrameGraphPass::AddTransientAttachment(
	const FrameGraphImageSubResourceState* inInitialState, 
	const FrameGraphImageSubResourceState* inFinalState)
{
	AttachmentInfo info{};

	info.imageState = *inInitialState;
	info.imageState2 = *inFinalState;
	info.shouldHoldResource = true;
	info.hasInput = false;
	info.hasOutput = false;
	attachments.push_back(std::move(info));

	return *this;
}

FrameGraphPass& FrameGraphPass::AddInAttachment(const FrameGraphBufferSubResourceState* inRequireState)
{
	AttachmentInfo info{};

	info.bufferState = *inRequireState;
	info.shouldHoldResource = false;
	info.hasInput = true;
	info.hasOutput = false;
	attachments.push_back(std::move(info));

	return *this;
}

FrameGraphPass& FrameGraphPass::AddOutAttachment(const FrameGraphBufferSubResourceState* inFinalState)
{
	AttachmentInfo info{};

	info.bufferState = *inFinalState;
	info.shouldHoldResource = true;
	info.hasInput = false;
	info.hasOutput = true;
	attachments.push_back(std::move(info));

	return *this;
}

FrameGraphPass& FrameGraphPass::AddInoutAttachment(
	const FrameGraphBufferSubResourceState* inInitialState, 
	const FrameGraphBufferSubResourceState* inFinalState)
{
	AttachmentInfo info{};

	info.bufferState = *inInitialState;
	info.bufferState2 = *inFinalState;
	info.shouldHoldResource = false;
	info.hasInput = true;
	info.hasOutput = true;
	attachments.push_back(std::move(info));

	return *this;
}

FrameGraphPass& FrameGraphPass::AddTransientAttachment(
	const FrameGraphBufferSubResourceState* inInitialState, 
	const FrameGraphBufferSubResourceState* inFinalState)
{
	AttachmentInfo info{};

	info.bufferState = *inInitialState;
	info.bufferState2 = *inFinalState;
	info.shouldHoldResource = true;
	info.hasInput = false;
	info.hasOutput = false;
	attachments.push_back(std::move(info));

	return *this;
}
