#pragma once
#include "common.h"

class PipelineCache;

class IPipelineCacheInitializer
{
public:
	virtual void InitPipelineCache(PipelineCache* outPipelineCachePtr) const = 0;
};

class PipelineCache
{
private:
	VkPipelineCache m_vkPipelineCache = VK_NULL_HANDLE;
	bool m_fileCacheValid = false;

public:
	class Initializer : public IPipelineCacheInitializer
	{
	private:
		std::string& m_file;

		void _LoadBinary(const std::string& inPath, std::vector<uint8_t>& outData) const;

	public:
		virtual void InitPipelineCache(PipelineCache* outPipelineCachePtr) const override;

		PipelineCache::Initializer& Reset();

		PipelineCache::Initializer& CustomizeSourceFile(const std::string& inFilePath);
	};

public:
	~PipelineCache();

	// Init device object
	void Init(const IPipelineCacheInitializer* inInitPtr);

	// Return device handle
	VkPipelineCache GetVkPipelineCache() const;

	// Return whether the current cache is empty and will be written by pipeline creation.
	// Return false if we set source file before initialization, and we successfully load a valid pipeline cache from it
	bool IsEmptyCache() const;

	// Destroy device object
	void Uninit();

	static void SaveCacheToFile(VkPipelineCache inCacheToStore, const std::string& inDstFilePath);
};