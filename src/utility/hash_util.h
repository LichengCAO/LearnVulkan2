#pragma once
#include <common.h>

// Based on resource_caching.h from Vulkan-Samples: https://github.com/KhronosGroup/Vulkan-Samples/tree/main

template <class T>
inline void hash_combine(size_t& seed, const T& v)
{
	std::hash<T> hasher;
	glm::detail::hash_combine(seed, hasher(v));
}

inline const VkWriteDescriptorSetAccelerationStructureKHR* find_descriptor_acceleration_structure_write_info(const void* pNext)
{
	const VkBaseInStructure* current = static_cast<const VkBaseInStructure*>(pNext);
	while (current != nullptr)
	{
		if (current->sType == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR)
		{
			return reinterpret_cast<const VkWriteDescriptorSetAccelerationStructureKHR*>(current);
		}
		current = current->pNext;
	}
	return nullptr;
}

namespace std
{
	template <>
	struct hash<VkDescriptorBufferInfo>
	{
		std::size_t operator()(const VkDescriptorBufferInfo& descriptor_buffer_info) const
		{
			std::size_t result = 0;

			::hash_combine(result, descriptor_buffer_info.buffer);
			::hash_combine(result, descriptor_buffer_info.range);
			::hash_combine(result, descriptor_buffer_info.offset);

			return result;
		}
	};

	template <>
	struct hash<VkDescriptorImageInfo>
	{
		std::size_t operator()(const VkDescriptorImageInfo& descriptor_image_info) const
		{
			std::size_t result = 0;

			::hash_combine(result, descriptor_image_info.imageView);
			::hash_combine(result, static_cast<std::underlying_type<VkImageLayout>::type>(descriptor_image_info.imageLayout));
			::hash_combine(result, descriptor_image_info.sampler);

			return result;
		}
	};

	template <>
	struct hash<VkWriteDescriptorSet>
	{
		std::size_t operator()(const VkWriteDescriptorSet& write_descriptor_set) const
		{
			std::size_t result = 0;

			::hash_combine(result, write_descriptor_set.dstSet);
			::hash_combine(result, write_descriptor_set.dstBinding);
			::hash_combine(result, write_descriptor_set.dstArrayElement);
			::hash_combine(result, write_descriptor_set.descriptorCount);
			::hash_combine(result, write_descriptor_set.descriptorType);

			switch (write_descriptor_set.descriptorType)
			{
			case VK_DESCRIPTOR_TYPE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
			case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
			case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
			case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
				for (uint32_t i = 0; i < write_descriptor_set.descriptorCount; i++)
				{
					::hash_combine(result, write_descriptor_set.pImageInfo[i]);
				}
				break;

			case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
				for (uint32_t i = 0; i < write_descriptor_set.descriptorCount; i++)
				{
					::hash_combine(result, write_descriptor_set.pTexelBufferView[i]);
				}
				break;

			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
			case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
			case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
				for (uint32_t i = 0; i < write_descriptor_set.descriptorCount; i++)
				{
					::hash_combine(result, write_descriptor_set.pBufferInfo[i]);
				}
				break;

			case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
			{
				const auto* pAccelWriteInfo = ::find_descriptor_acceleration_structure_write_info(write_descriptor_set.pNext);
				CHECK_TRUE(pAccelWriteInfo != nullptr, "Missing acceleration structure descriptor write info!");
				::hash_combine(result, pAccelWriteInfo->accelerationStructureCount);
				for (uint32_t i = 0; i < pAccelWriteInfo->accelerationStructureCount; i++)
				{
					::hash_combine(result, pAccelWriteInfo->pAccelerationStructures[i]);
				}
			}
				break;

			default:
				// Not implemented
				break;
			};

			return result;
		}
	};

	template <>
	struct hash<VkVertexInputAttributeDescription>
	{
		std::size_t operator()(const VkVertexInputAttributeDescription& vertex_attrib) const
		{
			std::size_t result = 0;

			::hash_combine(result, vertex_attrib.binding);
			::hash_combine(result, static_cast<std::underlying_type<VkFormat>::type>(vertex_attrib.format));
			::hash_combine(result, vertex_attrib.location);
			::hash_combine(result, vertex_attrib.offset);

			return result;
		}
	};

	template <>
	struct hash<VkVertexInputBindingDescription>
	{
		std::size_t operator()(const VkVertexInputBindingDescription& vertex_binding) const
		{
			std::size_t result = 0;

			::hash_combine(result, vertex_binding.binding);
			::hash_combine(result, static_cast<std::underlying_type<VkVertexInputRate>::type>(vertex_binding.inputRate));
			::hash_combine(result, vertex_binding.stride);

			return result;
		}
	};

	template <>
	struct hash<VkExtent2D>
	{
		size_t operator()(const VkExtent2D& extent) const
		{
			size_t result = 0;

			::hash_combine(result, extent.width);
			::hash_combine(result, extent.height);

			return result;
		}
	};

	template <>
	struct hash<VkOffset2D>
	{
		size_t operator()(const VkOffset2D& offset) const
		{
			size_t result = 0;

			::hash_combine(result, offset.x);
			::hash_combine(result, offset.y);

			return result;
		}
	};

	template <>
	struct hash<VkRect2D>
	{
		size_t operator()(const VkRect2D& rect) const
		{
			size_t result = 0;

			::hash_combine(result, rect.extent);
			::hash_combine(result, rect.offset);

			return result;
		}
	};

	template <>
	struct hash<VkViewport>
	{
		size_t operator()(const VkViewport& viewport) const
		{
			size_t result = 0;

			::hash_combine(result, viewport.width);
			::hash_combine(result, viewport.height);
			::hash_combine(result, viewport.maxDepth);
			::hash_combine(result, viewport.minDepth);
			::hash_combine(result, viewport.x);
			::hash_combine(result, viewport.y);

			return result;
		}
	};


	//template <>
	//struct hash<::PipelineState>
	//{
	//	std::size_t operator()(const ::PipelineState& pipeline_state) const
	//	{
	//		std::size_t result = 0;

	//		::hash_combine(result, pipeline_state.get_pipeline_layout().get_handle());

	//		// For graphics only
	//		if (auto render_pass = pipeline_state.get_render_pass())
	//		{
	//			::hash_combine(result, render_pass->get_handle());
	//		}

	//		::hash_combine(result, pipeline_state.get_specialization_constant_state());

	//		::hash_combine(result, pipeline_state.get_subpass_index());

	//		for (auto shader_module : pipeline_state.get_pipeline_layout().get_shader_modules())
	//		{
	//			::hash_combine(result, shader_module->get_id());
	//		}

	//		// VkPipelineVertexInputStateCreateInfo
	//		for (auto& attribute : pipeline_state.get_vertex_input_state().attributes)
	//		{
	//			::hash_combine(result, attribute);
	//		}

	//		for (auto& binding : pipeline_state.get_vertex_input_state().bindings)
	//		{
	//			::hash_combine(result, binding);
	//		}

	//		// VkPipelineInputAssemblyStateCreateInfo
	//		::hash_combine(result, pipeline_state.get_input_assembly_state().primitive_restart_enable);
	//		::hash_combine(result, static_cast<std::underlying_type<VkPrimitiveTopology>::type>(pipeline_state.get_input_assembly_state().topology));

	//		// VkPipelineViewportStateCreateInfo
	//		::hash_combine(result, pipeline_state.get_viewport_state().viewport_count);
	//		::hash_combine(result, pipeline_state.get_viewport_state().scissor_count);

	//		// VkPipelineRasterizationStateCreateInfo
	//		::hash_combine(result, pipeline_state.get_rasterization_state().cull_mode);
	//		::hash_combine(result, pipeline_state.get_rasterization_state().depth_bias_enable);
	//		::hash_combine(result, pipeline_state.get_rasterization_state().depth_clamp_enable);
	//		::hash_combine(result, static_cast<std::underlying_type<VkFrontFace>::type>(pipeline_state.get_rasterization_state().front_face));
	//		::hash_combine(result, static_cast<std::underlying_type<VkPolygonMode>::type>(pipeline_state.get_rasterization_state().polygon_mode));
	//		::hash_combine(result, pipeline_state.get_rasterization_state().rasterizer_discard_enable);

	//		// VkPipelineMultisampleStateCreateInfo
	//		::hash_combine(result, pipeline_state.get_multisample_state().alpha_to_coverage_enable);
	//		::hash_combine(result, pipeline_state.get_multisample_state().alpha_to_one_enable);
	//		::hash_combine(result, pipeline_state.get_multisample_state().min_sample_shading);
	//		::hash_combine(result, static_cast<std::underlying_type<VkSampleCountFlagBits>::type>(pipeline_state.get_multisample_state().rasterization_samples));
	//		::hash_combine(result, pipeline_state.get_multisample_state().sample_shading_enable);
	//		::hash_combine(result, pipeline_state.get_multisample_state().sample_mask);

	//		// VkPipelineDepthStencilStateCreateInfo
	//		::hash_combine(result, pipeline_state.get_depth_stencil_state().back);
	//		::hash_combine(result, pipeline_state.get_depth_stencil_state().depth_bounds_test_enable);
	//		::hash_combine(result, static_cast<std::underlying_type<VkCompareOp>::type>(pipeline_state.get_depth_stencil_state().depth_compare_op));
	//		::hash_combine(result, pipeline_state.get_depth_stencil_state().depth_test_enable);
	//		::hash_combine(result, pipeline_state.get_depth_stencil_state().depth_write_enable);
	//		::hash_combine(result, pipeline_state.get_depth_stencil_state().front);
	//		::hash_combine(result, pipeline_state.get_depth_stencil_state().stencil_test_enable);

	//		// VkPipelineColorBlendStateCreateInfo
	//		::hash_combine(result, static_cast<std::underlying_type<VkLogicOp>::type>(pipeline_state.get_color_blend_state().logic_op));
	//		::hash_combine(result, pipeline_state.get_color_blend_state().logic_op_enable);

	//		for (auto& attachment : pipeline_state.get_color_blend_state().attachments)
	//		{
	//			::hash_combine(result, attachment);
	//		}

	//		return result;
	//	}
	//};
}        // namespace std
