#pragma once
#include "common.h"
#include "image.h"
#include "commandbuffer.h"
#include "utility/my_gui.h"

class GraphicsProgram;
class RenderPass;
class Framebuffer;
class DescriptorSetLayout;
class DescriptorSet;

class SwapchainPass
{
private:
	uint32_t m_uMaxFrameCount = 1u;
	uint32_t m_uCurrentFrame = 0u;

	std::vector<VkSemaphore> m_aquireImages;
	std::vector<std::unique_ptr<ImageView>> m_swapchainViews;

	std::unique_ptr<GraphicsProgram> m_program;				  // graphics pipeline that run pass through shaders
	std::unique_ptr<RenderPass> m_renderPass;				  // render pass that write result color to swapchain
	std::vector<std::unique_ptr<Framebuffer>> m_framebuffers; // frame buffers that hold swapchain

	std::vector<std::unique_ptr<CommandSubmission>> m_cmds;

private:
	void _InitViews();
	void _UninitViews();

	void _InitRenderPass();
	void _UninitRenderPass();

	void _InitPipeline();
	void _UninitPipeline();

	void _InitFramebuffer();
	void _UninitFramebuffer();

	void _InitCommandBuffer();
	void _UninitCommandBuffer();

	void _InitSemaphores();
	void _UninitSemaphores();

public:
	SwapchainPass();
	~SwapchainPass();

	// Init this pass
	void Init(uint32_t _frameInFlight);

	// Destroy attribute this pass holds
	void Uninit();

	// call this after swapchain images are recreated,
	// this pass will destroy necessary objects it holds and recreate them
	void OnSwapchainRecreated();
	
	// this pass will get a swapchain image, render the result to it and then present the swapchain
	void Execute(const VkDescriptorImageInfo& _originalImage, const std::vector<VkSemaphore>& _waitSignals);
};