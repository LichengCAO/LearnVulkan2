#include "pipeline_layout.h"
#include "device.h"

auto PipelineLayoutCreateInfo::AddDescriptorSetLayout(
	DescriptorSetLayout inLayout) -> PipelineLayoutCreateInfo&
{
	CHECK_TRUE(inLayout.GetVkDescriptorSetLayout() != VK_NULL_HANDLE, "Invalid descriptor set layout!");

	m_descriptorSetLayouts.push_back(std::move(inLayout));
	return *this;
}

auto PipelineLayoutCreateInfo::AddPushConstantRange(
	VkShaderStageFlags inStages,
	uint32_t inOffset,
	uint32_t inSize) -> PipelineLayoutCreateInfo&
{
	CHECK_TRUE(inStages != 0, "Push constant stages cannot be empty!");
	CHECK_TRUE(inSize > 0, "Push constant size must be greater than zero!");

	VkPushConstantRange range{};
	range.stageFlags = inStages;
	range.offset = inOffset;
	range.size = inSize;

	m_pushConstantRanges.push_back(range);
	return *this;
}

PipelineLayout::~PipelineLayout()
{
}

void PipelineLayout::Create(const PipelineLayoutCreateInfo& inCreateInfo)
{
	CHECK_TRUE(m_vkPipelineLayout == VK_NULL_HANDLE, "Pipeline layout already created!");
	m_descriptorSetLayouts = inCreateInfo.m_descriptorSetLayouts;
	m_pushConstantRanges = inCreateInfo.m_pushConstantRanges;

	std::vector<VkDescriptorSetLayout> descriptorSetLayouts;
	descriptorSetLayouts.reserve(m_descriptorSetLayouts.size());
	for (const DescriptorSetLayout& descriptorSetLayout : m_descriptorSetLayouts)
	{
		CHECK_TRUE(
			descriptorSetLayout.GetVkDescriptorSetLayout() != VK_NULL_HANDLE,
			"Pipeline layout contains an invalid descriptor set layout!");
		descriptorSetLayouts.push_back(descriptorSetLayout.GetVkDescriptorSetLayout());
	}

	VkPipelineLayoutCreateInfo layoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	layoutInfo.setLayoutCount = static_cast<uint32_t>(descriptorSetLayouts.size());
	layoutInfo.pSetLayouts = descriptorSetLayouts.data();
	layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(m_pushConstantRanges.size());
	layoutInfo.pPushConstantRanges = m_pushConstantRanges.data();

	m_vkPipelineLayout = MyDevice::GetInstance().CreatePipelineLayout(layoutInfo);
}

void PipelineLayout::Destroy()
{
	m_vkPipelineLayout = VK_NULL_HANDLE;
	m_descriptorSetLayouts.clear();
	m_pushConstantRanges.clear();
}

auto PipelineLayout::GetDescriptorSetLayout(uint32_t inSet) const -> const DescriptorSetLayout&
{
	CHECK_TRUE(inSet < m_descriptorSetLayouts.size(), "Pipeline layout descriptor set index is out of range!");
	return m_descriptorSetLayouts[inSet];
}
