#pragma once

#include <string>
#include "common.h"

class MyGUI
{
private:
	VkRenderPass m_vkRenderPass = VK_NULL_HANDLE;
	bool m_bFrameStarted = false;

private:
	void _StartNewFrame();

public:
	// set up render pass for initialization, call this before Init()
	// every other ui function call should be inside this render pass
	inline void SetUpRenderPass(VkRenderPass _renderpass) { m_vkRenderPass = _renderpass; };
	
	void Init();

	// start to draw a window, render pass should be started and frame buffers should be bound before it
	// _title: name of the window
	// _pShowWindow: if it's not nullptr, the window can be hidden and *_pShowWindow will be set
	// _width, _height: width and height of the window
	// _posX, _posY: position of the window
	void StartWindow(
		const std::string& _title,
		bool* _pShowWindow = nullptr,
		float _width = -1,
		float _height = -1,
		float _posX = -1,
		float _posY = -1);

	// add text to a window
	void Text(const std::string& _text);

	void FrameRateText();

	// add check box to a window
	void CheckBox(const std::string& _title, bool& _bChecked);

	// add slider to a window
	void SliderFloat(const std::string& _title, float& _value, float _minValue, float _maxValue);

	// add button to the window
	// _bClicked: whether button is clicked in the current frame
	void Button(const std::string& _title, bool& _bClicked);

	// the next item will be in the same line
	void SameLine();

	// end current window
	void EndWindow();

	// record draw command into command buffer
	void Apply(VkCommandBuffer _cmd);

	void Uninit();
};