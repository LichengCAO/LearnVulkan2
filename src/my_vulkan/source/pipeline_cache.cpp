#include "pipeline.h"
#include "device.h"
#include "buffer.h"
#include "pipeline_io.h"
#include "utils.h"
#include <fstream>

PipelineCache::~PipelineCache()
{
	CHECK_TRUE(m_vkPipelineCache == VK_NULL_HANDLE);
}

void PipelineCache::Init(const IPipelineCacheInitializer* inInitPtr)
{
	inInitPtr->InitPipelineCache(this);
}

VkPipelineCache PipelineCache::GetVkPipelineCache() const
{
	return m_vkPipelineCache;
}

bool PipelineCache::IsEmptyCache() const
{
	return (!m_fileCacheValid) && m_vkPipelineCache != VK_NULL_HANDLE;
}

void PipelineCache::Uninit()
{
	auto& device = MyDevice::GetInstance();

	device.DestroyPipelineCache(m_vkPipelineCache);

	m_vkPipelineCache = VK_NULL_HANDLE;
	m_fileCacheValid = false;
}

void PipelineCache::SaveCacheToFile(VkPipelineCache inCacheToStore, const std::string& inDstFilePath)
{
	// code from https://mysvac.github.io/vulkan-hpp-tutorial/md/04/11_pipelinecache/
	auto& device = MyDevice::GetInstance();
	std::vector<uint8_t> cacheData;
	std::ofstream out(inDstFilePath, std::ios::binary);

	VK_CHECK(device.GetPipelineCacheData(inCacheToStore, cacheData));
	
	out.write(reinterpret_cast<const char*>(cacheData.data()), cacheData.size());
}

void PipelineCache::Initializer::_LoadBinary(const std::string& inPath, std::vector<uint8_t>& outData) const
{
	// code from https://mysvac.github.io/vulkan-hpp-tutorial/md/04/11_pipelinecache/
	outData.clear();
	if (std::ifstream in(inPath, std::ios::binary | std::ios::ate); in)
	{
		const std::size_t size = in.tellg();
		in.seekg(0);
		outData.resize(size);
		in.read(reinterpret_cast<char*>(outData.data()), size);
	}
}

PipelineCache::Initializer& PipelineCache::Initializer::Reset()
{
	m_file.clear();

	return *this;
}

void PipelineCache::Initializer::InitPipelineCache(PipelineCache* outPipelineCachePtr) const
{
	auto& device = MyDevice::GetInstance();
	VkPipelineCacheCreateInfo createInfo{};
	std::vector<uint8_t> binaryData{};
	VkPipelineCacheHeaderVersionOne* pCacheHeader;

	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
	createInfo.pNext = nullptr;
	createInfo.flags = 0;

	// now, load data from file
	if (!m_file.empty())
	{
		_LoadBinary(m_file, binaryData);
		pCacheHeader = reinterpret_cast<VkPipelineCacheHeaderVersionOne*>(binaryData.data());
		if (device.IsPipelineCacheValid(pCacheHeader))
		{
			createInfo.initialDataSize = binaryData.size();
			createInfo.pInitialData = binaryData.data();
			outPipelineCachePtr->m_fileCacheValid = true;
		}
	}

	outPipelineCachePtr->m_vkPipelineCache = device.CreatePipelineCache(createInfo);
}

PipelineCache::Initializer& PipelineCache::Initializer::CustomizeSourceFile(const std::string& inFilePath)
{
	m_file = inFilePath;

	return *this;
}
