#pragma once
#include <enkiTS/src/TaskScheduler.h>

class MyTaskScheduler
{
public:

};

class RunPinnedTaskLoopTask
{
private:
	MyTaskScheduler* m_pTaskScheduler = nullptr;

public:
	bool execute = true;

public:
	void Execute();
};

class AsynchronizeLoadTask
{
private:
	MyTaskScheduler* m_pTaskScheduler = nullptr;

public:
	bool execute = true;

public:
	void Execute();
};