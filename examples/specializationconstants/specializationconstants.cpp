/*
* Vulkan Example - Shader specialization constants
* 
* This samples uses specialization constants to define shader constants at pipeline creation
* These are used to compile shaders with different execution paths and settings 
* With these constants one can create different shader configurations from a single shader file
* See uber.frag for how such a shader can look
*
* For details see https://www.khronos.org/registry/vulkan/specs/misc/GL_KHR_vulkan_glsl.txt
*
* Copyright (C) 2016-2025 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include "vulkanexamplebase.h"
#include "VulkanglTFModel.h"
#include "ztgfx_parity.h"

class VulkanExample: public VulkanExampleBase
{
public:
	vkglTF::Model scene;
	vks::Texture2D colormap;
	ztgfx::Parity parity;

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 modelView;
		glm::vec4 lightPos{ 0.0f, -2.0f, 1.0f, 0.0f };
	} uniformData;
	std::array<vks::Buffer, maxConcurrentFrames> uniformBuffers;

	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	std::array<VkDescriptorSet, maxConcurrentFrames> descriptorSets{};

	struct Pipelines{
		VkPipeline phong{ VK_NULL_HANDLE };
		VkPipeline toon{ VK_NULL_HANDLE };
		VkPipeline textured{ VK_NULL_HANDLE };
	} pipelines;

	VulkanExample() : VulkanExampleBase()
	{
		title = "Specialization constants";
		camera.type = Camera::CameraType::lookat;
		camera.setPerspective(60.0f, ((float)width / 3.0f) / (float)height, 0.1f, 512.0f);
		camera.setRotation(glm::vec3(-40.0f, -90.0f, 0.0f));
		camera.setTranslation(glm::vec3(0.0f, 0.0f, -2.0f));

		parity = ztgfx::Parity::fromEnvironment();
		if (parity.enabled) {
			settings.overlay = false;
			uniformData.lightPos = glm::vec4(0.0f, -2.0f, 1.0f, 0.0f);
		}
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, pipelines.phong, nullptr);
			vkDestroyPipeline(device, pipelines.textured, nullptr);
			vkDestroyPipeline(device, pipelines.toon, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			colormap.destroy();
			for (auto& buffer : uniformBuffers) {
				buffer.destroy();
			}
		}
	}

	void loadAssets()
	{
		scene.loadFromFile(getAssetPath() + "models/color_teapot_spheres.gltf", vulkanDevice, queue , vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY);
		colormap.loadFromFile(getAssetPath() + "textures/metalplate_nomips_rgba.ktx", VK_FORMAT_R8G8B8A8_UNORM, vulkanDevice, queue);
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames),
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, maxConcurrentFrames)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0),
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1),
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Sets per frame, just like the buffers themselves
		// Images do not need to be duplicated per frame, we use the same for each descriptor to keep things simple
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		for (auto i = 0; i < uniformBuffers.size(); i++) {
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i]));
			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				// Binding 0 : Vertex shader uniform buffer
				vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].descriptor),
				// Binding 1 : Fragment shader image sampler
				vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, &colormap.descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	void preparePipelines()
	{
		// Layout
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Pipeline
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE, VK_FRONT_FACE_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT, 0);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages;

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass, 0);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState = vkglTF::Vertex::getPipelineVertexInputState({ vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::UV, vkglTF::VertexComponent::Color });

		// Prepare specialization constants data

		// Host data to take specialization constants from
		struct SpecializationData {
			// Sets the lighting model used in the fragment "uber" shader
			uint32_t lightingModel{ 0 };
			// Parameter for the toon shading part of the fragment shader
			float toonDesaturationFactor{ 0.5f };
		} specializationData;

		// Each shader constant of a shader stage corresponds to one map entry
		std::array<VkSpecializationMapEntry, 2> specializationMapEntries;
		// Shader bindings based on specialization constants are marked by the new "constant_id" layout qualifier:
		//	layout (constant_id = 0) const int LIGHTING_MODEL = 0;
		//	layout (constant_id = 1) const float PARAM_TOON_DESATURATION = 0.0f;

		// Map entry for the lighting model to be used by the fragment shader
		specializationMapEntries[0].constantID = 0;
		specializationMapEntries[0].size = sizeof(specializationData.lightingModel);
		specializationMapEntries[0].offset = 0;

		// Map entry for the toon shader parameter
		specializationMapEntries[1].constantID = 1;
		specializationMapEntries[1].size = sizeof(specializationData.toonDesaturationFactor);
		specializationMapEntries[1].offset = offsetof(SpecializationData, toonDesaturationFactor);

		// Prepare specialization info block for the shader stage
		VkSpecializationInfo specializationInfo{};
		specializationInfo.dataSize = sizeof(specializationData);
		specializationInfo.mapEntryCount = static_cast<uint32_t>(specializationMapEntries.size());
		specializationInfo.pMapEntries = specializationMapEntries.data();
		specializationInfo.pData = &specializationData;

		// Create pipelines
		// All pipelines will use the same "uber" shader and specialization constants to change branching and parameters of that shader
		shaderStages[0] = loadShader(getShadersPath() + "specializationconstants/uber.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "specializationconstants/uber.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		// Specialization info is assigned is part of the shader stage (modul) and must be set after creating the module and before creating the pipeline
		shaderStages[1].pSpecializationInfo = &specializationInfo;

		// Solid phong shading
		specializationData.lightingModel = 0;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.phong));

		// Phong and textured
		specializationData.lightingModel = 1;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.toon));

		// Textured discard
		specializationData.lightingModel = 2;
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.textured));
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers) {
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer, sizeof(UniformData), &uniformData));
			VK_CHECK_RESULT(buffer.map());
		}
	}

	void updateUniformBuffers()
	{
		camera.setPerspective(60.0f, ((float)width / 3.0f) / (float)height, 0.1f, 512.0f);
		uniformData.projection = camera.matrices.perspective;
		uniformData.modelView = camera.matrices.view;
		memcpy(uniformBuffers[currentBuffer].mapped, &uniformData, sizeof(UniformData));
	}

	void buildCommandBuffer()
	{
		VkCommandBuffer cmdBuffer = drawCmdBuffers[currentBuffer];
		
		VkCommandBufferBeginInfo cmdBufInfo = vks::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2]{};
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vks::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;
		renderPassBeginInfo.framebuffer = frameBuffers[currentImageIndex];

		VK_CHECK_RESULT(vkBeginCommandBuffer(cmdBuffer, &cmdBufInfo));

		vkCmdBeginRenderPass(cmdBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer], 0, nullptr);

		// Left
		VkViewport viewport = vks::initializers::viewport((float)width / 3.0f, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phong);
		scene.draw(cmdBuffer);

		// Center
		viewport.x = (float)width / 3.0f;
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.toon);
		scene.draw(cmdBuffer);

		// Right
		viewport.x = (float)width / 3.0f + (float)width / 3.0f;
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.textured);
		scene.draw(cmdBuffer);

		drawUI(cmdBuffer);

		vkCmdEndRenderPass(cmdBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadAssets();
		prepareUniformBuffers();
		setupDescriptors();
		preparePipelines();
		prepared = true;
	}


	void dumpPipelineState(
		ztgfx::JsonWriter& out, const char* name, unsigned int lightingModel)
	{
		out.key(name).beginObject();
		out.key("lightingModel").value(lightingModel);
		out.key("polygonMode").value("FILL");
		out.key("cullMode").value("NONE");
		out.key("frontFace").value("CLOCKWISE");
		out.key("lineWidth").value(1.0f);
		out.key("depthCompare").value("LESS_OR_EQUAL");
		out.key("vertexShader").value("specializationconstants/uber.vert.spv");
		out.key("fragmentShader").value("specializationconstants/uber.frag.spv");
		out.endObject();
	}

	void dumpColormapSampler(ztgfx::JsonWriter& out)
	{
		out.key("colormap").beginObject();
		out.key("format").value((unsigned int)VK_FORMAT_R8G8B8A8_UNORM);
		out.key("width").value((unsigned int)colormap.width);
		out.key("height").value((unsigned int)colormap.height);
		out.key("mips").value((unsigned int)colormap.mipLevels);
		out.key("sampler").beginObject();
		out.key("magFilter").value("LINEAR");
		out.key("minFilter").value("LINEAR");
		out.key("mipmapMode").value("LINEAR");
		out.key("addressModeU").value("REPEAT");
		out.key("addressModeV").value("REPEAT");
		out.key("addressModeW").value("REPEAT");
		out.key("minLod").value(0.0f);
		out.key("maxLod").value((float)colormap.mipLevels);
		out.key("anisotropyEnable").value(
			vulkanDevice->enabledFeatures.samplerAnisotropy != 0
		);
		out.key("maxAnisotropy").value(
			vulkanDevice->enabledFeatures.samplerAnisotropy
				? vulkanDevice->properties.limits.maxSamplerAnisotropy
				: 1.0f
		);
		out.key("borderColor").value("FLOAT_OPAQUE_WHITE");
		out.endObject();
		out.endObject();
	}

	void dumpParityMetadata(std::string filename)
	{
		const float aspect = ((float)width / 3.0f) / (float)height;

		ztgfx::JsonWriter out(filename);
		ztgfx::Parity::header(out, parity.captureFrame, width, height);
		ztgfx::Parity::timeValue(out, parity.time);

		ztgfx::Parity::CameraLens lens{
			glm::radians(60.0f), aspect, 0.1f, 512.0f
		};
		ztgfx::Parity::camera(out, camera, &lens);
		ztgfx::Parity::light(out, glm::vec3(0.0f, -2.0f, 1.0f));

		out.key("uniforms").beginObject();
		out.key("projection").array(&uniformData.projection[0][0], 16);
		out.key("modelView").array(&uniformData.modelView[0][0], 16);
		out.key("lightPos").array(&uniformData.lightPos.x, 4);
		out.endObject();

		out.key("formats").beginObject();
		out.key("color").value((unsigned int)swapChain.colorFormat);
		out.key("depth").value((unsigned int)depthFormat);
		out.endObject();
		out.key("clearColor").array(defaultClearColor.float32, 4);

		out.key("viewports").beginArray();
		for (int i = 0; i < 3; i++) {
			out.beginObject();
			out.key("x").value(((float)width / 3.0f) * i);
			out.key("y").value(0.0f);
			out.key("width").value((float)width / 3.0f);
			out.key("height").value((float)height);
			out.key("minDepth").value(0.0f);
			out.key("maxDepth").value(1.0f);
			out.endObject();
		}
		out.endArray();

		out.key("scissors").beginArray();
		for (int i = 0; i < 3; i++) {
			out.beginObject();
			out.key("x").value(0);
			out.key("y").value(0);
			out.key("width").value(width);
			out.key("height").value(height);
			out.endObject();
		}
		out.endArray();

		out.key("pipelines").beginObject();
		dumpPipelineState(out, "phong", 0u);
		dumpPipelineState(out, "toon", 1u);
		dumpPipelineState(out, "textured", 2u);
		out.endObject();

		out.key("specConstants").beginObject();
		out.key("LIGHTING_MODEL").beginArray();
		out.value(0u).value(1u).value(2u);
		out.endArray();
		out.key("PARAM_TOON_DESATURATION").value(0.5f);
		out.endObject();

		dumpColormapSampler(out);

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
				camera, glm::vec3(0.0f, 0.0f, -2.0f),
				glm::vec3(-40.0f, -90.0f, 0.0f)
			);
			camera.setPerspective(
				60.0f, ((float)width / 3.0f) / (float)height, 0.1f, 512.0f
			);
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
				dumpParityMetadata(parity.path("cpp.json"));
				parity.writeCaptures(parity.path("captures.json"));
				finishParity();
			}
			parity.frame++;
		}
	}
};

VULKAN_EXAMPLE_MAIN()
