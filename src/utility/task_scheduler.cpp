#include "task_scheduler.h"
#include "device.h"
#include <TaskScheduler.h>

namespace
{
	template<typename T>
	T* _UnpackImpl(T* inPtr)
	{
		T* piUnpacked{ inPtr };
		IImplement* piImpl{ dynamic_cast<IImplement*>(piUnpacked) };

		// see if this has implement behind?
		while (piImpl != nullptr)
		{
			piUnpacked = static_cast<T*>(piImpl->GetRealImplement());
			piImpl = dynamic_cast<IImplement*>(piUnpacked);
		}

		return piUnpacked;
	}

	template<typename T>
	const T* _UnpackImpl(const T* inPtr)
	{
		const T* piUnpacked{ inPtr };
		IImplement* piImpl{ dynamic_cast<IImplement*>(piUnpacked) };

		// see if this has implement behind?
		while (piImpl != nullptr)
		{
			piUnpacked = static_cast<const T*>(piImpl->GetRealImplement());
			piImpl = dynamic_cast<IImplement*>(piUnpacked);
		}

		return piUnpacked;
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
	virtual void DependOn(IWaitable* pPrecondition) override;
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
	virtual void DependOn(IWaitable* pPrecondition) override;
	virtual void ExecuteSubTask(uint32_t SubStart, uint32_t SubEnd, uint32_t inThreadIndex) override;
	virtual void ExecuteRange(enki::TaskSetPartition inRange, uint32_t inThreadnum) override;
};

class MyTaskSchedulerImpl final : public ITaskScheduler, public enki::TaskScheduler
{
public:
	virtual void AddSingleThreadTask(ISingleThreadTask* pSingleThreadTask) override;
	virtual void AddMutiThreadTask(IMultiThreadTask* pMultiThreadTask) override;
	virtual void WaitForTask(const IWaitable* pToWait) override;
	virtual void WaitForAll() override;
};

void MySinglThreadTaskImpl::DependOn(IWaitable* pPrecondition)
{
	enki::ICompletable* pCompletable = dynamic_cast<enki::ICompletable*>(pPrecondition);
	CHECK_TRUE(pCompletable != nullptr);
	m_dependencies.push_back({});
	this->SetDependency(m_dependencies.back(), pCompletable);
}

void MySinglThreadTaskImpl::Execute()
{
	m_function();
}

void MyMultiThreadTaskImpl::DependOn(IWaitable* pPrecondition)
{
	IImplement* pImpl = dynamic_cast<IImplement*>(pPrecondition);
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

void MyTaskSchedulerImpl::AddSingleThreadTask(ISingleThreadTask* pSingleThreadTask)
{
	enki::IPinnedTask* piPinned = dynamic_cast<enki::IPinnedTask*>(pSingleThreadTask);
	CHECK_TRUE(piPinned != nullptr);
	this->AddPinnedTask(piPinned);
}

void MyTaskSchedulerImpl::AddMutiThreadTask(IMultiThreadTask* pMultiThreadTask)
{
	enki::ITaskSet* piTaskSet = dynamic_cast<enki::ITaskSet*>(pMultiThreadTask);
	CHECK_TRUE(piTaskSet != nullptr);
	this->AddTaskSetToPipe(piTaskSet);
}

void MyTaskSchedulerImpl::WaitForTask(const IWaitable* pToWait)
{
	const enki::ICompletable* piComplete = dynamic_cast<const enki::ICompletable*>(pToWait);
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

void MySinglThreadTask::DependOn(IWaitable* pPrecondition)
{
	m_uptrImpl->DependOn(_UnpackImpl(pPrecondition));
}

void MySinglThreadTask::Execute()
{
	m_uptrImpl->Execute();
}

void* MySinglThreadTask::GetRealImplement()
{
	return m_uptrImpl.get();
}

MyMultiThreadTask::MyMultiThreadTask(std::function<void(uint32_t, uint32_t, uint32_t)> inFunction, uint32_t inSubTaskCount)
	:m_uptrImpl(std::make_unique<MyMultiThreadTaskImpl>(inFunction, inSubTaskCount))
{
}

void MyMultiThreadTask::DependOn(IWaitable* pPrecondition)
{
	m_uptrImpl->DependOn(_UnpackImpl(pPrecondition));
}

void MyMultiThreadTask::ExecuteSubTask(uint32_t SubStart, uint32_t SubEnd, uint32_t inThreadIndex)
{
	m_uptrImpl->ExecuteSubTask(SubStart, SubEnd, inThreadIndex);
}

void* MyMultiThreadTask::GetRealImplement()
{
	return m_uptrImpl.get();
}

MyTaskScheduler::MyTaskScheduler()
	:m_uptrImpl(std::make_unique<MyTaskSchedulerImpl>())
{
}

MyTaskScheduler& MyTaskScheduler::GetInstance()
{
	if (g_uptrInstance == nullptr)
	{
		g_uptrInstance = std::make_unique<MyTaskScheduler>();
	}

	return *g_uptrInstance;
}

void MyTaskScheduler::Init()
{
	enki::TaskScheduler* piImpl = dynamic_cast<enki::TaskScheduler*>(m_uptrImpl.get());
	CHECK_TRUE(piImpl != nullptr);
	piImpl->Initialize(4);
}

void MyTaskScheduler::AddSingleThreadTask(ISingleThreadTask* pSingleThreadTask)
{
	m_uptrImpl->AddSingleThreadTask(_UnpackImpl(pSingleThreadTask));
}

void MyTaskScheduler::AddMutiThreadTask(IMultiThreadTask* pMultiThreadTask)
{
	m_uptrImpl->AddMutiThreadTask(_UnpackImpl(pMultiThreadTask));
}

void MyTaskScheduler::WaitForTask(const IWaitable* pToWait)
{
	m_uptrImpl->WaitForTask(_UnpackImpl(pToWait));
}

void MyTaskScheduler::WaitForAll()
{
	m_uptrImpl->WaitForAll();
}

void MyTaskScheduler::Uninit()
{
	enki::TaskScheduler* piImpl = dynamic_cast<enki::TaskScheduler*>(m_uptrImpl.get());
	CHECK_TRUE(piImpl != nullptr);
	piImpl->ShutdownNow();
}
