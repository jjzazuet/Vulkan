/*
* Vulkan Example - Using different pipelines in a single renderpass
* 
* This sample shows how to setup multiple graphics pipelines and how to use them for drawing objects with differring visuals
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
	ztgfx::Parity parity;

	struct UniformData {
		glm::mat4 projection;
		glm::mat4 modelView;
		glm::vec4 lightPos{ 0.0f, 2.0f, 1.0f, 0.0f };
	} uniformData;
	std::array<vks::Buffer, maxConcurrentFrames> uniformBuffers;

	VkPipelineLayout pipelineLayout{ VK_NULL_HANDLE };
	VkDescriptorSetLayout descriptorSetLayout{ VK_NULL_HANDLE };
	std::array<VkDescriptorSet, maxConcurrentFrames> descriptorSets{};

	struct {
		VkPipeline phong{ VK_NULL_HANDLE };
		VkPipeline wireframe{ VK_NULL_HANDLE };
		VkPipeline toon{ VK_NULL_HANDLE };
	} pipelines;

	VulkanExample() : VulkanExampleBase()
	{
		title = "Pipeline state objects";
		camera.type = Camera::CameraType::lookat;
		camera.setPosition(glm::vec3(0.0f, 0.0f, -10.5f));
		camera.setRotation(glm::vec3(-25.0f, 15.0f, 0.0f));
		camera.setRotationSpeed(0.5f);
		camera.setPerspective(60.0f, (float)(width / 3.0f) / (float)height, 0.1f, 256.0f);

		parity = ztgfx::Parity::fromEnvironment();
		if (parity.enabled) {
			settings.overlay = false;
			uniformData.lightPos = glm::vec4(0.0f, 2.0f, 1.0f, 0.0f);
		}
	}

	~VulkanExample()
	{
		if (device) {
			vkDestroyPipeline(device, pipelines.phong, nullptr);
			if (enabledFeatures.fillModeNonSolid) {
				vkDestroyPipeline(device, pipelines.wireframe, nullptr);
			}
			vkDestroyPipeline(device, pipelines.toon, nullptr);
			vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
			vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);
			for (auto& buffer : uniformBuffers) {
				buffer.destroy();
			}
		}
	}

	// Enable physical device features required for this example
	virtual void getEnabledFeatures()
	{
		// Fill mode non solid is required for wireframe display
		if (deviceFeatures.fillModeNonSolid) {
			enabledFeatures.fillModeNonSolid = VK_TRUE;
		};

		// Wide lines must be present for line width > 1.0f
		if (deviceFeatures.wideLines) {
			enabledFeatures.wideLines = VK_TRUE;
		}
	}

	void loadAssets()
	{
		const uint32_t glTFLoadingFlags = vkglTF::FileLoadingFlags::PreTransformVertices | vkglTF::FileLoadingFlags::PreMultiplyVertexColors | vkglTF::FileLoadingFlags::FlipY;
		scene.loadFromFile(getAssetPath() + "models/treasure_smooth.gltf", vulkanDevice, queue, glTFLoadingFlags);
	}

	void setupDescriptors()
	{
		// Pool
		std::vector<VkDescriptorPoolSize> poolSizes = {
			vks::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, maxConcurrentFrames)
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = vks::initializers::descriptorPoolCreateInfo(poolSizes, maxConcurrentFrames);
		VK_CHECK_RESULT(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool));

		// Layout
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings = {
			// Binding 0 : Vertex shader uniform buffer
			vks::initializers::descriptorSetLayoutBinding(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT, 0)
		};
		VkDescriptorSetLayoutCreateInfo descriptorLayout = vks::initializers::descriptorSetLayoutCreateInfo(setLayoutBindings);
		VK_CHECK_RESULT(vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout));

		// Sets per frame, just like the buffers themselves
		VkDescriptorSetAllocateInfo allocInfo = vks::initializers::descriptorSetAllocateInfo(descriptorPool, &descriptorSetLayout, 1);
		for (auto i = 0; i < uniformBuffers.size(); i++) {
			VK_CHECK_RESULT(vkAllocateDescriptorSets(device, &allocInfo, &descriptorSets[i]));
			std::vector<VkWriteDescriptorSet> writeDescriptorSets = {
				// Binding 0 : Vertex shader uniform buffer
				vks::initializers::writeDescriptorSet(descriptorSets[i], VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &uniformBuffers[i].descriptor)
			};
			vkUpdateDescriptorSets(device, static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);
		}
	}

	void preparePipelines()
	{
		// Layout
		VkPipelineLayoutCreateInfo pipelineLayoutCreateInfo = vks::initializers::pipelineLayoutCreateInfo(&descriptorSetLayout, 1);
		VK_CHECK_RESULT(vkCreatePipelineLayout(device, &pipelineLayoutCreateInfo, nullptr, &pipelineLayout));

		// Pipelines
		
		// Most state is shared between all pipelines
		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState = vks::initializers::pipelineInputAssemblyStateCreateInfo(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, 0, VK_FALSE);
		VkPipelineRasterizationStateCreateInfo rasterizationState = vks::initializers::pipelineRasterizationStateCreateInfo(VK_POLYGON_MODE_FILL, VK_CULL_MODE_BACK_BIT, VK_FRONT_FACE_COUNTER_CLOCKWISE, 0);
		VkPipelineColorBlendAttachmentState blendAttachmentState = vks::initializers::pipelineColorBlendAttachmentState(0xf, VK_FALSE);
		VkPipelineColorBlendStateCreateInfo colorBlendState = vks::initializers::pipelineColorBlendStateCreateInfo(1, &blendAttachmentState);
		VkPipelineDepthStencilStateCreateInfo depthStencilState = vks::initializers::pipelineDepthStencilStateCreateInfo(VK_TRUE, VK_TRUE, VK_COMPARE_OP_LESS_OR_EQUAL);
		VkPipelineViewportStateCreateInfo viewportState = vks::initializers::pipelineViewportStateCreateInfo(1, 1, 0);
		VkPipelineMultisampleStateCreateInfo multisampleState = vks::initializers::pipelineMultisampleStateCreateInfo(VK_SAMPLE_COUNT_1_BIT);
		std::vector<VkDynamicState> dynamicStateEnables = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH, };
		VkPipelineDynamicStateCreateInfo dynamicState = vks::initializers::pipelineDynamicStateCreateInfo(dynamicStateEnables);
		std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};

		VkGraphicsPipelineCreateInfo pipelineCI = vks::initializers::pipelineCreateInfo(pipelineLayout, renderPass);
		pipelineCI.pInputAssemblyState = &inputAssemblyState;
		pipelineCI.pRasterizationState = &rasterizationState;
		pipelineCI.pColorBlendState = &colorBlendState;
		pipelineCI.pMultisampleState = &multisampleState;
		pipelineCI.pViewportState = &viewportState;
		pipelineCI.pDepthStencilState = &depthStencilState;
		pipelineCI.pDynamicState = &dynamicState;
		pipelineCI.stageCount = static_cast<uint32_t>(shaderStages.size());
		pipelineCI.pStages = shaderStages.data();
		pipelineCI.pVertexInputState  = vkglTF::Vertex::getPipelineVertexInputState({vkglTF::VertexComponent::Position, vkglTF::VertexComponent::Normal, vkglTF::VertexComponent::Color});

		// Create the different pipelines used in this sample

		// We are using this pipeline as the base for the other pipelines (derivatives)
		// Pipeline derivatives can be used for pipelines that share most of their state
		// Depending on the implementation this may result in better performance for pipeline
		// switching and faster creation time
		pipelineCI.flags = VK_PIPELINE_CREATE_ALLOW_DERIVATIVES_BIT;

		// Textured pipeline
		// Phong shading pipeline
		shaderStages[0] = loadShader(getShadersPath() + "pipelines/phong.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "pipelines/phong.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.phong));

		// All pipelines created after the base pipeline will be derivatives
		pipelineCI.flags = VK_PIPELINE_CREATE_DERIVATIVE_BIT;
		// Base pipeline will be our first created pipeline
		pipelineCI.basePipelineHandle = pipelines.phong;
		// It's only allowed to either use a handle or index for the base pipeline
		// As we use the handle, we must set the index to -1 (see section 9.5 of the specification)
		pipelineCI.basePipelineIndex = -1;

		// Toon shading pipeline
		shaderStages[0] = loadShader(getShadersPath() + "pipelines/toon.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getShadersPath() + "pipelines/toon.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.toon));

		// Pipeline for wire frame rendering
		// Non solid rendering is not a mandatory Vulkan feature
		if (enabledFeatures.fillModeNonSolid) {
			rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
			shaderStages[0] = loadShader(getShadersPath() + "pipelines/wireframe.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
			shaderStages[1] = loadShader(getShadersPath() + "pipelines/wireframe.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
			VK_CHECK_RESULT(vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCI, nullptr, &pipelines.wireframe));
		}
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		for (auto& buffer : uniformBuffers) {
			VK_CHECK_RESULT(vulkanDevice->createBuffer(VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &buffer, sizeof(UniformData)));
			VK_CHECK_RESULT(buffer.map());
		}
	}

	void updateUniformBuffers()
	{
		// Override the base sample camera setup, since we use three viewports
		camera.setPerspective(60.0f, (float)(width / 3.0f) / (float)height, 0.1f, 256.0f);
		uniformData.projection = camera.matrices.perspective;
		uniformData.modelView = camera.matrices.view;
		memcpy(uniformBuffers[currentBuffer].mapped, &uniformData, sizeof(UniformData));
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

		VkViewport viewport = vks::initializers::viewport((float)width, (float)height, 0.0f, 1.0f);
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);

		VkRect2D scissor = vks::initializers::rect2D(width, height, 0, 0);
		vkCmdSetScissor(cmdBuffer, 0, 1, &scissor);

		vkCmdBindDescriptorSets(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSets[currentBuffer], 0, nullptr);
		scene.bindBuffers(cmdBuffer);

		// Left : Render the scene using the solid colored pipeline with phong shading
		viewport.width = (float)width / 3.0f;
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.phong);
		vkCmdSetLineWidth(cmdBuffer, 1.0f);
		scene.draw(cmdBuffer);

		// Center : Render the scene using a toon style pipeline
		viewport.x = (float)width / 3.0f;
		vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
		vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.toon);
		// Line width > 1.0f only if wide lines feature is supported
		if (enabledFeatures.wideLines) {
			vkCmdSetLineWidth(cmdBuffer, 2.0f);
		}
		scene.draw(cmdBuffer);

		// Right : Render the scene as wireframe (if that feature is supported by the implementation)
		if (enabledFeatures.fillModeNonSolid) {
			viewport.x = (float)width / 3.0f + (float)width / 3.0f;
			vkCmdSetViewport(cmdBuffer, 0, 1, &viewport);
			vkCmdBindPipeline(cmdBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines.wireframe);
			scene.draw(cmdBuffer);
		}

		drawUI(cmdBuffer);

		vkCmdEndRenderPass(cmdBuffer);

		VK_CHECK_RESULT(vkEndCommandBuffer(cmdBuffer));
	}


	void dumpPipelineState(
		ztgfx::JsonWriter& out, const char* name, VkPolygonMode polygonMode,
		const char* vertexShader, const char* fragmentShader,
		float lineWidth, bool created)
	{
		out.key(name).beginObject();
		out.key("polygonMode").value(
			polygonMode == VK_POLYGON_MODE_LINE ? "LINE" : "FILL"
		);
		out.key("cullMode").value("BACK");
		out.key("frontFace").value("COUNTER_CLOCKWISE");
		out.key("lineWidth").value(lineWidth);
		out.key("depthCompare").value("LESS_OR_EQUAL");
		out.key("vertexShader").value(vertexShader);
		out.key("fragmentShader").value(fragmentShader);
		out.key("created").value(created);
		out.endObject();
	}

	void dumpParityMetadata(std::string filename)
	{
		const float aspect = ((float)width / 3.0f) / (float)height;
		const bool wireframeCreated = enabledFeatures.fillModeNonSolid;
		const float lineWidth = enabledFeatures.wideLines ? 2.0f : 1.0f;

		ztgfx::JsonWriter out(filename);
		ztgfx::Parity::header(out, parity.captureFrame, width, height);
		ztgfx::Parity::timeValue(out, parity.time);

		ztgfx::Parity::CameraLens lens{
			glm::radians(60.0f), aspect, 0.1f, 256.0f
		};
		ztgfx::Parity::camera(out, camera, &lens);
		ztgfx::Parity::light(out, glm::vec3(0.0f, 2.0f, 1.0f));

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

		out.key("features").beginObject();
		out.key("wideLines").value(enabledFeatures.wideLines != 0);
		out.key("fillModeNonSolid").value(enabledFeatures.fillModeNonSolid != 0);
		out.endObject();

		out.key("pipelines").beginObject();
		dumpPipelineState(out, "phong", VK_POLYGON_MODE_FILL,
			"pipelines/phong.vert.spv", "pipelines/phong.frag.spv", 1.0f, true);
		dumpPipelineState(out, "toon", VK_POLYGON_MODE_FILL,
			"pipelines/toon.vert.spv", "pipelines/toon.frag.spv",
			lineWidth, true);
		dumpPipelineState(out, "wireframe", VK_POLYGON_MODE_LINE,
			"pipelines/wireframe.vert.spv", "pipelines/wireframe.frag.spv",
			lineWidth, wireframeCreated);
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
				camera, glm::vec3(0.0f, 0.0f, -10.5f),
				glm::vec3(-25.0f, 15.0f, 0.0f)
			);
			camera.setPerspective(
				60.0f, ((float)width / 3.0f) / (float)height, 0.1f, 256.0f
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

	virtual void OnUpdateUIOverlay(vks::UIOverlay *overlay)
	{
		if (!enabledFeatures.fillModeNonSolid) {
			if (overlay->header("Info")) {
				overlay->text("Non solid fill modes not supported!");
			}
		}
	}
};

VULKAN_EXAMPLE_MAIN()
