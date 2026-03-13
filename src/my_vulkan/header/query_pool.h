#pragma once
#include "common.h"

class QueryPool
{
private:
	VkQueryPool m_vkQueryPool = VK_NULL_HANDLE;

public:
	class Creator
	{
	private:
		VkQueryPoolCreateInfo m_createInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };

	public:
		void SetType(VkQueryType inType) { m_createInfo.queryType = inType; };
		void SetQueryCount(uint32_t inCount) { m_createInfo.queryCount = inCount; };
		void CreateQueryPool(QueryPool* outPool) const;
	};

public:
	void Create(const QueryPool::Creator* inCreator);
	void Reset();
	auto GetVkQueryPool()->VkQueryPool { return m_vkQueryPool; };
	void Destroy();
};