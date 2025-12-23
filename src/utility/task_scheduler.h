#pragma once
#include <enkiTS/src/TaskScheduler.h>

class MyTaskScheduler;

class IWaitable
{
public:
	virtual void WaitForThis(MyTaskScheduler*) = 0;
};

class ISingleThreadTask : public IWaitable
{
public:
	virtual void AssignToThread(MyTaskScheduler*) = 0;
	virtual void Execute() = 0;
};

class IMultiThreadTask : public IWaitable
{
public:
	virtual void AssignToThreadGroup(MyTaskScheduler*) = 0;
	virtual void Execute(uint32_t inThreadIndex) = 0;
};

class MyTaskScheduler final
{
private:
	static std::unique_ptr<MyTaskScheduler> m_uptrInstance;

	MyTaskScheduler();

public:
	~MyTaskScheduler();

	static MyTaskScheduler& GetInstance();

	void WaitForTask(const IWaitable* pToWait);


	void WaitForAll();
};