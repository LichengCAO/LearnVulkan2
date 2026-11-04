#pragma once
#include "pipeline_layout_create_info.h"
#include "render_pass_create_info.h"
#include "private/shader_module_create_info.h"

class GraphicsShaderProgramCreateInfo final
{
    friend class GraphicsShaderProgram;
    friend class CommandBuffer;
private:
    struct ColorAttachment { VkFormat format; VkImageLayout finalLayout; };
    struct FinalizedData
    {
        ShaderDetail::FinalizedShaders shaders;
        PipelineLayoutCreateInfo layout;
        std::vector<VkVertexInputBindingDescription> vertexBindings;
        std::vector<VkVertexInputAttributeDescription> vertexAttributes;
        bool automaticVertexInputSupported = true;
        std::shared_ptr<const RenderPassCreateInfo> defaultRenderPass;
    };
    std::vector<ShaderDetail::Request> m_shaders;
    std::map<uint32_t, ColorAttachment> m_defaultColors;
    std::optional<VkFormat> m_defaultDepth;
    std::shared_ptr<const FinalizedData> m_finalized;
    auto _AddShader(std::string_view inFile, VkShaderStageFlagBits inStage) -> GraphicsShaderProgramCreateInfo&;
public:
    auto Reset() -> GraphicsShaderProgramCreateInfo&;
    auto AddVertexShader(std::string_view inSpirv) -> GraphicsShaderProgramCreateInfo&;
    auto AddFragmentShader(std::string_view inSpirv) -> GraphicsShaderProgramCreateInfo&;
    auto AddGeometryShader(std::string_view inSpirv) -> GraphicsShaderProgramCreateInfo&;
    auto AddTaskShader(std::string_view inSpirv) -> GraphicsShaderProgramCreateInfo&;
    auto AddMeshShader(std::string_view inSpirv) -> GraphicsShaderProgramCreateInfo&;
    auto SetDefaultColorAttachment(uint32_t inLocation, VkFormat inFormat,
        VkImageLayout inFinalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) -> GraphicsShaderProgramCreateInfo&;
    auto SetDefaultDepthAttachment(VkFormat inFormat) -> GraphicsShaderProgramCreateInfo&;
    auto Finalize() -> GraphicsShaderProgramCreateInfo&;
    auto IsFinalized() const noexcept -> bool { return bool(m_finalized); }
};

class ComputeShaderProgramCreateInfo final
{
    friend class ComputeShaderProgram;
private:
    struct FinalizedData
    {
        ShaderModuleCreateInfo shader;
        PipelineLayoutCreateInfo layout;
    };
    std::string m_spirvFile;
    VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;
    std::shared_ptr<const FinalizedData> m_finalized;
public:
    auto Reset() -> ComputeShaderProgramCreateInfo&;
    auto SetShader(std::string_view inSpirv) -> ComputeShaderProgramCreateInfo&;
    auto CustomizePipelineCache(VkPipelineCache inCache) -> ComputeShaderProgramCreateInfo&;
    auto Finalize() -> ComputeShaderProgramCreateInfo&;
    auto IsFinalized() const noexcept -> bool { return bool(m_finalized); }
};

enum class ShaderTableType : uint8_t { RayGeneration = 0, Miss = 1, Hit = 2, Callable = 3, Max = 4 };

class RayTracingShaderProgramCreateInfo final
{
    friend class RayTracingShaderProgram;
private:
    struct ShaderGroup
    {
        std::string name;
        ShaderTableType type;
        VkRayTracingShaderGroupCreateInfoKHR record;
    };
    struct FinalizedData
    {
        ShaderDetail::FinalizedShaders shaders;
        PipelineLayoutCreateInfo layout;
        std::vector<ShaderGroup> groups;
        uint32_t recursionDepth;
    };
    std::vector<ShaderDetail::Request> m_shaders;
    std::vector<ShaderGroup> m_shaderGroups;
    uint32_t m_maxRayRecursionDepth = 1;
    VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;
    std::shared_ptr<const FinalizedData> m_finalized;
    void _ValidateGroupName(std::string_view inName) const;
    auto _RegisterShader(const ShaderDetail::Request& inShader) -> uint32_t;
    auto _AddGeneralShaderGroup(std::string_view inName, const ShaderDetail::Request& inShader,
        ShaderTableType inType) -> RayTracingShaderProgramCreateInfo&;
    auto _AddHitShaderGroup(std::string_view inName, const std::optional<ShaderDetail::Request>& inClosestHit,
        const ShaderDetail::Request* inIntersection, const std::optional<ShaderDetail::Request>& inAnyHit) -> RayTracingShaderProgramCreateInfo&;
public:
    auto Reset() -> RayTracingShaderProgramCreateInfo&;
    auto CustomizeMaxRecursionDepth(uint32_t inMaxDepth) -> RayTracingShaderProgramCreateInfo&;
    auto CustomizePipelineCache(VkPipelineCache inCache) -> RayTracingShaderProgramCreateInfo&;
    auto AddRayGenerationShaderGroup(std::string_view inName, std::string_view inSpirv) -> RayTracingShaderProgramCreateInfo&;
    auto AddMissShaderGroup(std::string_view inName, std::string_view inSpirv) -> RayTracingShaderProgramCreateInfo&;
    auto AddCallableShaderGroup(std::string_view inName, std::string_view inSpirv) -> RayTracingShaderProgramCreateInfo&;
    auto AddTriangleHitShaderGroup(std::string_view inName, std::optional<std::string_view> inClosestHitSpirv = std::nullopt,
        std::optional<std::string_view> inAnyHitSpirv = std::nullopt) -> RayTracingShaderProgramCreateInfo&;
    auto AddProceduralHitShaderGroup(std::string_view inName, std::optional<std::string_view> inClosestHitSpirv,
        std::string_view inIntersectionSpirv, std::optional<std::string_view> inAnyHitSpirv = std::nullopt) -> RayTracingShaderProgramCreateInfo&;
    auto Finalize() -> RayTracingShaderProgramCreateInfo&;
    auto IsFinalized() const noexcept -> bool { return bool(m_finalized); }
};
