#include "pipeline_allocator.h"
#include "utility/hash_util.h"

#include <type_traits>

namespace
{
	template <class Enum>
	void HashEnum(size_t& seed, Enum value)
	{
		hash_combine(seed, static_cast<std::underlying_type_t<Enum>>(value));
	}

	void HashBool(size_t& seed, VkBool32 value)
	{
		hash_combine(seed, value == VK_TRUE);
	}

	void HashPNext(size_t& seed, const void* pNext, const char* ownerName)
	{
		const auto* current = static_cast<const VkBaseInStructure*>(pNext);
		while (current != nullptr)
		{
			HashEnum(seed, current->sType);
			CHECK_TRUE(false, std::string("Unsupported ") + ownerName + " pNext!");
			current = current->pNext;
		}
	}

	void HashSpecializationInfo(size_t& seed, const VkSpecializationInfo* info)
	{
		hash_combine(seed, info != nullptr);
		if (info == nullptr)
		{
			return;
		}

		hash_combine(seed, info->mapEntryCount);
		for (uint32_t i = 0; i < info->mapEntryCount; i++)
		{
			const VkSpecializationMapEntry& entry = info->pMapEntries[i];
			hash_combine(seed, entry.constantID);
			hash_combine(seed, entry.offset);
			hash_combine(seed, entry.size);
		}

		hash_combine(seed, info->dataSize);
		const auto* data = static_cast<const uint8_t*>(info->pData);
		for (size_t i = 0; i < info->dataSize; i++)
		{
			hash_combine(seed, data[i]);
		}
	}

	void HashShaderStageCreateInfo(size_t& seed, const VkPipelineShaderStageCreateInfo& stage)
	{
		HashEnum(seed, stage.sType);
		HashPNext(seed, stage.pNext, "shader stage create info");
		hash_combine(seed, stage.flags);
		HashEnum(seed, stage.stage);
		hash_combine(seed, stage.module);
		hash_combine(seed, std::string(stage.pName != nullptr ? stage.pName : ""));
		HashSpecializationInfo(seed, stage.pSpecializationInfo);
	}

	void HashVertexInputStateCreateInfo(size_t& seed, const VkPipelineVertexInputStateCreateInfo* state)
	{
		hash_combine(seed, state != nullptr);
		if (state == nullptr)
		{
			return;
		}

		HashEnum(seed, state->sType);
		HashPNext(seed, state->pNext, "vertex input state create info");
		hash_combine(seed, state->flags);
		hash_combine(seed, state->vertexBindingDescriptionCount);
		for (uint32_t i = 0; i < state->vertexBindingDescriptionCount; i++)
		{
			hash_combine(seed, state->pVertexBindingDescriptions[i]);
		}

		hash_combine(seed, state->vertexAttributeDescriptionCount);
		for (uint32_t i = 0; i < state->vertexAttributeDescriptionCount; i++)
		{
			hash_combine(seed, state->pVertexAttributeDescriptions[i]);
		}
	}

	void HashInputAssemblyStateCreateInfo(size_t& seed, const VkPipelineInputAssemblyStateCreateInfo* state)
	{
		hash_combine(seed, state != nullptr);
		if (state == nullptr)
		{
			return;
		}

		HashEnum(seed, state->sType);
		HashPNext(seed, state->pNext, "input assembly state create info");
		hash_combine(seed, state->flags);
		HashEnum(seed, state->topology);
		HashBool(seed, state->primitiveRestartEnable);
	}

	void HashTessellationStateCreateInfo(size_t& seed, const VkPipelineTessellationStateCreateInfo* state)
	{
		hash_combine(seed, state != nullptr);
		if (state == nullptr)
		{
			return;
		}

		HashEnum(seed, state->sType);
		HashPNext(seed, state->pNext, "tessellation state create info");
		hash_combine(seed, state->flags);
		hash_combine(seed, state->patchControlPoints);
	}

	void HashViewportStateCreateInfo(size_t& seed, const VkPipelineViewportStateCreateInfo* state)
	{
		hash_combine(seed, state != nullptr);
		if (state == nullptr)
		{
			return;
		}

		HashEnum(seed, state->sType);
		HashPNext(seed, state->pNext, "viewport state create info");
		hash_combine(seed, state->flags);
		hash_combine(seed, state->viewportCount);
		for (uint32_t i = 0; i < state->viewportCount && state->pViewports != nullptr; i++)
		{
			hash_combine(seed, state->pViewports[i]);
		}
		hash_combine(seed, state->scissorCount);
		for (uint32_t i = 0; i < state->scissorCount && state->pScissors != nullptr; i++)
		{
			hash_combine(seed, state->pScissors[i]);
		}
	}

	void HashRasterizationStateCreateInfo(size_t& seed, const VkPipelineRasterizationStateCreateInfo* state)
	{
		hash_combine(seed, state != nullptr);
		if (state == nullptr)
		{
			return;
		}

		HashEnum(seed, state->sType);
		HashPNext(seed, state->pNext, "rasterization state create info");
		hash_combine(seed, state->flags);
		HashBool(seed, state->depthClampEnable);
		HashBool(seed, state->rasterizerDiscardEnable);
		HashEnum(seed, state->polygonMode);
		hash_combine(seed, state->cullMode);
		HashEnum(seed, state->frontFace);
		HashBool(seed, state->depthBiasEnable);
		hash_combine(seed, state->depthBiasConstantFactor);
		hash_combine(seed, state->depthBiasClamp);
		hash_combine(seed, state->depthBiasSlopeFactor);
		hash_combine(seed, state->lineWidth);
	}

	void HashMultisampleStateCreateInfo(size_t& seed, const VkPipelineMultisampleStateCreateInfo* state)
	{
		hash_combine(seed, state != nullptr);
		if (state == nullptr)
		{
			return;
		}

		HashEnum(seed, state->sType);
		HashPNext(seed, state->pNext, "multisample state create info");
		hash_combine(seed, state->flags);
		HashEnum(seed, state->rasterizationSamples);
		HashBool(seed, state->sampleShadingEnable);
		hash_combine(seed, state->minSampleShading);
		hash_combine(seed, state->pSampleMask != nullptr);
		if (state->pSampleMask != nullptr)
		{
			const uint32_t sampleMaskCount = (static_cast<uint32_t>(state->rasterizationSamples) + 31) / 32;
			for (uint32_t i = 0; i < sampleMaskCount; i++)
			{
				hash_combine(seed, state->pSampleMask[i]);
			}
		}
		HashBool(seed, state->alphaToCoverageEnable);
		HashBool(seed, state->alphaToOneEnable);
	}

	void HashStencilOpState(size_t& seed, const VkStencilOpState& state)
	{
		HashEnum(seed, state.failOp);
		HashEnum(seed, state.passOp);
		HashEnum(seed, state.depthFailOp);
		HashEnum(seed, state.compareOp);
		hash_combine(seed, state.compareMask);
		hash_combine(seed, state.writeMask);
		hash_combine(seed, state.reference);
	}

	void HashDepthStencilStateCreateInfo(size_t& seed, const VkPipelineDepthStencilStateCreateInfo* state)
	{
		hash_combine(seed, state != nullptr);
		if (state == nullptr)
		{
			return;
		}

		HashEnum(seed, state->sType);
		HashPNext(seed, state->pNext, "depth stencil state create info");
		hash_combine(seed, state->flags);
		HashBool(seed, state->depthTestEnable);
		HashBool(seed, state->depthWriteEnable);
		HashEnum(seed, state->depthCompareOp);
		HashBool(seed, state->depthBoundsTestEnable);
		HashBool(seed, state->stencilTestEnable);
		HashStencilOpState(seed, state->front);
		HashStencilOpState(seed, state->back);
		hash_combine(seed, state->minDepthBounds);
		hash_combine(seed, state->maxDepthBounds);
	}

	void HashColorBlendAttachmentState(size_t& seed, const VkPipelineColorBlendAttachmentState& state)
	{
		HashBool(seed, state.blendEnable);
		HashEnum(seed, state.srcColorBlendFactor);
		HashEnum(seed, state.dstColorBlendFactor);
		HashEnum(seed, state.colorBlendOp);
		HashEnum(seed, state.srcAlphaBlendFactor);
		HashEnum(seed, state.dstAlphaBlendFactor);
		HashEnum(seed, state.alphaBlendOp);
		hash_combine(seed, state.colorWriteMask);
	}

	void HashColorBlendStateCreateInfo(size_t& seed, const VkPipelineColorBlendStateCreateInfo* state)
	{
		hash_combine(seed, state != nullptr);
		if (state == nullptr)
		{
			return;
		}

		HashEnum(seed, state->sType);
		HashPNext(seed, state->pNext, "color blend state create info");
		hash_combine(seed, state->flags);
		HashBool(seed, state->logicOpEnable);
		HashEnum(seed, state->logicOp);
		hash_combine(seed, state->attachmentCount);
		for (uint32_t i = 0; i < state->attachmentCount; i++)
		{
			HashColorBlendAttachmentState(seed, state->pAttachments[i]);
		}
		for (float constant : state->blendConstants)
		{
			hash_combine(seed, constant);
		}
	}

	void HashDynamicStateCreateInfo(size_t& seed, const VkPipelineDynamicStateCreateInfo* state)
	{
		hash_combine(seed, state != nullptr);
		if (state == nullptr)
		{
			return;
		}

		HashEnum(seed, state->sType);
		HashPNext(seed, state->pNext, "dynamic state create info");
		hash_combine(seed, state->flags);
		hash_combine(seed, state->dynamicStateCount);
		for (uint32_t i = 0; i < state->dynamicStateCount; i++)
		{
			HashEnum(seed, state->pDynamicStates[i]);
		}
	}

	uint64_t HashGraphicsPipelineCreateInfo(const VkGraphicsPipelineCreateInfo& createInfo)
	{
		size_t result = 0;

		HashEnum(result, createInfo.sType);
		HashPNext(result, createInfo.pNext, "graphics pipeline create info");
		hash_combine(result, createInfo.flags);

		hash_combine(result, createInfo.stageCount);
		for (uint32_t i = 0; i < createInfo.stageCount; i++)
		{
			HashShaderStageCreateInfo(result, createInfo.pStages[i]);
		}

		HashVertexInputStateCreateInfo(result, createInfo.pVertexInputState);
		HashInputAssemblyStateCreateInfo(result, createInfo.pInputAssemblyState);
		HashTessellationStateCreateInfo(result, createInfo.pTessellationState);
		HashViewportStateCreateInfo(result, createInfo.pViewportState);
		HashRasterizationStateCreateInfo(result, createInfo.pRasterizationState);
		HashMultisampleStateCreateInfo(result, createInfo.pMultisampleState);
		HashDepthStencilStateCreateInfo(result, createInfo.pDepthStencilState);
		HashColorBlendStateCreateInfo(result, createInfo.pColorBlendState);
		HashDynamicStateCreateInfo(result, createInfo.pDynamicState);

		hash_combine(result, createInfo.layout);
		hash_combine(result, createInfo.renderPass);
		hash_combine(result, createInfo.subpass);
		hash_combine(result, createInfo.basePipelineHandle);
		hash_combine(result, createInfo.basePipelineIndex);

		return static_cast<uint64_t>(result);
	}
}

GraphicsPipelineAllocator::~GraphicsPipelineAllocator()
{
	Destroy();
}

void GraphicsPipelineAllocator::Create(VkDevice inDevice)
{
	CHECK_TRUE(inDevice != VK_NULL_HANDLE, "Invalid graphics pipeline allocator device!");
	CHECK_TRUE(m_vkDevice == VK_NULL_HANDLE || m_vkDevice == inDevice, "Graphics pipeline allocator already uses another device!");

	m_vkDevice = inDevice;
}

bool GraphicsPipelineAllocator::AllocateGraphicsPipeline(
	const VkGraphicsPipelineCreateInfo* inCreateInfo,
	VkPipelineCache inCache,
	VkPipeline& outPipeline)
{
	CHECK_TRUE(m_vkDevice != VK_NULL_HANDLE, "Graphics pipeline allocator is not created!");
	CHECK_TRUE(inCreateInfo != nullptr, "Missing graphics pipeline create info!");

	const uint64_t hash = HashGraphicsPipelineCreateInfo(*inCreateInfo);
	const auto iter = m_mapHashToVkPipeline.find(hash);
	if (iter != m_mapHashToVkPipeline.end())
	{
		outPipeline = iter->second;
		return true;
	}

	VkPipeline pipeline = VK_NULL_HANDLE;
	VK_CHECK(vkCreateGraphicsPipelines(m_vkDevice, inCache, 1, inCreateInfo, nullptr, &pipeline));

	m_mapHashToVkPipeline.emplace(hash, pipeline);
	outPipeline = pipeline;
	return true;
}

void GraphicsPipelineAllocator::FreeGraphicsPipeline(VkPipeline& inoutPipeline)
{
	inoutPipeline = VK_NULL_HANDLE;
}

void GraphicsPipelineAllocator::Destroy()
{
	if (m_vkDevice != VK_NULL_HANDLE)
	{
		for (auto& [hash, pipeline] : m_mapHashToVkPipeline)
		{
			if (pipeline != VK_NULL_HANDLE)
			{
				vkDestroyPipeline(m_vkDevice, pipeline, nullptr);
			}
		}
	}

	m_mapHashToVkPipeline.clear();
	m_vkDevice = VK_NULL_HANDLE;
}
