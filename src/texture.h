#ifndef TEXTURE_H
#define TEXTURE_H

/*
 * stb_image.h must already be included.
 */

#include <vulkan/vulkan.h>

#include "external/render-c/src/buffer.h"
#include "external/render-c/src/cbuf.h"
#include "external/render-c/src/image.h"
#include "external/render-c/src/set.h"

#include <stdint.h>
#include <vulkan/vulkan_core.h>

const VkFormat TEXTURE_FORMAT = VK_FORMAT_B8G8R8A8_SRGB;

// Expects all mip levels to be TRANSFER_DST_OPTIMAL, and transfers all mip levels to
// SHADER_READ_ONLY_OPTIMAL.
void texture_generate_mipmaps(VkDevice device, VkQueue queue, VkCommandPool cpool,
                              VkImage image, int32_t width, int32_t height, uint32_t levels)
{
	VkCommandBuffer cbuf;
	cbuf_alloc(device, cpool, &cbuf);
	cbuf_begin_onetime(cbuf);

	// Copy mip chain
	int32_t cur_width = width, cur_height = height;
	for (uint32_t i = 1; i < levels; i++) {
        	// Transition previous mip level to TRANSFER_SRC
        	cbuf_barrier_image(cbuf, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, i - 1,
        	                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        	                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
        	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

		// Blit scaled image
        	VkImageBlit blit = {0};
        	blit.srcOffsets[0] = (VkOffset3D){0, 0, 0};
        	blit.srcOffsets[1] = (VkOffset3D){cur_width, cur_height, 1};
        	blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        	blit.srcSubresource.mipLevel = i - 1;
        	blit.srcSubresource.baseArrayLayer = 0;
        	blit.srcSubresource.layerCount = 1;
        	blit.dstOffsets[0] = (VkOffset3D){0, 0, 0};
        	blit.dstOffsets[1] = (VkOffset3D){cur_width > 1 ? cur_width / 2 : 1,
                                                  cur_height > 1 ? cur_height / 2 : 1,
                                                  1};
        	blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        	blit.dstSubresource.mipLevel = i;
        	blit.dstSubresource.baseArrayLayer = 0;
        	blit.dstSubresource.layerCount = 1;
        	vkCmdBlitImage(cbuf, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        	               image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

        	if (cur_width > 1) cur_width /= 2;
        	if (cur_height > 1) cur_height /= 2;

        	// Transition previous image to SHADER_READ_ONLY_OPTIMAL
        	cbuf_barrier_image(cbuf, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, i - 1,
        	                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        	                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
	}

	// Transition last image to SHADER_READ_ONLY_OPTIMAL
	cbuf_barrier_image(cbuf, image, VK_IMAGE_ASPECT_COLOR_BIT, 1, levels - 1,
	                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	                   VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
	                   VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

	cbuf_submit_wait(queue, cbuf);
	vkFreeCommandBuffers(device, cpool, 1, &cbuf);
}

// Creates mipmaps too.
void texture_set_from_path(VkPhysicalDevice phys_dev, VkDevice device, VkQueue queue,
                           VkCommandPool cpool, VkDescriptorPool dpool, VkSampler sampler,
                           const char* path, struct Image* texture,
			   VkDescriptorSetLayout set_layout, VkDescriptorSet* set)
{
        int width, height, channels;
	stbi_uc* pixels = stbi_load(path, &width, &height, &channels, STBI_rgb_alpha);

	if (pixels == NULL) {
        	fprintf(stderr, "Could not load %s!\n", path);
        	// Replace with a single pink pixel
        	width = 1;
        	height = 1;
        	pixels = malloc(4);
        	pixels[0] = 1.0F;
        	pixels[1] = 0.0F;
        	pixels[2] = 1.0F;
        	pixels[3] = 1.0F;
	}

	int tex_size = 4 * width * height;
	int mip_levels = 1;
	while (width >= (1 << mip_levels) || height >= (1 << mip_levels)) mip_levels++;

	// Create source buffer
	struct Buffer tex_buf;
	buffer_create(phys_dev, device, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
	              tex_size, &tex_buf);
	mem_write(device, tex_buf.mem, tex_size, pixels);
	stbi_image_free(pixels);

	// Create texture
	image_create(phys_dev, device, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_TYPE_2D, width, height, 1,
	             VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_ASPECT_COLOR_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		     VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT, mip_levels, VK_SAMPLE_COUNT_1_BIT, texture);

	// Copy to texture
	image_trans(device, queue, cpool, texture->handle, VK_IMAGE_ASPECT_COLOR_BIT,
	            VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
	            0, VK_ACCESS_TRANSFER_WRITE_BIT,
	            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, mip_levels);

	image_copy_from_buffer(device, queue, cpool, VK_IMAGE_ASPECT_COLOR_BIT,
	                       tex_buf.handle, texture->handle, width, height, 1);

	texture_generate_mipmaps(device, queue, cpool, texture->handle, width, height, mip_levels);

	buffer_destroy(device, &tex_buf);

	// Create set
        struct DescriptorInfo tex_desc = {0};
        tex_desc.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        tex_desc.shader_stage_flags = VK_SHADER_STAGE_FRAGMENT_BIT;
        tex_desc.image.sampler = sampler;
        tex_desc.image.imageView = texture->view;
        tex_desc.image.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

	struct SetInfo set_info = {0};
	set_info.desc_ct = 1;
	set_info.descs = &tex_desc;
	
        set_create(device, dpool, set_layout, &set_info, set);
}

#endif // TEXTURE_H
