#pragma once
#include "common.h"
#include <functional>

class IWaitable
{
public:
	virtual ~IWaitable() = default;
	virtual void DependOn(IWaitable& inPrecondition) = 0;
};

class ISingleThreadTask : public IWaitable
{
public:
	virtual ~ISingleThreadTask() = default;
	virtual void Execute() = 0;
};

class IMultiThreadTask : public IWaitable
{
public:
	virtual ~IMultiThreadTask() = default;
	virtual void ExecuteSubTask(uint32_t SubStart, uint32_t SubEnd, uint32_t inThreadIndex) = 0;
};

class ITaskScheduler
{
public:
	virtual ~ITaskScheduler() = default;
	virtual void AddSingleThreadTask(ISingleThreadTask& inSingleThreadTask) = 0;
	virtual void AddMutiThreadTask(IMultiThreadTask& inMultiThreadTask) = 0;
	virtual void WaitForTask(const IWaitable& inToWait) = 0;
	virtual void WaitForAll() = 0;
};

// interface that get the real class behind the scene
class IImplement
{
public:
	virtual ~IImplement() = default;
	virtual auto GetRealImplement() -> void* = 0;
	virtual auto GetRealImplement() const -> const void* = 0;
};

class MySinglThreadTask final : public ISingleThreadTask, public IImplement
{
private:
	std::unique_ptr<ISingleThreadTask> m_uptrImpl;

public:
	MySinglThreadTask(std::function<void()> inFunction, uint32_t inThreadIndex = 0);
	virtual void DependOn(IWaitable& inPrecondition) override;
	virtual void Execute() override;
	virtual auto GetRealImplement() -> void* override;
	virtual auto GetRealImplement() const -> const void* override;
};

class MyMultiThreadTask final : public IMultiThreadTask, public IImplement
{
private:
	std::unique_ptr<IMultiThreadTask> m_uptrImpl;

public:
	MyMultiThreadTask(std::function<void(uint32_t, uint32_t, uint32_t)> inFunction, uint32_t inSubTaskCount = 1);
	virtual void DependOn(IWaitable& inPrecondition) override;
	virtual void ExecuteSubTask(uint32_t SubStart, uint32_t SubEnd, uint32_t inThreadIndex) override;
	virtual auto GetRealImplement() -> void* override;
	virtual auto GetRealImplement() const -> const void* override;
};

class MyTaskScheduler final : public ITaskScheduler
{
private:
	static std::unique_ptr<MyTaskScheduler> g_uptrInstance;
	std::unique_ptr<ITaskScheduler> m_uptrImpl;
	MyTaskScheduler();

public:
	static constexpr uint32_t THREAD_COUNT = 4;

	static auto GetInstance() -> MyTaskScheduler&;
	void Create();
	virtual void AddSingleThreadTask(ISingleThreadTask& inSingleThreadTask) override;
	virtual void AddMutiThreadTask(IMultiThreadTask& inMultiThreadTask) override;
	// _Wait till the task is no longer pending/executing on the system
	virtual void WaitForTask(const IWaitable& inToWait) override;
	virtual void WaitForAll() override;
	void Destroy();
};
