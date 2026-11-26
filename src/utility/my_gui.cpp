#include "my_gui.h"
#include "device.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"
#include <algorithm>

void MyGUI::_StartNewFrame()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void MyGUI::Create()
{
	const auto& myDevice = MyDevice::GetInstance();
	const uint32_t imageCount = static_cast<uint32_t>(myDevice.m_uptrSwapchainImages.size());
	CHECK_TRUE(imageCount != 0, "GUI creation requires an initialized swapchain!");

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;

	// Setup Dear ImGui style
	ImGui::StyleColorsDark();

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForVulkan(myDevice.pWindow, true);
	ImGui_ImplVulkan_InitInfo init_info{};
	init_info.ApiVersion = VK_API_VERSION_1_3;              // Pass in your value of VkApplicationInfo::apiVersion, otherwise will default to header version.
	init_info.Instance = myDevice.vkInstance;
	init_info.PhysicalDevice = myDevice.vkPhysicalDevice;
	init_info.Device = myDevice.vkDevice;
	init_info.QueueFamily = myDevice.GetQueueFamilyIndexOfType(QueueFamilyType::GRAPHICS);
	init_info.Queue = myDevice.GetQueueOfType(QueueFamilyType::GRAPHICS);
	init_info.DescriptorPoolSize = 1000;
	init_info.PipelineInfoMain.RenderPass = m_vkRenderPass;
	init_info.PipelineInfoMain.Subpass = 0;
	init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	init_info.MinImageCount = 2;
	init_info.ImageCount = std::max(init_info.MinImageCount, imageCount);
	init_info.Allocator = nullptr;
	ImGui_ImplVulkan_Init(&init_info);
}

void MyGUI::StartWindow(
	const std::string& _title, 
	bool* _pShowWindow, 
	float _width,
	float _height,
	float _posX,
	float _posY)
{
	bool bSetSize = (_width >= 0) && (_height >= 0);
	bool bSetPos = (_posX >= 0) && (_posY >= 0);
	if (!m_bFrameStarted)
	{
		_StartNewFrame();
		m_bFrameStarted = true;
	}
	if (bSetSize)
	{
		ImGui::SetNextWindowSize({ _width, _height }, ImGuiCond_FirstUseEver);
	}
	if (bSetPos)
	{
		ImGui::SetNextWindowPos({ _posX, _posY }, ImGuiCond_FirstUseEver);
	}
	ImGui::Begin(_title.c_str(), _pShowWindow);
}

void MyGUI::Text(const std::string& _text)
{
	ImGui::TextUnformatted(_text.data(), _text.data() + _text.size());
}

void MyGUI::FrameRateText()
{
	ImGuiIO& io = ImGui::GetIO(); (void)io;
	ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io.Framerate, io.Framerate);
}

void MyGUI::CheckBox(const std::string& _title, bool& _bChecked)
{
	ImGui::Checkbox(_title.c_str(), &_bChecked);
}

void MyGUI::SliderFloat(const std::string& _title, float& _value, float _minValue, float _maxValue)
{
	ImGui::SliderFloat(_title.c_str(), &_value, _minValue, _maxValue);
}

void MyGUI::Button(const std::string& _title, bool& _bClicked)
{
	_bClicked = ImGui::Button(_title.c_str());
}

void MyGUI::SameLine()
{
	ImGui::SameLine();
}

void MyGUI::EndWindow()
{
	ImGui::End();
}

void MyGUI::Apply(VkCommandBuffer _cmd)
{
	if (!m_bFrameStarted)
		return;

	ImGui::Render();
	ImDrawData* pDrawData = ImGui::GetDrawData();
	if (pDrawData->Textures != nullptr)
	{
		for (ImTextureData* texture : *pDrawData->Textures)
		{
			if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates)
			{
				// The backend submits uploads and waits on the graphics queue. Draw callbacks run after the lock is released.
				const auto& device = MyDevice::GetInstance();
				auto queueLock = device._AcquireQueueHostLock(device.GetQueueOfType(QueueFamilyType::GRAPHICS));
				ImGui_ImplVulkan_UpdateTexture(texture);
			}
		}
	}
	ImGui_ImplVulkan_RenderDrawData(pDrawData, _cmd);
	m_bFrameStarted = false;
}

void MyGUI::Destroy()
{
	MyDevice::GetInstance().WaitIdle();
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	m_bFrameStarted = false;
}
