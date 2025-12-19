#pragma once
#include "utility/my_gui.h"
#include "commandbuffer.h"
#include "common.h"
class GraphicsProgram;
class RenderPass;
class Framebuffer;
class Image;
class ImageView;
class MyGUI;

class GUIPass
{
private:
	uint32_t m_uMaxFrameCount = 3u;
	uint32_t m_uCurrentFrame = 0u;
	std::unique_ptr<GraphicsProgram> m_uptrProgram;
	std::unique_ptr<RenderPass> m_uptrRenderPass;
	std::vector<std::unique_ptr<Framebuffer>> m_uptrFramebuffers;
	std::vector<std::unique_ptr<Image>> m_uptrImages;
	std::vector<std::unique_ptr<ImageView>> m_uptrViews;
	std::vector<std::unique_ptr<CommandSubmission>> m_uptrCmds;
	std::unique_ptr<MyGUI> m_uptrGUI;
	VkSampler m_sampler = VK_NULL_HANDLE;

private:
	void _InitSampler();
	void _UninitSampler();

	void _InitImageAndViews();
	void _UninitImageAndViews();

	void _InitRenderPass();
	void _UninitRenderPass();

	void _InitFramebuffers();
	void _UninitFramebuffers();

	void _InitPipeline();
	void _UninitPipeline();

	void _InitCommandBuffer();
	void _UninitCommandBuffer();

	void _InitMyGUI();
	void _UninitMyGUI();

public:
	GUIPass();
	~GUIPass();

	void Init(uint32_t _frameInFlight);

	void Uninit();

	void OnSwapchainRecreated();

	MyGUI& StartPass(const VkDescriptorImageInfo& _originalImage, const std::vector<VkSemaphore>& _waitSignals);

	// Execute the pass, signal _finishSignals when pass done, _outputImage is the result that can be used as texture in other pass
	void EndPass(const std::vector<VkSemaphore>& _finishSignals, VkDescriptorImageInfo& _outputImage);
};