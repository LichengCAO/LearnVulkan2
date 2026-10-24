#pragma once
#include "resource/descriptor_set.h"

class ShaderProgramLayout;

// Settings are copied by the typed resource binding commands; referenced resources must survive GPU completion.
class ShaderResources final
{
	friend class CommandBuffer;
	friend struct ShaderBindingTestProbe;

private:
	struct Item
	{
		std::string name;
		uint32_t arrayElement = 0;
		DescriptorSetDesc::DescriptorBindingInfo resource;
	};
	std::vector<Item> m_items;
	void _Set(std::string_view inName, uint32_t inArrayElement, DescriptorSetDesc::DescriptorBindingInfo inResource);
	auto _Resolve(const ShaderProgramLayout& inLayout) const -> std::vector<DescriptorSetDesc>;

public:
	void Clear();
	void SetBuffer(std::string_view inName, const Buffer* inBuffer, VkDeviceSize inOffset = 0,
	    VkDeviceSize inRange = VK_WHOLE_SIZE, uint32_t inArrayElement = 0);
	void SetImage(std::string_view inName, const ImageView* inImageView,
	    VkImageLayout inLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uint32_t inArrayElement = 0);
	void SetCombinedImageSampler(std::string_view inName, const ImageView* inImageView, const Sampler* inSampler,
	    VkImageLayout inLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, uint32_t inArrayElement = 0);
	void SetSampler(std::string_view inName, const Sampler* inSampler, uint32_t inArrayElement = 0);
	void SetBufferView(std::string_view inName, const BufferView* inView, uint32_t inArrayElement = 0);
	void SetAccelerationStructure(
	    std::string_view inName, const TopLevelAccelStruct* inAccelerationStructure, uint32_t inArrayElement = 0);
};
