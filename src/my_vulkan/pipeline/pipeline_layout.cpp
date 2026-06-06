#include "pipeline_layout.h"
#include "device.h"

#include <cassert>

void PipelineLayoutCreateInfo::AddDescriptorSetLayout(VkDescriptorSetLayout inLayout)
{
	CHECK_TRUE(inLayout != VK_NULL_HANDLE, "Invalid descriptor set layout!");

	m_descriptorSetLayouts.push_back(inLayout);
}

void PipelineLayoutCreateInfo::AddPushConstantRange(VkShaderStageFlags inStages, uint32_t inOffset, uint32_t inSize)
{
	CHECK_TRUE(inStages != 0, "Push constant stages cannot be empty!");
	CHECK_TRUE(inSize > 0, "Push constant size must be greater than zero!");

	VkPushConstantRange range{};
	range.stageFlags = inStages;
	range.offset = inOffset;
	range.size = inSize;

	m_pushConstantRanges.push_back(range);
}

PipelineLayout::~PipelineLayout()
{
	assert(m_vkPipelineLayout == VK_NULL_HANDLE);
}

void PipelineLayout::Create(const PipelineLayoutCreateInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No pipeline layout create info!");
	CHECK_TRUE(m_vkPipelineLayout == VK_NULL_HANDLE, "Pipeline layout already created!");

	VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layoutInfo.setLayoutCount = static_cast<uint32_t>(inCreateInfo->m_descriptorSetLayouts.size());
	layoutInfo.pSetLayouts = inCreateInfo->m_descriptorSetLayouts.data();
	layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(inCreateInfo->m_pushConstantRanges.size());
	layoutInfo.pPushConstantRanges = inCreateInfo->m_pushConstantRanges.data();

	m_vkPipelineLayout = MyDevice::GetInstance().CreatePipelineLayout(layoutInfo);
}

void PipelineLayout::Destroy()
{
	if (m_vkPipelineLayout != VK_NULL_HANDLE)
	{
		MyDevice::GetInstance().DestroyPipelineLayout(m_vkPipelineLayout);
		m_vkPipelineLayout = VK_NULL_HANDLE;
	}
}
