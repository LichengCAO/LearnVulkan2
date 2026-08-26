#pragma once

#include <atomic>
#include <cstdint>

class RefCounted
{
private:
	mutable std::atomic_uint32_t m_referenceCount{ 0 };

protected:
	RefCounted() = default;
	RefCounted(const RefCounted&) = delete;
	RefCounted& operator=(const RefCounted&) = delete;
	virtual ~RefCounted() = default;

public:
	auto AddRef() const noexcept->void
	{
		m_referenceCount.fetch_add(1, std::memory_order_relaxed);
	}

	auto Release() const noexcept->void
	{
		const uint32_t previousCount = m_referenceCount.fetch_sub(1, std::memory_order_acq_rel);
		if (previousCount == 1)
		{
			delete this;
		}
	}
};
