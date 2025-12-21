#pragma once
#include "common.h"
#pragma push_macro("max")
#include "vk_mem_alloc.h"
#pragma pop_macro("max")

class MyDevice;

class MemoryAllocator
{
private:
	std::unique_ptr<VmaAllocator> m_uptrVmaAllocator;
	std::unordered_map<VkImage, VmaAllocation> m_mapVkImageToAllocation;
	std::unordered_map<VkBuffer, VmaAllocation> m_mapVkBufferToAllocation;

private:
	static VmaMemoryUsage _ToVmaMemoryUsage(VkMemoryPropertyFlags _propertyFlags);

	VkDevice _GetVkDevice() const;

	VmaAllocator* _GetPtrVmaAllocator();

	MemoryAllocator(); // I only want allocator to be created by MyDevice for now

public:
	MemoryAllocator(const MemoryAllocator& _other) = delete;

	virtual ~MemoryAllocator();

	// Allocate memory and then bind it to vkImage
	void AllocateForVkImage(VkImage _vkImage, VkMemoryPropertyFlags _flags);

	// Allocate memory and then bind it to m_vkBuffer, 
	// the address of the buffer will align with _alignment if it's not 0
	void AllocateForVkBuffer(VkBuffer _vkBuffer, VkMemoryPropertyFlags _flags, VkDeviceSize _alignment = 0);

	// Map m_vkBuffer(host coherent) to host
	void MapVkBufferToHost(VkBuffer _vkBuffer, void*& _outHostAddress);

	// Unmap m_vkBuffer
	void UnmapVkBuffer(VkBuffer _vkBuffer);

	void FreeMemory(VkImage _vkImage);

	void FreeMemory(VkBuffer _vkBuffer);

	friend class MyDevice;
};