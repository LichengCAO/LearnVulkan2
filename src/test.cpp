#include "common.h"
#include "utility/render_graph/render_graph.h"

struct RenderGraphTestProbe
{
	static auto GetScheduledPassNames(const RenderGraph& inGraph) -> std::vector<std::string>
	{
		std::vector<std::string> names;

		for (const RenderGraph::SubmitBatch& submitBatch : inGraph.m_submitBatches)
		{
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
		summary.submitBatchCount = static_cast<uint32_t>(inGraph.m_submitBatches.size());

		for (const RenderGraph::SubmitBatch& submitBatch : inGraph.m_submitBatches)
		{
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
		RenderGraph::BuildContext context;
		inGraph._LinkPasses(context);
		inGraph._CullPasses(context);
		CHECK_TRUE(context.queueSyncPlans.empty(), "Link and cull should not resolve queue sync plans yet!");
		inGraph._ResolveDependency(context);
		return static_cast<uint32_t>(context.queueSyncPlans.size());
	}

	static auto SummarizeScheduledResourceBarriers(RenderGraph& inGraph) -> CrossQueueBuildSummary
	{
		RenderGraph::BuildContext context;
		inGraph._LinkPasses(context);
		inGraph._CullPasses(context);
		inGraph._ResolveDependency(context);
		inGraph._BuildScheduleAndBatches(context);
		inGraph._BuildResourceAliases(context);
		inGraph._BuildScheduledResourceBarriers(context);
		return SummarizeCrossQueueBuild(inGraph);
	}

	static auto GetBufferAliasRoot(const RenderGraph& inGraph, const std::string& inName) -> RenderGraph::BufferIndex
	{
		const RenderGraph::BufferIndex index = inGraph._GetBufferIndex(inName);
		CHECK_TRUE(index < inGraph.m_bufferAliasRoots.size(), "Render graph buffer alias root is missing!");
		return inGraph.m_bufferAliasRoots[index];
	}

	static auto GetImageAliasRoot(const RenderGraph& inGraph, const std::string& inName) -> RenderGraph::ImageIndex
	{
		const RenderGraph::ImageIndex index = inGraph._GetImageIndex(inName);
		CHECK_TRUE(index < inGraph.m_imageAliasRoots.size(), "Render graph image alias root is missing!");
		return inGraph.m_imageAliasRoots[index];
	}

	static auto CountAliasedBufferTransitions(const RenderGraph& inGraph) -> uint32_t
	{
		uint32_t count = 0;
		for (const RenderGraph::SubmitBatch& submitBatch : inGraph.m_submitBatches)
		{
			auto funcCountGroup = [&](const std::vector<RenderGraph::SubmitBatch::PassGroupPlan>& inGroups)
			{
				for (const RenderGraph::SubmitBatch::PassGroupPlan& group : inGroups)
				{
					for (const RenderGraph::BarrierPlan& plan : group.prologueBarriers)
					{
						if (plan.resourceType == RenderGraph::ResourceType::BUFFER &&
							plan.sourceBuffer != RenderGraph::INVALID_INDEX &&
							plan.sourceBuffer != plan.buffer)
						{
							++count;
						}
					}
				}
			};

			funcCountGroup(submitBatch.graphicsGroups);
			funcCountGroup(submitBatch.computeGroups);
		}
		return count;
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
		subpass.AddColorAttachment(
			"color",
			VK_ATTACHMENT_LOAD_OP_LOAD,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ATTACHMENT_STORE_OP_STORE,
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
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
			RenderGraphTestProbe::GetBufferAliasRoot(graph, "scratch_a") ==
			RenderGraphTestProbe::GetBufferAliasRoot(graph, "scratch_b"),
			"Non-overlapping compatible internal buffers should alias by default!");
		CHECK_TRUE(
			RenderGraphTestProbe::CountAliasedBufferTransitions(graph) > 0,
			"Aliased buffers should emit a transition from the previous logical resource state!");
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
			RenderGraphTestProbe::GetBufferAliasRoot(graph, "scratch_a") !=
			RenderGraphTestProbe::GetBufferAliasRoot(graph, "scratch_b"),
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
			RenderGraphTestProbe::GetBufferAliasRoot(graph, "scratch_compute") !=
			RenderGraphTestProbe::GetBufferAliasRoot(graph, "scratch_graphics"),
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
			RenderGraphTestProbe::GetBufferAliasRoot(graph, "scratch_a") !=
			RenderGraphTestProbe::GetBufferAliasRoot(graph, "scratch_b"),
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
			RenderGraphTestProbe::GetImageAliasRoot(graph, "scratch_a") ==
			RenderGraphTestProbe::GetImageAliasRoot(graph, "scratch_b"),
			"Non-overlapping compatible internal images should alias!");
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
			RenderGraphTestProbe::GetImageAliasRoot(graph, "scratch_a") !=
			RenderGraphTestProbe::GetImageAliasRoot(graph, "scratch_b"),
			"Internal images with different descriptors must not alias!");
	}
}

int main()
{
	try
	{
		TestInternalSampledImageRequiresWriter();
		TestInternalAttachmentLoadRequiresWriter();
		TestInternalStorageImageCanBeFirstWriter();
		TestGraphicsToComputeImageDependencyBuildsCrossQueueSync();
		TestResolveAndCullBuildActiveQueueSyncPlans();
		TestComputeToGraphicsBufferDependencyBuildsCrossQueueSync();
		TestScheduledResourceBarriersAssignCrossQueuePlansDirectly();
		TestCrossQueueWriteAfterWriteBuildsSync();
		TestPassWithoutExternalUsageIsCulled();
		TestNeverCullPassWithoutExternalUsageIsScheduled();
		TestCullKeepsExternalUsageDependencies();
		TestNonOverlappingInternalBuffersAliasByDefault();
		TestOverlappingInternalBuffersDoNotAlias();
		TestCrossQueueInternalBuffersDoNotAlias();
		TestResourceAliasingCanBeDisabled();
		TestNonOverlappingInternalImagesAliasWhenDescriptorsMatch();
		TestInternalImagesWithDifferentDescriptorsDoNotAlias();
	}
	catch (const std::exception& e)
	{
		std::cerr << e.what() << std::endl;
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
