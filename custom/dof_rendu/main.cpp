// Minimal SSAO example ported from the Vulkan 3D Graphics Rendering Cookbook.
// Source: PacktPublishing/3D-Graphics-Rendering-Cookbook-Second-Edition,
//         Chapter10/04_SSAO and data/shaders/Blur.comp (MIT), commit 6777c3e.
//
// This harness deliberately uses only the cookbook's SSAO/blur/combine
// algorithm. It has no Bistro, LightweightVK, ImGui, MSAA, or window.
// It loads a fixed raw fixture, runs the three compute passes, reads the
// final RGBA8 image back, and writes a PPM.

#include <vulkan/vulkan.h>
#include "VulkanTools.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef DOF_SHADER_DIR
#define DOF_SHADER_DIR "shaders"
#endif

#ifndef DOF_FIXTURE_DIR
#define DOF_FIXTURE_DIR "fixtures"
#endif

namespace {

constexpr uint32_t kWidth = 512;
constexpr uint32_t kHeight = 512;
constexpr uint32_t kRotationSize = 4;

struct Buffer {
  VkBuffer buffer = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkDeviceSize size = 0;
};

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

void destroyBuffer(VkDevice device, Buffer& buffer) {
  if (buffer.buffer) vkDestroyBuffer(device, buffer.buffer, nullptr);
  if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
  buffer = {};
}

Image createImage(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t width, uint32_t height,
                  VkFormat format, VkImageUsageFlags usage) {
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
  viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
  check(vkCreateImageView(device, &viewInfo, nullptr, &result.view), "vkCreateImageView");
  return result;
}

void destroyImage(VkDevice device, Image& image) {
  if (image.view) vkDestroyImageView(device, image.view, nullptr);
  if (image.image) vkDestroyImage(device, image.image, nullptr);
  if (image.memory) vkFreeMemory(device, image.memory, nullptr);
  image = {};
}

VkSampler createSampler(VkDevice device, VkFilter filter) {
  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = filter;
  samplerInfo.minFilter = filter;
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

void uploadImage(VkDevice device, VkCommandBuffer commandBuffer, Buffer& staging,
                 const Image& image, const std::vector<uint8_t>& bytes) {
  VkBufferMemoryBarrier hostBarrier{};
  hostBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  hostBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
  hostBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
  hostBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  hostBarrier.buffer = staging.buffer;
  hostBarrier.size = VK_WHOLE_SIZE;
  vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_HOST_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                       0, 0, nullptr, 1, &hostBarrier, 0, nullptr);

  imageBarrier(commandBuffer, image.image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               0, VK_ACCESS_TRANSFER_WRITE_BIT,
               VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

  VkBufferImageCopy region{};
  region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
  region.imageExtent = {image.width, image.height, 1};
  vkCmdCopyBufferToImage(commandBuffer, staging.buffer, image.image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  imageBarrier(commandBuffer, image.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
               VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
               VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
}

VkDescriptorSetLayout createDescriptorSetLayout(VkDevice device,
                                                const std::vector<VkDescriptorSetLayoutBinding>& bindings) {
  VkDescriptorSetLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
  layoutInfo.pBindings = bindings.data();
  VkDescriptorSetLayout layout = VK_NULL_HANDLE;
  check(vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &layout), "vkCreateDescriptorSetLayout");
  return layout;
}

VkPipelineLayout createPipelineLayout(VkDevice device, VkDescriptorSetLayout setLayout, uint32_t pushConstantSize) {
  VkPushConstantRange range{};
  range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  range.offset = 0;
  range.size = pushConstantSize;

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &setLayout;
  layoutInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;
  layoutInfo.pPushConstantRanges = pushConstantSize > 0 ? &range : nullptr;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout), "vkCreatePipelineLayout");
  return layout;
}

VkPipeline createPipeline(VkDevice device, const std::string& spvPath, VkPipelineLayout layout,
                          const VkSpecializationInfo* specialization = nullptr) {
  const VkShaderModule shader = vks::tools::loadShader(spvPath.c_str(), device);
  if (shader == VK_NULL_HANDLE) {
    fail("Cannot load shader " + spvPath);
  }

  VkComputePipelineCreateInfo pipelineInfo{};
  pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
  pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
  pipelineInfo.stage.module = shader;
  pipelineInfo.stage.pName = "main";
  pipelineInfo.stage.pSpecializationInfo = specialization;
  pipelineInfo.layout = layout;

  VkPipeline pipeline = VK_NULL_HANDLE;
  check(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "vkCreateComputePipelines");
  vkDestroyShaderModule(device, shader, nullptr);
  return pipeline;
}

void writePPM(const std::string& path, const uint8_t* rgba, uint32_t width, uint32_t height) {
  std::ofstream out(path, std::ios::binary);
  if (!out) {
    fail("Cannot open output " + path);
  }
  out << "P6\n" << width << " " << height << "\n255\n";
  for (uint32_t i = 0; i < width * height; ++i) {
    out.write(reinterpret_cast<const char*>(rgba + i * 4), 3);
  }
  if (!out) {
    fail("Cannot write output " + path);
  }
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

}  // namespace


int main(int argc, char** argv) {
  const std::string output = argc > 1 ? argv[1] : "dof.ppm";
  const std::string shaderDir = DOF_SHADER_DIR;
  const std::string fixtureDir = DOF_FIXTURE_DIR;
  constexpr uint32_t fullSize = 512;
  constexpr uint32_t halfSize = 256;

  try {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "dof_rendu";
    appInfo.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &appInfo;
    VkInstance instance = VK_NULL_HANDLE;
    check(vkCreateInstance(&instanceInfo, nullptr, &instance), "vkCreateInstance");

    uint32_t deviceCount = 0;
    check(vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr), "vkEnumeratePhysicalDevices");
    if (deviceCount == 0) fail("No Vulkan physical device");
    std::vector<VkPhysicalDevice> devices(deviceCount);
    check(vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data()), "vkEnumeratePhysicalDevices");
    VkPhysicalDevice physicalDevice = devices[0];
    VkPhysicalDeviceProperties props{};
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    std::cout << "Device: " << props.deviceName << "\n";

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
    uint32_t familyIndex = UINT32_MAX;
    for (uint32_t i = 0; i < familyCount; ++i) {
      if (families[i].queueFlags & VK_QUEUE_COMPUTE_BIT) { familyIndex = i; break; }
    }
    if (familyIndex == UINT32_MAX) fail("No compute queue");

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = familyIndex;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    VkDevice device = VK_NULL_HANDLE;
    check(vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device), "vkCreateDevice");
    VkQueue queue = VK_NULL_HANDLE;
    vkGetDeviceQueue(device, familyIndex, 0, &queue);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = familyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    check(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool");

    auto allocateCommandBuffer = [&]() {
      VkCommandBufferAllocateInfo info{};
      info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      info.commandPool = commandPool;
      info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      info.commandBufferCount = 1;
      VkCommandBuffer cb = VK_NULL_HANDLE;
      check(vkAllocateCommandBuffers(device, &info, &cb), "vkAllocateCommandBuffers");
      return cb;
    };
    auto submitAndWait = [&](VkCommandBuffer cb) {
      VkFenceCreateInfo fenceInfo{};
      fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
      VkFence fence = VK_NULL_HANDLE;
      check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "vkCreateFence");
      VkSubmitInfo submitInfo{};
      submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submitInfo.commandBufferCount = 1;
      submitInfo.pCommandBuffers = &cb;
      check(vkQueueSubmit(queue, 1, &submitInfo, fence), "vkQueueSubmit");
      check(vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX), "vkWaitForFences");
      vkDestroyFence(device, fence, nullptr);
    };

    Image sceneColor = createImage(device, physicalDevice, fullSize, fullSize,
                                   VK_FORMAT_R16G16B16A16_SFLOAT,
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    Image sceneDepth = createImage(device, physicalDevice, fullSize, fullSize,
                                   VK_FORMAT_R32_SFLOAT,
                                   VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    Image halfColor = createImage(device, physicalDevice, halfSize, halfSize,
                                  VK_FORMAT_R16G16B16A16_SFLOAT,
                                  VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    Image cocDepth = createImage(device, physicalDevice, halfSize, halfSize,
                                 VK_FORMAT_R32G32B32A32_SFLOAT,
                                 VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    Image blur = createImage(device, physicalDevice, halfSize, halfSize,
                             VK_FORMAT_R16G16B16A16_SFLOAT,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    Image finalImage = createImage(device, physicalDevice, fullSize, fullSize,
                                   VK_FORMAT_R16G16B16A16_SFLOAT,
                                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

    const auto colorBytes = readFile(fixtureDir + "/color.rgba16f");
    const auto depthBytes = readFile(fixtureDir + "/depth.r32f");
    if (colorBytes.size() != fullSize * fullSize * 8 ||
        depthBytes.size() != fullSize * fullSize * 4) {
      fail("Fixture size mismatch");
    }
    Buffer colorStaging = createBuffer(device, physicalDevice, colorBytes.size(),
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buffer depthStaging = createBuffer(device, physicalDevice, depthBytes.size(),
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    auto uploadBytes = [&](Buffer& buffer, const std::vector<uint8_t>& bytes) {
      void* mapped = nullptr;
      check(vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
      std::memcpy(mapped, bytes.data(), bytes.size());
      vkUnmapMemory(device, buffer.memory);
    };
    uploadBytes(colorStaging, colorBytes);
    uploadBytes(depthStaging, depthBytes);

    {
      VkCommandBuffer cb = allocateCommandBuffer();
      VkCommandBufferBeginInfo beginInfo{};
      beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      check(vkBeginCommandBuffer(cb, &beginInfo), "vkBeginCommandBuffer(upload)");
      uploadImage(device, cb, colorStaging, sceneColor, colorBytes);
      uploadImage(device, cb, depthStaging, sceneDepth, depthBytes);
      check(vkEndCommandBuffer(cb), "vkEndCommandBuffer(upload)");
      submitAndWait(cb);
      vkFreeCommandBuffers(device, commandPool, 1, &cb);
    }
    destroyBuffer(device, colorStaging);
    destroyBuffer(device, depthStaging);

    const VkSampler linearSampler = createSampler(device, VK_FILTER_LINEAR);
    const VkSampler nearestSampler = createSampler(device, VK_FILTER_NEAREST);

    VkDescriptorSetLayoutBinding cocBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    std::vector<VkDescriptorSetLayoutBinding> cocBindingList(cocBindings, cocBindings + 4);
    VkDescriptorSetLayout cocLayout = createDescriptorSetLayout(device, cocBindingList);
    VkPipelineLayout cocPipelineLayout = createPipelineLayout(device, cocLayout, 4 * sizeof(float));

    VkDescriptorSetLayoutBinding gatherBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    std::vector<VkDescriptorSetLayoutBinding> gatherBindingList(gatherBindings, gatherBindings + 3);
    VkDescriptorSetLayout gatherLayout = createDescriptorSetLayout(device, gatherBindingList);
    VkPipelineLayout gatherPipelineLayout = createPipelineLayout(device, gatherLayout, 2 * sizeof(float));

    VkDescriptorSetLayoutBinding compositeBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    std::vector<VkDescriptorSetLayoutBinding> compositeBindingList(compositeBindings, compositeBindings + 3);
    VkDescriptorSetLayout compositeLayout = createDescriptorSetLayout(device, compositeBindingList);
    VkPipelineLayout compositePipelineLayout = createPipelineLayout(device, compositeLayout, 0);

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 6},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 5},
    };
    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = 2;
    descriptorPoolInfo.pPoolSizes = poolSizes;
    descriptorPoolInfo.maxSets = 3;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    check(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");

    auto allocateSet = [&](VkDescriptorSetLayout layout) {
      VkDescriptorSetAllocateInfo info{};
      info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
      info.descriptorPool = descriptorPool;
      info.descriptorSetCount = 1;
      info.pSetLayouts = &layout;
      VkDescriptorSet set = VK_NULL_HANDLE;
      check(vkAllocateDescriptorSets(device, &info, &set), "vkAllocateDescriptorSets");
      return set;
    };
    VkDescriptorSet cocSet = allocateSet(cocLayout);
    VkDescriptorSet gatherSet = allocateSet(gatherLayout);
    VkDescriptorSet compositeSet = allocateSet(compositeLayout);

    auto updateImageDescriptor = [&](VkDescriptorSet set, uint32_t binding, VkDescriptorType type,
                                     VkImageView view, VkSampler sampler, VkImageLayout layout) {
      VkDescriptorImageInfo imageInfo{};
      imageInfo.sampler = sampler;
      imageInfo.imageView = view;
      imageInfo.imageLayout = layout;
      VkWriteDescriptorSet write{};
      write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
      write.dstSet = set;
      write.dstBinding = binding;
      write.descriptorCount = 1;
      write.descriptorType = type;
      write.pImageInfo = &imageInfo;
      vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    };

    updateImageDescriptor(cocSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sceneColor.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(cocSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sceneDepth.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(cocSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, halfColor.view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);
    updateImageDescriptor(cocSet, 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, cocDepth.view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);

    updateImageDescriptor(gatherSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, halfColor.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(gatherSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, cocDepth.view, nearestSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(gatherSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, blur.view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);

    updateImageDescriptor(compositeSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sceneColor.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(compositeSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, blur.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(compositeSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, finalImage.view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);

    VkPipeline cocPipeline = createPipeline(device, shaderDir + "/dof-coc.comp.spv", cocPipelineLayout);
    VkPipeline gatherPipeline = createPipeline(device, shaderDir + "/dof-gather.comp.spv", gatherPipelineLayout);
    VkPipeline compositePipeline = createPipeline(device, shaderDir + "/dof-composite.comp.spv", compositePipelineLayout);

    Buffer readback = createBuffer(device, physicalDevice, fullSize * fullSize * 8,
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkCommandBuffer cb = allocateCommandBuffer();
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(cb, &beginInfo), "vkBeginCommandBuffer");

    for (VkImage image : {halfColor.image, cocDepth.image, blur.image, finalImage.image}) {
      imageBarrier(cb, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                   0, VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    struct CocPush { float projX, projY, focusDist, focusScale; };
    const CocPush cocPush{-100.0f / 99.9f, -(100.0f * 0.1f) / 99.9f, 2.0f, 10.0f};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cocPipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, cocPipelineLayout, 0, 1, &cocSet, 0, nullptr);
    vkCmdPushConstants(cb, cocPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CocPush), &cocPush);
    vkCmdDispatch(cb, halfSize / 8, halfSize / 8, 1);

    imageBarrier(cb, halfColor.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    imageBarrier(cb, cocDepth.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    struct GatherPush { float invX, invY; };
    const GatherPush gatherPush{1.0f / halfSize, 1.0f / halfSize};
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, gatherPipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, gatherPipelineLayout, 0, 1, &gatherSet, 0, nullptr);
    vkCmdPushConstants(cb, gatherPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(GatherPush), &gatherPush);
    vkCmdDispatch(cb, halfSize / 8, halfSize / 8, 1);

    imageBarrier(cb, blur.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compositePipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, compositePipelineLayout, 0, 1, &compositeSet, 0, nullptr);
    vkCmdDispatch(cb, fullSize / 16, fullSize / 16, 1);

    imageBarrier(cb, finalImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent = {fullSize, fullSize, 1};
    vkCmdCopyImageToBuffer(cb, finalImage.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           readback.buffer, 1, &copyRegion);

    VkBufferMemoryBarrier readBarrier{};
    readBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    readBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    readBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    readBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    readBarrier.buffer = readback.buffer;
    readBarrier.size = VK_WHOLE_SIZE;
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                         0, 0, nullptr, 1, &readBarrier, 0, nullptr);

    check(vkEndCommandBuffer(cb), "vkEndCommandBuffer");
    submitAndWait(cb);
    vkFreeCommandBuffers(device, commandPool, 1, &cb);

    void* mapped = nullptr;
    check(vkMapMemory(device, readback.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory(readback)");
    writePPMHalf(output, static_cast<const uint8_t*>(mapped), fullSize, fullSize);
    vkUnmapMemory(device, readback.memory);
    std::cout << "Wrote " << output << " (" << fullSize << "x" << fullSize << ")\n";

    vkDestroyBuffer(device, readback.buffer, nullptr);
    vkFreeMemory(device, readback.memory, nullptr);
    vkDestroyPipeline(device, compositePipeline, nullptr);
    vkDestroyPipeline(device, gatherPipeline, nullptr);
    vkDestroyPipeline(device, cocPipeline, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyPipelineLayout(device, compositePipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, gatherPipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, cocPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, compositeLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, gatherLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, cocLayout, nullptr);
    vkDestroySampler(device, nearestSampler, nullptr);
    vkDestroySampler(device, linearSampler, nullptr);
    destroyImage(device, finalImage);
    destroyImage(device, blur);
    destroyImage(device, cocDepth);
    destroyImage(device, halfColor);
    destroyImage(device, sceneDepth);
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
