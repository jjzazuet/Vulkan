// Minimal standalone baseline for the zt-gfx camera-only motion blur (B6).
//
// The effect shaders are the exact files used by the Java fixture:
//   scene.frag  - draws the line/depth/velocity scene
//   motion.frag - 8-tap symmetric blur along the velocity buffer
//
// This harness renders the same 256x256 scene with the verified reprojection
// matrix, applies the blur, reads the RGBA16F result back, and writes a PPM.
// It has no engine, asset pack, window, or ImGui.

#include <vulkan/vulkan.h>
#include "VulkanTools.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef MOTION_SHADER_DIR
#define MOTION_SHADER_DIR "shaders"
#endif

namespace {

constexpr uint32_t kSize = 256;

struct Image {
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0;
  uint32_t height = 0;
};

void fail(const std::string& message) {
  throw std::runtime_error(message);
}

void check(VkResult result, const char* what) {
  if (result != VK_SUCCESS) {
    fail(std::string(what) + " failed with VkResult " + std::to_string(result));
  }
}

std::vector<uint8_t> readFile(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    fail("Cannot open " + path);
  }
  in.seekg(0, std::ios::end);
  const auto size = static_cast<size_t>(in.tellg());
  in.seekg(0, std::ios::beg);
  std::vector<uint8_t> data(size);
  in.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size));
  if (!in) {
    fail("Cannot read " + path);
  }
  return data;
}

uint32_t findMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeBits, VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memoryProperties{};
  vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);
  for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
    if ((typeBits & (1u << i)) &&
        (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties) {
      return i;
    }
  }
  fail("No suitable memory type");
  return 0;
}

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};

Buffer createBuffer(VkDevice device, VkPhysicalDevice physicalDevice, VkDeviceSize size,
                    VkBufferUsageFlags usage, VkMemoryPropertyFlags properties) {
  Buffer result{};
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  check(vkCreateBuffer(device, &bufferInfo, nullptr, &result.buffer), "vkCreateBuffer");

  VkMemoryRequirements requirements{};
  vkGetBufferMemoryRequirements(device, result.buffer, &requirements);
  VkMemoryAllocateInfo allocateInfo{};
  allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocateInfo.allocationSize = requirements.size;
  allocateInfo.memoryTypeIndex = findMemoryType(physicalDevice, requirements.memoryTypeBits, properties);
  check(vkAllocateMemory(device, &allocateInfo, nullptr, &result.memory), "vkAllocateMemory(buffer)");
  check(vkBindBufferMemory(device, result.buffer, result.memory, 0), "vkBindBufferMemory");
  result.size = size;
  return result;
}

Image createImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                  VkFormat format, VkImageUsageFlags usage, bool depth) {
  Image result{};
  result.format = format;
  result.width = width;
  result.height = height;

  VkImageCreateInfo imageInfo{};
  imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  imageInfo.imageType = VK_IMAGE_TYPE_2D;
  imageInfo.format = format;
  imageInfo.extent = {width, height, 1};
  imageInfo.mipLevels = 1;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
  imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
  imageInfo.usage = usage;
  imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  check(vkCreateImage(device, &imageInfo, nullptr, &result.image), "vkCreateImage");

  VkMemoryRequirements requirements{};
  vkGetImageMemoryRequirements(device, result.image, &requirements);
  VkMemoryAllocateInfo allocateInfo{};
  allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocateInfo.allocationSize = requirements.size;
  allocateInfo.memoryTypeIndex = findMemoryType(physicalDevice, requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
  check(vkAllocateMemory(device, &allocateInfo, nullptr, &result.memory), "vkAllocateMemory(image)");
  check(vkBindImageMemory(device, result.image, result.memory, 0), "vkBindImageMemory");

  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = result.image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange = {
    depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1
  };
  check(vkCreateImageView(device, &viewInfo, nullptr, &result.view), "vkCreateImageView");
  return result;
}

void destroyImage(VkDevice device, Image& image) {
  if (image.view) vkDestroyImageView(device, image.view, nullptr);
  if (image.image) vkDestroyImage(device, image.image, nullptr);
  if (image.memory) vkFreeMemory(device, image.memory, nullptr);
  image = {};
}

VkSampler createSampler(VkDevice device) {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
  VkSampler sampler = VK_NULL_HANDLE;
  check(vkCreateSampler(device, &samplerInfo, nullptr, &sampler), "vkCreateSampler");
  return sampler;
}

void imageBarrier(VkCommandBuffer commandBuffer, VkImage image,
                  VkImageLayout oldLayout, VkImageLayout newLayout,
                  VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                  VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.srcAccessMask = srcAccess;
  barrier.dstAccessMask = dstAccess;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkShaderModule loadShaderModule(VkDevice device, const std::string& path) {
  const auto bytes = readFile(path);
  if (bytes.size() % 4 != 0) {
    fail("Shader size is not a multiple of 4: " + path);
  }
  VkShaderModuleCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
  info.codeSize = bytes.size();
  info.pCode = reinterpret_cast<const uint32_t*>(bytes.data());
  VkShaderModule module = VK_NULL_HANDLE;
  check(vkCreateShaderModule(device, &info, nullptr, &module), "vkCreateShaderModule");
  return module;
}

VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device) {
  VkDescriptorSetLayoutBinding bindings[2]{};
  for (uint32_t i = 0; i < 2; ++i) {
    bindings[i].binding = i;
    bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[i].descriptorCount = 1;
    bindings[i].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  }
  VkDescriptorSetLayoutCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  info.bindingCount = 2;
  info.pBindings = bindings;
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  check(vkCreateDescriptorSetLayout(device, &info, nullptr, &layout), "vkCreateDescriptorSetLayout");
  return layout;
}

VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout, uint32_t pushSize) {
  VkPushConstantRange range{};
  range.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
  range.offset = 0;
  range.size = pushSize;

  VkPipelineLayoutCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  info.setLayoutCount = setLayout ? 1 : 0;
  info.pSetLayouts = setLayout ? &setLayout : nullptr;
  info.pushConstantRangeCount = pushSize > 0 ? 1 : 0;
  info.pPushConstantRanges = pushSize > 0 ? &range : nullptr;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  check(vkCreatePipelineLayout(device, &info, nullptr, &layout), "vkCreatePipelineLayout");
  return layout;
}

VkPipeline createGraphicsPipeline(
    VkDevice device, const std::string& vertPath, const std::string& fragPath,
    VkPipelineLayout layout, VkRenderPass renderPass, bool depthTest,
    uint32_t colorAttachments) {
  VkShaderModule vert = loadShaderModule(device, vertPath);
  VkShaderModule frag = loadShaderModule(device, fragPath);

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vert;
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = frag;
  stages[1].pName = "main";

  VkPipelineVertexInputStateCreateInfo vertexInput{};
  vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

  VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
  inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
  inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkViewport viewport{0.0f, 0.0f, static_cast<float>(kSize), static_cast<float>(kSize), 0.0f, 1.0f};
  VkRect2D scissor{{0, 0}, {kSize, kSize}};
  VkPipelineViewportStateCreateInfo viewportState{};
  viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
  viewportState.viewportCount = 1;
  viewportState.pViewports = &viewport;
  viewportState.scissorCount = 1;
  viewportState.pScissors = &scissor;

  VkPipelineRasterizationStateCreateInfo raster{};
  raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
  raster.polygonMode = VK_POLYGON_MODE_FILL;
  raster.cullMode = VK_CULL_MODE_NONE;
  raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  raster.lineWidth = 1.0f;

  VkPipelineMultisampleStateCreateInfo multisample{};
  multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
  multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo depthStencil{};
  depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
  if (depthTest) {
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
  }

  std::array<VkPipelineColorBlendAttachmentState, 2> blend{};
  for (uint32_t i = 0; i < colorAttachments; ++i) {
    blend[i].blendEnable = VK_FALSE;
    blend[i].colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  }
  VkPipelineColorBlendStateCreateInfo colorBlend{};
  colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
  colorBlend.attachmentCount = colorAttachments;
  colorBlend.pAttachments = blend.data();

  VkGraphicsPipelineCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
  info.stageCount = 2;
  info.pStages = stages;
  info.pVertexInputState = &vertexInput;
  info.pInputAssemblyState = &inputAssembly;
  info.pViewportState = &viewportState;
  info.pRasterizationState = &raster;
  info.pMultisampleState = &multisample;
  info.pDepthStencilState = &depthStencil;
  info.pColorBlendState = &colorBlend;
  info.layout = layout;
  info.renderPass = renderPass;
  info.subpass = 0;

  VkPipeline pipeline = VK_NULL_HANDLE;
  check(vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &info, nullptr, &pipeline),
        "vkCreateGraphicsPipelines");
  vkDestroyShaderModule(device, frag, nullptr);
  vkDestroyShaderModule(device, vert, nullptr);
  return pipeline;
}

VkRenderPass createSceneRenderPass(VkDevice device) {
  std::array<VkAttachmentDescription, 3> attachments{};
  attachments[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
  attachments[1].format = VK_FORMAT_R16G16_SFLOAT;
  attachments[2].format = VK_FORMAT_D32_SFLOAT;
  for (uint32_t i = 0; i < 2; ++i) {
    attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  }
  attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
  attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachments[2].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference colorRefs[2]{};
  colorRefs[0] = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  colorRefs[1] = {1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depthRef{2, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 2;
  subpass.pColorAttachments = colorRefs;
  subpass.pDepthStencilAttachment = &depthRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = static_cast<uint32_t>(attachments.size());
  info.pAttachments = attachments.data();
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 1;
  info.pDependencies = &dependency;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  check(vkCreateRenderPass(device, &info, nullptr, &renderPass), "vkCreateRenderPass(scene)");
  return renderPass;
}

VkRenderPass createBlurRenderPass(VkDevice device) {
  VkAttachmentDescription attachment{};
  attachment.format = VK_FORMAT_R16G16B16A16_SFLOAT;
  attachment.samples = VK_SAMPLE_COUNT_1_BIT;
  attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

  VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorRef;

  VkSubpassDependency dependency{};
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.dstSubpass = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

  VkRenderPassCreateInfo info{};
  info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  info.attachmentCount = 1;
  info.pAttachments = &attachment;
  info.subpassCount = 1;
  info.pSubpasses = &subpass;
  info.dependencyCount = 1;
  info.pDependencies = &dependency;
  VkRenderPass renderPass = VK_NULL_HANDLE;
  check(vkCreateRenderPass(device, &info, nullptr, &renderPass), "vkCreateRenderPass(blur)");
  return renderPass;
}

float halfToFloat(uint16_t h) {
  const uint32_t sign = (h >> 15) & 0x1u;
  const uint32_t exp = (h >> 10) & 0x1Fu;
  const uint32_t mant = h & 0x3FFu;
  float value;
  if (exp == 0) {
    value = static_cast<float>(mant) * 5.960464477539063e-8f;
  } else if (exp == 31) {
    value = mant ? std::numeric_limits<float>::quiet_NaN() : std::numeric_limits<float>::infinity();
  } else {
    value = std::ldexp(1.0f + static_cast<float>(mant) / 1024.0f, static_cast<int>(exp) - 15);
  }
  return sign ? -value : value;
}

void writePPMHalf(const std::string& path, const uint8_t* rgba, uint32_t width, uint32_t height) {
  std::ofstream out(path, std::ios::binary);
  if (!out) fail("Cannot open output " + path);
  out << "P6\n" << width << " " << height << "\n255\n";
  for (uint32_t i = 0; i < width * height; ++i) {
    const uint16_t* h = reinterpret_cast<const uint16_t*>(rgba + i * 8);
    uint8_t rgb[3];
    for (int c = 0; c < 3; ++c) {
      float v = halfToFloat(h[c]);
      if (std::isnan(v)) v = 0.0f;
      v = std::max(0.0f, std::min(1.0f, v));
      rgb[c] = static_cast<uint8_t>(std::lround(v * 255.0f));
    }
    out.write(reinterpret_cast<const char*>(rgb), 3);
  }
  if (!out) fail("Cannot write output " + path);
}

float floatBits(uint32_t bits) {
  float value;
  std::memcpy(&value, &bits, sizeof(value));
  return value;
}

// Column-major previousViewProj * inverse(currentViewProj) for the Java fixture:
// camera yaw 0 -> 8 degrees, yfov 1.0 rad, aspect 1, near 0.1, far 100.
const float kReprojection[16] = {
  floatBits(0x3f7d8236u), floatBits(0x00000000u), floatBits(0xbd9bddb3u), floatBits(0xbd9bb5ccu),
  floatBits(0x00000000u), floatBits(0x3f7fffdfu), floatBits(0x00000000u), floatBits(0x00000000u),
  floatBits(0xc14b998bu), floatBits(0x00000000u), floatBits(0x3fbe4880u), floatBits(0x3ef8e280u),
  floatBits(0x414fe131u), floatBits(0x00000000u), floatBits(0xbefe6080u), floatBits(0x3f00f040u),
};

}  // namespace

int main(int argc, char** argv) {
  const std::string output = argc > 1 ? argv[1] : "motion.ppm";
  const std::string shaderDir = MOTION_SHADER_DIR;

  try {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "motion_blur";
    appInfo.apiVersion = VK_API_VERSION_1_1;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    uint32_t physicalDeviceCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, nullptr), "vkEnumeratePhysicalDevices");
    if (physicalDeviceCount == 0) {
      fail("No Vulkan physical device");
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalDeviceCount);
    check(vkEnumeratePhysicalDevices(instance, &physicalDeviceCount, physicalDevices.data()), "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physicalDevice = physicalDevices[0];

    VkPhysicalDeviceProperties deviceProperties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    std::cout << "Device: " << deviceProperties.deviceName << "\n";

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());
    uint32_t queueFamilyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < queueFamilyCount; ++i) {
      if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
        queueFamilyIndex = i;
        break;
      }
    }
    if (queueFamilyIndex == UINT32_MAX) {
      fail("No graphics queue");
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamilyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;

    VkDevice device = VK_NULL_HANDLE;
    check(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device), "vkCreateDevice");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, queueFamilyIndex, 0, &queue);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    check(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool");

    Image sceneColor = createImage(device, physicalDevice, kSize, kSize,
                                   VK_FORMAT_R16G16B16A16_SFLOAT,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);
    Image velocity = createImage(device, physicalDevice, kSize, kSize,
                                 VK_FORMAT_R16G16_SFLOAT,
                                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT, false);
    Image depth = createImage(device, physicalDevice, kSize, kSize,
                              VK_FORMAT_D32_SFLOAT,
                              VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, true);
    Image finalImage = createImage(device, physicalDevice, kSize, kSize,
                                   VK_FORMAT_R16G16B16A16_SFLOAT,
                                   VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, false);

    VkRenderPass scenePass = createSceneRenderPass(device);
    VkRenderPass blurPass = createBlurRenderPass(device);

    VkImageView sceneViews[3] = {sceneColor.view, velocity.view, depth.view};
    VkFramebufferCreateInfo sceneFbInfo{};
    sceneFbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    sceneFbInfo.renderPass = scenePass;
    sceneFbInfo.attachmentCount = 3;
    sceneFbInfo.pAttachments = sceneViews;
    sceneFbInfo.width = kSize;
    sceneFbInfo.height = kSize;
    sceneFbInfo.layers = 1;
    VkFramebuffer sceneFramebuffer = VK_NULL_HANDLE;
    check(vkCreateFramebuffer(device, &sceneFbInfo, nullptr, &sceneFramebuffer), "vkCreateFramebuffer(scene)");

    VkFramebufferCreateInfo blurFbInfo{};
    blurFbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    blurFbInfo.renderPass = blurPass;
    blurFbInfo.attachmentCount = 1;
    blurFbInfo.pAttachments = &finalImage.view;
    blurFbInfo.width = kSize;
    blurFbInfo.height = kSize;
    blurFbInfo.layers = 1;
    VkFramebuffer blurFramebuffer = VK_NULL_HANDLE;
    check(vkCreateFramebuffer(device, &blurFbInfo, nullptr, &blurFramebuffer), "vkCreateFramebuffer(blur)");

    VkSampler sampler = createSampler(device);
    VkDescriptorSetLayout blurSetLayout = createDescriptorSetLayout(device);
    VkPipelineLayout scenePipelineLayout = createPipelineLayout(device, VK_NULL_HANDLE, 16 * sizeof(float));
    VkPipelineLayout blurPipelineLayout = createPipelineLayout(device, blurSetLayout, 2 * sizeof(float));

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2};
    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = 1;
    descriptorPoolInfo.pPoolSizes = &poolSize;
    descriptorPoolInfo.maxSets = 1;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    check(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateInfo.descriptorPool = descriptorPool;
    allocateInfo.descriptorSetCount = 1;
    allocateInfo.pSetLayouts = &blurSetLayout;
    VkDescriptorSet blurSet = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &allocateInfo, &blurSet), "vkAllocateDescriptorSets");

    auto updateImageDescriptor = [&](uint32_t binding, VkImageView view) {
      VkDescriptorImageInfo imageInfo{};
      imageInfo.sampler = sampler;
      imageInfo.imageView = view;
      imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      VkWriteDescriptorSet write{};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = blurSet;
      write.dstBinding = binding;
      write.descriptorCount = 1;
      write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
      write.pImageInfo = &imageInfo;
      vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    };
    updateImageDescriptor(0, sceneColor.view);
    updateImageDescriptor(1, velocity.view);

    VkPipeline scenePipeline = createGraphicsPipeline(
        device, shaderDir + "/fullscreen.vert.spv", shaderDir + "/scene.frag.spv",
        scenePipelineLayout, scenePass, true, 2);
    VkPipeline blurPipeline = createGraphicsPipeline(
        device, shaderDir + "/fullscreen.vert.spv", shaderDir + "/motion.frag.spv",
        blurPipelineLayout, blurPass, false, 1);

    Buffer readback = createBuffer(device, physicalDevice, kSize * kSize * 8,
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkCommandBufferAllocateInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandInfo.commandPool = commandPool;
    commandInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    check(vkAllocateCommandBuffers(device, &commandInfo, &commandBuffer), "vkAllocateCommandBuffers");

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    VkClearValue sceneClears[3]{};
    sceneClears[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    sceneClears[1].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    sceneClears[2].depthStencil = {1.0f, 0};
    VkRenderPassBeginInfo sceneBegin{};
    sceneBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    sceneBegin.renderPass = scenePass;
    sceneBegin.framebuffer = sceneFramebuffer;
    sceneBegin.renderArea = {{0, 0}, {kSize, kSize}};
    sceneBegin.clearValueCount = 3;
    sceneBegin.pClearValues = sceneClears;
    vkCmdBeginRenderPass(commandBuffer, &sceneBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, scenePipeline);
    vkCmdPushConstants(commandBuffer, scenePipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(kReprojection), kReprojection);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    imageBarrier(commandBuffer, sceneColor.image,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    imageBarrier(commandBuffer, velocity.image,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);

    VkClearValue blurClear{};
    blurClear.color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    VkRenderPassBeginInfo blurBegin{};
    blurBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    blurBegin.renderPass = blurPass;
    blurBegin.framebuffer = blurFramebuffer;
    blurBegin.renderArea = {{0, 0}, {kSize, kSize}};
    blurBegin.clearValueCount = 1;
    blurBegin.pClearValues = &blurClear;
    vkCmdBeginRenderPass(commandBuffer, &blurBegin, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, blurPipelineLayout,
                            0, 1, &blurSet, 0, nullptr);
    struct MotionPush { float shutterAngle, maxBlurPixels; };
    const MotionPush motionPush{180.0f, 16.0f};
    vkCmdPushConstants(commandBuffer, blurPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(MotionPush), &motionPush);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRenderPass(commandBuffer);

    imageBarrier(commandBuffer, finalImage.image,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent = {kSize, kSize, 1};
    vkCmdCopyImageToBuffer(commandBuffer, finalImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback.buffer, 1, &copyRegion);

    VkBufferMemoryBarrier readBarrier{};
    readBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    readBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    readBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    readBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readBarrier.buffer = readback.buffer;
    readBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &readBarrier, 0, nullptr);

    check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    VkFence fence = VK_NULL_HANDLE;
    check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    check(vkQueueSubmit(queue, 1, &submitInfo, fence), "vkQueueSubmit");
    check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    vkDestroyFence(device, fence, nullptr);

    void* mapped = nullptr;
    check(vkMapMemory(device, readback.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory(readback)");
    writePPMHalf(output, static_cast<const uint8_t*>(mapped), kSize, kSize);
    vkUnmapMemory(device, readback.memory);

    std::cout << "Wrote " << output << " (" << kSize << "x" << kSize << ")\n";

    vkDestroyBuffer(device, readback.buffer, nullptr);
    vkFreeMemory(device, readback.memory, nullptr);
    vkDestroyPipeline(device, blurPipeline, nullptr);
    vkDestroyPipeline(device, scenePipeline, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyPipelineLayout(device, blurPipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, scenePipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, blurSetLayout, nullptr);
    vkDestroySampler(device, sampler, nullptr);
    vkDestroyFramebuffer(device, blurFramebuffer, nullptr);
    vkDestroyFramebuffer(device, sceneFramebuffer, nullptr);
    vkDestroyRenderPass(device, blurPass, nullptr);
    vkDestroyRenderPass(device, scenePass, nullptr);
    destroyImage(device, finalImage);
    destroyImage(device, depth);
    destroyImage(device, velocity);
    destroyImage(device, sceneColor);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
  }
}
