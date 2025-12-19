#include "swapchain_pass.h"
#include "device.h"
#include "image.h"
#include "pipeline_io.h"
#include "pipeline_program.h"
#include "shader.h"

void SwapchainPass::_InitViews()
{
	std::vector<Image*> pSwapchainImages;
	MyDevice::GetInstance().GetSwapchainImagePointers(pSwapchainImages);
	m_swapchainViews.reserve(pSwapchainImages.size());
	for (size_t i = 0; i < pSwapchainImages.size(); ++i)
	{
		std::unique_ptr<ImageView> uptrView = std::make_unique<ImageView>(pSwapchainImages[i]->NewImageView(VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1));
		uptrView->Init();
		m_swapchainViews.push_back(std::move(uptrView));
	}
}

void SwapchainPass::_UninitViews()
{
	for (auto& uptrView : m_swapchainViews)
	{
		uptrView->Uninit();
		uptrView.reset();
	}
	m_swapchainViews.clear();
}

void SwapchainPass::_InitRenderPass()
{
	RenderPass::Subpass subpass{};

	m_renderPass = std::make_unique<RenderPass>();
	m_renderPass->PreAddAttachment(RenderPass::AttachmentPreset::SWAPCHAIN);
	subpass.AddColorAttachment(0);
	m_renderPass->PreAddSubpass(subpass);

	m_renderPass->Init();
}

void SwapchainPass::_UninitRenderPass()
{
	if (m_renderPass.get() != nullptr)
	{
		m_renderPass->Uninit();
	}
	m_renderPass.reset();
}

void SwapchainPass::_InitPipeline()
{
	m_program = std::make_unique<GraphicsProgram>();
	m_program->PresetRenderPass(m_renderPass.get(), 0);
	m_program->Init({ "E:/GitStorage/LearnVulkan/bin/shaders/pass_through.vert.spv", "E:/GitStorage/LearnVulkan/bin/shaders/swapchain.frag.spv" }, m_uMaxFrameCount);
}

void SwapchainPass::_UninitPipeline()
{
	if (m_program)
	{
		m_program->Uninit();
		m_program.reset();
	}
}

void SwapchainPass::_InitFramebuffer()
{
	int n = m_swapchainViews.size();
	m_framebuffers.reserve(n);

	for (int i = 0; i < n; ++i)
	{
		std::unique_ptr<Framebuffer> uptrFramebuffer = std::make_unique<Framebuffer>(m_renderPass->NewFramebuffer({ m_swapchainViews[i].get() }));
		
		uptrFramebuffer->Init();

		m_framebuffers.push_back(std::move(uptrFramebuffer));
	}
}

void SwapchainPass::_UninitFramebuffer()
{
	for (auto& uptrFramebuffer : m_framebuffers)
	{
		if (uptrFramebuffer.get() != nullptr)
		{
			uptrFramebuffer->Uninit();
		}
		uptrFramebuffer.reset();
	}
	m_framebuffers.clear();
}

void SwapchainPass::_InitCommandBuffer()
{
	for (uint32_t i = 0; i < m_uMaxFrameCount; ++i)
	{
		std::unique_ptr<CommandSubmission> uptrCmd = std::make_unique<CommandSubmission>();
		uptrCmd->Init();
		m_cmds.push_back(std::move(uptrCmd));
	}
}

void SwapchainPass::_UninitCommandBuffer()
{
	for (auto& uptr : m_cmds)
	{
		uptr->Uninit();
	}
	m_cmds.clear();
}

void SwapchainPass::_InitSemaphores()
{
	auto& device = MyDevice::GetInstance();

	m_aquireImages.resize(m_uMaxFrameCount);

	for (int i = 0; i < m_uMaxFrameCount; ++i)
	{
		m_aquireImages[i] = device.CreateVkSemaphore();
	}
}

void SwapchainPass::_UninitSemaphores()
{
	auto& device = MyDevice::GetInstance();

	for (auto& semaphore : m_aquireImages)
	{
		device.DestroyVkSemaphore(semaphore);
	}
	m_aquireImages.clear();
}

SwapchainPass::SwapchainPass()
{
}

SwapchainPass::~SwapchainPass()
{
}

void SwapchainPass::Init(uint32_t _frameInFlight)
{
	m_uMaxFrameCount = _frameInFlight;
	_InitViews();
	_InitRenderPass();
	_InitPipeline();
	_InitFramebuffer();
	_InitCommandBuffer();
	_InitSemaphores();
}

void SwapchainPass::Uninit()
{
	_UninitSemaphores();
	_UninitCommandBuffer();
	_UninitFramebuffer();
	_UninitPipeline();
	_UninitRenderPass();
	_UninitViews();
}

void SwapchainPass::OnSwapchainRecreated()
{
	_UninitSemaphores();
	_UninitFramebuffer();
	_UninitViews();
	_InitViews();
	_InitFramebuffer();
	_InitSemaphores();
	m_program->GetDescriptorSetManager().ClearDescriptorSets();
}

void SwapchainPass::Execute(const VkDescriptorImageInfo& _originalImage, const std::vector<VkSemaphore>& _waitSignals)
{
	auto pCmd = m_cmds[m_uCurrentFrame].get();
	auto& device = MyDevice::GetInstance();
	std::optional<uint32_t> index;
	VkSemaphore semaphoreRenderFinish = VK_NULL_HANDLE;
	CommandSubmission::WaitInformation waitForSwapchain{ m_aquireImages[m_uCurrentFrame], VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	GraphicsPipeline::PipelineInput_Draw input{};
	std::vector<CommandSubmission::WaitInformation> waitInfos{};

	pCmd->WaitTillAvailable(); // make sure that m_aquireImages[m_uCurrentFrame] is signaled
	index = device.AquireAvailableSwapchainImageIndex(m_aquireImages[m_uCurrentFrame]);
	if (!index.has_value())
	{
		device.WaitIdle();  // make sure that m_aquireImages[m_uCurrentFrame] is signaled
		return;
	}

	for (const auto& semaphore : _waitSignals)
	{
		CommandSubmission::WaitInformation waitInfo{};

		waitInfo.waitPipelineStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		waitInfo.waitSamaphore = semaphore;

		waitInfos.push_back(waitInfo);
	}
	waitInfos.push_back(waitForSwapchain);

	pCmd->StartCommands(waitInfos);
	m_program->BindFramebuffer(pCmd, m_framebuffers[index.value()].get());
	auto& binder = m_program->GetDescriptorSetManager();
	binder.StartBind();
	binder.BindDescriptor(0, 0, { _originalImage }, DescriptorSetManager::DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME);
	binder.EndBind();
	m_program->Draw(pCmd, 6);
	m_program->UnbindFramebuffer(pCmd);
	semaphoreRenderFinish = pCmd->SubmitCommands();

	device.PresentSwapchainImage({ semaphoreRenderFinish }, index.value());
	
	m_program->EndFrame();
	m_uCurrentFrame = (m_uCurrentFrame + 1) % m_uMaxFrameCount;
}
