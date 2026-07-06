#include "compute_shader_program.h"
#include "push_constant_manager.h"
#include "device.h"
#include "shader_reflect.h"

ComputeShaderProgramCreateInfo& ComputeShaderProgramCreateInfo::Reset()
{
	m_spirvFile.clear();
	m_entry = "main";
	m_vkPipelineCache = VK_NULL_HANDLE;
	return *this;
}

ComputeShaderProgramCreateInfo& ComputeShaderProgramCreateInfo::SetSpirvFile(std::string inFile)
{
	m_spirvFile = std::move(inFile);
	return *this;
}

ComputeShaderProgramCreateInfo& ComputeShaderProgramCreateInfo::SetEntry(std::string inEntry)
{
	CHECK_TRUE(!inEntry.empty(), "Compute shader entry cannot be empty!");
	m_entry = std::move(inEntry);
	return *this;
}

ComputeShaderProgramCreateInfo& ComputeShaderProgramCreateInfo::CustomizePipelineCache(VkPipelineCache inCache)
{
	m_vkPipelineCache = inCache;
	return *this;
}

ComputeShaderProgram::~ComputeShaderProgram()
{
	assert(m_vkPipeline == VK_NULL_HANDLE);
	assert(m_pipelineLayout == nullptr);
	assert(m_uptrShaderModule == nullptr);
	assert(m_descriptorSetLayouts.empty());
}

void ComputeShaderProgram::Create(const ComputeShaderProgramCreateInfo* inCreateInfo)
{
	CHECK_TRUE(inCreateInfo != nullptr, "No compute shader program create info!");
	CHECK_TRUE(!inCreateInfo->m_spirvFile.empty(), "Compute shader SPIR-V file is unset!");
	CHECK_TRUE(m_vkPipeline == VK_NULL_HANDLE, "Compute shader program already created!");
	CHECK_TRUE(m_pipelineLayout == nullptr, "Compute shader program already created!");

	ShaderReflector reflector{};
	std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>> descriptorSetData;
	std::unordered_map<std::string, uint32_t> pushConstantIndex;
	std::vector<std::pair<VkShaderStageFlags, std::pair<uint32_t, uint32_t>>> reflectedPushConstants;
	std::vector<VkPushConstantRange> pushConstantRanges;

	reflector.Create({ inCreateInfo->m_spirvFile });
	reflector.ReflectDescriptorSets(m_nameToSetBinding, descriptorSetData);
	reflector.ReflectPushConst(pushConstantIndex, reflectedPushConstants);

	for (const auto& reflectedPushConstant : reflectedPushConstants)
	{
		VkPushConstantRange range{};
		range.stageFlags = reflectedPushConstant.first;
		range.offset = reflectedPushConstant.second.first;
		range.size = reflectedPushConstant.second.second;
		pushConstantRanges.push_back(range);
	}

	_CreateDescriptorSetLayouts(descriptorSetData);
	_CreatePipelineLayout(pushConstantRanges);
	_CreatePushConstantManager(pushConstantRanges);

	ShaderModuleCreateInfo shaderModuleCreateInfo{};
	shaderModuleCreateInfo.SetSpirvFile(inCreateInfo->m_spirvFile);
	shaderModuleCreateInfo.AddEntry(VK_SHADER_STAGE_COMPUTE_BIT, inCreateInfo->m_entry);

	m_uptrShaderModule = std::make_unique<ShaderModule>();
	m_uptrShaderModule->Create(shaderModuleCreateInfo);
	_CreateVkPipeline(
		m_uptrShaderModule->GetShaderStageInfo(VK_SHADER_STAGE_COMPUTE_BIT),
		inCreateInfo->m_vkPipelineCache);

	reflector.Destroy();

	CHECK_TRUE(m_vkPipeline != VK_NULL_HANDLE);
	CHECK_TRUE(m_pipelineLayout != nullptr);
	CHECK_TRUE(m_pipelineLayout->GetVkPipelineLayout() != VK_NULL_HANDLE);
	CHECK_TRUE(m_uptrPushConstant != nullptr);
}

void ComputeShaderProgram::_CreateDescriptorSetLayouts(
	const std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>& inDescriptorSetData)
{
	for (const auto& descriptorSetBindings : inDescriptorSetData)
	{
		auto descriptorSetLayout = std::make_unique<DescriptorSetLayout>();
		DescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo{};

		for (const auto& [bindingIndex, layoutBinding] : descriptorSetBindings)
		{
			CHECK_TRUE(
				layoutBinding.pImmutableSamplers == nullptr,
				"Immutable samplers are not supported by DescriptorSetLayoutCreateInfo::SetBinding yet!");

			if (layoutBinding.descriptorCount > 1)
			{
				DescriptorSetLayoutCreateInfo::ArraySizeInfo arraySizeInfo{};
				arraySizeInfo.size = layoutBinding.descriptorCount;
				descriptorSetLayoutCreateInfo.SetBinding(
					layoutBinding.binding,
					layoutBinding.descriptorType,
					layoutBinding.stageFlags,
					&arraySizeInfo);
			}
			else
			{
				descriptorSetLayoutCreateInfo.SetBinding(
					layoutBinding.binding,
					layoutBinding.descriptorType,
					layoutBinding.stageFlags);
			}
		}

		descriptorSetLayout->Create(descriptorSetLayoutCreateInfo);
		m_descriptorSetLayouts.push_back(std::move(descriptorSetLayout));
	}
}

void ComputeShaderProgram::_CreatePipelineLayout(const std::vector<VkPushConstantRange>& inPushConstantRanges)
{
	PipelineLayoutCreateInfo pipelineLayoutCreateInfo{};

	for (const auto& descriptorSetLayout : m_descriptorSetLayouts)
	{
		pipelineLayoutCreateInfo.AddDescriptorSetLayout(descriptorSetLayout->GetVkDescriptorSetLayout());
	}

	for (const auto& pushConstantRange : inPushConstantRanges)
	{
		pipelineLayoutCreateInfo.AddPushConstantRange(
			pushConstantRange.stageFlags,
			pushConstantRange.offset,
			pushConstantRange.size);
	}

	m_pipelineLayout = std::make_unique<PipelineLayout>();
	m_pipelineLayout->Create(&pipelineLayoutCreateInfo);
}

void ComputeShaderProgram::_CreateVkPipeline(
	const VkPipelineShaderStageCreateInfo& inShaderStageInfo,
	VkPipelineCache inPipelineCache)
{
	VkComputePipelineCreateInfo pipelineInfo{ VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
	pipelineInfo.stage = inShaderStageInfo;
	CHECK_TRUE(m_pipelineLayout != nullptr, "Compute shader program needs a pipeline layout!");
	pipelineInfo.layout = m_pipelineLayout->GetVkPipelineLayout();
	m_vkPipeline = MyDevice::GetInstance().CreateComputePipeline(pipelineInfo, inPipelineCache);
}

void ComputeShaderProgram::_CreatePushConstantManager(const std::vector<VkPushConstantRange>& inPushConstantRanges)
{
	m_uptrPushConstant = std::make_unique<PushConstantManager>();
	for (const auto& pushConstantRange : inPushConstantRanges)
	{
		m_uptrPushConstant->AddConstantRange(
			pushConstantRange.stageFlags,
			pushConstantRange.offset,
			pushConstantRange.size);
	}
}

void ComputeShaderProgram::Destroy()
{
	auto& device = MyDevice::GetInstance();
	if (m_vkPipeline != VK_NULL_HANDLE)
	{
		device.DestroyPipeline(m_vkPipeline);
		m_vkPipeline = VK_NULL_HANDLE;
	}
	if (m_uptrShaderModule != nullptr)
	{
		m_uptrShaderModule->Destroy();
		m_uptrShaderModule.reset();
	}
	if (m_pipelineLayout != nullptr)
	{
		m_pipelineLayout->Destroy();
		m_pipelineLayout.reset();
	}
	for (auto& descriptorSetLayout : m_descriptorSetLayouts)
	{
		descriptorSetLayout->Destroy();
	}
	m_descriptorSetLayouts.clear();
	m_nameToSetBinding.clear();
	m_uptrPushConstant.reset();
}

const std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>& ComputeShaderProgram::GetNameToSetBinding() const
{
	return m_nameToSetBinding;
}

VkPipeline ComputeShaderProgram::GetVkPipeline() const
{
	return m_vkPipeline;
}
