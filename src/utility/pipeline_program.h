#pragma once
#include "shader_reflect.h"
#include "pipeline_io.h"
#include "pipeline.h"
#include <functional>
#include "render_object/camera.h"

class DescriptorBindRecord;
class GraphicsProgram;
class ComputeProgram;
class RayTracingProgram;
class CommandSubmission;

// builds descriptor set layouts and descriptor sets
class DescriptorSetManager
{
public:
	enum class DESCRIPTOR_BIND_SETTING {
		CONSTANT_DESCRIPTOR_SET_ACROSS_FRAMES,	// 1 descriptor set for all frames
		CONSTANT_DESCRIPTOR_SET_PER_FRAME,		// 1 descriptor set for each frame
		DEDICATE_DESCRIPTOR_SET_PER_FRAME,		// 1 descriptor set for each bind each frame
		DEDICATE_DESCRIPTOR_SET_ACROSS_FRAMES,	// 1 descriptor set for each bind for all frames
	};

private:
	struct DescriptorSetData
	{
		std::unique_ptr<DescriptorSet> uptrDescriptorSet;
		std::map<uint32_t, DescriptorBindRecord> bindInfo; // to record binding states of this descriptor set
		~DescriptorSetData();
	};
	struct BindPlan
	{
		DESCRIPTOR_BIND_SETTING bindType;
		uint32_t set;
		uint32_t location;
		std::vector<VkDescriptorImageInfo>		imageBind;
		std::vector<VkBufferView>				viewBind;
		std::vector<VkDescriptorBufferInfo>		bufferBind;
		std::vector<VkAccelerationStructureKHR> accelStructBind;
	};
	struct DynamicOffsetRecord
	{
		uint32_t set = 0;
		uint32_t binding = 0;
		std::vector<uint32_t> offsets;

		bool operator<(const DynamicOffsetRecord& _other) const
		{
			if (set != _other.set)
			{
				return set < _other.set;
			}
			return binding < _other.binding;
		}
	};

private:
	// get by reflection:
	std::unordered_map<std::string, std::pair<uint32_t, uint32_t>>	m_mapNameToSetBinding;		// name in shader -> set, binding
	std::vector<std::map<uint32_t, VkDescriptorSetLayoutBinding>>	m_vkDescriptorSetBindingInfo; // m_vkDescriptorSetBindingInfo[set]

	// device objects:
	std::vector<std::unique_ptr<DescriptorSetLayout>>							m_vecUptrDescriptorSetLayout;// m_vecUptrDescriptorSetLayout[set]
	std::vector<std::unique_ptr<DescriptorSetData>>								m_uptrDescriptorSetSetting0; // 1 descriptor set for all frames
	std::vector<std::vector<std::unique_ptr<DescriptorSetData>>>				m_uptrDescriptorSetSetting1; // 1 descriptor set for each frame
	std::vector<std::vector<std::vector<std::unique_ptr<DescriptorSetData>>>>	m_uptrDescriptorSetSetting2; // 1 descriptor set for each bind each frame
	std::vector<std::vector<std::unique_ptr<DescriptorSetData>>>				m_uptrDescriptorSetSetting3; // 1 descriptor set for each bind for all frames

	std::vector<BindPlan> m_currentPlan; // done after endbind, cleared at begin bind
	
	std::vector<DescriptorSet*> m_pCurrentDescriptorSets; // valid descriptor set in current bound, cleared at begin bind
	std::vector<DynamicOffsetRecord> m_vecDynamicOffset;

	uint32_t m_uFrameInFlightCount = 1;
	uint32_t m_uCurrentFrame = 0;

private:
	DescriptorSetManager() {};
	DescriptorSetManager(const DescriptorSetManager& _other) = delete;

	// given descriptor name, get the set and binding of the descriptor
	// return true if the output is valid, return false if we cannot map the name
	bool _GetDescriptorLocation(const std::string& _name, uint32_t& _set, uint32_t& _binding) const;

	// create descriptor set layouts, init them this will be only done once
	void _InitDescriptorSetLayouts();

	// destroy descriptor set layout and descriptor sets, uninit them
	void _UninitDescriptorSetLayouts();

	// descriptor set cannot be bound till we have descriptor set inited,
	// I postpone the binding since we cannot build descriptor set till we know
	// all binding types which will not be known till the first call of EndBind()
	void _PlanBinding(const BindPlan& _bindPlan);

	// in this function, I create descriptor sets based on descriptor layouts and
	// descriptor set allocate strategies, which suggest us to cache or update descriptor sets
	// when necessary
	void _ProcessPlan();

public:
	DescriptorSetManager(DescriptorSetManager& other) = delete;
	
	~DescriptorSetManager();

	void Init(const std::vector<std::string>& _pipelineShaders, uint32_t _uFrameInFlightCount);

	void EndFrame();

	void StartBind();

	// bind data to descriptor, if _bForce is true then vkUpdateDescriptorSets will be called, 
	// otherwise vkUpdateDescriptorSets will only be called the first time for each frame in flight
	void BindDescriptor(
		const std::string& _nameInShader,
		const std::vector<VkDescriptorBufferInfo>& _data,
		DESCRIPTOR_BIND_SETTING _setting = DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME);

	void BindDescriptor(
		uint32_t _set, uint32_t _binding,
		const std::vector<VkDescriptorBufferInfo>& _data,
		DESCRIPTOR_BIND_SETTING _setting = DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME);

	// bind data to a dynamic descriptor, if _bForce is true then vkUpdateDescriptorSets will be called, 
	// otherwise vkUpdateDescriptorSets will only be called the first time for each frame in flight,
	// size of _dynmaicOffsets must equals size of _data
	// https://github.com/KhronosGroup/Vulkan-Samples/blob/main/samples/performance/descriptor_management/README.adoc
	void BindDescriptor(
		const std::string& _nameInShader,
		const std::vector<VkDescriptorBufferInfo>& _data,
		const std::vector<uint32_t>& _dynmaicOffsets,
		DESCRIPTOR_BIND_SETTING _setting = DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME);

	void BindDescriptor(
		uint32_t _set, 
		uint32_t _binding,
		const std::vector<VkDescriptorBufferInfo>& _data,
		const std::vector<uint32_t>& _dynmaicOffsets,
		DESCRIPTOR_BIND_SETTING _setting = DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME);

	// bind data to descriptor, if _bForce is true then vkUpdateDescriptorSets will be called, 
	// otherwise vkUpdateDescriptorSets will only be called the first time for each frame in flight
	void BindDescriptor(
		const std::string& _nameInShader,
		const std::vector<VkDescriptorImageInfo>& _data,
		DESCRIPTOR_BIND_SETTING _setting = DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME);

	void BindDescriptor(
		uint32_t _set, 
		uint32_t _binding,
		const std::vector<VkDescriptorImageInfo>& _data,
		DESCRIPTOR_BIND_SETTING _setting = DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME);

	void BindDescriptor(
		const std::string& _nameInShader,
		const std::vector<VkBufferView>& _data,
		DESCRIPTOR_BIND_SETTING _setting = DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME);

	void BindDescriptor(
		uint32_t _set, 
		uint32_t _binding,
		const std::vector<VkBufferView>& _data,
		DESCRIPTOR_BIND_SETTING _setting = DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME);

	void BindDescriptor(
		const std::string& _nameInShader,
		const std::vector<VkAccelerationStructureKHR>& _data,
		DESCRIPTOR_BIND_SETTING _setting = DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME);

	void BindDescriptor(
		uint32_t _set, 
		uint32_t _binding,
		const std::vector<VkAccelerationStructureKHR>& _data,
		DESCRIPTOR_BIND_SETTING _setting = DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME);

	void EndBind();

	// only be valid after the first call of EndBind(), the layout will be filled ordered
	void GetCurrentDescriptorSets(std::vector<VkDescriptorSet>& _outDescriptorSets, std::vector<uint32_t>& _outDynamicOffsets);

	// only be valid after the first call of EndBind(), the layout will be filled ordered
	void GetDescriptorSetLayouts(std::vector<VkDescriptorSetLayout>& _outDescriptorSetLayouts);

	// clear all stored descriptor sets
	void ClearDescriptorSets();

	void Uninit();

	friend class GraphicsProgram;
	friend class ComputeProgram;
	friend class RayTracingProgram;
};

class GraphicsProgram
{
private:
	std::unique_ptr<DescriptorSetManager> m_uptrDescriptorSetManager;
	std::unique_ptr<GraphicsPipeline> m_uptrPipeline;
	std::vector<std::string> m_vecShaderPath;
	ShaderReflector	m_shaderReflector;

	VkBuffer m_indexBuffer = VK_NULL_HANDLE;
	std::vector<VkBuffer> m_vertexBuffers;
	VkIndexType		m_vkIndexType = VK_INDEX_TYPE_UINT32;

	std::vector<std::pair<VkShaderStageFlags, const void*>> m_pushConstants;
	
	// for vertex inputs
	std::vector<std::function<void(GraphicsPipeline*)>> m_lateInitialization;
	std::unordered_map<uint32_t, VkFormat>				m_mapVertexFormat;

	// binded framebuffer
	const Framebuffer* m_pFramebuffer = nullptr;

private:
	void _InitPipeline();

	void _UninitPipeline();

public:
	void PresetRenderPass(const RenderPass* _pRenderPass, uint32_t _subpass);

	void Init(const std::vector<std::string>& _shaderPaths, uint32_t _frameInFlight);

	void EndFrame();

	DescriptorSetManager& GetDescriptorSetManager();

	void BindVertexBuffer(
		VkBuffer _vertexBuffer,
		uint32_t _vertexStride,
		const std::unordered_map<uint32_t, uint32_t>& _mapLocationOffset);

	void BindIndexBuffer(
		VkBuffer _indexBuffer,
		VkIndexType _indexType = VK_INDEX_TYPE_UINT32);

	void PushConstant(VkShaderStageFlags _stages, const void* _data);

	void BindFramebuffer(
		CommandSubmission* _pCmd, 
		const Framebuffer* _pFramebuffer);

	void Draw(
		CommandSubmission* _pCmd, 
		uint32_t _vertexCount);

	void DrawIndexed(
		CommandSubmission* _pCmd,
		uint32_t _indexCount);

	void DispatchWorkGroup(
		CommandSubmission* _pCmd,
		uint32_t _groupCountX, 
		uint32_t _groupCountY, 
		uint32_t _groupCountZ);

	// always call it before command submission if you call BindFramebuffer upfront 
	void UnbindFramebuffer(CommandSubmission* _pCmd);

	void Uninit();
};

class ComputeProgram
{
private:
	std::unique_ptr<DescriptorSetManager> m_uptrDescriptorSetManager;
	std::unique_ptr<ComputePipeline> m_uptrPipeline;
	std::vector<std::string> m_vecShaderPath;
	ShaderReflector	m_shaderReflector;
	std::vector<std::pair<VkShaderStageFlags, const void*>> m_pushConstants;

private:
	void _InitPipeline();

	void _UninitPipeline();

public:
	void Init(const std::vector<std::string>& _shaderPaths, uint32_t _frameInFlight);

	void EndFrame();

	DescriptorSetManager& GetDescriptorSetManager();

	void PushConstant(VkShaderStageFlags _stages, const void* _data);

	void DispatchWorkGroup(
		CommandSubmission* _pCmd,
		uint32_t _groupCountX,
		uint32_t _groupCountY,
		uint32_t _groupCountZ);

	void Uninit();
};

class RayTracingProgram
{
private:
	ShaderReflector	m_shaderReflector;
	std::unique_ptr<DescriptorSetManager> m_uptrDescriptorSetManager;
	std::unique_ptr<RayTracingPipeline> m_uptrPipeline;
	std::vector<std::string> m_vecShaderPath;
	std::vector<std::pair<VkShaderStageFlags, const void*>> m_pushConstants;
	std::unordered_map<std::string, uint32_t> m_mapShaderToIndex;
	std::vector<std::function<void(RayTracingPipeline*)>> m_lateInitialization;

private:
	void _InitPipeline();
	void _UninitPipeline();

public:
	void PreAddRayGenerationShader(const std::string& _path);
	void PreAddTriangleHitShaders(const std::string& _closestHit, const std::optional<std::string>& _anyHit);
	void PreAddProceduralHitShaders(const std::string& _closestHit, const std::string& _intersection, const std::optional<std::string>& _anyHit);
	void PreAddMissShader(const std::string& _miss);
	void PresetMaxRecursion(uint32_t _maxRecur);

	void Init(uint32_t _frameInFlight);

	void EndFrame();

	DescriptorSetManager& GetDescriptorSetManager();

	void PushConstant(VkShaderStageFlags _stages, const void* _data);

	void TraceRay(
		CommandSubmission* _pCmd, 
		uint32_t _uWidth, 
		uint32_t _uHeight);

	void Uninit();
};

// helper class that gives you a user controlled camera
class CameraComponent
{
private:
	std::optional<float> m_lastX;
	std::optional<float> m_lastY;
	double	m_lastTime = -1.0f;
	double	m_currentTime = 0.0;
	float		m_sensitivity = 0.3f;
public:
	PersCamera camera;

public:
	CameraComponent(unsigned int w, unsigned int h, const glm::vec3& e, const glm::vec3& r, const glm::vec3& worldUp) 
		:camera(w, h, e, r, worldUp)
	{};
	
	// update camera based on user input, call it every frame
	void UpdateCamera(float _moveSpeed);
};