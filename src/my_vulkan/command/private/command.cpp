#include "command.h"
#include "device.h"

void ExternalCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    CHECK_TRUE(static_cast<bool>(m_parameters.record), "Recording callback must not be empty!");
    m_parameters.record(inVkCommandBuffer);
}

void BeginRenderPassCommand::_Prepare() const
{
	if (m_prepared) return;
	CHECK_TRUE(m_snapshot.description, "Render pass description is missing!");
	auto& device = MyDevice::GetInstance();
	const auto framebuffer = device._GetOrCreateFramebuffer(m_snapshot, m_framebuffer);
	const auto renderPass = device._GetOrCreateRenderPass(m_snapshot);
	m_parameters.framebuffer = framebuffer;
	m_parameters.renderPass = renderPass;
	m_prepared = true;
}

auto BeginRenderPassCommand::Record(VkCommandBuffer inVkCommandBuffer) const->void
{
	CHECK_TRUE(m_prepared, "Render pass command was not prepared!");
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
    std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> ranges(parameters.buildInfos.size());
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
        // Moves of the owning vectors preserve their allocated storage and these views.
        info.pGeometries = geometries[i].data();
        info.ppGeometries = nullptr;
        ranges[i] = parameters.buildRanges[i].data();
    }
    m_parameters = std::move(parameters);
    m_geometries = std::move(geometries);
    m_rangePointers = std::move(ranges);
    return *this;
}

void BuildAccelerationStructuresKHRCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE, "Invalid command buffer!");
    CHECK_TRUE(vkCmdBuildAccelerationStructuresKHR != nullptr, "Acceleration structure commands are unavailable!");
    vkCmdBuildAccelerationStructuresKHR(inVkCommandBuffer, static_cast<uint32_t>(m_parameters.buildInfos.size()),
        m_parameters.buildInfos.data(), m_rangePointers.data());
}

void BuildClusterAccelerationStructuresNVCommand::SetParameters(const Parameters& inParameters)
{
    const auto& info = inParameters.info;
    CHECK_TRUE(info.pNext == nullptr && info.input.pNext == nullptr &&
        inParameters.triangles.pNext == nullptr && inParameters.clusters.pNext == nullptr,
        "Unsupported cluster build pNext!");
    CHECK_TRUE((info.input.opMode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_IMPLICIT_DESTINATIONS_NV ||
                info.input.opMode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV ||
                info.input.opMode == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_COMPUTE_SIZES_NV) &&
        info.input.maxAccelerationStructureCount != 0 && info.input.flags == 0 &&
        (info.input.opType == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_NV ||
         (info.input.opType == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_TRIANGLE_CLUSTER_TEMPLATE_NV &&
          info.input.opMode != VK_CLUSTER_ACCELERATION_STRUCTURE_OP_MODE_EXPLICIT_DESTINATIONS_NV) ||
         info.input.opType == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_INSTANTIATE_TRIANGLE_CLUSTER_NV ||
         info.input.opType == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV),
        "Unsupported cluster build operation!");
    m_parameters = inParameters;
    // All Host pointer targets are owned values; reconstruct the active union at Record.
    m_parameters.info.input.opInput.pTriangleClusters = nullptr;
}

void BuildClusterAccelerationStructuresNVCommand::Record(VkCommandBuffer inVkCommandBuffer) const
{
    CHECK_TRUE(inVkCommandBuffer != VK_NULL_HANDLE && vkCmdBuildClusterAccelerationStructureIndirectNV != nullptr,
        "Cluster acceleration structure commands are unavailable!");
    auto info = m_parameters.info;
    auto triangles = m_parameters.triangles;
    auto clusters = m_parameters.clusters;
    if (info.input.opType == VK_CLUSTER_ACCELERATION_STRUCTURE_OP_TYPE_BUILD_CLUSTERS_BOTTOM_LEVEL_NV)
        info.input.opInput.pClustersBottomLevel = &clusters;
    else
        info.input.opInput.pTriangleClusters = &triangles;
    vkCmdBuildClusterAccelerationStructureIndirectNV(inVkCommandBuffer, &info);
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

void BuildPartitionedAccelerationStructuresNVCommand::Record(VkCommandBuffer commandBuffer) const
{
    CHECK_TRUE(commandBuffer && vkCmdBuildPartitionedAccelerationStructuresNV, "PTLAS build entrypoint is unavailable!");
    VkPartitionedAccelerationStructureFlagsNV flags{};
    VkBuildPartitionedAccelerationStructureInfoNV info{VK_STRUCTURE_TYPE_BUILD_PARTITIONED_ACCELERATION_STRUCTURE_INFO_NV};
    info.input = snapshot.configuration.MakeInput(flags);
    info.srcAccelerationStructureData = snapshot.source;
    info.dstAccelerationStructureData = snapshot.target;
    info.scratchData = snapshot.scratch;
    info.srcInfos = snapshot.arguments;
    info.srcInfosCount = snapshot.count;
    vkCmdBuildPartitionedAccelerationStructuresNV(commandBuffer, &info);
}
