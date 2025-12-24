#pragma once
#include "common.h"
#include <functional>

namespace enki
{
	class LambdaPinnedTask;
	class TaskSet;
	class TaskScheduler;
}
class MyTaskScheduler;

class IWaitable
{
public:
	virtual void WaitForThis(MyTaskScheduler*) const = 0;
};

//-----------------------------------------------------
// 
//				  Single Thread Task
// 
//-----------------------------------------------------
class MySingleThreadTask : public IWaitable
{
protected:
	std::unique_ptr<enki::LambdaPinnedTask> m_uptrTask;

public:
	virtual void WaitForThis(MyTaskScheduler* pSchedular) const override;
	
	virtual void AssignToSchedular(MyTaskScheduler* pSchedular, uint32_t inThreadId = 0);

	virtual void Execute() = 0;
};

class MySingleThreadTaskLambda : public MySingleThreadTask
{
private:
	std::function<void()> m_function;

public:
	MySingleThreadTaskLambda(std::function<void()> lambda);

	virtual void Execute() override;
};

class GraphicsQueueTask : public MySingleThreadTask
{
private:
	std::function<void()> m_function;
	VkQueue m_vkQueue = VK_NULL_HANDLE;

public:
	GraphicsQueueTask(std::function<void()> lambda);

	virtual void AssignToSchedular(MyTaskScheduler* pSchedular, uint32_t inThreadId /* = 0 */) override;

	virtual void Execute() override;
};

class TransferQueueTask : public MySingleThreadTask
{
private:
	std::function<void()> m_function;
	VkQueue m_vkQueue = VK_NULL_HANDLE;
	bool m_useMainThread = false;

public:
	TransferQueueTask(std::function<void()> lambda);

	virtual void AssignToSchedular(MyTaskScheduler* pSchedular, uint32_t inThreadId /* = 0 */) override;

	virtual void Execute() override;
};

//-----------------------------------------------------
// 
//				  Muti-Thread Task
// 
//-----------------------------------------------------
class MyMultiThreadTask : public IWaitable
{
private:
	std::unique_ptr<enki::TaskSet> m_uptrTask;

public:
	virtual void WaitForThis(MyTaskScheduler* pSchedular) const override;

	virtual void AssignToSchedular(MyTaskScheduler* pSchedular, uint32_t inSubTaskCount = 1);

	virtual void ExecuteSubTask(uint32_t SubStart, uint32_t SubEnd, uint32_t inThreadIndex) = 0;
};

class MyMultiThreadTaskLambda : public MyMultiThreadTask
{
private:
	std::function<void(uint32_t, uint32_t, uint32_t)> m_function;

public:
	MyMultiThreadTaskLambda(std::function<void(uint32_t, uint32_t, uint32_t)> lambda);

	virtual void ExecuteSubTask(uint32_t SubStart, uint32_t SubEnd, uint32_t inThreadIndex) override;
};

//-----------------------------------------------------
// 
//				    Task Scheduler
// 
//-----------------------------------------------------
class MyTaskScheduler
{
private:
	static std::unique_ptr<MyTaskScheduler> g_uptrInstance;
	MyTaskScheduler();

public:
	static MyTaskScheduler& GetInstance();

private:
	std::unique_ptr <enki::TaskScheduler> m_uptrTaskSchedular;
	uint32_t m_threadCount;

public:
	~MyTaskScheduler();

	void Init();

	void AddSingleThreadTask(MySingleThreadTask* pSingleThreadTask, uint32_t inThreadIndex = 0);

	void AddMutiThreadTask(MyMultiThreadTask* pMultiThreadTask, uint32_t inSubTaskCount = 1);

	void WaitForTask(const IWaitable* pToWait);

	void WaitForAll();

	void Uninit();

	friend class MySingleThreadTask;
	friend class MyMultiThreadTask;
	friend class GraphicsQueueTask;
	friend class TransferQueueTask;
};