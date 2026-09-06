#include "completion_fence.h"

#include "device.h"

CompletionFence::CompletionFence()
{
	CHECK_TRUE(MyDevice::GetInstance().GetVkDevice() != VK_NULL_HANDLE,
		"Cannot create completion fence before the device!");
	m_vkFence = MyDevice::GetInstance().CreateVkFence(0);
}

CompletionFence::~CompletionFence()
{
	if (MyDevice::GetInstance().GetVkDevice() != VK_NULL_HANDLE)
	{
		ForceComplete();
		if (m_vkFence != VK_NULL_HANDLE)
		{
			MyDevice::GetInstance().DestroyVkFence(m_vkFence);
		}
	}
	m_pendingCallbacks.clear();
	m_activeCallbacks.clear();
}

bool CompletionFence::IsComplete() const
{
	if (!m_isInFlight)
	{
		return true;
	}

	return MyDevice::GetInstance().GetFenceStatus(m_vkFence) == VK_SUCCESS;
}

void CompletionFence::Wait()
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
	RunCallbacks();
}

bool CompletionFence::Poll()
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
	RunCallbacks();
	return true;
}

CompletionFence& CompletionFence::AddCallback(Callback inCallback)
{
	CHECK_TRUE(static_cast<bool>(inCallback), "Completion callback is empty!");
	m_pendingCallbacks.push_back(std::move(inCallback));
	return *this;
}

void CompletionFence::PrepareForSubmit()
{
	// A fence cannot be reset or reused while its previous submission is active.
	Wait();
	VK_CHECK(vkResetFences(MyDevice::GetInstance().GetVkDevice(), 1, &m_vkFence),
		"Failed to reset completion fence!");
}

void CompletionFence::CommitSubmit()
{
	m_activeCallbacks = std::move(m_pendingCallbacks);
	m_pendingCallbacks.clear();
	m_hasSubmittedWork = true;
	m_isInFlight = true;
}

void CompletionFence::ForceComplete()
{
	if (m_isInFlight)
	{
		Wait();
	}
}

void CompletionFence::RunCallbacks()
{
	std::vector<Callback> callbacks = std::move(m_activeCallbacks);
	m_activeCallbacks.clear();
	for (Callback& callback : callbacks)
	{
		callback();
	}
}
