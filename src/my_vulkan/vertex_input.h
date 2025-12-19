#pragma once
#include "common.h"
// Graphics pipeline can have multiple vertex bindings,
// i.e. layout(binding = 0, location = 0); layout(binding = 1, location = 0)... ==> wrong, see: https://gist.github.com/SaschaWillems/428d15ed4b5d71ead462bc63adffa93a
// This class helps to describe vertex attribute layout that belongs to the same binding
// in a graphics pipeline, I use this to create a graphics pipeline.
// This class doesn't hold any Vulkan handle,
// and can be destroyed after the construction of the graphics pipeline.
class VertexInputLayout final
{
private:
	uint32_t          m_uStride = 0;
	VkVertexInputRate m_InputRate = VK_VERTEX_INPUT_RATE_VERTEX;
	std::vector<VkVertexInputAttributeDescription> m_Locations;

public:
	// Set up per Vertex Description information
	void SetUpVertex(uint32_t _stride, VkVertexInputRate _inputRate = VK_VERTEX_INPUT_RATE_VERTEX);

	// Add location for this vertex binding, return the index of this location.
	// Then you can use it in layout(binding = ..., location = <return>) in the shader
	// _format: input, format of the input
	// _offset: input, offset of the data of VBO from host
	// _location: input, should match layout(location = ...) in shader, if it's ~0, then it will be the count of locations set minus 1
	uint32_t AddLocation(VkFormat _format, uint32_t _offset, uint32_t _location = ~0);

	// Call after the SetUpVertex() and AddLocation() are called, used in graphics pipeline creation
	VkVertexInputBindingDescription	GetVertexInputBindingDescription(uint32_t _binding = 0) const;

	// Call after the SetUpVertex() and AddLocation() are called, used in graphics pipeline creation
	std::vector<VkVertexInputAttributeDescription>	GetVertexInputAttributeDescriptions(uint32_t _binding = 0) const;
};