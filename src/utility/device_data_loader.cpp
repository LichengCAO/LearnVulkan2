#include "device_data_loader.h"
#include "buffer.h"
#include "device.h"
#include "commandbuffer.h"

void AsyncDeviceDataLoader::_CopyToStagingBuffer(const void* inSrcData, size_t inSize)
{
	m_uptrBuffer->CopyFromHost(inSrcData, 0, inSize);
}

void AsyncDeviceDataLoader::Init()
{
	auto& device = MyDevice::GetInstance();
	Buffer::Initializer bufferInit{};
	VkCommandPoolCreateInfo cmdPoolCreateInfo{ VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };

	m_vkFence = device.CreateFence(VK_FENCE_CREATE_SIGNALED_BIT);

	cmdPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmdPoolCreateInfo.queueFamilyIndex = device.GetQueueFamilyIndexOfType(QueueFamilyType::TRANSFER);
	m_vkCommandPool = device.CreateCommandPool(cmdPoolCreateInfo);
	
	m_uptrBuffer = std::make_unique<Buffer>();
	bufferInit.CustomizeMemoryProperty(VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
	bufferInit.SetBufferSize(64 * 1024 * 1024);
	bufferInit.SetBufferUsage(VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
	m_uptrBuffer->Init(bufferInit);

	m_uptrCommandBuffer = std::make_unique<CommandBuffer>();
	m_uptrCommandBuffer->PreSetCommandPool(m_vkCommandPool);
	m_uptrCommandBuffer->Init();
}
