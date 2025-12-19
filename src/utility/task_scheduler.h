#pragma once
#include <enkiTS/src/TaskScheduler.h>

class MyTaskScheduler
{
public:

};

class IPinnedTask
{
public:
	virtual void Execute() = 0;
	virtual ~IPinnedTask() {};
};

class RunPinnedTaskLoopTask : public IPinnedTask
{
private:
	MyTaskScheduler* m_pTaskScheduler = nullptr;

public:
	bool execute = true;

public:
	virtual void Execute() override;
};

class AsynchronizeLoadTask : public IPinnedTask
{
private:
	MyTaskScheduler* m_pTaskScheduler = nullptr;

public:
	bool execute = true;

public:
	virtual void Execute() override;
};