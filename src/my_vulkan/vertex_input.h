#pragma once
#include "common.h"
// Graphics pipeline can have multiple vertex bindings,
// i.e. layout(binding = 0, location = 0); layout(binding = 1, location = 0)... ==> wrong, see: https://gist.github.com/SaschaWillems/428d15ed4b5d71ead462bc63adffa93a
// This class helps to describe vertex attribute layout that belongs to the same binding
// in a graphics pipeline, I use this to create a graphics pipeline.
// This class doesn't hold any Vulkan handle,
// and can be destroyed after the construction of the graphics pipeline.
class VertexInputDescriptionBuilder final
{
private:
	uint32_t          m_uStride = 0;
	VkVertexInputRate m_InputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	std::vector<VkVertexInputAttributeDescription> m_Locations;

public:
	VertexInputDescriptionBuilder& Reset();

	// Set up per Vertex Description information
	VertexInputDescriptionBuilder& SetVertexStrideAndInputRate(uint32_t inStride, VkVertexInputRate inInputRate = VK_VERTEX_INPUT_RATE_VERTEX);

	// Add location for this vertex binding.
	// Then this location will read data from binding buffer with input format
	// inFormat: input, format of the input
	// inOffset: input, offset of the data of VBO from host
	// inLocation: input, should match layout(location = ...) in shader
	VertexInputDescriptionBuilder& AddLocation(VkFormat inFormat, uint32_t inOffset, uint32_t inLocation);

	void BuildDescription(
		uint32_t inBinding,
		VkVertexInputBindingDescription& outBindingDescription, 
		std::vector<VkVertexInputAttributeDescription>& outAttributeDescriptions) const;
};