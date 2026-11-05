/*
* Vulkan Example - Implements a separable two-pass fullscreen blur (also known as bloom)
*
* Copyright (C) 2016 - 2025 Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"
#include "ztgfx_parity.h"


// Offscreen frame buffer properties
#define FB_DIM 256
#define FB_COLOR_FORMAT VK_FORMAT_R8G8B8A8_UNORM

class VulkanExample : public VulkanExampleBase
{
public:
	bool bloom = true;
	ztgfx::Parity parity;
	VkFormat offscreenDepthFormat = VK_FORMAT_UNDEFINED;

	vks::TextureCubeMap cubemap;

	struct {
		vkglTF::Model ufo;
		vkglTF::Model ufoGlow;
		vkglTF::Model skyBox;
	} models;

	struct UBO {
		glm::mat4 projection;
		glm::mat4 view;
		glm::mat4 model;
	};

	struct UBOBlurParams {
		float blurScale = 1.0f;
		float blurStrength = 1.5f;
	};

	struct {
		UBO scene, skyBox;
		UBOBlurParams blurParams;
	} ubos;

	struct UniformBuffers {
		vks::Buffer scene;
		vks::Buffer skyBox;
		vks::Buffer blurParams;
	};
	std::array<UniformBuffers, maxConcurrentFrames> uniformBuffers{};

	struct {
		VkPipelineLayout blur;
		VkPipelineLayout scene;
	} pipelineLayouts{};

	struct {
		VkPipeline blurVert;
		VkPipeline blurHorz;
		VkPipeline glowPass;
		VkPipeline phongPass;
		VkPipeline skyBox;
	} pipelines{};

	struct {
		VkDescriptorSetLayout blur;
		VkDescriptorSetLayout scene;
	} descriptorSetLayouts{};

	struct DescriptorSets {
		VkDescriptorSet blurVert;
		VkDescriptorSet blurHorz;
		VkDescriptorSet scene;
		VkDescriptorSet skyBox;
	};
	std::array<DescriptorSets, maxConcurrentFrames> descriptorSets{};

	// Framebuffer for offscreen rendering
	struct FrameBufferAttachment {
		VkImage image;
		VkDeviceMemory mem;
		VkImageView view;
	};
	struct FrameBuffer {
		VkFramebuffer framebuffer;
		FrameBufferAttachment color, depth;
		VkDescriptorImageInfo descriptor;
	};
	struct OffscreenPass {
		int32_t width, height;
		VkRenderPass renderPass;
		VkSampler sampler;
		std::array<FrameBuffer, 2> framebuffers;
	} offscreenPass{};

	VulkanExample() : VulkanExampleBase()
	{
		title = "Bloom (offscreen rendering)";
		timerSpeed *= 0.5f;
		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -10.25f));
		camera.setRotation(glm::vec3(7.5f, -343.0f, 0.0f));
		camera.setPerspective(45.0f, (float)width / (float)height, 0.1f, 256.0f);

		parity = ztgfx::Parity::fromEnvironment();
		if (parity.enabled) {
			settings.overlay = false;
			bloom = true;
		}
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroySampler(device, offscreenPass.sampler, nullptr);
			for (auto& framebuffer : offscreenPass.framebuffers) {
				vkDestroyImageView(device, framebuffer.color.view, nullptr);
				vkDestroyImage(device, framebuffer.color.image, nullptr);
				vkFreeMemory(device, framebuffer.color.mem, nullptr);
				vkDestroyImageView(device, framebuffer.depth.view, nullptr);
				vkDestroyImage(device, framebuffer.depth.image, nullptr);
				vkFreeMemory(device, framebuffer.depth.mem, nullptr);
				vkDestroyFramebuffer(device, framebuffer.framebuffer, nullptr);
			}
			vkDestroyRenderPass(device, offscreenPass.renderPass, nullptr);
			vkDestroyPipeline(device, pipelines.blurHorz, nullptr);
			vkDestroyPipeline(device, pipelines.blurVert, nullptr);
			vkDestroyPipeline(device, pipelines.phongPass, nullptr);
			vkDestroyPipeline(device, pipelines.glowPass, nullptr);
			vkDestroyPipeline(device, pipelines.skyBox, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.blur, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayouts.scene, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.blur, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayouts.scene, nullptr);
			for (auto& buffer : uniformBuffers) {
				buffer.blurParams.destroy();
				buffer.scene.destroy();
				buffer.skyBox.destroy();
			}
			cubemap.destroy();
		}
	}

	// Setup the offscreen framebuffer for rendering the mirrored scene
	// The color attachment of this framebuffer will then be sampled from
	void prepareOffscreenFramebuffer(FrameBuffer *frameBuf, VkFormat colorFormat, VkFormat depthFormat)
	{
		// Color attachment
		VkImageCreateInfo image = vks::initializers::imageCreateInfo();
		image.imageType = VK_IMAGE_TYPE_2D;
		image.format = colorFormat;
		image.extent.width = FB_DIM;
		image.extent.height = FB_DIM;
		image.extent.depth = 1;
		image.mipLevels = 1;
		image.arrayLayers = 1;
		image.samples = VK_SAMPLE_COUNT_1_BIT;
		image.tiling = VK_IMAGE_TILING_OPTIMAL;
		// We will sample directly from the color attachment
		image.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

		VkMemoryAllocateInfo memAlloc = vks::initializers::memoryAllocateInfo();
		VkMemoryRequirements memReqs;

		VkImageViewCreateInfo colorImageView = vks::initializers::imageViewCreateInfo();
		colorImageView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		colorImageView.format = colorFormat;
		colorImageView.flags = 0;
		colorImageView.subresourceRange = {};
		colorImageView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		colorImageView.subresourceRange.baseMipLevel = 0;
		colorImageView.subresourceRange.levelCount = 1;
		colorImageView.subresourceRange.baseArrayLayer = 0;
		colorImageView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &frameBuf->color.image));
		vkGetImageMemoryRequirements(device, frameBuf->color.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &frameBuf->color.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, frameBuf->color.image, frameBuf->color.mem, 0));

		colorImageView.image = frameBuf->color.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &colorImageView, nullptr, &frameBuf->color.view));

		// Depth stencil attachment
		image.format = depthFormat;
		image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

		VkImageViewCreateInfo depthStencilView = vks::initializers::imageViewCreateInfo();
		depthStencilView.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthStencilView.format = depthFormat;
		depthStencilView.flags = 0;
		depthStencilView.subresourceRange = {};
		depthStencilView.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		if (vks::tools::formatHasStencil(depthFormat)) {
			depthStencilView.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
		}
		depthStencilView.subresourceRange.baseMipLevel = 0;
		depthStencilView.subresourceRange.levelCount = 1;
		depthStencilView.subresourceRange.baseArrayLayer = 0;
		depthStencilView.subresourceRange.layerCount = 1;

		VK_CHECK_RESULT(vkCreateImage(device, &image, nullptr, &frameBuf->depth.image));
		vkGetImageMemoryRequirements(device, frameBuf->depth.image, &memReqs);
		memAlloc.allocationSize = memReqs.size;
		memAlloc.memoryTypeIndex = vulkanDevice->getMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
		VK_CHECK_RESULT(vkAllocateMemory(device, &memAlloc, nullptr, &frameBuf->depth.mem));
		VK_CHECK_RESULT(vkBindImageMemory(device, frameBuf->depth.image, frameBuf->depth.mem, 0));

		depthStencilView.image = frameBuf->depth.image;
		VK_CHECK_RESULT(vkCreateImageView(device, &depthStencilView, nullptr, &frameBuf->depth.view));

		VkImageView attachments[2]{
			frameBuf->color.view,
			frameBuf->depth.view
		};

		VkFramebufferCreateInfo fbufCreateInfo = vks::initializers::framebufferCreateInfo();
		fbufCreateInfo.renderPass = offscreenPass.renderPass;
		fbufCreateInfo.attachmentCount = 2;
		fbufCreateInfo.pAttachments = attachments;
		fbufCreateInfo.width = FB_DIM;
		fbufCreateInfo.height = FB_DIM;
		fbufCreateInfo.layers = 1;

		VK_CHECK_RESULT(vkCreateFramebuffer(device, &fbufCreateInfo, nullptr, &frameBuf->framebuffer));

		// Fill a descriptor for later use in a descriptor set
		frameBuf->descriptor.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		frameBuf->descriptor.imageView = frameBuf->color.view;
		frameBuf->descriptor.sampler = offscreenPass.sampler;
	}

	// Prepare the offscreen framebuffers used for the vertical- and horizontal blur
	void prepareOffscreen()
	{
		offscreenPass.width = FB_DIM;
		offscreenPass.height = FB_DIM;

		// Find a suitable depth format
		VkFormat fbDepthFormat;
		VkBool32 validDepthFormat = vks::tools::getSupportedDepthFormat(physicalDevice, &fbDepthFormat);
		assert(validDepthFormat);
		offscreenDepthFormat = fbDepthFormat;

		// Create a separate render pass for the offscreen rendering as it may differ from the one used for scene rendering

		std::array<VkAttachmentDescription, 2> attchmentDescriptions = {};
		// Color attachment
		attchmentDescriptions[0].format = FB_COLOR_FORMAT;
		attchmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attchmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attchmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
		// Depth attachment
		attchmentDescriptions[1].format = fbDepthFormat;
		attchmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
		attchmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		attchmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		attchmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		attchmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		attchmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorReference = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthReference = { 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpassDescription = {};
		subpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpassDescription.colorAttachmentCount = 1;
		subpassDescription.pColorAttachments = &colorReference;
		subpassDescription.pDepthStencilAttachment = &depthReference;

		// Use subpass dependencies for layout transitions
		std::array<VkSubpassDependency, 3> dependencies{};

		dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[0].dstSubpass = 0;
		dependencies[0].srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
		dependencies[0].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		dependencies[0].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
		dependencies[0].dependencyFlags = 0;

		dependencies[1].srcSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[1].dstSubpass = 0;
		dependencies[1].srcStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[1].srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		dependencies[2].srcSubpass = 0;
		dependencies[2].dstSubpass = VK_SUBPASS_EXTERNAL;
		dependencies[2].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependencies[2].dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		dependencies[2].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
		dependencies[2].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		dependencies[2].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

		// Create the actual renderpass
		VkRenderPassCreateInfo renderPassInfo = {};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attchmentDescriptions.size());
		renderPassInfo.pAttachments = attchmentDescriptions.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpassDescription;
		renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
		renderPassInfo.pDependencies = dependencies.data();

		VK_CHECK_RESULT(vkCreateRenderPass(device, &renderPassInfo, nullptr, &offscreenPass.renderPass));

		// Create sampler to sample from the color attachments
		VkSamplerCreateInfo sampler = vks::initializers::samplerCreateInfo();
		sampler.magFilter = VK_FILTER_LINEAR;
		sampler.minFilter = VK_FILTER_LINEAR;
		sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
		sampler.addressModeV = sampler.addressModeU;
		sampler.addressModeW = sampler.addressModeU;
		sampler.mipLodBias = 0.0f;
		sampler.maxAnisotropy = 1.0f;
		sampler.minLod = 0.0f;
		sampler.maxLod = 1.0f;
		sampler.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
		VK_CHECK_RESULT(vkCreateSampler(device, &sampler, nullptr, &offscreenPass.sampler));

		// Create two frame buffers
		prepareOffscreenFramebuffer(&offscreenPass.framebuffers[0], FB_COLOR_FORMAT, fbDepthFormat);
		prepareOffscreenFramebuffer(&offscreenPass.framebuffers[1], FB_COLOR_FORMAT, fbDepthFormat);
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		models.ufo.loadFromFile(getAssetPath() + "models/retroufo.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.ufoGlow.loadFromFile(getAssetPath() + "models/retroufo_glow.gltf", vulkanDevice, queue, glTFLoadingFlags);
		models.skyBox.loadFromFile(getAssetPath() + "models/cube.gltf", vulkanDevice, queue, glTFLoadingFlags);
		cubemap.loadFromFile(getAssetPath() + "textures/cubemap_space.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames * 8),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames * 6)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames * 4);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layouts
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings;
		VkDescriptorSetLayoutCreateInfo descriptorSetLayoutCreateInfo;

		// Fullscreen blur
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 0),			// Binding 0: Fragment shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1)	// Binding 1: Fragment shader image sampler
		};
		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings.data(), static_cast<uint32_t>(setLayoutBindings.size()));
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayouts.blur));

		// Scene rendering
		setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),	// Binding 1 : Fragment shader image sampler
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_FRAGMENT_BIT, 2),			// Binding 2 : Fragment shader image sampler
		};

		descriptorSetLayoutCreateInfo = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorSetLayoutCreateInfo, nullptr, &descriptorSetLayouts.scene));

		// Sets per frame, just like the buffers themselves
		// Images do not need to be duplicated per frame, we reuse the same one for each frame
		for (auto i = 0; i < uniformBuffers.size(); i++) {

			// Vertical full screen blur
			VkDescriptorSetAllocateInfo descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.blur, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets[i].blurVert));
			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].blurVert, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].blurParams.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].blurVert, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &offscreenPass.framebuffers[0].descriptor),
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
			
			// Horizontal full screen blur
			descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.blur, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets[i].blurHorz));
			writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].blurHorz, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].blurParams.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].blurHorz, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &offscreenPass.framebuffers[1].descriptor),
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

			// Scene rendering
			descriptorSetAllocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayouts.scene, 1);
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets[i].scene));
			writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].scene, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].scene.descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

			// Skybox
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &descriptorSetAllocInfo, &descriptorSets[i].skyBox));
			writeDescriptorSets = {
				vks::initializers::writeDescriptorSet(descriptorSets[i].skyBox, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].skyBox.descriptor),
				vks::initializers::writeDescriptorSet(descriptorSets[i].skyBox, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,	1, &cubemap.descriptor),
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	void preparePipelines()
	{
		// Layouts
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.blur, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.blur));

		pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayouts.scene, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayouts.scene));

		// Pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCI = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationStateCI = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendStateCI = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilStateCI = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportStateCI = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleStateCI = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamicStateCI = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayouts.blur, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyStateCI;
		pipelineCI.pRasterizationState = &rasterizationStateCI;
		pipelineCI.pColorBlendState = &colorBlendStateCI;
		pipelineCI.pMultisampleState = &multisampleStateCI;
		pipelineCI.pViewportState = &viewportStateCI;
		pipelineCI.pDepthStencilState = &depthStencilStateCI;
		pipelineCI.pDynamicState = &dynamicStateCI;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();

		// Blur pipelines
		shaderStages[0] = loadShader(getShadersPath() + "bloom/gaussblur.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "bloom/gaussblur.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Empty vertex input state
		VkPipelineVertexInputStateCreateInfo emptyInputState = vks::initializers::pipelineVertexInputStateCreateInfo();
		pipelineCI.pVertexInputState = &emptyInputState;
		pipelineCI.layout = pipelineLayouts.blur;
		// Additive blending
		blendAttachmentState.colorWriteMask = 0xF;
		blendAttachmentState.blendEnable = VK_TRUE;
		blendAttachmentState.colorBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
		blendAttachmentState.alphaBlendOp = VK_BLEND_OP_ADD;
		blendAttachmentState.srcAlphaBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
		blendAttachmentState.dstAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;

		// Use specialization constants to select between horizontal and vertical blur
		uint32_t blurdirection = 0;
		VkSpecializationMapEntry specializationMapEntry = vks::initializers::specializationMapEntry(0, 0, sizeof(uint32_t));
		VkSpecializationInfo specializationInfo = vks::initializers::specializationInfo(1, &specializationMapEntry, sizeof(uint32_t), &blurdirection);
		shaderStages[1].pSpecializationInfo = &specializationInfo;
		// Vertical blur pipeline
		pipelineCI.renderPass = offscreenPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.blurVert));
		// Horizontal blur pipeline
		blurdirection = 1;
		pipelineCI.renderPass = renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.blurHorz));

		// Phong pass (3D model)
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({vkglTF::VertexComponent::Position, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color, vkglTF::VertexComponent::Normal});
		pipelineCI.layout = pipelineLayouts.scene;
		shaderStages[0] = loadShader(getShadersPath() + "bloom/phongpass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "bloom/phongpass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		blendAttachmentState.blendEnable = VK_FALSE;
		depthStencilStateCI.depthWriteEnable = VK_TRUE;
		rasterizationStateCI.cullMode = VK_CULL_MODE_BACK_BIT;
		pipelineCI.renderPass = renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.phongPass));

		// Color only pass (offscreen blur base)
		shaderStages[0] = loadShader(getShadersPath() + "bloom/colorpass.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "bloom/colorpass.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		pipelineCI.renderPass = offscreenPass.renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.glowPass));

		// Skybox (cubemap)
		shaderStages[0] = loadShader(getShadersPath() + "bloom/skybox.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "bloom/skybox.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		depthStencilStateCI.depthWriteEnable = VK_FALSE;
		rasterizationStateCI.cullMode = VK_CULL_MODE_FRONT_BIT;
		pipelineCI.renderPass = renderPass;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.skyBox));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers) {
			// Phong and color pass vertex shader uniform buffer
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.scene, sizeof(ubos.scene)));
			VK_CHECK_RESULT(buffer.scene.map());
			// Blur parameters uniform buffers
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.blurParams, sizeof(ubos.blurParams)));
			VK_CHECK_RESULT(buffer.blurParams.map());
			// Skybox
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer.skyBox, sizeof(ubos.skyBox)));
			VK_CHECK_RESULT(buffer.skyBox.map());
		}
	}

	void updateUniformBuffers()
	{
		// UFO
		ubos.scene.projection = camera.matrices.perspective;
		ubos.scene.view = camera.matrices.view;
		ubos.scene.model = glm::translate(glm::mat4(1.0f), glm::vec3(sin(glm::radians(timer * 360.0f)) * 0.25f, -1.0f, cos(glm::radians(timer * 360.0f)) * 0.25f));
		ubos.scene.model = glm::rotate(ubos.scene.model, -sinf(glm::radians(timer * 360.0f)) * 0.15f, glm::vec3(1.0f, 0.0f, 0.0f));
		ubos.scene.model = glm::rotate(ubos.scene.model, glm::radians(timer * 360.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		memcpy(uniformBuffers[currentBuffer].scene.mapped, &ubos.scene, sizeof(ubos.scene));

		// Skybox
		ubos.skyBox.projection = glm::perspective(glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f);
		ubos.skyBox.view = glm::mat4(glm::mat3(camera.matrices.view));
		ubos.skyBox.model = glm::mat4(1.0f);
		memcpy(uniformBuffers[currentBuffer].skyBox.mapped, &ubos.skyBox, sizeof(ubos.skyBox));

		// Blur parameters
		memcpy(uniformBuffers[currentBuffer].blurParams.mapped, &ubos.blurParams, sizeof(ubos.blurParams));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareUniformBuffers();
		prepareOffscreen();
		setupDescriptors();
		preparePipelines();
		prepared = true;
	}

	void buildCommandBuffer()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];
		
		/*
			The blur method used in this example is multi pass and renders the vertical blur first and then the horizontal one
			While it's possible to blur in one pass, this method is widely used as it requires far less samples to generate the blur
		*/
		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

		if (bloom) {
			VkClearValue clearValues[2]{};
			clearValues[0].color = { { 0.0f, 0.0f, 0.0f, 1.0f } };
			clearValues[1].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
			renderPassBeginInfo.renderPass = offscreenPass.renderPass;
			renderPassBeginInfo.framebuffer = offscreenPass.framebuffers[0].framebuffer;
			renderPassBeginInfo.renderArea.extent.width = offscreenPass.width;
			renderPassBeginInfo.renderArea.extent.height = offscreenPass.height;
			renderPassBeginInfo.clearValueCount = 2;
			renderPassBeginInfo.pClearValues = clearValues;

			VkViewport viewport = vks::initializers::viewport((float)offscreenPass.width, (float)offscreenPass.height, 0.0f, 1.0f);
			vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
			VkRect2D scissor = vks::initializers::rect2D(offscreenPass.width, offscreenPass.height, 0, 0);
			vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

			/*
				First render pass: Render glow parts of the model (separate mesh) to an offscreen frame buffer
			*/

			vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets[currentBuffer].scene, 0, nullptr);
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.glowPass);

			models.ufoGlow.draw(cmdBuffer);

			vkCmdEndRenderPass(cmdBuffer);

			/*
				Second render pass: Vertical blur

				Render contents of the first pass into a second framebuffer and apply a vertical blur
				This is the first blur pass, the horizontal blur is applied when rendering on top of the scene
			*/

			renderPassBeginInfo.framebuffer = offscreenPass.framebuffers[1].framebuffer;

			vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.blur, 0, 1, &descriptorSets[currentBuffer].blurVert, 0, nullptr);
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.blurVert);
			vkCmdDraw(cmdBuffer, 3, 1, 0, 0);

			vkCmdEndRenderPass(cmdBuffer);
		}

		/*
			Note: Explicit synchronization is not required between the render pass, as this is done implicit via sub pass dependencies
		*/

		/*
			Third render pass: Scene rendering with applied vertical blur

			Renders the scene and the (vertically blurred) contents of the second framebuffer and apply a horizontal blur

		*/
		{
			VkClearValue clearValues[2]{};
			clearValues[0].color = defaultClearColor;
			clearValues[1].depthStencil = { 1.0f, 0 };

			VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
			renderPassBeginInfo.renderPass = renderPass;
			renderPassBeginInfo.framebuffer = frameBuffers[currentImageIndex];
			renderPassBeginInfo.renderArea.extent.width = width;
			renderPassBeginInfo.renderArea.extent.height = height;
			renderPassBeginInfo.clearValueCount = 2;
			renderPassBeginInfo.pClearValues = clearValues;

			vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
			vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
			VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
			vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

			// Skybox
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets[currentBuffer].skyBox, 0, nullptr);
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.skyBox);
			models.skyBox.draw(cmdBuffer);

			// 3D scene
			vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.scene, 0, 1, &descriptorSets[currentBuffer].scene, 0, nullptr);
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phongPass);
			models.ufo.draw(cmdBuffer);

			if (bloom) {
				vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts.blur, 0, 1, &descriptorSets[currentBuffer].blurHorz, 0, nullptr);
				vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.blurHorz);
				vkCmdDraw(cmdBuffer, 3, 1, 0, 0);
			}

			drawUI(cmdBuffer);

			vkCmdEndRenderPass(cmdBuffer);

		}

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}


	void dumpSampler(
		ztgfx::JsonWriter& out, const char* name,
		float maxLod, bool anisotropy, float maxAnisotropy)
	{
		out.key(name).beginObject();
		out.key("magFilter").value("LINEAR");
		out.key("minFilter").value("LINEAR");
		out.key("mipmapMode").value("LINEAR");
		out.key("addressModeU").value("CLAMP_TO_EDGE");
		out.key("addressModeV").value("CLAMP_TO_EDGE");
		out.key("addressModeW").value("CLAMP_TO_EDGE");
		out.key("minLod").value(0.0f);
		out.key("maxLod").value(maxLod);
		out.key("anisotropyEnable").value(anisotropy);
		out.key("maxAnisotropy").value(maxAnisotropy);
		out.key("borderColor").value("FLOAT_OPAQUE_WHITE");
		out.endObject();
	}

	void dumpPipelineState(
		ztgfx::JsonWriter& out, const char* name,
		const char* vertexShader, const char* fragmentShader,
		const char* cullMode, bool blend, bool depthWrite, int blurDirection)
	{
		out.key(name).beginObject();
		out.key("polygonMode").value("FILL");
		out.key("cullMode").value(cullMode);
		out.key("frontFace").value("COUNTER_CLOCKWISE");
		out.key("lineWidth").value(1.0f);
		out.key("depthTest").value(true);
		out.key("depthWrite").value(depthWrite);
		out.key("depthCompare").value("LESS_OR_EQUAL");
		out.key("blendEnable").value(blend);
		out.key("srcColorBlendFactor").value("ONE");
		out.key("dstColorBlendFactor").value(blend ? "ONE" : "ZERO");
		out.key("srcAlphaBlendFactor").value(blend ? "SRC_ALPHA" : "ONE");
		out.key("dstAlphaBlendFactor").value(blend ? "DST_ALPHA" : "ZERO");
		out.key("vertexShader").value(vertexShader);
		out.key("fragmentShader").value(fragmentShader);
		if (blurDirection >= 0) {
			out.key("blurDirection").value(blurDirection);
		}
		out.endObject();
	}

	void dumpParityMetadata(std::string filename)
	{
		ztgfx::JsonWriter out(filename);
		ztgfx::Parity::header(out, parity.captureFrame, width, height);
		ztgfx::Parity::timeValue(out, parity.time);

		ztgfx::Parity::CameraLens lens{
			glm::radians(45.0f), (float)width / (float)height, 0.1f, 256.0f
		};
		ztgfx::Parity::camera(out, camera, &lens);
		out.key("light").beginObject();
		out.key("position").beginArray();
		out.value(-5.0f).value(-5.0f).value(0.0f);
		out.endArray();
		out.endObject();

		out.key("uniforms").beginObject();
		out.key("scene").beginObject();
		out.key("projection").array(&ubos.scene.projection[0][0], 16);
		out.key("view").array(&ubos.scene.view[0][0], 16);
		out.key("model").array(&ubos.scene.model[0][0], 16);
		out.endObject();
		out.key("skyBox").beginObject();
		out.key("projection").array(&ubos.skyBox.projection[0][0], 16);
		out.key("view").array(&ubos.skyBox.view[0][0], 16);
		out.key("model").array(&ubos.skyBox.model[0][0], 16);
		out.endObject();
		out.key("blurParams").beginObject();
		out.key("blurScale").value(ubos.blurParams.blurScale);
		out.key("blurStrength").value(ubos.blurParams.blurStrength);
		out.endObject();
		out.endObject();

		out.key("formats").beginObject();
		out.key("color").value((unsigned int)swapChain.colorFormat);
		out.key("offscreenColor").value((unsigned int)FB_COLOR_FORMAT);
		out.key("depth").value((unsigned int)depthFormat);
		out.key("offscreenDepth").value((unsigned int)offscreenDepthFormat);
		out.endObject();

		out.key("clears").beginObject();
		out.key("scene").array(defaultClearColor.float32, 4);
		out.key("offscreen").beginArray();
		out.value(0.0f).value(0.0f).value(0.0f).value(1.0f);
		out.endArray();
		out.endObject();

		out.key("textures").beginObject();
		out.key("glow").beginObject();
		out.key("width").value(FB_DIM);
		out.key("height").value(FB_DIM);
		out.key("format").value((unsigned int)FB_COLOR_FORMAT);
		out.endObject();
		out.key("verticalBlur").beginObject();
		out.key("width").value(FB_DIM);
		out.key("height").value(FB_DIM);
		out.key("format").value((unsigned int)FB_COLOR_FORMAT);
		out.endObject();
		out.key("cubemap").beginObject();
		out.key("width").value((unsigned int)cubemap.width);
		out.key("height").value((unsigned int)cubemap.height);
		out.key("layers").value(6u);
		out.key("format").value((unsigned int)VK_FORMAT_R8G8B8A8_UNORM);
		out.endObject();
		out.endObject();

		out.key("samplers").beginObject();
		dumpSampler(out, "offscreen", 1.0f, false, 1.0f);
		dumpSampler(out, "cubemap",
			(float)cubemap.mipLevels,
			vulkanDevice->enabledFeatures.samplerAnisotropy != 0,
			vulkanDevice->enabledFeatures.samplerAnisotropy
				? vulkanDevice->properties.limits.maxSamplerAnisotropy : 1.0f);
		out.endObject();

		out.key("pipelines").beginObject();
		dumpPipelineState(out, "glow", "bloom/colorpass.vert.spv",
			"bloom/colorpass.frag.spv", "BACK", false, true, -1);
		dumpPipelineState(out, "phong", "bloom/phongpass.vert.spv",
			"bloom/phongpass.frag.spv", "BACK", false, true, -1);
		dumpPipelineState(out, "skybox", "bloom/skybox.vert.spv",
			"bloom/skybox.frag.spv", "FRONT", false, false, -1);
		dumpPipelineState(out, "blurVert", "bloom/gaussblur.vert.spv",
			"bloom/gaussblur.frag.spv", "NONE", true, true, 0);
		dumpPipelineState(out, "blurHorz", "bloom/gaussblur.vert.spv",
			"bloom/gaussblur.frag.spv", "NONE", true, true, 1);
		out.endObject();

		out.key("specConstants").beginObject();
		out.key("blurdirection").beginArray();
		out.value(0u).value(1u);
		out.endArray();
		out.endObject();

		ztgfx::Parity::device(out, vulkanDevice->properties);
		out.close();
	}

	void finishParity()
	{
#if defined(VK_USE_PLATFORM_WAYLAND_KHR) || defined(VK_USE_PLATFORM_XCB_KHR) || defined(_DIRECT2DISPLAY) || defined(VK_USE_PLATFORM_HEADLESS_EXT)
		quit = true;
#else
		std::exit(0);
#endif
	}

	virtual void render()
	{
		if (!prepared)
			return;
		if (parity.enabled) {
			timer = parity.time;
			ztgfx::Parity::pinCamera(
				camera, glm::vec3(0.0f, 0.0f, -10.25f),
				glm::vec3(7.5f, -343.0f, 0.0f)
			);
			camera.setPerspective(
				45.0f, (float)width / (float)height, 0.1f, 256.0f
			);
			bloom = true;
		}
		VulkanExampleBase::prepareFrame();
		updateUniformBuffers();
		buildCommandBuffer();
		VulkanExampleBase::submitFrame();

		if (parity.enabled) {
			vkDeviceWaitIdle(device);
			if (parity.frame == parity.captureFrame) {
				parity.saveSwapchain(
					vulkanDevice, queue, swapChain.images[currentImageIndex],
					swapChain.colorFormat, width, height,
					parity.path("cpp_scene.ppm"), "scene"
				);
				parity.saveImage(
					vulkanDevice, queue, offscreenPass.framebuffers[0].color.image,
					FB_COLOR_FORMAT, FB_DIM, FB_DIM,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
					parity.path("cpp_glow.ppm"), "glow"
				);
				parity.saveImage(
					vulkanDevice, queue, offscreenPass.framebuffers[1].color.image,
					FB_COLOR_FORMAT, FB_DIM, FB_DIM,
					VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
					VK_IMAGE_ASPECT_COLOR_BIT, 0, 0,
					parity.path("cpp_blur_vertical.ppm"), "verticalBlur"
				);
				dumpParityMetadata(parity.path("cpp.json"));
				parity.writeCaptures(parity.path("captures.json"));
				finishParity();
			}
			parity.frame++;
		}
	}

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (overlay->header("Settings")) {
			overlay->checkBox("Bloom", &bloom);
			overlay->inputFloat("Scale", &ubos.blurParams.blurScale, 0.1f, 2);
		}
	}
};

VULKAN_EXAMPLE_MAIN()
