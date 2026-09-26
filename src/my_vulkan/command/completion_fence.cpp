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
		if (m_isInFlight)
		{
			Wait();
		}
		if (m_vkFence != VK_NULL_HANDLE)
		{
			MyDevice::GetInstance().DestroyVkFence(m_vkFence);
		}
	}
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
	if (MyDevice::GetInstance().GetFenceStatus(m_vkFence) != VK_SUCCESS)
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
	CHECK_TRUE(
		!m_isInFlight,
		"Cannot add a callback while the previous submission is unconsumed!");
	m_callbacks.push_back(std::move(inCallback));
	return *this;
}

void HostFence::_PrepareForSubmit()
{
	CHECK_TRUE(
		!m_isInFlight,
		"Host fence is still bound to an unconsumed submission!");
	VK_CHECK(vkResetFences(MyDevice::GetInstance().GetVkDevice(), 1, &m_vkFence),
		"Failed to reset completion fence!");
}

void HostFence::_CommitSubmit()
{
	m_isInFlight = true;
}

void HostFence::_RunCallbacks()
{
	std::vector<Callback> callbacks = std::move(m_callbacks);
	m_callbacks.clear();
	for (Callback& callback : callbacks)
	{
		callback();
	}
}
