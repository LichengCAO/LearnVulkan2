#include "image_storage.h"
namespace VulkanDetail
{
ImageResource::ImageResource(std::unique_ptr<ImageStorage> storage) : m_storage(std::move(storage)) {}
ImageResource::~ImageResource() noexcept = default;
auto ImageResource::GetHandle() const noexcept -> VkImage { return m_storage->image; }
auto ImageResource::GetDevice() const noexcept -> VkDevice { return m_storage->device; }
}
