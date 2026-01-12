#pragma once
#include "common.h"
//class A {
//public:
//	A(std::function<void()> lambda) { m_f = lambda; }
//	void call() const { m_f(); } // Calls the stored lambda
//private:
//	std::function<void()> m_f;
//};
//
//int main() {
//	auto sharedLambda = []() { static int a = 0; ++a; std::cout << "shared: " << a << "\n"; };
//
//	A a1(sharedLambda);
//	A a2(sharedLambda);
//
//	a1.call(); // shared: 1
//	a2.call(); // shared: 2
//	a1.call(); // shared: 3
//	a2.call(); // shared: 4
//
//	return 0;
//}
//
//#include <iostream>
//#include <functional>
//
//class A {
//public:
//	void setLambda(std::function<void()> lambda) { m_f = lambda; }
//	void call() const { m_f(); } // Calls the stored lambda
//private:
//	std::function<void()> m_f;
//};
//
//int main() {
//	A a1, a2;
//
//	a1.setLambda([]() { static int a = 0; ++a; std::cout << "Lambda in a1: " << a << "\n"; });
//	a2.setLambda([]() { static int a = 0; ++a; std::cout << "Lambda in a2: " << a << "\n"; });
//
//	a1.call(); // Lambda in a1: 1
//	a2.call(); // Lambda in a2: 1
//	a1.call(); // Lambda in a1: 2
//	a2.call(); // Lambda in a2: 2
//
//	return 0;
//}
class Image;
class ImageView;
class Buffer;
class FrameGraphNodeInput;
class FrameGraphNodeOutput;

struct FrameGraphBufferResourceHandle
{
	uint32_t handle;
};

struct FrameGraphImageResourceHandle
{
	uint32_t handle;
};

struct SemaphoreHandle
{
	uint32_t handle;
};

struct FrameGraphImageResourceState
{
	VkImageSubresourceRange range;
	VkImageLayout layout;
	uint32_t queueFamily;
	VkAccessFlags access;
	VkPipelineStageFlags stage; // when does output ready
};

struct FrameGraphBufferResourceState
{
	VkDeviceSize offset;
	VkDeviceSize range;
	uint32_t queueFamily;
	VkAccessFlags access;
	VkPipelineStageFlags stage; // when does output ready
};

enum class FrameGraphTaskThreadType
{
	GRAPHICS_ONLY,  // so, it can be delegated to graphics recording thread
	COMPUTE_ONLY,	// so, it can be delegated to compute recording thread
	ALL				// it can only be run on main thread
};

// Context that helps us to decide whether to add barrier between execution of frame graph nodes
class FrameGraphCompileContext
{
private:
	std::vector<VkSemaphore> m_graphicsWaitSemaphores;
	std::vector<VkPipelineStageFlags> m_graphicsWaitStages;
	std::vector<VkSemaphore> m_computeWaitSemaphores;
	std::vector<VkPipelineStageFlags> m_computeWaitStages;

public:
	void SetResourceState(FrameGraphBufferResourceHandle inHandle, const FrameGraphBufferResourceState& inNewState);
	void SetResourceState(FrameGraphImageResourceHandle inHandle, const FrameGraphImageResourceState& inNewState);
	const std::vector<FrameGraphBufferResourceState>& GetResourceState(FrameGraphBufferResourceHandle inHandle) const;
	const std::vector<FrameGraphImageResourceState>& GetResourceState(FrameGraphImageResourceHandle inHandle) const;
	void MergeFrameGraphNodeInput(const FrameGraphNodeInput* inFrameGraphNodeInput);
	void MergeFrameGraphNodeOutput(const FrameGraphNodeOutput* inFrameGraphNodeOutput);
	void SetFrameGraphNodeOutputToExecutionState(
		const FrameGraphNodeOutput* inNodeOutputSubmitted, 
		SemaphoreHandle inSemaphoreHandle) const;
};