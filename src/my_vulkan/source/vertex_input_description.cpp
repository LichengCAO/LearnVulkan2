#include "vertex_input.h"

VertexInputDescriptionBuilder& VertexInputDescriptionBuilder::Reset()
{
	*this = VertexInputDescriptionBuilder{};

	return *this;
}

VertexInputDescriptionBuilder& VertexInputDescriptionBuilder::SetVertexStrideAndInputRate(uint32_t _stride, VkVertexInputRate _inputRate)
{
	m_uStride = _stride;
	m_InputRate = _inputRate;

	return *this;
}

VertexInputDescriptionBuilder& VertexInputDescriptionBuilder::AddLocation(VkFormat _format, uint32_t _offset, uint32_t _location)
{
	VkVertexInputAttributeDescription attr{};

	attr.binding = ~0; // unset
	attr.format = _format;
	attr.offset = _offset;
	attr.location = _location;

	m_Locations.push_back(attr);

	return *this;
}

void VertexInputDescriptionBuilder::BuildDescription(uint32_t inBinding, VkVertexInputBindingDescription& outBindingDescription, std::vector<VkVertexInputAttributeDescription>& outAttributeDescriptions) const
{
	std::vector<VkVertexInputAttributeDescription> ret = m_Locations;

	outBindingDescription.binding = inBinding;
	outBindingDescription.stride = m_uStride;
	outBindingDescription.inputRate = m_InputRate;

	for (size_t i = 0; i < ret.size(); ++i)
	{
		ret[i].binding = inBinding;
	}

	outAttributeDescriptions.insert(outAttributeDescriptions.end(), ret.begin(), ret.end());
}