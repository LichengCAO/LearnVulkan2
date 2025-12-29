#pragma once
#include "common.h"

class FrameGraphInput
{

};

class FrameGraphOutput
{

};

class FrameGraphNode
{
private:
	std::unique_ptr<FrameGraphInput> m_input;
	std::unique_ptr<FrameGraphOutput> m_output;

public:

};

class FrameGraphSratcher
{

};

class FrameGraph
{
public:
	// Decide static process based in input, only do once,
	// unless window resized, or other dramatic change
	// This should include: 
	// 1. order frame graph nodes
	// 2. synchronize them(barrier, semaphore), maybe layout transfer
	// 3. allocate resource for each nodes(include memory aliasing)
	void Compile();

	void StartFrame();

	void EndFrame();
};