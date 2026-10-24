#include "pipeline/render_pass.h"
#include "command_buffer.h"
#include "device.h"

auto CommandBuffer::_AppendCommand(const Command* inCommand)->void
{
	CHECK_TRUE(inCommand != nullptr, "No command to append!");
	m_commands.push_back(inCommand);
}

auto CommandBuffer::_AppendOwnedCommand(std::unique_ptr<Command> inCommand)->void
{
	CHECK_TRUE(inCommand != nullptr, "No command to append!");
	const Command* command = inCommand.get();
	m_ownedCommands.push_back(std::move(inCommand));
	_AppendCommand(command);
}

auto CommandBuffer::_Append(CommandBuffer&& inCommandBuffer)->CommandBuffer&
{
	CHECK_TRUE(
		std::holds_alternative<std::monostate>(inCommandBuffer.m_renderingScopeState),
		"Cannot append a command buffer with an active rendering scope!");
	if (!std::holds_alternative<std::monostate>(m_renderingScopeState))
	{
		CHECK_TRUE(
			!inCommandBuffer.m_hasRenderingCommands && !inCommandBuffer.m_hasProgramBindings,
			"Cannot append rendering or program-binding commands into an active rendering scope!");
	}

	m_commands.insert(
		m_commands.end(),
		inCommandBuffer.m_commands.begin(),
		inCommandBuffer.m_commands.end());
	for (std::unique_ptr<Command>& command : inCommandBuffer.m_ownedCommands)
	{
		m_ownedCommands.push_back(std::move(command));
	}
	m_hasRenderingCommands = m_hasRenderingCommands || inCommandBuffer.m_hasRenderingCommands;
	m_hasProgramBindings = m_hasProgramBindings || inCommandBuffer.m_hasProgramBindings;
	m_graphicsProgram = nullptr;
	m_computeProgram = nullptr;
	m_rayTracingProgram = nullptr;

	inCommandBuffer.m_commands.clear();
	inCommandBuffer.m_ownedCommands.clear();
	inCommandBuffer.m_hasRenderingCommands = false;
	inCommandBuffer.m_hasProgramBindings = false;
	inCommandBuffer.m_graphicsProgram = nullptr;
	inCommandBuffer.m_computeProgram = nullptr;
	inCommandBuffer.m_rayTracingProgram = nullptr;
	return *this;
}

auto CommandBuffer::BeginRenderPass(
	const BeginRenderPassCommand::Parameters& inParameters,
	uint32_t inSubpassCount)->CommandBuffer&
{
	CHECK_TRUE(
		std::holds_alternative<std::monostate>(m_renderingScopeState),
		"A rendering scope is already active!");
	CHECK_TRUE(inParameters.renderPass != VK_NULL_HANDLE, "Invalid render pass!");
	CHECK_TRUE(inParameters.framebuffer != VK_NULL_HANDLE, "Invalid framebuffer!");
	CHECK_TRUE(inSubpassCount > 0, "Render pass must contain at least one subpass!");

	auto command = std::make_unique<BeginRenderPassCommand>();
	command->SetParameters(inParameters);
	_AppendOwnedCommand(std::move(command));
	m_hasRenderingCommands = true;
	m_graphicsProgram = nullptr;

	m_renderingScopeState = LegacyRenderPassState{
		inParameters.renderPass,
		inParameters.framebuffer,
		0,
		inSubpassCount,
		inParameters.contents};
	return *this;
}

auto CommandBuffer::NextSubpass(VkSubpassContents inContents)->CommandBuffer&
{
	LegacyRenderPassState* state = std::get_if<LegacyRenderPassState>(&m_renderingScopeState);
	CHECK_TRUE(state != nullptr, "NextSubpass requires an active render pass!");
	CHECK_TRUE(
		state->currentSubpass + 1 < state->subpassCount,
		"No next subpass exists!");

	auto command = std::make_unique<NextSubpassCommand>();
	command->SetContents(inContents);
	_AppendOwnedCommand(std::move(command));

	++state->currentSubpass;
	m_graphicsProgram = nullptr;
	state->contents = inContents;
	return *this;
}

auto CommandBuffer::EndRenderPass()->CommandBuffer&
{
	CHECK_TRUE(
		std::holds_alternative<LegacyRenderPassState>(m_renderingScopeState),
		"EndRenderPass requires an active render pass!");

	_AppendOwnedCommand(std::make_unique<EndRenderPassCommand>());
	m_renderingScopeState = std::monostate{};
	m_graphicsProgram = nullptr;
	return *this;
}

auto CommandBuffer::AddCommands(const Command* const* inCommands, size_t inCount)->CommandBuffer&
{
	if (inCount == 0)
	{
		return *this;
	}

	CHECK_TRUE(inCommands != nullptr, "No commands!");
	for (size_t commandIndex = 0; commandIndex < inCount; ++commandIndex)
	{
		const Command* command = inCommands[commandIndex];
		CHECK_TRUE(command != nullptr, "No command to append!");
		CHECK_TRUE(
			dynamic_cast<const BeginRenderPassCommand*>(command) == nullptr &&
			dynamic_cast<const NextSubpassCommand*>(command) == nullptr &&
			dynamic_cast<const EndRenderPassCommand*>(command) == nullptr,
			"Rendering scope commands must be appended through CommandBuffer members!");
	}
	for (size_t commandIndex = 0; commandIndex < inCount; ++commandIndex)
	{
		_AppendCommand(inCommands[commandIndex]);
	}
	return *this;
}

// Prepared on the submitting thread; worker threads only read resolved handles.
struct CommandBuffer::BindingCommand final : Command
{
	GraphicsShaderProgram* graphics = nullptr;
	ComputeShaderProgram* compute = nullptr;
	RayTracingShaderProgram* rayTracing = nullptr;
	GraphicsPipelineStateInfo state;
	const PipelineLayout* resourceLayout = nullptr;
	std::vector<DescriptorSetDesc> descriptions;
	VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	mutable VkPipeline pipeline = VK_NULL_HANDLE;
	mutable VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	mutable std::vector<std::pair<uint32_t, VkDescriptorSet>> sets;
	mutable bool prepared = false;

	void _Prepare(DynamicDescriptorSetAllocator& allocator, QueueFamilyType queue) const
	{
		CHECK_TRUE(queue == QueueFamilyType::GRAPHICS ||
		               (queue == QueueFamilyType::COMPUTE && bindPoint != VK_PIPELINE_BIND_POINT_GRAPHICS),
		    "Pipeline cannot bind on this queue!");
		if (bindPoint == VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR)
		{
			auto& device = MyDevice::GetInstance();
			uint32_t count = 0;
			vkGetPhysicalDeviceQueueFamilyProperties(device.vkPhysicalDevice, &count, nullptr);
			std::vector<VkQueueFamilyProperties> families(count);
			vkGetPhysicalDeviceQueueFamilyProperties(device.vkPhysicalDevice, &count, families.data());
			const auto family = device.GetQueueFamilyIndexOfType(queue);
			CHECK_TRUE(family < count && (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT),
			    "Ray tracing requires a compute-capable queue family!");
		}
		if (graphics)
			pipeline = graphics->GetVkPipeline(state);
		else if (compute)
			pipeline = compute->GetVkPipeline();
		else if (rayTracing)
			pipeline = rayTracing->GetVkPipeline();
		else
		{
			pipelineLayout = resourceLayout->GetVkPipelineLayout();
			sets.clear();
			for (uint32_t set = 0; set < descriptions.size(); ++set)
			{
				if (resourceLayout->GetVkDescriptorBindings()[set].empty())
					continue;
				DynamicDescriptorSetAllocator::AllocateInfo info;
				info.SetLayout(resourceLayout->GetDescriptorSetLayout(set));
				info.SetDescriptorSetDesc(descriptions[set]);
				sets.emplace_back(set, allocator.GetOrAllocateDescriptorSet(info));
			}
		}
		prepared = true;
	}
	void Record(VkCommandBuffer commandBuffer) const override
	{
		CHECK_TRUE(prepared, "Shader binding command was not prepared by FrameSlot!");
		if (graphics || compute || rayTracing)
			vkCmdBindPipeline(commandBuffer, bindPoint, pipeline);
		else
			for (const auto& [set, descriptor] : sets)
				vkCmdBindDescriptorSets(commandBuffer, bindPoint, pipelineLayout,
				    set, 1, &descriptor, 0, nullptr);
	}
};

auto CommandBuffer::BindGraphicsProgram(GraphicsShaderProgram& program, const GraphicsPipelineStateInfo& input)
    -> CommandBuffer&
{
	const auto* scope = std::get_if<LegacyRenderPassState>(&m_renderingScopeState);
	CHECK_TRUE(scope != nullptr && scope->contents == VK_SUBPASS_CONTENTS_INLINE,
	    "Graphics binding requires an inline render pass!");
	CHECK_TRUE(program.GetLayout().GetVkPipelineLayout() != VK_NULL_HANDLE, "Graphics program was not created!");
	auto state = program._ResolveState(input);
	CHECK_TRUE(
	    state.m_renderPassPtr->GetVkRenderPass() == scope->renderPass && state.m_subpassIndex == scope->currentSubpass,
	    "Graphics pipeline does not match the active render pass/subpass!");
	auto command = std::make_unique<BindingCommand>();
	command->graphics = &program;
	command->state = std::move(state);
	_AppendOwnedCommand(std::move(command));
	m_graphicsProgram = &program;
	m_hasProgramBindings = true;
	return *this;
}
auto CommandBuffer::BindComputeProgram(ComputeShaderProgram& program) -> CommandBuffer&
{
	CHECK_TRUE(std::holds_alternative<std::monostate>(m_renderingScopeState),
	    "Compute binding requires no active render pass!");
	CHECK_TRUE(program.GetVkPipeline() != VK_NULL_HANDLE, "Compute program was not created!");
	auto command = std::make_unique<BindingCommand>();
	command->compute = &program;
	command->bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
	_AppendOwnedCommand(std::move(command));
	m_computeProgram = &program;
	m_hasProgramBindings = true;
	return *this;
}
auto CommandBuffer::BindRayTracingProgram(RayTracingShaderProgram& program) -> CommandBuffer&
{
	CHECK_TRUE(std::holds_alternative<std::monostate>(m_renderingScopeState),
	    "Ray tracing binding requires no active render pass!");
	CHECK_TRUE(program.GetVkPipeline() != VK_NULL_HANDLE, "Ray tracing program was not created!");
	auto command = std::make_unique<BindingCommand>();
	command->rayTracing = &program;
	command->bindPoint = VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
	_AppendOwnedCommand(std::move(command));
	m_rayTracingProgram = &program;
	m_hasProgramBindings = true;
	return *this;
}

auto CommandBuffer::BindGraphicsShaderResource(const ShaderResources& resources) -> CommandBuffer&
{
	CHECK_TRUE(m_graphicsProgram, "Bind a graphics program before shader resources!");
	const auto* scope = std::get_if<LegacyRenderPassState>(&m_renderingScopeState);
	CHECK_TRUE(scope && scope->contents == VK_SUBPASS_CONTENTS_INLINE,
	    "Graphics resources require an inline render pass!");
	return _BindShaderResources(m_graphicsProgram->m_pipelineLayout, VK_PIPELINE_BIND_POINT_GRAPHICS, resources);
}

auto CommandBuffer::BindComputeShaderResource(const ShaderResources& resources) -> CommandBuffer&
{
	CHECK_TRUE(m_computeProgram, "Bind a compute program before shader resources!");
	CHECK_TRUE(std::holds_alternative<std::monostate>(m_renderingScopeState),
	    "Compute resources require no active render pass!");
	return _BindShaderResources(m_computeProgram->m_pipelineLayout, VK_PIPELINE_BIND_POINT_COMPUTE, resources);
}

auto CommandBuffer::BindRayTracingShaderResource(const ShaderResources& resources) -> CommandBuffer&
{
	CHECK_TRUE(m_rayTracingProgram, "Bind a ray tracing program before shader resources!");
	CHECK_TRUE(std::holds_alternative<std::monostate>(m_renderingScopeState),
	    "Ray tracing resources require no active render pass!");
	return _BindShaderResources(m_rayTracingProgram->m_pipelineLayout, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, resources);
}

auto CommandBuffer::_BindShaderResources(const PipelineLayout& layout, VkPipelineBindPoint bindPoint,
    const ShaderResources& resources) -> CommandBuffer&
{
	auto command = std::make_unique<BindingCommand>();
	command->resourceLayout = &layout;
	command->descriptions = resources._Resolve(layout);
	command->bindPoint = bindPoint;
	_AppendOwnedCommand(std::move(command));
	m_hasProgramBindings = true;
	return *this;
}
void CommandBuffer::_PrepareCommands(DynamicDescriptorSetAllocator& allocator, QueueFamilyType queue)
{
	for (const auto* command : m_commands)
		if (const auto* binding = dynamic_cast<const BindingCommand*>(command))
			binding->_Prepare(allocator, queue);
}
