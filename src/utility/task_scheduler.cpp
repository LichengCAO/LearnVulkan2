#include "task_scheduler.h"
#include "device.h"
#include <TaskScheduler.h>

namespace
{
	template<typename T>
	auto _UnpackImpl(T& inObject) -> T&
	{
		T* piUnpacked{ &inObject };
		IImplement* piImpl{ dynamic_cast<IImplement*>(piUnpacked) };

		// see if this has implement behind?
		while (piImpl != nullptr)
		{
			piUnpacked = static_cast<T*>(piImpl->GetRealImplement());
			piImpl = dynamic_cast<IImplement*>(piUnpacked);
		}

		return *piUnpacked;
	}

	template<typename T>
	auto _UnpackImpl(const T& inObject) -> const T&
	{
		const T* piUnpacked{ &inObject };
		const IImplement* piImpl{ dynamic_cast<const IImplement*>(piUnpacked) };

		// see if this has implement behind?
		while (piImpl != nullptr)
		{
			piUnpacked = static_cast<const T*>(piImpl->GetRealImplement());
			piImpl = dynamic_cast<const IImplement*>(piUnpacked);
		}

		return *piUnpacked;
	}
}

std::unique_ptr<MyTaskScheduler> MyTaskScheduler::g_uptrInstance;

class MySinglThreadTaskImpl final : public ISingleThreadTask, public enki::IPinnedTask
{
private:
	std::function<void()> m_function;
	std::vector<enki::Dependency> m_dependencies;

public:
	MySinglThreadTaskImpl(std::function<void()> inFunction, uint32_t inThreadId = 0) 
		: enki::IPinnedTask(inThreadId), m_function(inFunction){};
	virtual void DependOn(IWaitable& inPrecondition) override;
	virtual void Execute() override;
};

class MyMultiThreadTaskImpl final : public IMultiThreadTask, public enki::ITaskSet
{
private:
	std::function<void(uint32_t, uint32_t, uint32_t)> m_function;
	std::vector<enki::Dependency> m_dependencies;

public:
	MyMultiThreadTaskImpl(std::function<void(uint32_t, uint32_t, uint32_t)> inFunction, uint32_t inSubTaskCount = 1) 
		: enki::ITaskSet(inSubTaskCount), m_function(inFunction) {};
	virtual void DependOn(IWaitable& inPrecondition) override;
	virtual void ExecuteSubTask(uint32_t SubStart, uint32_t SubEnd, uint32_t inThreadIndex) override;
	virtual void ExecuteRange(enki::TaskSetPartition inRange, uint32_t inThreadnum) override;
};

class MyTaskSchedulerImpl final : public ITaskScheduler, public enki::TaskScheduler
{
public:
	virtual void AddSingleThreadTask(ISingleThreadTask& inSingleThreadTask) override;
	virtual void AddMutiThreadTask(IMultiThreadTask& inMultiThreadTask) override;
	virtual void WaitForTask(const IWaitable& inToWait) override;
	virtual void WaitForAll() override;
};

void MySinglThreadTaskImpl::DependOn(IWaitable& inPrecondition)
{
	enki::ICompletable* pCompletable = dynamic_cast<enki::ICompletable*>(&inPrecondition);
	CHECK_TRUE(pCompletable != nullptr);
	m_dependencies.push_back({});
	this->SetDependency(m_dependencies.back(), pCompletable);
}

void MySinglThreadTaskImpl::Execute()
{
	m_function();
}

void MyMultiThreadTaskImpl::DependOn(IWaitable& inPrecondition)
{
	IImplement* pImpl = dynamic_cast<IImplement*>(&inPrecondition);
	CHECK_TRUE(pImpl != nullptr);
	enki::ICompletable* pCompletable = static_cast<enki::ICompletable*>(pImpl->GetRealImplement());
	CHECK_TRUE(pCompletable != nullptr);
	m_dependencies.push_back({});
	this->SetDependency(m_dependencies.back(), pCompletable);
}

void MyMultiThreadTaskImpl::ExecuteSubTask(uint32_t SubStart, uint32_t SubEnd, uint32_t inThreadIndex)
{
	m_function(SubStart, SubEnd, inThreadIndex);
}

void MyMultiThreadTaskImpl::ExecuteRange(enki::TaskSetPartition inRange, uint32_t inThreadnum)
{
	ExecuteSubTask(inRange.start, inRange.end, inThreadnum);
}

void MyTaskSchedulerImpl::AddSingleThreadTask(ISingleThreadTask& inSingleThreadTask)
{
	enki::IPinnedTask* piPinned = dynamic_cast<enki::IPinnedTask*>(&inSingleThreadTask);
	CHECK_TRUE(piPinned != nullptr);
	this->AddPinnedTask(piPinned);
}

void MyTaskSchedulerImpl::AddMutiThreadTask(IMultiThreadTask& inMultiThreadTask)
{
	enki::ITaskSet* piTaskSet = dynamic_cast<enki::ITaskSet*>(&inMultiThreadTask);
	CHECK_TRUE(piTaskSet != nullptr);
	this->AddTaskSetToPipe(piTaskSet);
}

void MyTaskSchedulerImpl::WaitForTask(const IWaitable& inToWait)
{
	const enki::ICompletable* piComplete = dynamic_cast<const enki::ICompletable*>(&inToWait);
	CHECK_TRUE(piComplete != nullptr);
	this->enki::TaskScheduler::WaitforTask(piComplete);
}

void MyTaskSchedulerImpl::WaitForAll()
{
	this->enki::TaskScheduler::WaitforAll();
}

MySinglThreadTask::MySinglThreadTask(std::function<void()> inFunction, uint32_t inThreadIndex)
	:m_uptrImpl(std::make_unique<MySinglThreadTaskImpl>(inFunction, inThreadIndex))
{
}

void MySinglThreadTask::DependOn(IWaitable& inPrecondition)
{
	m_uptrImpl->DependOn(_UnpackImpl(inPrecondition));
}

void MySinglThreadTask::Execute()
{
	m_uptrImpl->Execute();
}

auto MySinglThreadTask::GetRealImplement() -> void*
{
	return m_uptrImpl.get();
}

auto MySinglThreadTask::GetRealImplement() const -> const void*
{
	return m_uptrImpl.get();
}

MyMultiThreadTask::MyMultiThreadTask(std::function<void(uint32_t, uint32_t, uint32_t)> inFunction, uint32_t inSubTaskCount)
	:m_uptrImpl(std::make_unique<MyMultiThreadTaskImpl>(inFunction, inSubTaskCount))
{
}

void MyMultiThreadTask::DependOn(IWaitable& inPrecondition)
{
	m_uptrImpl->DependOn(_UnpackImpl(inPrecondition));
}

void MyMultiThreadTask::ExecuteSubTask(uint32_t SubStart, uint32_t SubEnd, uint32_t inThreadIndex)
{
	m_uptrImpl->ExecuteSubTask(SubStart, SubEnd, inThreadIndex);
}

auto MyMultiThreadTask::GetRealImplement() -> void*
{
	return m_uptrImpl.get();
}

auto MyMultiThreadTask::GetRealImplement() const -> const void*
{
	return m_uptrImpl.get();
}

MyTaskScheduler::MyTaskScheduler()
	:m_uptrImpl(std::make_unique<MyTaskSchedulerImpl>())
{
}

auto MyTaskScheduler::GetInstance() -> MyTaskScheduler&
{
	if (g_uptrInstance == nullptr)
	{
		g_uptrInstance.reset(new MyTaskScheduler());
	}

	return *g_uptrInstance;
}

void MyTaskScheduler::Create()
{
	enki::TaskScheduler* piImpl = dynamic_cast<enki::TaskScheduler*>(m_uptrImpl.get());
	CHECK_TRUE(piImpl != nullptr);
	piImpl->Initialize(THREAD_COUNT);
}

void MyTaskScheduler::AddSingleThreadTask(ISingleThreadTask& inSingleThreadTask)
{
	m_uptrImpl->AddSingleThreadTask(_UnpackImpl(inSingleThreadTask));
}

void MyTaskScheduler::AddMutiThreadTask(IMultiThreadTask& inMultiThreadTask)
{
	m_uptrImpl->AddMutiThreadTask(_UnpackImpl(inMultiThreadTask));
}

void MyTaskScheduler::WaitForTask(const IWaitable& inToWait)
{
	m_uptrImpl->WaitForTask(_UnpackImpl(inToWait));
}

void MyTaskScheduler::WaitForAll()
{
	m_uptrImpl->WaitForAll();
}

void MyTaskScheduler::Destroy()
{
	enki::TaskScheduler* piImpl = dynamic_cast<enki::TaskScheduler*>(m_uptrImpl.get());
	CHECK_TRUE(piImpl != nullptr);
	piImpl->ShutdownNow();
}
