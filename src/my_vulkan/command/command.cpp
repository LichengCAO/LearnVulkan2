#include "command.h"

void ExternalCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    CHECK_TRUE(static_cast<bool>(m_parameters.record), "Recording callback must not be empty!");
    m_parameters.record(inVkCommandBuffer);
}

auto BeginRenderPassCommand::Record(VkCommandBuffer inVkCommandBuffer) const->void
{
	CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
	CHECK_TRUE(m_parameters.renderPass != VK_NULL_HANDLE, "Invalid render pass!");
	CHECK_TRUE(m_parameters.framebuffer != VK_NULL_HANDLE, "Invalid framebuffer!");

	VkRenderPassBeginInfo beginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	beginInfo.pNext = m_parameters.next;
	beginInfo.renderPass = m_parameters.renderPass;
	beginInfo.framebuffer = m_parameters.framebuffer;
	beginInfo.renderArea = m_parameters.renderArea;
	beginInfo.clearValueCount = static_cast<uint32_t>(m_parameters.clearValues.size());
	beginInfo.pClearValues = m_parameters.clearValues.data();

	vkCmdBeginRenderPass(inVkCommandBuffer, &beginInfo, m_parameters.contents);
}

auto EndRenderPassCommand::Record(VkCommandBuffer inVkCommandBuffer) const->void
{
	CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");

	vkCmdEndRenderPass(inVkCommandBuffer);
}

auto NextSubpassCommand::Record(VkCommandBuffer inVkCommandBuffer) const->void
{
	CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");

	vkCmdNextSubpass(inVkCommandBuffer, m_contents);
}

auto PipelineBarrierCommand::Record(VkCommandBuffer inVkCommandBuffer) const->void
{
	CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");

	const bool hasMemoryBarriers = !m_parameters.memoryBarriers.empty();
	const bool hasBufferBarriers = !m_parameters.bufferBarriers.empty();
	const bool hasImageBarriers = !m_parameters.imageBarriers.empty();
	if (!hasMemoryBarriers && !hasBufferBarriers && !hasImageBarriers)
	{
		return;
	}

	CHECK_TRUE(m_parameters.srcStageMask != 0, "Invalid source pipeline stage mask!");
	CHECK_TRUE(m_parameters.dstStageMask != 0, "Invalid destination pipeline stage mask!");

	vkCmdPipelineBarrier(
		inVkCommandBuffer,
		m_parameters.srcStageMask,
		m_parameters.dstStageMask,
		m_parameters.flags,
		static_cast<uint32_t>(m_parameters.memoryBarriers.size()),
		m_parameters.memoryBarriers.empty() ? nullptr : m_parameters.memoryBarriers.data(),
		static_cast<uint32_t>(m_parameters.bufferBarriers.size()),
		m_parameters.bufferBarriers.empty() ? nullptr : m_parameters.bufferBarriers.data(),
		static_cast<uint32_t>(m_parameters.imageBarriers.size()),
		m_parameters.imageBarriers.empty() ? nullptr : m_parameters.imageBarriers.data());
}

void ClearColorImageCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdClearColorImage(inVkCommandBuffer, m_parameters.image, m_parameters.imageLayout,
        &m_parameters.clearColor, static_cast<uint32_t>(m_parameters.ranges.size()), m_parameters.ranges.data());
}

void FillBufferCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdFillBuffer(inVkCommandBuffer, m_parameters.buffer, m_parameters.offset, m_parameters.size, m_parameters.value);
}

void BlitImageCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdBlitImage(inVkCommandBuffer, m_parameters.srcImage, m_parameters.srcLayout,
        m_parameters.dstImage, m_parameters.dstLayout, static_cast<uint32_t>(m_parameters.regions.size()),
        m_parameters.regions.data(), m_parameters.filter);
}

void CopyBufferToImageCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdCopyBufferToImage(inVkCommandBuffer, m_parameters.srcBuffer, m_parameters.dstImage, m_parameters.dstLayout,
        static_cast<uint32_t>(m_parameters.regions.size()), m_parameters.regions.data());
}

void CopyBufferCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    vkCmdCopyBuffer(inVkCommandBuffer, m_parameters.srcBuffer, m_parameters.dstBuffer,
        static_cast<uint32_t>(m_parameters.regions.size()), m_parameters.regions.data());
}

auto BuildAccelerationStructuresKHRCommand::SetParameters(const Parameters& inParameters)
    -> BuildAccelerationStructuresKHRCommand&
{
    CHECK_TRUE(!inParameters.buildInfos.empty(), "No acceleration structure build infos!");
    CHECK_TRUE(inParameters.buildInfos.size() == inParameters.buildRanges.size(), "Build info/range count mismatch!");
    Parameters parameters = inParameters;
    std::vector<std::vector<VkAccelerationStructureGeometryKHR>> geometries(parameters.buildInfos.size());
    for (size_t i = 0; i < parameters.buildInfos.size(); ++i)
    {
        auto& info = parameters.buildInfos[i];
        CHECK_TRUE(info.pNext == nullptr, "Unsupported acceleration structure build pNext!");
        CHECK_TRUE(info.geometryCount != 0 && info.geometryCount == parameters.buildRanges[i].size(),
            "Geometry/range count mismatch!");
        CHECK_TRUE((info.pGeometries != nullptr) != (info.ppGeometries != nullptr),
            "Provide exactly one geometry array!");
        geometries[i].reserve(info.geometryCount);
        for (uint32_t j = 0; j < info.geometryCount; ++j)
        {
            const auto* geometry = info.pGeometries ? &info.pGeometries[j] : info.ppGeometries[j];
            CHECK_TRUE(geometry != nullptr, "Missing geometry!");
            CHECK_TRUE(geometry->pNext == nullptr, "Unsupported geometry pNext!");
            switch (geometry->geometryType)
            {
            case VK_GEOMETRY_TYPE_TRIANGLES_KHR:
                CHECK_TRUE(geometry->geometry.triangles.pNext == nullptr, "Unsupported triangle pNext!");
                break;
            case VK_GEOMETRY_TYPE_AABBS_KHR:
                CHECK_TRUE(geometry->geometry.aabbs.pNext == nullptr, "Unsupported AABB pNext!");
                break;
            case VK_GEOMETRY_TYPE_INSTANCES_KHR:
                CHECK_TRUE(geometry->geometry.instances.pNext == nullptr, "Unsupported instance pNext!");
                break;
            default:
                CHECK_TRUE(false, "Unsupported geometry type!");
            }
            geometries[i].push_back(*geometry);
        }
        // Record reconstructs all pointers from the owned arrays.
        info.pGeometries = nullptr;
        info.ppGeometries = nullptr;
    }
    m_parameters = std::move(parameters);
    m_geometries = std::move(geometries);
    return *this;
}

void BuildAccelerationStructuresKHRCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    CHECK_TRUE(vkCmdBuildAccelerationStructuresKHR != nullptr, "Acceleration structure commands are unavailable!");
    auto infos = m_parameters.buildInfos;
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ranges(infos.size());
    for (size_t i = 0; i < infos.size(); ++i)
    {
        infos[i].pGeometries = m_geometries[i].data();
        ranges[i] = m_parameters.buildRanges[i].data();
    }
    vkCmdBuildAccelerationStructuresKHR(inVkCommandBuffer, static_cast<uint32_t>(infos.size()),
        infos.data(), ranges.data());
}

void CopyAccelerationStructureKHRCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    CHECK_TRUE(vkCmdCopyAccelerationStructureKHR != nullptr, "Acceleration structure commands are unavailable!");
    VkCopyAccelerationStructureInfoKHR info{ VK_STRUCTURE_TYPE_COPY_ACCELERATION_STRUCTURE_INFO_KHR };
    info.pNext = m_parameters.next;
    info.src = m_parameters.src;
    info.dst = m_parameters.dst;
    info.mode = m_parameters.mode;
    vkCmdCopyAccelerationStructureKHR(inVkCommandBuffer, &info);
}
