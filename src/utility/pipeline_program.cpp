#include "pipeline_program.h"
#include "commandbuffer.h"
#include "my_vulkan/shader.h"
#include "device.h"
#include <algorithm>

namespace
{

	bool operator==(const VkDescriptorImageInfo& lhs, const VkDescriptorImageInfo& rhs) { return lhs.imageView == rhs.imageView; };
	bool operator==(const VkDescriptorBufferInfo& lhs, const VkDescriptorBufferInfo& rhs) { return lhs.buffer == rhs.buffer; };

}

static class DescriptorBindRecord
{
#define DESCRIPTOR_VARIANT(_T)	private:\
									std::optional<_T> m_opt##_T;\
								public:\
									void Bind##_T(const std::vector<_T>& _input){ m_opt##_T = _input[0]; isBound = true; };\
									bool IsBindTo##_T(const std::vector<_T>& _input) const { return m_opt##_T.has_value() && m_opt##_T.value() == _input[0]; };

private:
	bool isBound = false;
	DESCRIPTOR_VARIANT(VkDescriptorImageInfo);
	DESCRIPTOR_VARIANT(VkBufferView);
	DESCRIPTOR_VARIANT(VkDescriptorBufferInfo);
	DESCRIPTOR_VARIANT(VkAccelerationStructureKHR);

public:
	// return true if the descriptor is bound with something
	bool IsBound() const { return isBound; };

#undef DESCRIPTOR_VARIANT
};

bool DescriptorSetManager::_GetDescriptorLocation(const std::string& _name, uint32_t& _set, uint32_t& _binding) const
{
	const auto itr = m_mapNameToSetBinding.find(_name);
	bool bResult = itr != m_mapNameToSetBinding.end();

	if (bResult)
	{
		_set = (*itr).second.first;
		_binding = (*itr).second.second;
	}

	return bResult;
}

void DescriptorSetManager::_InitDescriptorSetLayouts()
{
	for (size_t setId = 0; setId < m_vkDescriptorSetBindingInfo.size(); ++setId)
	{
		const auto& bindings = m_vkDescriptorSetBindingInfo[setId];
		std::unique_ptr<DescriptorSetLayout> uptrLayout = std::make_unique<DescriptorSetLayout>();

		for (const auto& binding : bindings)
		{
			uint32_t bindingId = binding.first;
			const VkDescriptorSetLayoutBinding& vkBinding = binding.second;

			uptrLayout->PreAddBinding(
				vkBinding.binding,
				vkBinding.descriptorCount,
				vkBinding.descriptorType,
				vkBinding.stageFlags,
				vkBinding.pImmutableSamplers);
		}

		uptrLayout->Init();

		m_vecUptrDescriptorSetLayout.push_back(std::move(uptrLayout));
	}
}

void DescriptorSetManager::_UninitDescriptorSetLayouts()
{
	for (auto& uptrLayout : m_vecUptrDescriptorSetLayout)
	{
		uptrLayout->Uninit();
	}
	m_vecUptrDescriptorSetLayout.clear();
}

void DescriptorSetManager::_PlanBinding(const BindPlan& _bindPlan)
{
	m_currentPlan.emplace_back(_bindPlan);
}

void DescriptorSetManager::_ProcessPlan()
{
	std::vector<DESCRIPTOR_BIND_SETTING> descriptorSetTypes;
	std::vector<DescriptorSetData*> pDescriptorSetDatas;
	size_t setCount = m_vkDescriptorSetBindingInfo.size();
	static bool bMapInited = false;
	static std::map<DESCRIPTOR_BIND_SETTING, std::map<DESCRIPTOR_BIND_SETTING, DESCRIPTOR_BIND_SETTING>> mapType;
	if (!bMapInited)
	{
		auto key = DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_ACROSS_FRAMES;
		mapType[key][DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_ACROSS_FRAMES] = 
			DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_ACROSS_FRAMES;
		mapType[key][DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_ACROSS_FRAMES] = 
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_ACROSS_FRAMES;
		mapType[key][DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME] = 
			DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME;
		mapType[key][DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME] = 
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME;
		
		key = DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_ACROSS_FRAMES;
		mapType[key][DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_ACROSS_FRAMES] =
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_ACROSS_FRAMES;
		mapType[key][DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_ACROSS_FRAMES] =
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_ACROSS_FRAMES;
		mapType[key][DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME] =
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME;
		mapType[key][DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME] =
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME;

		key = DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME;
		mapType[key][DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_ACROSS_FRAMES] =
			DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME;
		mapType[key][DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_ACROSS_FRAMES] =
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME;
		mapType[key][DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME] =
			DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME;
		mapType[key][DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME] =
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME;

		key = DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME;
		mapType[key][DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_ACROSS_FRAMES] =
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME;
		mapType[key][DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_ACROSS_FRAMES] =
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME;
		mapType[key][DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME] =
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME;
		mapType[key][DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME] =
			DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME;

		bMapInited = true;
	}

	// find out descriptor set allocate stratigies
	descriptorSetTypes.resize(setCount, DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_ACROSS_FRAMES);
	for (const auto& plan : m_currentPlan)
	{
		CHECK_TRUE(mapType.find(plan.bindType) != mapType.end() 
			&& mapType[plan.bindType].find(descriptorSetTypes[plan.set]) != mapType[plan.bindType].end(),
			"Not define for this pair of descriptor set allocations");
		descriptorSetTypes[plan.set] = mapType[plan.bindType][descriptorSetTypes[plan.set]];
	}

	// prepare descriptor sets
	for (size_t i = 0; i < setCount; ++i)
	{
		switch (descriptorSetTypes[i])
		{
		case DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_ACROSS_FRAMES:
		{
			size_t j = m_uptrDescriptorSetSetting0.size();
			while (j <= i )
			{
				std::unique_ptr<DescriptorSetData> setData = std::make_unique<DescriptorSetData>();
				auto pDescriptorSet = m_vecUptrDescriptorSetLayout[j]->NewDescriptorSetPtr();
				pDescriptorSet->Init();
				setData->uptrDescriptorSet.reset(pDescriptorSet);
				m_uptrDescriptorSetSetting0.push_back(std::move(setData));
				++j;
			}
			pDescriptorSetDatas.push_back(m_uptrDescriptorSetSetting0[i].get());
		}
			break;
		case DESCRIPTOR_BIND_SETTING::CONSTANT_DESCRIPTOR_SET_PER_FRAME:
		{
			size_t j = 0;
			m_uptrDescriptorSetSetting1.resize(m_uFrameInFlightCount);
			j = m_uptrDescriptorSetSetting1[m_uCurrentFrame].size();
			while (j <= i)
			{
				std::unique_ptr<DescriptorSetData> setData = std::make_unique<DescriptorSetData>();
				auto pDescriptorSet = m_vecUptrDescriptorSetLayout[j]->NewDescriptorSetPtr();
				pDescriptorSet->Init();
				setData->uptrDescriptorSet.reset(pDescriptorSet);
				m_uptrDescriptorSetSetting1[m_uCurrentFrame].push_back(std::move(setData));
				++j;
			}
			pDescriptorSetDatas.push_back(m_uptrDescriptorSetSetting1[m_uCurrentFrame][i].get());
		}
			break;
		case DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_PER_FRAME:
		{
			std::vector<size_t> planIds;// all plan to bind to this descriptor set
			size_t planId = 0;
			bool   needToCreateDescriptorSet = false;
			DescriptorSetData* pDescriptorSetData = nullptr;

			// find out all bind plan for this set
			for (const auto& plan : m_currentPlan)
			{
				if (plan.set == i)
				{
					planIds.push_back(planId);
				}
				++planId;
			}
			m_uptrDescriptorSetSetting2.resize(m_uFrameInFlightCount);
			m_uptrDescriptorSetSetting2[m_uCurrentFrame].resize(setCount);
			needToCreateDescriptorSet = (needToCreateDescriptorSet || m_uptrDescriptorSetSetting2[m_uCurrentFrame][i].size() == 0);
			
			// find valid descriptor set in current descriptor sets
			if (!needToCreateDescriptorSet)
			{
				for (const auto& uptrDescriptorSetData : m_uptrDescriptorSetSetting2[m_uCurrentFrame][i])
				{
					bool isValid = true;
					for (auto planId : planIds)
					{
						const auto& plan = m_currentPlan[planId];
						const auto& record = uptrDescriptorSetData->bindInfo[plan.location]; // https://helloacm.com/c-access-a-non-existent-key-in-stdmap-or-stdunordered_map/
						if (record.IsBound())
						{
							if (plan.bufferBind.size() > 0)
							{
								isValid = record.IsBindToVkDescriptorBufferInfo(plan.bufferBind);
							}
							else if (plan.imageBind.size() > 0)
							{
								isValid = record.IsBindToVkDescriptorImageInfo(plan.imageBind);
							}
							else if (plan.viewBind.size() > 0)
							{
								isValid = record.IsBindToVkBufferView(plan.viewBind);
							}
							else if (plan.accelStructBind.size() > 0)
							{
								isValid = record.IsBindToVkAccelerationStructureKHR(plan.accelStructBind);
							}
						}
						if (!isValid) break;
					}
					if (isValid)
					{
						pDescriptorSetData = uptrDescriptorSetData.get();
						break;
					}
				}
			}
			needToCreateDescriptorSet = (needToCreateDescriptorSet || pDescriptorSetData == nullptr);

			// create a new descriptor set if current descriptor sets doesn't meet requirements
			if (needToCreateDescriptorSet)
			{
				std::unique_ptr<DescriptorSetData> setData = std::make_unique<DescriptorSetData>();
				DescriptorSet* pDescriptorSet = m_vecUptrDescriptorSetLayout[i]->NewDescriptorSetPtr();
				pDescriptorSet->Init();
				setData->uptrDescriptorSet.reset(pDescriptorSet);
				pDescriptorSetData = setData.get();
				m_uptrDescriptorSetSetting2[m_uCurrentFrame][i].push_back(std::move(setData));
			}
			pDescriptorSetDatas.push_back(pDescriptorSetData);
		}
			break;
		case DESCRIPTOR_BIND_SETTING::DEDICATE_DESCRIPTOR_SET_ACROSS_FRAMES:
		{
			std::vector<size_t> planIds;// all plan to bind to this descriptor set
			size_t planId = 0;
			bool   needToCreateDescriptorSet = false;
			DescriptorSetData* pDescriptorSetData = nullptr;

			m_uptrDescriptorSetSetting3.resize(setCount);

			// find out all bind plan for this set
			for (const auto& plan : m_currentPlan)
			{
				if (plan.set == i)
				{
					planIds.push_back(planId);
				}
				++planId;
			}
			
			needToCreateDescriptorSet = m_uptrDescriptorSetSetting3[i].size() == 0;
			
			// try to find out if current descriptor set is ok for this bind
			if (!needToCreateDescriptorSet)
			{
				for (const auto& uptrDescriptorSetData : m_uptrDescriptorSetSetting3[i])
				{
					bool isValid = true;
					for (auto planId : planIds)
					{
						const auto& plan = m_currentPlan[planId];
						const auto& record = uptrDescriptorSetData->bindInfo[plan.location]; // https://helloacm.com/c-access-a-non-existent-key-in-stdmap-or-stdunordered_map/
						if (record.IsBound())
						{
							if (plan.bufferBind.size() > 0)
							{
								isValid = record.IsBindToVkDescriptorBufferInfo(plan.bufferBind);
							}
							else if (plan.imageBind.size() > 0)
							{
								isValid = record.IsBindToVkDescriptorImageInfo(plan.imageBind);
							}
							else if (plan.viewBind.size() > 0)
							{
								isValid = record.IsBindToVkBufferView(plan.viewBind);
							}
							else if (plan.accelStructBind.size() > 0)
							{
								isValid = record.IsBindToVkAccelerationStructureKHR(plan.accelStructBind);
							}
						}
						if (!isValid) break;
					}
					if (isValid)
					{
						pDescriptorSetData = uptrDescriptorSetData.get();
						break;
					}
				}
			}
			needToCreateDescriptorSet = needToCreateDescriptorSet || pDescriptorSetData == nullptr;
			
			// create a new descriptor set if current descriptor sets doesn't meet requirements
			if (needToCreateDescriptorSet)
			{
				std::unique_ptr<DescriptorSetData> setData = std::make_unique<DescriptorSetData>();
				DescriptorSet* pDescriptorSet = m_vecUptrDescriptorSetLayout[i]->NewDescriptorSetPtr();
				pDescriptorSet->Init();
				setData->uptrDescriptorSet.reset(pDescriptorSet);
				pDescriptorSetData = setData.get();
				m_uptrDescriptorSetSetting3[i].push_back(std::move(setData));
			}
			pDescriptorSetDatas.push_back(pDescriptorSetData);
		}
			break;
		default:
			CHECK_TRUE(false, "No such descriptor set type!");
			break;
		}
	}

	// now, do the binding
	for (auto& pDescriptorSetData : pDescriptorSetDatas)
	{
		pDescriptorSetData->uptrDescriptorSet->StartUpdate();
	}
	for (const auto& plan : m_currentPlan)
	{
		auto pDescriptorSetData = pDescriptorSetDatas[plan.set];
		auto pDescriptorSet = pDescriptorSetData->uptrDescriptorSet.get();
		auto& curBindInfo = pDescriptorSetData->bindInfo[plan.location];
		if (plan.bufferBind.size() > 0)
		{
			if (!curBindInfo.IsBindToVkDescriptorBufferInfo(plan.bufferBind))
			{
				pDescriptorSet->UpdateBinding(plan.location, plan.bufferBind);
				curBindInfo.BindVkDescriptorBufferInfo(plan.bufferBind);
			}
		}
		else if (plan.imageBind.size() > 0)
		{
			if (!curBindInfo.IsBindToVkDescriptorImageInfo(plan.imageBind))
			{
				pDescriptorSet->UpdateBinding(plan.location, plan.imageBind);
				curBindInfo.BindVkDescriptorImageInfo(plan.imageBind);
			}
		}
		else if (plan.viewBind.size() > 0)
		{
			if (!curBindInfo.IsBindToVkBufferView(plan.viewBind))
			{
				pDescriptorSet->UpdateBinding(plan.location, plan.viewBind);
				curBindInfo.BindVkBufferView(plan.viewBind);
			}
		}
		else if (plan.accelStructBind.size() > 0)
		{
			if (!curBindInfo.IsBindToVkAccelerationStructureKHR(plan.accelStructBind))
			{
				pDescriptorSet->UpdateBinding(plan.location, plan.accelStructBind);
				curBindInfo.BindVkAccelerationStructureKHR(plan.accelStructBind);
			}
		}
		else
		{
			assert(false);
		}
	}
	for (auto& pDescriptorSetData : pDescriptorSetDatas)
	{
		pDescriptorSetData->uptrDescriptorSet->FinishUpdate();
		m_pCurrentDescriptorSets.push_back(pDescriptorSetData->uptrDescriptorSet.get());
	}
	m_currentPlan.clear();
}

DescriptorSetManager::~DescriptorSetManager()
{
}

void DescriptorSetManager::Init(const std::vector<std::string>& _pipelineShaders, uint32_t _uFrameInFlightCount)
{
	ShaderReflector reflector{};

	reflector.Init(_pipelineShaders);
	reflector.ReflectDescriptorSets(m_mapNameToSetBinding, m_vkDescriptorSetBindingInfo);
	reflector.Uninit();

	m_uFrameInFlightCount = _uFrameInFlightCount;
	m_uCurrentFrame = 0u;
}

void DescriptorSetManager::StartBind()
{
	m_vecDynamicOffset.clear();
	m_pCurrentDescriptorSets.clear();
}

void DescriptorSetManager::EndFrame()
{
	m_uCurrentFrame = (m_uCurrentFrame + 1) % m_uFrameInFlightCount;
}

void DescriptorSetManager::BindDescriptor(
	const std::string& _nameInShader,
	const std::vector<VkDescriptorBufferInfo>& _data,
	DESCRIPTOR_BIND_SETTING _setting)
{
	uint32_t uSet = ~0;
	uint32_t uBinding = ~0;

	CHECK_TRUE(_GetDescriptorLocation(_nameInShader, uSet, uBinding), "Cannot find binding!");

	BindDescriptor(uSet, uBinding, _data, _setting);
}

void DescriptorSetManager::BindDescriptor(
	uint32_t _set,
	uint32_t _binding,
	const std::vector<VkDescriptorBufferInfo>& _data,
	DESCRIPTOR_BIND_SETTING _setting)
{
	BindPlan plan{};
	plan.bindType = _setting;
	plan.set = _set;
	plan.location = _binding;
	plan.bufferBind = _data;
	_PlanBinding(plan);
}

void DescriptorSetManager::BindDescriptor(
	const std::string& _nameInShader,
	const std::vector<VkDescriptorBufferInfo>& _data,
	const std::vector<uint32_t>& _dynmaicOffsets,
	DESCRIPTOR_BIND_SETTING _setting)
{
	uint32_t uSet = ~0;
	uint32_t uBinding = ~0;

	CHECK_TRUE(_GetDescriptorLocation(_nameInShader, uSet, uBinding), "Cannot find binding!");

	BindDescriptor(uSet, uBinding, _data, _dynmaicOffsets, _setting);
}

void DescriptorSetManager::BindDescriptor(
	uint32_t _set,
	uint32_t _binding,
	const std::vector<VkDescriptorBufferInfo>& _data,
	const std::vector<uint32_t>& _dynmaicOffsets,
	DESCRIPTOR_BIND_SETTING _setting)
{
	DynamicOffsetRecord record{};
	BindPlan plan{};

	plan.bindType = _setting;
	plan.bufferBind = _data;
	plan.set = _set;
	plan.location = _binding;
	_PlanBinding(plan);
	
	// dynamic offset is cleared each time we bind, so we add dynamic offsets to record
	record.binding = _binding;
	record.set = _set;
	record.offsets = _dynmaicOffsets;
	m_vecDynamicOffset.push_back(record);

	CHECK_TRUE(
		m_vkDescriptorSetBindingInfo.size() > _set
		&& m_vkDescriptorSetBindingInfo[_set].find(_binding) != m_vkDescriptorSetBindingInfo[_set].end(),
		"Do not have descriptor info!");

	// now update the descriptor type to dynamic
	{
		auto& vkInfo = m_vkDescriptorSetBindingInfo[_set][_binding];
		switch (vkInfo.descriptorType)
		{
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
		{
			vkInfo.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
			break;
		}
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
		{
			vkInfo.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
			break;
		}
		case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
		case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
		{
			break;
		}
		default:
		{
			CHECK_TRUE(false, "This cannot be a dynamic bound!");
			break;
		}
		}
	}
}

void DescriptorSetManager::BindDescriptor(
	const std::string& _nameInShader,
	const std::vector<VkDescriptorImageInfo>& _data,
	DESCRIPTOR_BIND_SETTING _setting)
{
	uint32_t uSet = ~0;
	uint32_t uBinding = ~0;
	CHECK_TRUE(_GetDescriptorLocation(_nameInShader, uSet, uBinding), "Cannot find binding!");

	BindDescriptor(uSet, uBinding, _data, _setting);
}

void DescriptorSetManager::BindDescriptor(
	uint32_t _set,
	uint32_t _binding,
	const std::vector<VkDescriptorImageInfo>& _data,
	DESCRIPTOR_BIND_SETTING _setting)
{
	BindPlan plan{};
	plan.bindType = _setting;
	plan.imageBind = _data;
	plan.set = _set;
	plan.location = _binding;

	_PlanBinding(plan);
}

void DescriptorSetManager::BindDescriptor(
	const std::string& _nameInShader,
	const std::vector<VkBufferView>& _data,
	DESCRIPTOR_BIND_SETTING _setting)
{
	uint32_t uSet = ~0;
	uint32_t uBinding = ~0;

	CHECK_TRUE(_GetDescriptorLocation(_nameInShader, uSet, uBinding), "Cannot find binding!");

	BindDescriptor(uSet, uBinding, _data, _setting);
}

void DescriptorSetManager::BindDescriptor(
	uint32_t _set,
	uint32_t _binding,
	const std::vector<VkBufferView>& _data,
	DESCRIPTOR_BIND_SETTING _setting)
{
	BindPlan plan{};
	plan.viewBind = _data;
	plan.bindType = _setting;
	plan.set = _set;
	plan.location = _binding;
	_PlanBinding(plan);
}

void DescriptorSetManager::BindDescriptor(
	const std::string& _nameInShader,
	const std::vector<VkAccelerationStructureKHR>& _data,
	DESCRIPTOR_BIND_SETTING _setting)
{
	uint32_t uSet = ~0;
	uint32_t uBinding = ~0;

	CHECK_TRUE(_GetDescriptorLocation(_nameInShader, uSet, uBinding), "Cannot find binding!");

	BindDescriptor(uSet, uBinding, _data, _setting);
}

void DescriptorSetManager::BindDescriptor(
	uint32_t _set,
	uint32_t _binding,
	const std::vector<VkAccelerationStructureKHR>& _data,
	DESCRIPTOR_BIND_SETTING _setting)
{
	BindPlan plan{};
	plan.bindType = _setting;
	plan.accelStructBind = _data;
	plan.set = _set;
	plan.location = _binding;

	_PlanBinding(plan);
}

void DescriptorSetManager::EndBind()
{
	if (m_vecUptrDescriptorSetLayout.size() == 0)
	{
		_InitDescriptorSetLayouts();
	}

	CHECK_TRUE(m_pCurrentDescriptorSets.size() == 0, "Descriptors already bound once!");
	_ProcessPlan();
}

void DescriptorSetManager::GetCurrentDescriptorSets(
	std::vector<VkDescriptorSet>& _outDescriptorSets,
	std::vector<uint32_t>& _outDynamicOffsets)
{
	// see specification of dynamic offsets
	// https://registry.khronos.org/vulkan/specs/latest/man/html/vkCmdBindDescriptorSets.html
	std::sort(m_vecDynamicOffset.begin(), m_vecDynamicOffset.end());

	for (const auto& dynamicOffsets : m_vecDynamicOffset)
	{
		_outDynamicOffsets.insert(_outDynamicOffsets.end(), dynamicOffsets.offsets.begin(), dynamicOffsets.offsets.end());
	}

	for (const auto& pDescriptorSetThisFrame : m_pCurrentDescriptorSets)
	{
		_outDescriptorSets.push_back(pDescriptorSetThisFrame->m_vkDescriptorSet);
	}
}

void DescriptorSetManager::GetDescriptorSetLayouts(std::vector<VkDescriptorSetLayout>& _outDescriptorSetLayouts)
{
	for (const auto& uptrDescriptorSetLayout : m_vecUptrDescriptorSetLayout)
	{
		_outDescriptorSetLayouts.push_back(uptrDescriptorSetLayout->m_vkDescriptorSetLayout);
	}
}

void DescriptorSetManager::ClearDescriptorSets()
{
	MyDevice::GetInstance().WaitIdle();
	
	m_uptrDescriptorSetSetting0.clear();
	m_uptrDescriptorSetSetting1.clear();
	m_uptrDescriptorSetSetting2.clear();
	m_uptrDescriptorSetSetting3.clear();
	m_vecDynamicOffset.clear();
	m_pCurrentDescriptorSets.clear();
}

void DescriptorSetManager::Uninit()
{
	size_t count = 0;
	std::cout << "====================================================" << std::endl;
	std::cout << "Descriptor sets managed: " << std::endl;
	std::cout << "----------------------------------------------------" << std::endl;
	std::cout << "1 descriptor set for all frames: " << m_uptrDescriptorSetSetting0.size() << std::endl;
	
	for (auto& i : m_uptrDescriptorSetSetting1)
	{
		for (auto& j : i)
		{
			count++;
		}
	}
	std::cout << "1 descriptor set for each frame: " << count << std::endl;

	count = 0;
	for (auto& i : m_uptrDescriptorSetSetting2)
	{
		for (auto& j : i)
		{
			for (auto& k : j)
			{
				count++;
			}
		}
	}
	std::cout << "1 descriptor set for each bind each frame: " << count << std::endl;

	count = 0;
	for (auto& i : m_uptrDescriptorSetSetting3)
	{
		for (auto& j : i)
		{
			count++;
		}
	}
	std::cout << "1 descriptor set for each bind for all frames: " << count << std::endl;
	std::cout << "====================================================\r\n" << std::endl;

	m_mapNameToSetBinding.clear();
	m_vkDescriptorSetBindingInfo.clear();
	m_uptrDescriptorSetSetting0.clear();
	m_uptrDescriptorSetSetting1.clear();
	m_uptrDescriptorSetSetting2.clear();
	m_uptrDescriptorSetSetting3.clear();
	_UninitDescriptorSetLayouts();
	m_currentPlan.clear();
	m_pCurrentDescriptorSets.clear();
	m_vecDynamicOffset.clear();
	m_uFrameInFlightCount = 0;
	m_uCurrentFrame = 0;
}

DescriptorSetManager::DescriptorSetData::~DescriptorSetData()
{
	if (uptrDescriptorSet.get())
	{
		uptrDescriptorSet.reset();
	}
	bindInfo.clear();
}

void GraphicsProgram::_InitPipeline()
{
	std::vector<std::unique_ptr<SimpleShader>> shaders;

	m_uptrPipeline = std::make_unique<GraphicsPipeline>();
	for (const auto& path : m_vecShaderPath)
	{
		std::unique_ptr<SimpleShader> shader = std::make_unique<SimpleShader>();
		shader->SetSPVFile(path);
		shader->Init();
		m_uptrPipeline->AddShader(shader->GetShaderStageInfo());
		shaders.push_back(std::move(shader));
	}

	// setup descriptor set layouts
	{
		std::vector<VkDescriptorSetLayout> vkLayouts{};
		m_uptrDescriptorSetManager->GetDescriptorSetLayouts(vkLayouts);

		for (const auto& layout : vkLayouts)
		{
			m_uptrPipeline->AddDescriptorSetLayout(layout);
		}
	}

	// add push constants
	{
		std::unordered_map<std::string, uint32_t> mapIndex;
		std::vector<std::pair<VkShaderStageFlags, std::pair<uint32_t, uint32_t>>> pushConstInfo;
		m_shaderReflector.ReflectPushConst(mapIndex, pushConstInfo);

		for (const auto& info : pushConstInfo)
		{
			m_uptrPipeline->AddPushConstant(info.first, info.second.first, info.second.second);
		}
	}

	// add vertex input, render pass
	{
		for (auto& func : m_lateInitialization)
		{
			func(m_uptrPipeline.get());
		}
		m_lateInitialization.clear();
	}

	m_uptrPipeline->Init();

	for (auto& shader : shaders)
	{
		shader->Uninit();
	}
	shaders.clear();
}

void GraphicsProgram::_UninitPipeline()
{
	if (m_uptrPipeline)
	{
		m_uptrPipeline->Uninit();
		m_uptrPipeline.reset();
	}
}

void GraphicsProgram::PresetRenderPass(const RenderPass* _pRenderPass, uint32_t _subpass)
{
	bool bPipelineInitialized = (m_uptrPipeline.get() != nullptr);

	CHECK_TRUE(!bPipelineInitialized, "Pipeline already initialized!");
	m_lateInitialization.push_back([_pRenderPass, _subpass](GraphicsPipeline* _pPipeline)
		{
			_pPipeline->BindToSubpass(_pRenderPass, _subpass);
		}
	);
}

void GraphicsProgram::Init(const std::vector<std::string>& _shaderPaths, uint32_t _frameInFlight = 3u)
{
	auto pDescriptorSetManager = new DescriptorSetManager();

	m_vecShaderPath = _shaderPaths;
	m_uptrDescriptorSetManager.reset(pDescriptorSetManager);
	m_uptrDescriptorSetManager->Init(_shaderPaths, _frameInFlight);
	m_shaderReflector.Init(_shaderPaths);
	{
		std::unordered_map<std::string, uint32_t> mapLocation;
		std::map<uint32_t, VkFormat> mapFormat;
		
		m_shaderReflector.ReflectInput(VK_SHADER_STAGE_VERTEX_BIT, mapLocation, mapFormat);
		for (const auto& p : mapFormat)
		{
			m_mapVertexFormat[p.first] = p.second;
		}
	}
	m_shaderReflector.PrintReflectResult();
}

void GraphicsProgram::EndFrame()
{
	if (m_uptrDescriptorSetManager)
	{
		m_uptrDescriptorSetManager->EndFrame();
	}
}

DescriptorSetManager& GraphicsProgram::GetDescriptorSetManager()
{
	return *m_uptrDescriptorSetManager;
}

void GraphicsProgram::BindVertexBuffer(
	VkBuffer _vertexBuffer, 
	uint32_t _vertexStride, 
	const std::unordered_map<uint32_t, uint32_t>& _mapLocationOffset)
{
	bool bPipelineInitialized = (m_uptrPipeline.get() != nullptr);
	
	m_vertexBuffers.push_back(_vertexBuffer);
	
	if (!bPipelineInitialized)
	{
		VertexInputLayout vertLayout{};
		uint32_t binding = m_vertexBuffers.size() - 1;

		vertLayout.SetUpVertex(_vertexStride);

		for (const auto& p : _mapLocationOffset)
		{
			vertLayout.AddLocation(m_mapVertexFormat[p.first], p.second, p.first);
		}

		m_lateInitialization.push_back([vertLayout, binding](GraphicsPipeline* _pPipeline)
			{
				_pPipeline->AddVertexInputLayout(vertLayout.GetVertexInputBindingDescription(binding), vertLayout.GetVertexInputAttributeDescriptions(binding));
			}
		);
	}
}

void GraphicsProgram::BindIndexBuffer(VkBuffer _indexBuffer, VkIndexType _indexType)
{
	m_indexBuffer = _indexBuffer;
	m_vkIndexType = _indexType;
}

void GraphicsProgram::PushConstant(VkShaderStageFlags _stages, const void* _data)
{
	m_pushConstants.push_back({ _stages, _data });
}

void GraphicsProgram::BindFramebuffer(
	CommandSubmission* _pCmd, 
	const Framebuffer* _pFramebuffer)
{
	_pCmd->StartRenderPass(_pFramebuffer->pRenderPass, _pFramebuffer);
	m_pFramebuffer = _pFramebuffer;
}

void GraphicsProgram::Draw(
	CommandSubmission* _pCmd, 
	uint32_t _vertexCount)
{
	GraphicsPipeline::PipelineInput_Draw input{};
	bool bPipelineInitlaized = (m_uptrPipeline.get() != nullptr);

	if (!bPipelineInitlaized)
	{
		_InitPipeline();
	}

	input.imageSize = m_pFramebuffer->GetImageSize();
	m_uptrDescriptorSetManager->GetCurrentDescriptorSets(input.vkDescriptorSets, input.optDynamicOffsets);
	input.vertexBuffers = m_vertexBuffers;
	input.vertexCount = _vertexCount;
	input.pushConstants = m_pushConstants;

	m_uptrPipeline->Do(_pCmd->vkCommandBuffer, input);
	m_pushConstants.clear();
	m_vertexBuffers.clear();
}

void GraphicsProgram::DrawIndexed(
	CommandSubmission* _pCmd, 
	uint32_t _indexCount)
{
	GraphicsPipeline::PipelineInput_DrawIndexed input{};
	bool bPipelineInitlaized = (m_uptrPipeline.get() != nullptr);

	if (!bPipelineInitlaized)
	{
		_InitPipeline();
	}

	input.imageSize = m_pFramebuffer->GetImageSize();
	input.indexBuffer = m_indexBuffer;
	input.indexCount = _indexCount;
	m_uptrDescriptorSetManager->GetCurrentDescriptorSets(input.vkDescriptorSets, input.optDynamicOffsets);
	input.pushConstants = m_pushConstants;
	input.vertexBuffers = m_vertexBuffers;
	input.vkIndexType = m_vkIndexType;

	m_uptrPipeline->Do(_pCmd->vkCommandBuffer, input);

	m_vertexBuffers.clear();
	m_pushConstants.clear();
	m_indexBuffer = VK_NULL_HANDLE;
}

void GraphicsProgram::DispatchWorkGroup(
	CommandSubmission* _pCmd, 
	uint32_t _groupCountX, 
	uint32_t _groupCountY, 
	uint32_t _groupCountZ)
{
	GraphicsPipeline::PipelineInput_Mesh input{};
	bool bPipelineInitlaized = (m_uptrPipeline.get() != nullptr);

	if (!bPipelineInitlaized)
	{
		_InitPipeline();
	}

	input.groupCountX = _groupCountX;
	input.groupCountY = _groupCountY;
	input.groupCountZ = _groupCountZ;
	input.imageSize = m_pFramebuffer->GetImageSize();
	input.pushConstants = m_pushConstants;
	m_uptrDescriptorSetManager->GetCurrentDescriptorSets(input.vkDescriptorSets, input.optDynamicOffsets);
	m_pushConstants.clear();
	m_uptrPipeline->Do(_pCmd->vkCommandBuffer, input);
}

void GraphicsProgram::UnbindFramebuffer(CommandSubmission* _pCmd)
{
	_pCmd->EndRenderPass();
	m_pFramebuffer = nullptr;
}

void GraphicsProgram::Uninit()
{
	_UninitPipeline();
	if (m_uptrDescriptorSetManager)
	{
		m_uptrDescriptorSetManager->Uninit();
		m_uptrDescriptorSetManager.reset();
	}
	m_vecShaderPath.clear();
	m_vertexBuffers.clear();
	m_vkIndexType = VK_INDEX_TYPE_UINT32;
	m_pushConstants.clear();
	m_shaderReflector.Uninit();
	m_mapVertexFormat.clear();
}

void ComputeProgram::_InitPipeline()
{
	std::vector<std::unique_ptr<SimpleShader>> shaders;

	m_uptrPipeline = std::make_unique<ComputePipeline>();
	for (const auto& path : m_vecShaderPath)
	{
		std::unique_ptr<SimpleShader> shader = std::make_unique<SimpleShader>();
		shader->SetSPVFile(path);
		shader->Init();
		m_uptrPipeline->AddShader(shader->GetShaderStageInfo());
		shaders.push_back(std::move(shader));
	}

	// setup descriptor set layouts
	{
		std::vector<VkDescriptorSetLayout> vkLayouts{};
		m_uptrDescriptorSetManager->GetDescriptorSetLayouts(vkLayouts);

		for (const auto& layout : vkLayouts)
		{
			m_uptrPipeline->AddDescriptorSetLayout(layout);
		}
	}

	// add push constants
	{
		std::unordered_map<std::string, uint32_t> mapIndex;
		std::vector<std::pair<VkShaderStageFlags, std::pair<uint32_t, uint32_t>>> pushConstInfo;
		m_shaderReflector.ReflectPushConst(mapIndex, pushConstInfo);

		for (const auto& info : pushConstInfo)
		{
			m_uptrPipeline->AddPushConstant(info.first, info.second.first, info.second.second);
		}
	}

	m_uptrPipeline->Init();

	for (auto& shader : shaders)
	{
		shader->Uninit();
	}
	shaders.clear();
}

void ComputeProgram::_UninitPipeline()
{
	if (m_uptrPipeline)
	{
		m_uptrPipeline->Uninit();
		m_uptrPipeline.reset();
	}
}

void ComputeProgram::Init(const std::vector<std::string>& _shaderPaths, uint32_t _frameInFlight = 3u)
{
	auto pDescriptorSetManager = new DescriptorSetManager();

	m_vecShaderPath = _shaderPaths;
	m_uptrDescriptorSetManager.reset(pDescriptorSetManager);
	m_uptrDescriptorSetManager->Init(_shaderPaths, _frameInFlight);
	m_shaderReflector.Init(_shaderPaths);
	m_shaderReflector.PrintReflectResult();
}

void ComputeProgram::EndFrame()
{
	if (m_uptrDescriptorSetManager)
	{
		m_uptrDescriptorSetManager->EndFrame();
	}
}

DescriptorSetManager& ComputeProgram::GetDescriptorSetManager()
{
	return *m_uptrDescriptorSetManager;
}

void ComputeProgram::PushConstant(VkShaderStageFlags _stages, const void* _data)
{
	m_pushConstants.push_back({ _stages, _data });
}

void ComputeProgram::DispatchWorkGroup(CommandSubmission* _pCmd, uint32_t _groupCountX, uint32_t _groupCountY, uint32_t _groupCountZ)
{
	ComputePipeline::PipelineInput input{};
	bool bPipelineInitlaized = (m_uptrPipeline.get() != nullptr);

	if (!bPipelineInitlaized)
	{
		_InitPipeline();
	}

	input.groupCountX = _groupCountX;
	input.groupCountY = _groupCountY;
	input.groupCountZ = _groupCountZ;
	input.pushConstants = m_pushConstants;
	m_uptrDescriptorSetManager->GetCurrentDescriptorSets(input.vkDescriptorSets, input.optDynamicOffsets);

	m_pushConstants.clear();

	m_uptrPipeline->Do(_pCmd->vkCommandBuffer, input);
}

void ComputeProgram::Uninit()
{
	_UninitPipeline();
	if (m_uptrDescriptorSetManager)
	{
		m_uptrDescriptorSetManager->Uninit();
		m_uptrDescriptorSetManager.reset();
	}
	m_pushConstants.clear();
	m_shaderReflector.Uninit();
}

void RayTracingProgram::_InitPipeline()
{
	std::vector<std::unique_ptr<SimpleShader>> shaders;

	m_uptrPipeline = std::make_unique<RayTracingPipeline>();
	for (const auto& path : m_vecShaderPath)
	{
		std::unique_ptr<SimpleShader> shader = std::make_unique<SimpleShader>();
		shader->SetSPVFile(path);
		shader->Init();
		m_uptrPipeline->AddShader(shader->GetShaderStageInfo());
		shaders.push_back(std::move(shader));
	}

	// setup descriptor set layouts
	{
		std::vector<VkDescriptorSetLayout> vkLayouts{};
		m_uptrDescriptorSetManager->GetDescriptorSetLayouts(vkLayouts);

		for (const auto& layout : vkLayouts)
		{
			m_uptrPipeline->AddDescriptorSetLayout(layout);
		}
	}

	// add push constants
	{
		std::unordered_map<std::string, uint32_t> mapIndex;
		std::vector<std::pair<VkShaderStageFlags, std::pair<uint32_t, uint32_t>>> pushConstInfo;
		m_shaderReflector.ReflectPushConst(mapIndex, pushConstInfo);

		for (const auto& info : pushConstInfo)
		{
			m_uptrPipeline->AddPushConstant(info.first, info.second.first, info.second.second);
		}
	}

	// build SBT
	{
		for (auto& func : m_lateInitialization)
		{
			func(m_uptrPipeline.get());
		}
		m_lateInitialization.clear();
	}

	m_uptrPipeline->Init();

	for (auto& shader : shaders)
	{
		shader->Uninit();
	}
	shaders.clear();
}

void RayTracingProgram::_UninitPipeline()
{
	if (m_uptrPipeline)
	{
		m_uptrPipeline->Uninit();
		m_uptrPipeline.reset();
	}
}

void RayTracingProgram::PreAddRayGenerationShader(const std::string& _path)
{
	uint32_t index = 0u;
	if (m_mapShaderToIndex.find(_path) == m_mapShaderToIndex.end())
	{
		m_mapShaderToIndex[_path] = m_vecShaderPath.size();
		m_vecShaderPath.push_back(_path);
	}

	index = m_mapShaderToIndex[_path];

	m_lateInitialization.push_back(
		[index](RayTracingPipeline* _pipeline)
		{
			_pipeline->SetRayGenerationShaderRecord(index);
		}
	);
}

void RayTracingProgram::PreAddTriangleHitShaders(const std::string& _closestHit, const std::optional<std::string>& _anyHit)
{
	uint32_t closestHit = 0u;
	uint32_t anyHit = VK_SHADER_UNUSED_KHR;

	if (m_mapShaderToIndex.find(_closestHit) == m_mapShaderToIndex.end())
	{
		m_mapShaderToIndex[_closestHit] = m_vecShaderPath.size();
		m_vecShaderPath.push_back(_closestHit);
	}

	closestHit = m_mapShaderToIndex[_closestHit];

	if (_anyHit.has_value())
	{
		if (m_mapShaderToIndex.find(_anyHit.value()) == m_mapShaderToIndex.end())
		{
			m_mapShaderToIndex[_anyHit.value()] = m_vecShaderPath.size();
			m_vecShaderPath.push_back(_anyHit.value());
		}

		anyHit = m_mapShaderToIndex[_anyHit.value()];
	}

	m_lateInitialization.push_back(
		[closestHit, anyHit](RayTracingPipeline* _pipeline)
		{
			_pipeline->AddTriangleHitShaderRecord(closestHit, anyHit);
		}
	);
}

void RayTracingProgram::PreAddProceduralHitShaders(const std::string& _closestHit, const std::string& _intersection, const std::optional<std::string>& _anyHit)
{
	uint32_t closestHit = 0u;
	uint32_t intersection = 0u;
	uint32_t anyHit = VK_SHADER_UNUSED_KHR;

	if (m_mapShaderToIndex.find(_closestHit) == m_mapShaderToIndex.end())
	{
		m_mapShaderToIndex[_closestHit] = m_vecShaderPath.size();
		m_vecShaderPath.push_back(_closestHit);
	}
	closestHit = m_mapShaderToIndex[_closestHit];

	if (m_mapShaderToIndex.find(_intersection) == m_mapShaderToIndex.end())
	{
		m_mapShaderToIndex[_intersection] = m_vecShaderPath.size();
		m_vecShaderPath.push_back(_intersection);
	}
	intersection = m_mapShaderToIndex[_intersection];

	if (_anyHit.has_value())
	{
		if (m_mapShaderToIndex.find(_anyHit.value()) == m_mapShaderToIndex.end())
		{
			m_mapShaderToIndex[_anyHit.value()] = m_vecShaderPath.size();
			m_vecShaderPath.push_back(_anyHit.value());
		}
		anyHit = m_mapShaderToIndex[_anyHit.value()];
	}

	m_lateInitialization.push_back(
		[closestHit, intersection, anyHit](RayTracingPipeline* _pipeline)
		{
			_pipeline->AddProceduralHitShaderRecord(closestHit, intersection, anyHit);
		}
	);
}

void RayTracingProgram::PreAddMissShader(const std::string& _miss)
{
	uint32_t miss = 0u;

	if (m_mapShaderToIndex.find(_miss) == m_mapShaderToIndex.end())
	{
		m_mapShaderToIndex[_miss] = m_vecShaderPath.size();
		m_vecShaderPath.push_back(_miss);
	}
	miss = m_mapShaderToIndex[_miss];

	m_lateInitialization.push_back(
		[miss](RayTracingPipeline* _pipeline)
		{
			_pipeline->AddMissShaderRecord(miss);
		}
	);
}

void RayTracingProgram::PresetMaxRecursion(uint32_t _maxRecur)
{
	m_lateInitialization.push_back(
		[_maxRecur](RayTracingPipeline* _pipeline)
		{
			_pipeline->SetMaxRecursion(_maxRecur);
		}
	);
}

void RayTracingProgram::Init(uint32_t _frameInFlight = 3u)
{
	auto pDescriptorSetManager = new DescriptorSetManager();

	m_uptrDescriptorSetManager.reset(pDescriptorSetManager);
	m_uptrDescriptorSetManager->Init(m_vecShaderPath, _frameInFlight);
	m_shaderReflector.Init(m_vecShaderPath);
	m_shaderReflector.PrintReflectResult();
}

void RayTracingProgram::EndFrame()
{
	m_uptrDescriptorSetManager->EndFrame();
}

DescriptorSetManager& RayTracingProgram::GetDescriptorSetManager()
{
	return *m_uptrDescriptorSetManager.get();
}

void RayTracingProgram::PushConstant(VkShaderStageFlags _stages, const void* _data)
{
	m_pushConstants.push_back({ _stages, _data });
}

void RayTracingProgram::TraceRay(CommandSubmission* _pCmd, uint32_t _uWidth, uint32_t _uHeight)
{
	RayTracingPipeline::PipelineInput input{};
	if (m_uptrPipeline.get() == nullptr)
	{
		_InitPipeline();
	}

	input.uWidth = _uWidth;
	input.uHeight = _uHeight;
	input.uDepth = 1u;
	input.pushConstants = m_pushConstants;
	m_uptrDescriptorSetManager->GetCurrentDescriptorSets(input.vkDescriptorSets, input.optDynamicOffsets);

	m_pushConstants.clear();

	m_uptrPipeline->Do(_pCmd->vkCommandBuffer, input);
}

void RayTracingProgram::Uninit()
{
	_UninitPipeline();
	if (m_uptrDescriptorSetManager)
	{
		m_uptrDescriptorSetManager->Uninit();
		m_uptrDescriptorSetManager.reset();
	}
	m_pushConstants.clear();
	m_shaderReflector.Uninit();
	m_vecShaderPath.clear();
	m_mapShaderToIndex.clear();
	m_lateInitialization.clear();
}

void CameraComponent::UpdateCamera(float _moveSpeed)
{
	UserInput userInput = MyDevice::GetInstance().GetUserInput();
	VkExtent2D swapchainExtent = MyDevice::GetInstance().GetSwapchainExtent();
	float xoffset;
	float yoffset;
	if (m_lastTime < 0)
	{
		m_lastTime = MyDevice::GetInstance().GetTime();
	}

	if (!userInput.RMB)
	{
		m_lastX = userInput.xPos;
		m_lastY = userInput.yPos;
	}
	xoffset = m_lastX.has_value() ? static_cast<float>(userInput.xPos) - m_lastX.value() : 0.0f;
	yoffset = m_lastY.has_value() ? m_lastY.value() - static_cast<float>(userInput.yPos) : 0.0f;
	m_currentTime = MyDevice::GetInstance().GetTime();
	m_lastX = userInput.xPos;
	m_lastY = userInput.yPos;
	xoffset *= m_sensitivity;
	yoffset *= m_sensitivity;
	camera.RotateAboutWorldUp(glm::radians(-xoffset));
	camera.RotateAboutRight(glm::radians(yoffset));
	if (userInput.RMB)
	{
		glm::vec3 fwd = glm::normalize(glm::cross(camera.world_up, camera.right));
		float speed = _moveSpeed * static_cast<float>(m_currentTime - m_lastTime);
		glm::vec3 mov = camera.eye;
		if (userInput.W) mov += (speed * fwd);
		if (userInput.S) mov += (-speed * fwd);
		if (userInput.Q) mov += (speed * camera.world_up);
		if (userInput.E) mov += (-speed * camera.world_up);
		if (userInput.A) mov += (-speed * camera.right);
		if (userInput.D) mov += (speed * camera.right);
		camera.MoveTo(mov);
	}

	m_lastTime = m_currentTime;
}
