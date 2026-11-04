#if __has_include("my_vulkan/pipeline/render_pass_create_info.h")
#include "my_vulkan/pipeline/render_pass_create_info.h"
#else
#include "my_vulkan/pipeline/render_pass.h"
#endif
#include <type_traits>
#include <utility>

namespace
{
template<typename Function>
void ExpectFinalizeFailure(Function&& function)
{
    bool failed = false;
    try { function(); } catch (const std::runtime_error&) { failed = true; }
    CHECK_TRUE(failed, "Invalid render pass metadata must fail during Finalize!");
}

auto ColorAttachment(VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT) -> AttachmentDescription
{
    AttachmentDescription attachment;
    VkClearValue clear{};
    clear.color.float32[0] = 0.25f;
    attachment.CustomizeFormat(VK_FORMAT_R8G8B8A8_UNORM, clear);
    attachment.CustomizeSampleCount(samples);
    return attachment;
}

template<typename Info>
void TestFinalizeContract()
{
    if constexpr (requires(Info& info, const Info& constant) { info.Finalize(); constant.IsFinalized(); })
    {
        static_assert(std::is_same_v<decltype(std::declval<Info&>().Finalize()), Info&>);
        static_assert(noexcept(std::declval<const Info&>().IsFinalized()));
        static_assert(!std::is_copy_constructible_v<Info> && !std::is_move_constructible_v<Info>);
        Info info;
        CHECK_TRUE(!info.IsFinalized(), "New render pass metadata must be unfinalized!");
        ExpectFinalizeFailure([&] { info.Finalize(); });
        CHECK_TRUE(!info.IsFinalized(), "Failed finalization must not publish metadata!");
        info.AddAttachment("color", ColorAttachment());
        SubpassDescription subpass;
        subpass.AddColorAttachment(0, "color");
        info.AddSubpass("main", subpass);
        CHECK_TRUE(&info.Finalize() == &info && info.IsFinalized(), "Finalize must publish metadata and return its builder!");
        CHECK_TRUE(&info.Finalize() == &info && info.IsFinalized(), "Repeated Finalize must preserve the finalized state!");
        ExpectFinalizeFailure([&] { info.AddAttachment("color", ColorAttachment()); });
        CHECK_TRUE(info.IsFinalized(), "Rejected mutations must preserve finalized metadata!");
        info.AddAttachment("extra", ColorAttachment());
        CHECK_TRUE(!info.IsFinalized(), "Adding an attachment must invalidate finalized metadata!");
        info.Finalize();
        info.AddSubpass("second", subpass);
        CHECK_TRUE(!info.IsFinalized(), "Adding a subpass must invalidate finalized metadata!");
        info.Finalize();

        const auto rejectSubpass = [](const SubpassDescription& invalid)
        {
            Info invalidInfo;
            invalidInfo.AddAttachment("color", ColorAttachment());
            invalidInfo.AddSubpass("main", invalid);
            ExpectFinalizeFailure([&] { invalidInfo.Finalize(); });
            CHECK_TRUE(!invalidInfo.IsFinalized(), "Invalid metadata must remain unfinalized!");
        };
        SubpassDescription missingColor;
        missingColor.AddColorAttachment(0, "missing");
        rejectSubpass(missingColor);
        info.AddSubpass("invalid", missingColor);
        ExpectFinalizeFailure([&] { info.Finalize(); });
        CHECK_TRUE(!info.IsFinalized(), "A failed rebuild must never expose its previous finalized metadata!");
        SubpassDescription missingDepth;
        missingDepth.AddDepthStencilAttachment("missing");
        rejectSubpass(missingDepth);
        SubpassDescription missingPreserve;
        missingPreserve.AddPreserveAttachment("missing");
        rejectSubpass(missingPreserve);
        SubpassDescription missingDependency;
        missingDependency.AddDependencyOnSubpass("missing", VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, VK_ACCESS_SHADER_READ_BIT);
        rejectSubpass(missingDependency);
        SubpassDescription missingResolve;
        missingResolve.AddResolvedAttachment(0, "color", "missing");
        rejectSubpass(missingResolve);

        Info mismatched;
        mismatched.AddAttachment("single", ColorAttachment());
        mismatched.AddAttachment("multi", ColorAttachment(VK_SAMPLE_COUNT_4_BIT));
        SubpassDescription differentSamples;
        differentSamples.AddColorAttachment(0, "single");
        differentSamples.AddColorAttachment(1, "multi");
        mismatched.AddSubpass("main", differentSamples);
        ExpectFinalizeFailure([&] { mismatched.Finalize(); });
        CHECK_TRUE(!mismatched.IsFinalized(), "Sample mismatches must fail before metadata publication!");

        Info invalidSamples;
        invalidSamples.AddAttachment("invalid", ColorAttachment(static_cast<VkSampleCountFlagBits>(0)));
        SubpassDescription invalidSampleSubpass;
        invalidSampleSubpass.AddColorAttachment(0, "invalid");
        invalidSamples.AddSubpass("main", invalidSampleSubpass);
        ExpectFinalizeFailure([&] { invalidSamples.Finalize(); });

        Info invalidResolve;
        invalidResolve.AddAttachment("first", ColorAttachment());
        invalidResolve.AddAttachment("second", ColorAttachment());
        SubpassDescription singleSampleResolve;
        singleSampleResolve.AddResolvedAttachment(0, "first", "second");
        invalidResolve.AddSubpass("main", singleSampleResolve);
        ExpectFinalizeFailure([&] { invalidResolve.Finalize(); });

        Info resolved;
        resolved.AddAttachment("multi", ColorAttachment(VK_SAMPLE_COUNT_4_BIT));
        resolved.AddAttachment("single", ColorAttachment());
        SubpassDescription resolve;
        resolve.AddResolvedAttachment(2, "multi", "single");
        resolve.AllowLocalPipelineBarrier();
        resolved.AddSubpass("main", resolve);
        resolved.Finalize();
        CHECK_TRUE(resolved.IsFinalized(), "Sparse color slots and multisample resolves must finalize without a device!");
    }
    else
    {
        CHECK_TRUE(false, "RenderPassCreateInfo requires explicit Finalize and IsFinalized!");
    }
}
}

void RunRenderPassFinalizeTests()
{
    TestFinalizeContract<RenderPassCreateInfo>();
}
