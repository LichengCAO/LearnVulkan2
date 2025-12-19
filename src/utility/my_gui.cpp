#include "my_gui.h"
#include "device.h"
#include "imgui.h"
#include "imgui_impl_vulkan.h"
#include "imgui_impl_glfw.h"

void MyGUI::_StartNewFrame()
{
	ImGui_ImplVulkan_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();
}

void MyGUI::Init()
{
	const auto& myDevice = MyDevice::GetInstance();

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
	init_info.QueueFamily = myDevice.queueFamilyIndices.graphicsAndComputeFamily.value();
	init_info.Queue = myDevice.GetQueue(myDevice.queueFamilyIndices.graphicsAndComputeFamily.value());
	init_info.DescriptorPoolSize = 1000;
	init_info.RenderPass = m_vkRenderPass;
	init_info.MinImageCount = 2;
	init_info.ImageCount = 3;
	init_info.Allocator = nullptr;
	init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
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
	ImGui::Text(_text.c_str());
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
	ImDrawData* pDrawData = nullptr;

	ImGui::Render();
	pDrawData = ImGui::GetDrawData();
	ImGui_ImplVulkan_RenderDrawData(pDrawData, _cmd);
	m_bFrameStarted = false;
}

void MyGUI::Uninit()
{
	MyDevice::GetInstance().WaitIdle();
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
}
