#include "task_scheduler.h"
#include <TaskScheduler.h>

MyTaskScheduler::MyTaskScheduler()
	:m_threadCount(4)
{
	m_uptrTaskSchedular = std::make_unique<enki::TaskScheduler>();
	m_uptrTaskSchedular->Initialize(m_threadCount);
}

MyTaskScheduler& MyTaskScheduler::GetInstance()
{
	if (g_uptrInstance == nullptr)
	{
		g_uptrInstance = std::make_unique<MyTaskScheduler>();
	}
	return *g_uptrInstance;
}

MyTaskScheduler::~MyTaskScheduler()
{
}

void MyTaskScheduler::AddSingleThreadTask(MySingleThreadTask* pSingleThreadTask, uint32_t inThreadIndex)
{
	pSingleThreadTask->AssignToSchedular(this, inThreadIndex);
}

void MyTaskScheduler::AddMutiThreadTask(MyMultiThreadTask* pMultiThreadTask, uint32_t inSubTaskCount)
{
	pMultiThreadTask->AssignToSchedular(this, inSubTaskCount);
}

void MyTaskScheduler::WaitForTask(const IWaitable* pToWait)
{
	pToWait->WaitForThis(this);
}

void MyTaskScheduler::WaitForAll()
{
	m_uptrTaskSchedular->WaitforAll();
}

void MySingleThreadTask::WaitForThis(MyTaskScheduler* pSchedular) const
{
	pSchedular->m_uptrTaskSchedular->WaitforTask(m_uptrTask.get());
}

void MySingleThreadTask::AssignToSchedular(MyTaskScheduler* pSchedular, uint32_t inThreadId)
{
	if (m_uptrTask == nullptr)
	{
		m_uptrTask = std::make_unique<enki::LambdaPinnedTask>(inThreadId, [&]()
			{
				this->Execute();
			}
		);
	}
	pSchedular->m_uptrTaskSchedular->AddPinnedTask(m_uptrTask.get());
}

void MyMultiThreadTask::WaitForThis(MyTaskScheduler* pSchedular) const
{
	pSchedular->m_uptrTaskSchedular->WaitforTask(m_uptrTask.get());
}

void MyMultiThreadTask::AssignToSchedular(MyTaskScheduler* pSchedular, uint32_t inSubTaskCount)
{
	if (m_uptrTask == nullptr)
	{
		m_uptrTask = std::make_unique<enki::TaskSet>(inSubTaskCount, [&](enki::TaskSetPartition inRange, uint32_t inThreadId)
			{
				this->ExecuteSubTask(inRange.start, inRange.end, inThreadId);
			}
		);
	}
	pSchedular->m_uptrTaskSchedular->AddTaskSetToPipe(m_uptrTask.get());
}

MySingleThreadTaskLambda::MySingleThreadTaskLambda(std::function<void()> lambda)
	:m_function(lambda)
{
}

void MySingleThreadTaskLambda::Execute()
{
	m_function();
}

MyMultiThreadTaskLambda::MyMultiThreadTaskLambda(std::function<void(uint32_t, uint32_t, uint32_t)> lambda)
	:m_function(lambda)
{
}

void MyMultiThreadTaskLambda::ExecuteSubTask(uint32_t SubStart, uint32_t SubEnd, uint32_t inThreadIndex)
{
	m_function(SubStart, SubEnd, inThreadIndex);
}
