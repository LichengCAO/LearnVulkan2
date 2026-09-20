#include "completion_fence.h"

#include "device.h"

HostFence::HostFence()
{
	CHECK_TRUE(MyDevice::GetInstance().GetVkDevice() != VK_NULL_HANDLE,
		"Cannot create completion fence before the device!");
	m_vkFence = MyDevice::GetInstance().CreateVkFence(0);
}

HostFence::~HostFence()
{
	if (MyDevice::GetInstance().GetVkDevice() != VK_NULL_HANDLE)
	{
		_ForceComplete();
		if (m_vkFence != VK_NULL_HANDLE)
		{
			MyDevice::GetInstance().DestroyVkFence(m_vkFence);
		}
	}
	m_pendingCallbacks.clear();
	m_activeCallbacks.clear();
}

auto HostFence::IsComplete() const->bool
{
	if (!m_isInFlight)
	{
		return true;
	}

	return MyDevice::GetInstance().GetFenceStatus(m_vkFence) == VK_SUCCESS;
}

void HostFence::Wait()
{
	if (!m_isInFlight)
	{
		return;
	}

	std::vector<VkFence> fences{ m_vkFence };
	VK_CHECK(
		MyDevice::GetInstance().WaitForFences(fences, true, UINT64_MAX),
		"Failed to wait for completion fence!");
	m_isInFlight = false;
	_RunCallbacks();
}

auto HostFence::Poll()->bool
{
	if (!m_isInFlight)
	{
		return true;
	}
	if (!IsComplete())
	{
		return false;
	}

	m_isInFlight = false;
	_RunCallbacks();
	return true;
}

auto HostFence::AddCallback(Callback inCallback)->HostFence&
{
	CHECK_TRUE(static_cast<bool>(inCallback), "Completion callback is empty!");
	m_pendingCallbacks.push_back(std::move(inCallback));
	return *this;
}

void HostFence::_PrepareForSubmit()
{
	// A fence cannot be reset or reused while its previous submission is active.
	Wait();
	VK_CHECK(vkResetFences(MyDevice::GetInstance().GetVkDevice(), 1, &m_vkFence),
		"Failed to reset completion fence!");
}

void HostFence::_CommitSubmit()
{
	m_activeCallbacks = std::move(m_pendingCallbacks);
	m_pendingCallbacks.clear();
	m_hasSubmittedWork = true;
	m_isInFlight = true;
}

void HostFence::_ForceComplete()
{
	if (m_isInFlight)
	{
		Wait();
	}
}

void HostFence::_RunCallbacks()
{
	std::vector<Callback> callbacks = std::move(m_activeCallbacks);
	m_activeCallbacks.clear();
	for (Callback& callback : callbacks)
	{
		callback();
	}
}
