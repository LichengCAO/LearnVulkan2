#include "common.h"
#include "utility/render_graph/render_graph.h"
#include "utility/render_graph/render_graph_instance.h"

struct RenderGraphTestProbe
{
	struct GraphicsGroupSummary
	{
		std::vector<std::string> passes;
		bool managedRenderPass = false;
		uint32_t attachmentCount = 0;
		uint32_t subpassCount = 0;
		std::vector<uint32_t> preserveCounts;
		std::vector<VkAttachmentLoadOp> loadOps;
		std::vector<VkAttachmentStoreOp> storeOps;
	};

	static auto GetGraphicsGroups(const RenderGraph& inGraph) -> std::vector<GraphicsGroupSummary>
	{
		std::vector<GraphicsGroupSummary> summaries;
		for (const RenderGraph::SubmitBatch& submitBatch : inGraph.m_buildResult.submitBatches)
		{
			for (const RenderGraph::SubmitBatch::PassGroupPlan& group : submitBatch.graphicsGroups)
			{
				GraphicsGroupSummary summary;
				summary.managedRenderPass = group.managedRenderPass;
				for (RenderGraph::PassIndex passIndex : group.passes)
				{
					summary.passes.push_back(inGraph.m_buildResult.GetPass(passIndex).name);
				}
				if (group.renderPassPlan.has_value())
				{
					summary.attachmentCount = static_cast<uint32_t>(group.renderPassPlan->attachments.size());
					summary.subpassCount = static_cast<uint32_t>(group.renderPassPlan->subpasses.size());
					for (const auto& attachment : group.renderPassPlan->attachments)
					{
						summary.loadOps.push_back(attachment.loadOp);
						summary.storeOps.push_back(attachment.storeOp);
					}
					for (const auto& subpass : group.renderPassPlan->subpasses)
					{
						summary.preserveCounts.push_back(static_cast<uint32_t>(subpass.preserveAttachments.size()));
					}
				}
				summaries.push_back(std::move(summary));
			}
		}
		return summaries;
	}

	static auto GetBuiltImageUsage(const RenderGraph& inGraph, const std::string& inName) -> VkImageUsageFlags
	{
		return inGraph.m_buildResult.GetImageInfo(inGraph.m_buildResult.GetImageIndex(inName)).m_usage;
	}

	static auto GetManagedSubpassPlan(
		const RenderGraph& inGraph,
		const std::string& inPassName) -> RenderGraph::SubmitBatch::ManagedSubpassPlan
	{
		const RenderGraph::PassIndex passIndex = inGraph.m_buildResult.GetPassIndex(inPassName);
		for (const RenderGraph::SubmitBatch& submitBatch : inGraph.m_buildResult.submitBatches)
		{
			for (const RenderGraph::SubmitBatch::PassGroupPlan& group : submitBatch.graphicsGroups)
			{
				if (!group.renderPassPlan.has_value())
				{
					continue;
				}
				for (const auto& subpass : group.renderPassPlan->subpasses)
				{
					if (subpass.pass == passIndex)
					{
						return subpass;
					}
				}
			}
		}
		CHECK_TRUE(false, "Managed subpass plan was not found!");
		return {};
	}

	static auto OpaqueGraphicsPreservesRenderPassScope() -> bool
	{
		RenderGraph graph;
		RenderGraph::GraphicsPassInfo graphicsPass;
		graphicsPass.SetNeverCull();
		graph.AddPass("opaque", graphicsPass);
		graph.Build();

		RenderGraphInstance instance(graph);
		RenderGraphInstance::PassInfo passInfo;
		passInfo.SetProcess([](RenderGraphInstance::ExecutionContext& inContext)
		{
			inContext.RecordCommands([](CommandBuffer* inCommandBuffer)
			{
				CommandBuffer::RenderPassScope scope;
				scope.renderPass = (VkRenderPass)1;
				scope.framebuffer = (VkFramebuffer)1;
				scope.subpassScopes.resize(1);
				inCommandBuffer->AppendRenderPass(&scope);
			});
		});
		instance.SetUpPass("opaque", passInfo);

		CommandBuffer commands;
		instance._AppendPassCommands(graph.m_buildResult.GetPassIndex("opaque"), commands);
		return commands.m_scopes.size() == 1 &&
			std::holds_alternative<CommandBuffer::RenderPassScope>(commands.m_scopes.front());
	}

	static auto ClearOverridesDoNotInvalidateCompileState() -> bool
	{
		RenderGraph graph;
		RenderGraph::ImageInfo image;
		image.SetAsExternal();
		graph.AddImage("color", image);
		RenderGraph::AttachmentInfo attachment;
		attachment.SetLoadStoreOperations(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
		RenderGraph::RenderPassInfo renderPass;
		renderPass.AddColorAttachment(0, "color", attachment);
		renderPass.SetNeverCull();
		graph.AddPass("render", renderPass);
		graph.Build();

		RenderGraphInstance instance(graph);
		instance.m_compiled = true;
		VkClearColorValue clear{};
		clear.float32[0] = 0.25f;
		instance.SetColorClearValue("render", 0, clear);
		const bool setPreservedCompile = instance.m_compiled && instance.m_colorClearValueOverrides.size() == 1;
		instance.ResetClearValue("render", 0);
		return setPreservedCompile && instance.m_compiled && instance.m_colorClearValueOverrides.empty();
	}

	static auto GetScheduledPassNames(const RenderGraph& inGraph) -> std::vector<std::string>
	{
		std::vector<std::string> names;

		for (uint32_t submitIndex = 0; submitIndex < inGraph.m_buildResult.GetSubmitBatchCount(); ++submitIndex)
		{
			const RenderGraph::SubmitBatch& submitBatch = inGraph.m_buildResult.GetSubmitBatch(submitIndex);
			auto funcCollectGroupNames = [&](const std::vector<RenderGraph::SubmitBatch::PassGroupPlan>& inGroups)
			{
				for (const RenderGraph::SubmitBatch::PassGroupPlan& group : inGroups)
				{
					for (const RenderGraph::PassIndex passIndex : group.passes)
					{
						CHECK_TRUE(passIndex < inGraph.m_passes.size(), "Invalid scheduled render graph pass!");
						names.push_back(inGraph.m_passes[passIndex].name);
					}
				}
			};

			funcCollectGroupNames(submitBatch.graphicsGroups);
			funcCollectGroupNames(submitBatch.computeGroups);
		}

		return names;
	}

	struct CrossQueueBuildSummary
	{
		uint32_t submitBatchCount = 0;
		uint32_t graphicsReleaseBarrierCount = 0;
		uint32_t graphicsAcquireBarrierCount = 0;
		uint32_t graphicsSignalPlanCount = 0;
		uint32_t graphicsWaitPlanCount = 0;
		uint32_t computeReleaseBarrierCount = 0;
		uint32_t computeAcquireBarrierCount = 0;
		uint32_t computeSignalPlanCount = 0;
		uint32_t computeWaitPlanCount = 0;
	};

	static auto SummarizeCrossQueueBuild(const RenderGraph& inGraph) -> CrossQueueBuildSummary
	{
		CrossQueueBuildSummary summary;
		summary.submitBatchCount = static_cast<uint32_t>(inGraph.m_buildResult.GetSubmitBatchCount());

		for (uint32_t submitIndex = 0; submitIndex < inGraph.m_buildResult.GetSubmitBatchCount(); ++submitIndex)
		{
			const RenderGraph::SubmitBatch& submitBatch = inGraph.m_buildResult.GetSubmitBatch(submitIndex);
			for (const RenderGraph::SubmitBatch::PassGroupPlan& group : submitBatch.graphicsGroups)
			{
				summary.graphicsReleaseBarrierCount += static_cast<uint32_t>(group.queueReleaseBarriers.size());
				summary.graphicsSignalPlanCount += static_cast<uint32_t>(group.queueSignalPlans.size());
				summary.graphicsWaitPlanCount += static_cast<uint32_t>(group.queueWaitPlans.size());
				for (const RenderGraph::BarrierPlan& plan : group.prologueBarriers)
				{
					if (plan.before < inGraph.m_passes.size() &&
						inGraph.m_passes[plan.before].queue != RenderGraph::QueueType::GRAPHICS)
					{
						++summary.graphicsAcquireBarrierCount;
					}
				}
			}

			for (const RenderGraph::SubmitBatch::PassGroupPlan& group : submitBatch.computeGroups)
			{
				summary.computeReleaseBarrierCount += static_cast<uint32_t>(group.queueReleaseBarriers.size());
				summary.computeSignalPlanCount += static_cast<uint32_t>(group.queueSignalPlans.size());
				summary.computeWaitPlanCount += static_cast<uint32_t>(group.queueWaitPlans.size());
				for (const RenderGraph::BarrierPlan& plan : group.prologueBarriers)
				{
					if (plan.before < inGraph.m_passes.size() &&
						inGraph.m_passes[plan.before].queue != RenderGraph::QueueType::COMPUTE)
					{
						++summary.computeAcquireBarrierCount;
					}
				}
			}
		}

		return summary;
	}

	static auto CountQueueSyncPlansAfterLinkCullAndResolve(RenderGraph& inGraph) -> uint32_t
	{
		RenderGraph::BuildContext context = inGraph._CreateBuildContext();
		inGraph._LinkPasses(context);
		inGraph._CullPasses(context);
		CHECK_TRUE(context.queueSyncPlans.empty(), "Link and cull should not resolve queue sync plans yet!");
		inGraph._ResolveDependency(context);
		return static_cast<uint32_t>(context.queueSyncPlans.size());
	}

	static auto SummarizeScheduledResourceBarriers(RenderGraph& inGraph) -> CrossQueueBuildSummary
	{
		RenderGraph::BuildContext context = inGraph._CreateBuildContext();
		inGraph._LinkPasses(context);
		inGraph._CullPasses(context);
		inGraph._ResolveDependency(context);
		RenderGraph::BuildResult result;
		inGraph._BuildScheduleAndBatches(context, result);
		inGraph._BuildResourceAliases(context, result);
		inGraph._MaterializeResourceAliases(context);
		inGraph._BuildScheduledResourceBarriers(context, result);
		result.passes = std::move(context.passes);
		inGraph.m_buildResult = std::move(result);
		return SummarizeCrossQueueBuild(inGraph);
	}

	static auto GetBuiltBufferIndex(const RenderGraph& inGraph, const std::string& inName) -> RenderGraph::BufferIndex
	{
		return inGraph.m_buildResult.GetBufferIndex(inName);
	}

	static auto GetBuiltImageIndex(const RenderGraph& inGraph, const std::string& inName) -> RenderGraph::ImageIndex
	{
		return inGraph.m_buildResult.GetImageIndex(inName);
	}

	static auto GetBuiltBufferCount(const RenderGraph& inGraph) -> size_t
	{
		return inGraph.m_buildResult.GetBufferCount();
	}

	static auto GetBuiltImageCount(const RenderGraph& inGraph) -> size_t
	{
		return inGraph.m_buildResult.GetImageCount();
	}

	static auto GetInputBufferUsageIndex(
		const RenderGraph& inGraph,
		const std::string& inPassName,
		const std::string& inBufferName) -> RenderGraph::BufferIndex
	{
		const RenderGraph::PassRecord& pass = inGraph.m_passes[inGraph._GetPassIndex(inPassName)];
		for (const RenderGraph::BufferUsage& usage : pass.bufferUsages)
		{
			if (usage.buffer == inBufferName)
			{
				return usage.bufferIndex;
			}
		}
		return RenderGraph::INVALID_INDEX;
	}

	static auto GetInputImageUsageIndex(
		const RenderGraph& inGraph,
		const std::string& inPassName,
		const std::string& inImageName) -> RenderGraph::ImageIndex
	{
		const RenderGraph::PassRecord& pass = inGraph.m_passes[inGraph._GetPassIndex(inPassName)];
		for (const RenderGraph::ImageUsage& usage : pass.imageUsages)
		{
			if (usage.image == inImageName)
			{
				return usage.imageIndex;
			}
		}
		return RenderGraph::INVALID_INDEX;
	}

	static auto GetBuiltBufferUsageIndex(
		const RenderGraph& inGraph,
		const std::string& inPassName,
		const std::string& inBufferName) -> RenderGraph::BufferIndex
	{
		const RenderGraph::PassRecord& pass = inGraph.m_buildResult.GetPass(inGraph.m_buildResult.GetPassIndex(inPassName));
		for (const RenderGraph::BufferUsage& usage : pass.bufferUsages)
		{
			if (usage.buffer == inBufferName)
			{
				return usage.bufferIndex;
			}
		}
		return RenderGraph::INVALID_INDEX;
	}

	static auto GetBuiltImageUsageIndex(
		const RenderGraph& inGraph,
		const std::string& inPassName,
		const std::string& inImageName) -> RenderGraph::ImageIndex
	{
		const RenderGraph::PassRecord& pass = inGraph.m_buildResult.GetPass(inGraph.m_buildResult.GetPassIndex(inPassName));
		for (const RenderGraph::ImageUsage& usage : pass.imageUsages)
		{
			if (usage.image == inImageName)
			{
				return usage.imageIndex;
			}
		}
		return RenderGraph::INVALID_INDEX;
	}

	static auto GetInputPassAdjacencyCount(const RenderGraph& inGraph, const std::string& inPassName) -> size_t
	{
		return inGraph.m_passes[inGraph._GetPassIndex(inPassName)].adjacency.size();
	}

	static auto FindFirstImagePrologueBarrierRange(
		const RenderGraph& inGraph,
		const std::string& inName) -> std::optional<RenderGraph::ImageSubresourceRange>
	{
		const RenderGraph::ImageIndex imageIndex = inGraph.m_buildResult.GetImageIndex(inName);
		for (uint32_t submitIndex = 0; submitIndex < inGraph.m_buildResult.GetSubmitBatchCount(); ++submitIndex)
		{
			const RenderGraph::SubmitBatch& submitBatch = inGraph.m_buildResult.GetSubmitBatch(submitIndex);
			auto funcFindInGroups = [&](const std::vector<RenderGraph::SubmitBatch::PassGroupPlan>& inGroups) -> std::optional<RenderGraph::ImageSubresourceRange>
			{
				for (const RenderGraph::SubmitBatch::PassGroupPlan& group : inGroups)
				{
					for (const RenderGraph::BarrierPlan& plan : group.prologueBarriers)
					{
						if (plan.resourceType == RenderGraph::ResourceType::IMAGE && plan.image == imageIndex)
						{
							return plan.subresourceRange;
						}
					}
				}

				return std::nullopt;
			};

			if (std::optional<RenderGraph::ImageSubresourceRange> range = funcFindInGroups(submitBatch.graphicsGroups))
			{
				return range;
			}
			if (std::optional<RenderGraph::ImageSubresourceRange> range = funcFindInGroups(submitBatch.computeGroups))
			{
				return range;
			}
		}

		return std::nullopt;
	}
};

namespace
{
	void ExpectThrows(const std::function<void()>& inProcess, const std::string& inMessageFragment)
	{
		try
		{
			inProcess();
		}
		catch (const std::exception& exception)
		{
			const std::string message = exception.what();
			CHECK_TRUE(
				message.find(inMessageFragment) != std::string::npos,
				std::string("Unexpected exception: ") + message);
			return;
		}

		CHECK_TRUE(false, "Expected exception was not thrown!");
	}

	void TestInternalSampledImageRequiresWriter()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_SAMPLED_BIT);
		graph.AddImage("image", image);

		RenderGraph::GraphicsPassInfo readPass;
		readPass.AddSampledImage("image", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
		graph.AddPass("read", readPass);

		ExpectThrows(
			[&graph]()
			{
				graph.Build();
			},
			"Internal render graph image cannot be read before it is written");
	}

	void TestInternalAttachmentLoadRequiresWriter()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);
		graph.AddImage("color", image);

		RenderGraph::SubpassInfo subpass;
		RenderGraph::AttachmentInfo attachment;
		attachment.SetLoadStoreOperations(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
		subpass.AddColorAttachment(0, "color", attachment);
		graph.AddPass("load", subpass);

		ExpectThrows(
			[&graph]()
			{
				graph.Build();
			},
			"Internal render graph attachment cannot use LOAD before it is written");
	}

	void TestInternalStorageImageCanBeFirstWriter()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT);
		graph.AddImage("image", image);

		RenderGraph::ComputePassInfo writePass;
		writePass.AddStorageImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write", writePass);

		graph.Build();
	}

	void TestImageSubresourceBarrierUsesMipRange()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		image.CustomizeMipLevels(2);
		graph.AddImage("image", image);

		RenderGraph::ComputePassInfo writePass;
		RenderGraph::ImageSubresourceRange mip0(0, 0);
		writePass.AddStorageImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, mip0);
		graph.AddPass("write", writePass);

		RenderGraph::ComputePassInfo readPass;
		readPass.AddSampledImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, mip0);
		readPass.SetNeverCull();
		graph.AddPass("read", readPass);

		graph.Build();

		const std::optional<RenderGraph::ImageSubresourceRange> range =
			RenderGraphTestProbe::FindFirstImagePrologueBarrierRange(graph, "image");
		CHECK_TRUE(range.has_value(), "Expected an image prologue barrier!");
		CHECK_TRUE(range->baseMipLevel == 0, "Barrier should target mip 0!");
		CHECK_TRUE(range->levelCount == 1, "Barrier should target one mip!");
		CHECK_TRUE(range->baseArrayLayer == 0, "Barrier should target layer 0!");
		CHECK_TRUE(range->layerCount == 1, "Barrier should target one layer!");
	}

	void TestInternalImageReadFromUntouchedMipStillFails()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		image.CustomizeMipLevels(2);
		graph.AddImage("image", image);

		RenderGraph::ComputePassInfo writePass;
		RenderGraph::ImageSubresourceRange mip0(0, 0);
		writePass.AddStorageImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, mip0);
		graph.AddPass("write", writePass);

		RenderGraph::ComputePassInfo readPass;
		RenderGraph::ImageSubresourceRange mip1(1, 0);
		readPass.AddSampledImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, mip1);
		graph.AddPass("read", readPass);

		ExpectThrows(
			[&graph]()
			{
				graph.Build();
			},
			"Internal render graph image cannot be read before it is written");
	}

	void TestImageSubresourceCrossQueueSyncDependsOnMipOverlap()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		image.CustomizeMipLevels(2);
		image.SetAsExternal();
		graph.AddImage("image", image);

		RenderGraph::GraphicsPassInfo writePass;
		RenderGraph::ImageSubresourceRange mip0(0, 0);
		writePass.AddStorageImage("image", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, mip0);
		graph.AddPass("graphics_write", writePass);

		RenderGraph::ComputePassInfo readPass;
		RenderGraph::ImageSubresourceRange mip1(1, 0);
		readPass.AddSampledImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, mip1);
		graph.AddPass("compute_read", readPass);

		CHECK_TRUE(
			RenderGraphTestProbe::CountQueueSyncPlansAfterLinkCullAndResolve(graph) == 0,
			"Different mips should not create a queue sync plan!");
	}

	void TestImageSubresourceCrossQueueSyncForSameMip()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		image.CustomizeMipLevels(2);
		image.SetAsExternal();
		graph.AddImage("image", image);

		RenderGraph::GraphicsPassInfo writePass;
		RenderGraph::ImageSubresourceRange mip0(0, 0);
		writePass.AddStorageImage("image", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, mip0);
		graph.AddPass("graphics_write", writePass);

		RenderGraph::ComputePassInfo readPass;
		readPass.AddSampledImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, mip0);
		graph.AddPass("compute_read", readPass);

		CHECK_TRUE(
			RenderGraphTestProbe::CountQueueSyncPlansAfterLinkCullAndResolve(graph) == 1,
			"Same mip should create one queue sync plan!");
	}

	void TestGraphicsToComputeImageDependencyBuildsCrossQueueSync()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		graph.AddImage("image", image);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::GraphicsPassInfo writePass;
		writePass.AddStorageImage("image", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
		graph.AddPass("graphics_write", writePass);

		RenderGraph::ComputePassInfo readPass;
		readPass.AddSampledImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readPass.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("compute_read", readPass);

		graph.Build();

		const RenderGraphTestProbe::CrossQueueBuildSummary summary =
			RenderGraphTestProbe::SummarizeCrossQueueBuild(graph);
		CHECK_TRUE(summary.submitBatchCount == 2, "Graphics-to-compute dependency should split submit batches!");
		CHECK_TRUE(summary.graphicsReleaseBarrierCount == 1, "Graphics producer should record one queue release barrier!");
		CHECK_TRUE(summary.computeAcquireBarrierCount == 1, "Compute consumer should record one queue acquire barrier!");
		CHECK_TRUE(summary.graphicsSignalPlanCount == 1, "Graphics producer should signal one queue sync plan!");
		CHECK_TRUE(summary.computeWaitPlanCount == 1, "Compute consumer should wait on one queue sync plan!");
	}

	void TestResolveAndCullBuildActiveQueueSyncPlans()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		graph.AddImage("image", image);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::GraphicsPassInfo writePass;
		writePass.AddStorageImage("image", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
		graph.AddPass("graphics_write", writePass);

		RenderGraph::ComputePassInfo readPass;
		readPass.AddSampledImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readPass.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("compute_read", readPass);

		const uint32_t queueSyncPlanCount =
			RenderGraphTestProbe::CountQueueSyncPlansAfterLinkCullAndResolve(graph);
		CHECK_TRUE(queueSyncPlanCount == 1, "Active cross-queue dependency should build queue sync before scheduling!");
	}

	void TestComputeToGraphicsBufferDependencyBuildsCrossQueueSync()
	{
		RenderGraph graph;

		RenderGraph::BufferInfo buffer;
		buffer.SetSize(256);
		buffer.AddUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		graph.AddBuffer("buffer", buffer);

		RenderGraph::ImageInfo output;
		output.SetAsExternal();
		graph.AddImage("output", output);

		RenderGraph::ComputePassInfo writePass;
		writePass.AddDescriptorStorageBuffer("buffer", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("compute_write", writePass);

		RenderGraph::GraphicsPassInfo readPass;
		readPass.AddDescriptorUniformBuffer("buffer", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
		readPass.AddStorageImage("output", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
		graph.AddPass("graphics_read", readPass);

		graph.Build();

		const RenderGraphTestProbe::CrossQueueBuildSummary summary =
			RenderGraphTestProbe::SummarizeCrossQueueBuild(graph);
		CHECK_TRUE(summary.submitBatchCount == 2, "Compute-to-graphics dependency should split submit batches!");
		CHECK_TRUE(summary.computeReleaseBarrierCount == 1, "Compute producer should record one queue release barrier!");
		CHECK_TRUE(summary.graphicsAcquireBarrierCount == 1, "Graphics consumer should record one queue acquire barrier!");
		CHECK_TRUE(summary.computeSignalPlanCount == 1, "Compute producer should signal one queue sync plan!");
		CHECK_TRUE(summary.graphicsWaitPlanCount == 1, "Graphics consumer should wait on one queue sync plan!");
	}

	void TestScheduledResourceBarriersAssignCrossQueuePlansDirectly()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		graph.AddImage("image", image);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::GraphicsPassInfo writePass;
		writePass.AddStorageImage("image", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
		graph.AddPass("graphics_write", writePass);

		RenderGraph::ComputePassInfo readPass;
		readPass.AddSampledImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readPass.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("compute_read", readPass);

		const RenderGraphTestProbe::CrossQueueBuildSummary summary =
			RenderGraphTestProbe::SummarizeScheduledResourceBarriers(graph);
		CHECK_TRUE(summary.submitBatchCount == 2, "Scheduled resource barrier walk should preserve submit splitting!");
		CHECK_TRUE(summary.graphicsReleaseBarrierCount == 1, "Scheduled resource barrier walk should write queue release to source group!");
		CHECK_TRUE(summary.computeAcquireBarrierCount == 1, "Scheduled resource barrier walk should write acquire barrier to target group!");
		CHECK_TRUE(summary.graphicsSignalPlanCount == 1, "Scheduled resource barrier walk should assign queue signal to source group!");
		CHECK_TRUE(summary.computeWaitPlanCount == 1, "Scheduled resource barrier walk should assign queue wait to target group!");
	}

	void TestCrossQueueWriteAfterWriteBuildsSync()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo image;
		image.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT);
		graph.AddImage("image", image);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::GraphicsPassInfo graphicsWritePass;
		graphicsWritePass.AddStorageImage("image", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
		graph.AddPass("graphics_write", graphicsWritePass);

		RenderGraph::ComputePassInfo computeWritePass;
		computeWritePass.AddStorageImage("image", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		computeWritePass.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("compute_write", computeWritePass);

		graph.Build();

		const RenderGraphTestProbe::CrossQueueBuildSummary summary =
			RenderGraphTestProbe::SummarizeCrossQueueBuild(graph);
		CHECK_TRUE(summary.submitBatchCount == 2, "Cross-queue write-after-write should split submit batches!");
		CHECK_TRUE(summary.graphicsReleaseBarrierCount == 1, "Cross-queue write-after-write should record a queue release barrier!");
		CHECK_TRUE(summary.computeAcquireBarrierCount == 1, "Cross-queue write-after-write should record a queue acquire barrier!");
		CHECK_TRUE(summary.graphicsSignalPlanCount == 1, "Cross-queue write-after-write should signal one queue sync plan!");
		CHECK_TRUE(summary.computeWaitPlanCount == 1, "Cross-queue write-after-write should wait on one queue sync plan!");
	}

	void TestPassWithoutExternalUsageIsCulled()
	{
		RenderGraph graph;

		RenderGraph::BufferInfo scratch;
		scratch.SetSize(256);
		scratch.AddUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		graph.AddBuffer("scratch", scratch);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::ComputePassInfo unusedPass;
		unusedPass.AddDescriptorStorageBuffer("scratch", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("unused", unusedPass);

		RenderGraph::ComputePassInfo outputPass;
		outputPass.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("output", outputPass);

		graph.Build();

		const std::vector<std::string> names = RenderGraphTestProbe::GetScheduledPassNames(graph);
		CHECK_TRUE(names.size() == 1, "Only the external output pass should be scheduled!");
		CHECK_TRUE(names.front() == "output", "The unused pass should be culled!");
	}

	void TestNeverCullPassWithoutExternalUsageIsScheduled()
	{
		RenderGraph graph;

		RenderGraph::BufferInfo scratch;
		scratch.SetSize(256);
		scratch.AddUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
		graph.AddBuffer("scratch", scratch);

		RenderGraph::ComputePassInfo sideEffectPass;
		sideEffectPass.SetNeverCull();
		sideEffectPass.AddDescriptorStorageBuffer("scratch", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("side_effect", sideEffectPass);

		graph.Build();

		const std::vector<std::string> names = RenderGraphTestProbe::GetScheduledPassNames(graph);
		CHECK_TRUE(names.size() == 1, "Never-cull pass should be scheduled without external resource usage!");
		CHECK_TRUE(names.front() == "side_effect", "Never-cull pass should be the scheduled pass!");
	}

	void TestCullKeepsExternalUsageDependencies()
	{
		RenderGraph graph;

		RenderGraph::BufferInfo intermediate;
		intermediate.SetSize(256);
		intermediate.AddUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		graph.AddBuffer("intermediate", intermediate);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::ComputePassInfo producerPass;
		producerPass.AddDescriptorStorageBuffer("intermediate", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("producer", producerPass);

		RenderGraph::ComputePassInfo outputPass;
		outputPass.AddDescriptorUniformBuffer("intermediate", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		outputPass.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("output", outputPass);

		graph.Build();

		const std::vector<std::string> names = RenderGraphTestProbe::GetScheduledPassNames(graph);
		CHECK_TRUE(names.size() == 2, "The external output pass and its dependency should be scheduled!");
		CHECK_TRUE(names[0] == "producer", "The dependency should be scheduled before the external output pass!");
		CHECK_TRUE(names[1] == "output", "The external output pass should be scheduled!");
	}

	void TestNonOverlappingInternalBuffersAliasByDefault()
	{
		RenderGraph graph;

		RenderGraph::BufferInfo scratch;
		scratch.SetSize(256);
		scratch.AddUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		graph.AddBuffer("scratch_a", scratch);
		graph.AddBuffer("scratch_b", scratch);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::ComputePassInfo writeA;
		writeA.AddDescriptorStorageBuffer("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_a", writeA);

		RenderGraph::ComputePassInfo readA;
		readA.AddDescriptorUniformBuffer("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readA.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_a", readA);

		RenderGraph::ComputePassInfo writeB;
		writeB.AddDescriptorStorageBuffer("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_b", writeB);

		RenderGraph::ComputePassInfo readB;
		readB.AddDescriptorUniformBuffer("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readB.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_b", readB);

		graph.AddExtraPassDependency("read_a", "write_b");
		graph.Build();

		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_a") ==
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_b"),
			"Non-overlapping compatible internal buffers should alias by default!");
		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_a") ==
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_b"),
			"Aliased buffers should resolve to the same compact physical resource!");
	}

	void TestAliasingMaterializationPreservesLogicalInputs()
	{
		RenderGraph graph;

		RenderGraph::BufferInfo scratch;
		scratch.SetSize(256);
		scratch.AddUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		graph.AddBuffer("scratch_a", scratch);
		graph.AddBuffer("scratch_b", scratch);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::ComputePassInfo writeA;
		writeA.AddDescriptorStorageBuffer("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_a", writeA);

		RenderGraph::ComputePassInfo readA;
		readA.AddDescriptorUniformBuffer("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readA.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_a", readA);

		RenderGraph::ComputePassInfo writeB;
		writeB.AddDescriptorStorageBuffer("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_b", writeB);

		RenderGraph::ComputePassInfo readB;
		readB.AddDescriptorUniformBuffer("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readB.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_b", readB);

		graph.AddExtraPassDependency("read_a", "write_b");
		graph.Build();

		const auto physicalScratchA = RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_a");
		const auto physicalScratchB = RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_b");
		const auto physicalOutput = RenderGraphTestProbe::GetBuiltBufferIndex(graph, "output");

		CHECK_TRUE(physicalScratchA == physicalScratchB, "Aliased logical buffers must share one physical index!");
		CHECK_TRUE(physicalScratchA < RenderGraphTestProbe::GetBuiltBufferCount(graph), "Build buffer index is out of bounds!");
		CHECK_TRUE(physicalOutput < RenderGraphTestProbe::GetBuiltBufferCount(graph), "External build buffer index is out of bounds!");
		CHECK_TRUE(RenderGraphTestProbe::GetBuiltBufferCount(graph) == 2, "Two logical scratch buffers should compact to one build buffer plus output!");

		CHECK_TRUE(
			RenderGraphTestProbe::GetInputBufferUsageIndex(graph, "write_a", "scratch_a") == ~0u,
			"Build must not write resolved indices into the original pass input!");
		CHECK_TRUE(
			RenderGraphTestProbe::GetInputBufferUsageIndex(graph, "write_b", "scratch_b") == ~0u,
			"Build must not write resolved indices into the original pass input!");
		CHECK_TRUE(
			RenderGraphTestProbe::GetInputBufferUsageIndex(graph, "read_a", "output") == ~0u,
			"Build must not write resolved indices into the original pass input!");
		CHECK_TRUE(
			RenderGraphTestProbe::GetInputPassAdjacencyCount(graph, "write_a") == 0,
			"Build must not write dependency edges into the original pass input!");

		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltBufferUsageIndex(graph, "write_a", "scratch_a") == physicalScratchA,
			"Built pass should use the physical buffer index!");

		graph.Build();
		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_a") == physicalScratchA &&
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_b") == physicalScratchB,
			"Repeated builds should preserve materialized resource mapping!");
	}

	void TestOverlappingInternalBuffersDoNotAlias()
	{
		RenderGraph graph;

		RenderGraph::BufferInfo scratch;
		scratch.SetSize(256);
		scratch.AddUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		graph.AddBuffer("scratch_a", scratch);
		graph.AddBuffer("scratch_b", scratch);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::ComputePassInfo writeA;
		writeA.AddDescriptorStorageBuffer("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_a", writeA);

		RenderGraph::ComputePassInfo writeB;
		writeB.AddDescriptorStorageBuffer("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_b", writeB);

		RenderGraph::ComputePassInfo readBoth;
		readBoth.AddDescriptorUniformBuffer("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readBoth.AddDescriptorUniformBuffer("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readBoth.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_both", readBoth);

		graph.Build();

		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_a") !=
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_b"),
			"Overlapping internal buffers must not alias!");
	}

	void TestCrossQueueInternalBuffersDoNotAlias()
	{
		RenderGraph graph;

		RenderGraph::BufferInfo scratch;
		scratch.SetSize(256);
		scratch.AddUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		graph.AddBuffer("scratch_compute", scratch);
		graph.AddBuffer("scratch_graphics", scratch);

		RenderGraph::ImageInfo output;
		output.SetAsExternal();
		graph.AddImage("output", output);

		RenderGraph::ComputePassInfo computeWrite;
		computeWrite.AddDescriptorStorageBuffer("scratch_compute", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("compute_write", computeWrite);

		RenderGraph::ComputePassInfo computeRead;
		computeRead.AddDescriptorUniformBuffer("scratch_compute", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("compute_read", computeRead);

		RenderGraph::GraphicsPassInfo graphicsWrite;
		graphicsWrite.AddDescriptorStorageBuffer("scratch_graphics", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
		graph.AddPass("graphics_write", graphicsWrite);

		RenderGraph::GraphicsPassInfo graphicsRead;
		graphicsRead.AddDescriptorUniformBuffer("scratch_graphics", VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT);
		graphicsRead.AddStorageImage("output", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
		graph.AddPass("graphics_read", graphicsRead);

		graph.AddExtraPassDependency("compute_read", "graphics_write");
		graph.Build();

		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_compute") !=
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_graphics"),
			"Internal buffers used by different queues must not alias!");
	}

	void TestResourceAliasingCanBeDisabled()
	{
		RenderGraph graph;
		graph.EnableResourceAliasing(false);

		RenderGraph::BufferInfo scratch;
		scratch.SetSize(256);
		scratch.AddUsage(VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
		graph.AddBuffer("scratch_a", scratch);
		graph.AddBuffer("scratch_b", scratch);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::ComputePassInfo writeA;
		writeA.AddDescriptorStorageBuffer("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_a", writeA);

		RenderGraph::ComputePassInfo readA;
		readA.AddDescriptorUniformBuffer("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readA.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_a", readA);

		RenderGraph::ComputePassInfo writeB;
		writeB.AddDescriptorStorageBuffer("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_b", writeB);

		RenderGraph::ComputePassInfo readB;
		readB.AddDescriptorUniformBuffer("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readB.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_b", readB);

		graph.AddExtraPassDependency("read_a", "write_b");
		graph.Build();

		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_a") !=
			RenderGraphTestProbe::GetBuiltBufferIndex(graph, "scratch_b"),
			"Disabled resource aliasing should keep internal buffers independent!");
	}

	void TestNonOverlappingInternalImagesAliasWhenDescriptorsMatch()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo scratch;
		scratch.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		graph.AddImage("scratch_a", scratch);
		graph.AddImage("scratch_b", scratch);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::ComputePassInfo writeA;
		writeA.AddStorageImage("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_a", writeA);

		RenderGraph::ComputePassInfo readA;
		readA.AddSampledImage("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readA.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_a", readA);

		RenderGraph::ComputePassInfo writeB;
		writeB.AddStorageImage("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_b", writeB);

		RenderGraph::ComputePassInfo readB;
		readB.AddSampledImage("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readB.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_b", readB);

		graph.AddExtraPassDependency("read_a", "write_b");
		graph.Build();

		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltImageIndex(graph, "scratch_a") ==
			RenderGraphTestProbe::GetBuiltImageIndex(graph, "scratch_b"),
			"Non-overlapping compatible internal images should alias!");
		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltImageIndex(graph, "scratch_a") ==
			RenderGraphTestProbe::GetBuiltImageIndex(graph, "scratch_b"),
			"Aliased images should resolve to the same compact physical resource!");
		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltImageCount(graph) == 1,
			"Two aliased logical images should compact to one physical image!");
		CHECK_TRUE(
			RenderGraphTestProbe::GetInputImageUsageIndex(graph, "write_a", "scratch_a") == ~0u,
			"Build must not write resolved image indices into the original pass input!");
		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltImageUsageIndex(graph, "write_a", "scratch_a") ==
			RenderGraphTestProbe::GetBuiltImageIndex(graph, "scratch_a"),
			"Built image pass should use the physical image index!");
	}

	void TestInternalImagesWithDifferentDescriptorsDoNotAlias()
	{
		RenderGraph graph;

		RenderGraph::ImageInfo scratchA;
		scratchA.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		scratchA.CustomizeSize2D(64, 64);
		graph.AddImage("scratch_a", scratchA);

		RenderGraph::ImageInfo scratchB;
		scratchB.AddUsage(VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
		scratchB.CustomizeSize2D(128, 64);
		graph.AddImage("scratch_b", scratchB);

		RenderGraph::BufferInfo output;
		output.SetAsExternal();
		graph.AddBuffer("output", output);

		RenderGraph::ComputePassInfo writeA;
		writeA.AddStorageImage("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_a", writeA);

		RenderGraph::ComputePassInfo readA;
		readA.AddSampledImage("scratch_a", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readA.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_a", readA);

		RenderGraph::ComputePassInfo writeB;
		writeB.AddStorageImage("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("write_b", writeB);

		RenderGraph::ComputePassInfo readB;
		readB.AddSampledImage("scratch_b", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		readB.AddDescriptorStorageBuffer("output", VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
		graph.AddPass("read_b", readB);

		graph.AddExtraPassDependency("read_a", "write_b");
		graph.Build();

		CHECK_TRUE(
			RenderGraphTestProbe::GetBuiltImageIndex(graph, "scratch_a") !=
			RenderGraphTestProbe::GetBuiltImageIndex(graph, "scratch_b"),
			"Internal images with different descriptors must not alias!");
	}

	void TestGraphicsPassIsOpaqueAndPreservesRenderPassScope()
	{
		RenderGraph graph;
		RenderGraph::GraphicsPassInfo pass;
		pass.SetNeverCull();
		graph.AddPass("opaque", pass);
		graph.Build();

		const auto groups = RenderGraphTestProbe::GetGraphicsGroups(graph);
		CHECK_TRUE(groups.size() == 1, "Opaque graphics pass should create one graphics group!");
		CHECK_TRUE(!groups.front().managedRenderPass, "GraphicsPassInfo must remain unmanaged!");
		CHECK_TRUE(RenderGraphTestProbe::OpaqueGraphicsPreservesRenderPassScope(), "Opaque graphics pass must preserve caller render pass scopes!");
	}

	void TestRenderPassInfoIsAHardGroupBoundary()
	{
		RenderGraph graph;
		RenderGraph::ImageInfo image;
		image.SetAsExternal();
		graph.AddImage("color", image);

		RenderGraph::SubpassInfo before;
		before.AddColorAttachment(0, "color");
		before.SetNeverCull();
		graph.AddPass("before", before);

		RenderGraph::RenderPassInfo middle;
		middle.AddColorAttachment(0, "color");
		middle.SetNeverCull();
		graph.AddPass("middle", middle);

		RenderGraph::SubpassInfo after;
		after.AddColorAttachment(0, "color");
		after.SetNeverCull();
		graph.AddPass("after", after);
		graph.Build();

		const auto groups = RenderGraphTestProbe::GetGraphicsGroups(graph);
		CHECK_TRUE(groups.size() == 3, "RenderPassInfo must split neighboring SubpassInfo groups!");
		for (const auto& group : groups)
		{
			CHECK_TRUE(group.managedRenderPass, "All attachment passes should be managed!");
			CHECK_TRUE(group.passes.size() == 1, "RenderPassInfo boundary should keep every group independent!");
			CHECK_TRUE(group.subpassCount == 1, "Each independent managed group should have one subpass!");
		}
	}

	void TestCompatibleSubpassesMergeAndRepeatedClearSplits()
	{
		RenderGraph graph;
		RenderGraph::ImageInfo image;
		image.SetAsExternal();
		graph.AddImage("color", image);

		RenderGraph::AttachmentInfo firstAttachment;
		firstAttachment.SetLoadStoreOperations(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_DONT_CARE);
		RenderGraph::SubpassInfo first;
		first.AddColorAttachment(0, "color", firstAttachment);
		first.SetNeverCull();
		graph.AddPass("first", first);

		RenderGraph::AttachmentInfo loadAttachment;
		loadAttachment.SetLoadStoreOperations(VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_STORE_OP_STORE);
		RenderGraph::SubpassInfo second;
		second.AddColorAttachment(0, "color", loadAttachment);
		second.SetNeverCull();
		graph.AddPass("second", second);
		graph.Build();

		const auto mergedGroups = RenderGraphTestProbe::GetGraphicsGroups(graph);
		CHECK_TRUE(mergedGroups.size() == 1, "Compatible SubpassInfo passes should merge!");
		CHECK_TRUE(mergedGroups.front().passes.size() == 2, "Merged render pass should contain two passes!");
		CHECK_TRUE(mergedGroups.front().subpassCount == 2, "Merged render pass should contain two subpasses!");
		CHECK_TRUE(mergedGroups.front().loadOps.front() == VK_ATTACHMENT_LOAD_OP_CLEAR, "Merged attachment must use the first subpass loadOp!");
		CHECK_TRUE(mergedGroups.front().storeOps.front() == VK_ATTACHMENT_STORE_OP_STORE, "Merged attachment must use the last subpass storeOp!");

		RenderGraph clearGraph;
		clearGraph.AddImage("color", image);
		RenderGraph::AttachmentInfo clearAttachment;
		clearAttachment.SetLoadStoreOperations(VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_STORE_OP_STORE);
		RenderGraph::SubpassInfo clearFirst;
		clearFirst.AddColorAttachment(0, "color", clearAttachment);
		clearFirst.SetNeverCull();
		clearGraph.AddPass("clear_first", clearFirst);
		RenderGraph::SubpassInfo clearSecond;
		clearSecond.AddColorAttachment(0, "color", clearAttachment);
		clearSecond.SetNeverCull();
		clearGraph.AddPass("clear_second", clearSecond);
		clearGraph.Build();
		CHECK_TRUE(RenderGraphTestProbe::GetGraphicsGroups(clearGraph).size() == 2, "A later CLEAR of the same attachment must split the render pass!");
	}

	void TestIncompatibleAttachmentRangesRolesAndResourceAccessSplit()
	{
		RenderGraph rangeGraph;
		RenderGraph::ImageInfo layeredImage;
		layeredImage.CustomizeArrayLayers(2);
		layeredImage.SetAsExternal();
		rangeGraph.AddImage("image", layeredImage);
		RenderGraph::AttachmentInfo bothLayers;
		RenderGraph::ImageSubresourceRange bothRange;
		bothRange.layerCount = 2;
		bothLayers.SetSubresourceRange(bothRange);
		RenderGraph::SubpassInfo rangeFirst;
		rangeFirst.AddColorAttachment(0, "image", bothLayers);
		rangeFirst.SetNeverCull();
		rangeGraph.AddPass("first", rangeFirst);
		RenderGraph::AttachmentInfo secondLayer;
		secondLayer.SetSubresourceRange(RenderGraph::ImageSubresourceRange(0, 1));
		RenderGraph::SubpassInfo rangeSecond;
		rangeSecond.AddColorAttachment(0, "image", secondLayer);
		rangeSecond.SetNeverCull();
		rangeGraph.AddPass("second", rangeSecond);
		rangeGraph.Build();
		CHECK_TRUE(RenderGraphTestProbe::GetGraphicsGroups(rangeGraph).size() == 2, "Overlapping unequal attachment ranges must split managed groups!");

		RenderGraph roleGraph;
		RenderGraph::ImageInfo roleImage;
		roleImage.SetAsExternal();
		roleGraph.AddImage("image", roleImage);
		RenderGraph::SubpassInfo colorPass;
		colorPass.AddColorAttachment(0, "image");
		colorPass.SetNeverCull();
		roleGraph.AddPass("color", colorPass);
		RenderGraph::SubpassInfo depthPass;
		depthPass.SetDepthStencilAttachment("image");
		depthPass.SetNeverCull();
		roleGraph.AddPass("depth", depthPass);
		roleGraph.Build();
		CHECK_TRUE(RenderGraphTestProbe::GetGraphicsGroups(roleGraph).size() == 2, "Attachment role conflicts must split managed groups!");

		RenderGraph accessGraph;
		RenderGraph::ImageInfo attachmentImage;
		attachmentImage.SetAsExternal();
		accessGraph.AddImage("attachment", attachmentImage);
		RenderGraph::ImageInfo sharedImage;
		sharedImage.SetAsExternal();
		accessGraph.AddImage("shared", sharedImage);
		RenderGraph::SubpassInfo readPass;
		readPass.AddColorAttachment(0, "attachment");
		readPass.AddSampledImage("shared", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
		readPass.SetNeverCull();
		accessGraph.AddPass("read", readPass);
		RenderGraph::SubpassInfo writePass;
		writePass.AddColorAttachment(0, "attachment");
		writePass.AddStorageImage("shared", VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
		writePass.SetNeverCull();
		accessGraph.AddPass("write", writePass);
		accessGraph.Build();
		CHECK_TRUE(RenderGraphTestProbe::GetGraphicsGroups(accessGraph).size() == 2, "Incompatible non-attachment image access must split managed groups!");
	}

	void TestManagedPlanUsesExactLayersAndPreserveAttachments()
	{
		RenderGraph graph;
		RenderGraph::ImageInfo imageA;
		imageA.CustomizeArrayLayers(2);
		graph.AddImage("a", imageA);
		RenderGraph::ImageInfo imageB;
		graph.AddImage("b", imageB);

		RenderGraph::AttachmentInfo layer0;
		layer0.SetSubresourceRange(RenderGraph::ImageSubresourceRange(0, 0));
		RenderGraph::SubpassInfo first;
		first.AddColorAttachment(0, "a", layer0);
		first.SetNeverCull();
		graph.AddPass("first", first);

		RenderGraph::SubpassInfo middle;
		middle.AddColorAttachment(0, "b");
		middle.SetNeverCull();
		graph.AddPass("middle", middle);

		RenderGraph::AttachmentInfo layer1;
		layer1.SetSubresourceRange(RenderGraph::ImageSubresourceRange(0, 1));
		RenderGraph::SubpassInfo last;
		last.AddColorAttachment(0, "a", layer1);
		last.SetNeverCull();
		graph.AddPass("last", last);
		graph.AddExtraPassDependency("first", "middle");
		graph.AddExtraPassDependency("middle", "last");
		graph.Build();

		const auto groups = RenderGraphTestProbe::GetGraphicsGroups(graph);
		CHECK_TRUE(groups.size() == 1, "Compatible exact layer views should share one render pass!");
		CHECK_TRUE(groups.front().attachmentCount == 3, "Different layer views must remain distinct attachments!");

		RenderGraph preserveGraph;
		preserveGraph.AddImage("a", imageB);
		preserveGraph.AddImage("b", imageB);
		RenderGraph::SubpassInfo preserveFirst;
		preserveFirst.AddColorAttachment(0, "a");
		preserveFirst.SetNeverCull();
		preserveGraph.AddPass("first", preserveFirst);
		RenderGraph::SubpassInfo preserveMiddle;
		preserveMiddle.AddColorAttachment(0, "b");
		preserveMiddle.SetNeverCull();
		preserveGraph.AddPass("middle", preserveMiddle);
		RenderGraph::SubpassInfo preserveLast;
		preserveLast.AddColorAttachment(0, "a");
		preserveLast.SetNeverCull();
		preserveGraph.AddPass("last", preserveLast);
		preserveGraph.AddExtraPassDependency("first", "middle");
		preserveGraph.AddExtraPassDependency("middle", "last");
		preserveGraph.Build();

		const auto preserveGroups = RenderGraphTestProbe::GetGraphicsGroups(preserveGraph);
		CHECK_TRUE(preserveGroups.size() == 1, "Preserve test subpasses should merge!");
		CHECK_TRUE(preserveGroups.front().preserveCounts.size() == 3, "Preserve test should have three subpass plans!");
		CHECK_TRUE(preserveGroups.front().preserveCounts[1] == 1, "Middle subpass must preserve an attachment used again later!");
	}

	void TestResolvePlanAndInferredImageUsages()
	{
		RenderGraph graph;
		RenderGraph::ImageInfo multisampled;
		multisampled.CustomizeSampleCount(VK_SAMPLE_COUNT_4_BIT);
		graph.AddImage("msaa", multisampled);
		RenderGraph::ImageInfo resolved;
		resolved.CustomizeSampleCount(VK_SAMPLE_COUNT_1_BIT);
		graph.AddImage("resolved", resolved);

		RenderGraph::RenderPassInfo pass;
		pass.AddColorAttachment(2, "msaa");
		pass.SetResolveAttachment(2, "resolved");
		pass.SetNeverCull();
		graph.AddPass("resolve", pass);
		graph.Build();

		const auto subpass = RenderGraphTestProbe::GetManagedSubpassPlan(graph, "resolve");
		CHECK_TRUE(subpass.colorAttachments.size() == 3 && subpass.colorAttachments[2] != ~0u, "Color location must be preserved in the managed plan!");
		CHECK_TRUE(subpass.resolveAttachments.size() == 3 && subpass.resolveAttachments[2] != ~0u, "Resolve location must match its source color location!");
		CHECK_TRUE((RenderGraphTestProbe::GetBuiltImageUsage(graph, "msaa") & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0, "Color attachment usage should be inferred!");
		CHECK_TRUE((RenderGraphTestProbe::GetBuiltImageUsage(graph, "resolved") & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) != 0, "Resolve attachment usage should be inferred!");
	}

	void TestManagedAttachmentDeclarationValidation()
	{
		RenderGraph::SubpassInfo duplicateColor;
		duplicateColor.AddColorAttachment(0, "a");
		ExpectThrows([&]() { duplicateColor.AddColorAttachment(0, "b"); }, "already used");

		RenderGraph::SubpassInfo duplicateDepth;
		duplicateDepth.SetDepthStencilAttachment("a");
		ExpectThrows([&]() { duplicateDepth.SetDepthStencilAttachment("b"); }, "only have one");

		RenderGraph emptyGraph;
		RenderGraph::RenderPassInfo emptyPass;
		ExpectThrows([&]() { emptyGraph.AddPass("empty", emptyPass); }, "at least one attachment");

		RenderGraph resolveGraph;
		RenderGraph::ImageInfo image;
		resolveGraph.AddImage("resolved", image);
		RenderGraph::SubpassInfo resolveOnly;
		resolveOnly.SetResolveAttachment(0, "resolved");
		ExpectThrows([&]() { resolveGraph.AddPass("resolve", resolveOnly); }, "no color attachment");

		CHECK_TRUE(RenderGraphTestProbe::ClearOverridesDoNotInvalidateCompileState(), "Clear overrides must not require recompilation!");
	}
}

int main()
{
	try
	{
		TestInternalSampledImageRequiresWriter();
		TestInternalAttachmentLoadRequiresWriter();
		TestInternalStorageImageCanBeFirstWriter();
		TestImageSubresourceBarrierUsesMipRange();
		TestInternalImageReadFromUntouchedMipStillFails();
		TestImageSubresourceCrossQueueSyncDependsOnMipOverlap();
		TestImageSubresourceCrossQueueSyncForSameMip();
		TestGraphicsToComputeImageDependencyBuildsCrossQueueSync();
		TestResolveAndCullBuildActiveQueueSyncPlans();
		TestComputeToGraphicsBufferDependencyBuildsCrossQueueSync();
		TestScheduledResourceBarriersAssignCrossQueuePlansDirectly();
		TestCrossQueueWriteAfterWriteBuildsSync();
		TestPassWithoutExternalUsageIsCulled();
		TestNeverCullPassWithoutExternalUsageIsScheduled();
		TestCullKeepsExternalUsageDependencies();
		TestNonOverlappingInternalBuffersAliasByDefault();
		TestAliasingMaterializationPreservesLogicalInputs();
		TestOverlappingInternalBuffersDoNotAlias();
		TestCrossQueueInternalBuffersDoNotAlias();
		TestResourceAliasingCanBeDisabled();
		TestNonOverlappingInternalImagesAliasWhenDescriptorsMatch();
		TestInternalImagesWithDifferentDescriptorsDoNotAlias();
		TestGraphicsPassIsOpaqueAndPreservesRenderPassScope();
		TestRenderPassInfoIsAHardGroupBoundary();
		TestCompatibleSubpassesMergeAndRepeatedClearSplits();
		TestIncompatibleAttachmentRangesRolesAndResourceAccessSplit();
		TestManagedPlanUsesExactLayersAndPreserveAttachments();
		TestResolvePlanAndInferredImageUsages();
		TestManagedAttachmentDeclarationValidation();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
