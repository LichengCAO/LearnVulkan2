#include "vertex_input.h"

void VertexInputLayout::SetUpVertex(uint32_t _stride, VkVertexInputRate _inputRate)
{
	m_uStride = _stride;
	m_InputRate = _inputRate;
}

uint32_t VertexInputLayout::AddLocation(VkFormat _format, uint32_t _offset, uint32_t _location)
{
	VkVertexInputAttributeDescription attr{};
	uint32_t ret = (_location == ~0) ? static_cast<uint32_t>(m_Locations.size()) : _location;

	attr.binding = ~0; // unset
	attr.format = _format;
	attr.offset = _offset;
	attr.location = ret;

	m_Locations.push_back(attr);

	return ret;
}

VkVertexInputBindingDescription VertexInputLayout::GetVertexInputBindingDescription(uint32_t _binding) const
{
	VkVertexInputBindingDescription ret{};
	ret.binding = _binding;
	ret.stride = m_uStride;
	ret.inputRate = m_InputRate;
	return ret;
}

std::vector<VkVertexInputAttributeDescription> VertexInputLayout::GetVertexInputAttributeDescriptions(uint32_t _binding) const
{
	std::vector<VkVertexInputAttributeDescription> ret = m_Locations;
	for (int i = 0; i < ret.size(); ++i)
	{
		ret[i].binding = _binding;
	}
	return ret;
}