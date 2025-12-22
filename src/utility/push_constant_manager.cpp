#include "push_constant_manager.h"
#include "utils.h"

PushConstantManager::PushConstantManager()
{
}

std::vector<VkShaderStageFlagBits> PushConstantManager::_GetBitsFromStageFlags(VkShaderStageFlags _flags)
{
	std::vector<VkShaderStageFlagBits> result;
	constexpr VkShaderStageFlagBits stagesToCheck[] =
	{
		VK_SHADER_STAGE_VERTEX_BIT,
		VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
		VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
		VK_SHADER_STAGE_GEOMETRY_BIT,
		VK_SHADER_STAGE_FRAGMENT_BIT,
		VK_SHADER_STAGE_COMPUTE_BIT,
		VK_SHADER_STAGE_RAYGEN_BIT_KHR,
		VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
		VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
		VK_SHADER_STAGE_MISS_BIT_KHR,
		VK_SHADER_STAGE_INTERSECTION_BIT_KHR,
		VK_SHADER_STAGE_CALLABLE_BIT_KHR,
		VK_SHADER_STAGE_TASK_BIT_EXT,
		VK_SHADER_STAGE_MESH_BIT_EXT,
		VK_SHADER_STAGE_SUBPASS_SHADING_BIT_HUAWEI,
		VK_SHADER_STAGE_CLUSTER_CULLING_BIT_HUAWEI,
	};
	size_t n = sizeof(stagesToCheck) / sizeof(stagesToCheck[0]);

	if (_flags == VK_SHADER_STAGE_ALL)
	{
		result = { VK_SHADER_STAGE_ALL };
	}
	else if (_flags == VK_SHADER_STAGE_ALL_GRAPHICS)
	{
		result = { VK_SHADER_STAGE_ALL_GRAPHICS };
	}
	else
	{
		for (size_t i = 0; i < n; ++i)
		{
			if ((_flags & stagesToCheck[i]) != 0)
			{
				result.push_back(stagesToCheck[i]);
			}
		}
	}

	return result;
}

void PushConstantManager::AddConstantRange(VkShaderStageFlags _stages, uint32_t _offset, uint32_t _size)
{
	uint32_t sizeAligned = common_utils::AlignUp(_size, 4); // Both offset and size are in units of bytes and must be a multiple of 4.
	CHECK_TRUE((m_usedStages & _stages) == 0, "Some stage already used!");
	m_usedStages = m_usedStages | _stages;
	m_currentSize += sizeAligned;
	CHECK_TRUE(m_currentSize <= 128, "Push constant cannot be larger than 128 bytes!");
	m_mapRange.insert({ _stages, { _offset, _size } });
}

void PushConstantManager::PushConstant(VkCommandBuffer _cmd, VkPipelineLayout _layout, VkShaderStageFlags _stage, const void* _data) const
{
	const auto& storedData = m_mapRange.at(_stage);
	uint32_t uSize = storedData.second;
	uint32_t uOffset = storedData.first;

	vkCmdPushConstants(_cmd, _layout, _stage, uOffset, uSize, _data);
}

void PushConstantManager::GetVkPushConstantRanges(std::vector<VkPushConstantRange>& _outRanges) const
{
	for (const auto& p : m_mapRange)
	{
		VkPushConstantRange range{};

		range.offset = p.second.first;
		range.size = p.second.second;
		range.stageFlags = p.first;

		_outRanges.push_back(std::move(range));
	}
}