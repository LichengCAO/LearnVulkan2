#include <common.h>
#include <variant>

class Buffer;
class ImageView;
class AccelStruct;

class DescriptorState final
{
public:
	class BufferInfo
	{
	public:
		void SetBuffer(const Buffer* inBuffer);
		void CustomizeRange(VkDeviceSize inOffset, VkDeviceSize inRange);
	};

	class ImageInfo
	{
	public:
		void SetImageView(const ImageView* inView);
	};

public:
	void SetDescriptor(std::variant<std::pair<uint32_t, uint32_t>, std::string> inDescriptor, const BufferInfo* inBufferInfo);
	void SetDescriptor(std::variant<std::pair<uint32_t, uint32_t>, std::string> inDescriptor, const ImageInfo* inImageInfo);
};

class PushConstantState final
{
public:
	void SetPushConstant(VkPipelineStageFlagBits inStage, const void* inData);
};

class PipelineState
{
public:
	void SetDescriptorState(const DescriptorState* inDescriptorState);
	void SetPushConstantState(const PushConstantState* inPushConstantState);
};