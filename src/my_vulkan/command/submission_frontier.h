#pragma once

#include "common.h"

#include <algorithm>

class SubmissionFrontier final
{
public:
	static constexpr size_t MAX_QUEUE_COUNT = 3;

private:
	std::array<uint64_t, MAX_QUEUE_COUNT> m_versions{};

public:
	void Merge(const SubmissionFrontier& inOther)
	{
		for (size_t queueIndex = 0; queueIndex < MAX_QUEUE_COUNT; ++queueIndex)
		{
			m_versions[queueIndex] = std::max(m_versions[queueIndex], inOther.m_versions[queueIndex]);
		}
	}

	void Advance(size_t inQueueIndex)
	{
		CHECK_TRUE(inQueueIndex < MAX_QUEUE_COUNT, "Submission frontier queue index is out of range!");
		CHECK_TRUE(m_versions[inQueueIndex] != UINT64_MAX, "Submission version overflow!");
		++m_versions[inQueueIndex];
	}

	auto GetVersion(size_t inQueueIndex) const->uint64_t
	{
		CHECK_TRUE(inQueueIndex < MAX_QUEUE_COUNT, "Submission frontier queue index is out of range!");
		return m_versions[inQueueIndex];
	}

	auto Covers(size_t inQueueIndex, uint64_t inVersion) const->bool
	{
		return GetVersion(inQueueIndex) >= inVersion;
	}

	auto Covers(const SubmissionFrontier& inOther) const->bool
	{
		for (size_t queueIndex = 0; queueIndex < MAX_QUEUE_COUNT; ++queueIndex)
		{
			if (!Covers(queueIndex, inOther.m_versions[queueIndex]))
			{
				return false;
			}
		}
		return true;
	}
};
