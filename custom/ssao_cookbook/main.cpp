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

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifndef SSAO_SHADER_DIR
#define SSAO_SHADER_DIR "shaders"
#endif

#ifndef SSAO_FIXTURE_DIR
#define SSAO_FIXTURE_DIR "fixtures"
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

}  // namespace

int main(int argc, char** argv) {
  const std::string output = argc > 1 ? argv[1] : "ssao.ppm";
  const std::string shaderDir = SSAO_SHADER_DIR;
  const std::string fixtureDir = SSAO_FIXTURE_DIR;

  try {
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "ssao_cookbook";
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
      if (queueFamilies[i].queueFlags & VK_QUEUE_COMPUTE_BIT) {
        queueFamilyIndex = i;
        break;
      }
    }
    if (queueFamilyIndex == UINT32_MAX) {
      fail("No compute queue");
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

    auto allocateCommandBuffer = [&]() {
      VkCommandBufferAllocateInfo allocateInfo{};
      allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
      allocateInfo.commandPool = commandPool;
      allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
      allocateInfo.commandBufferCount = 1;
      VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
      check(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer), "vkAllocateCommandBuffers");
      return commandBuffer;
    };

    auto submitAndWait = [&](VkCommandBuffer commandBuffer) {
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
    };

    Image depth = createImage(device, physicalDevice, kWidth, kHeight, VK_FORMAT_R32_SFLOAT,
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    Image color = createImage(device, physicalDevice, kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    Image rotation = createImage(device, physicalDevice, kRotationSize, kRotationSize, VK_FORMAT_R8G8B8A8_UNORM,
                                 VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    Image ssao = createImage(device, physicalDevice, kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    Image blurH = createImage(device, physicalDevice, kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    Image blurV = createImage(device, physicalDevice, kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM,
                              VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    Image finalImage = createImage(device, physicalDevice, kWidth, kHeight, VK_FORMAT_R8G8B8A8_UNORM,
                                   VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);

    const auto depthBytes = readFile(fixtureDir + "/depth.r32f");
    const auto colorBytes = readFile(fixtureDir + "/color.rgba8");
    const auto rotationBytes = readFile(fixtureDir + "/rotation.rgba8");
    if (depthBytes.size() != kWidth * kHeight * 4 ||
        colorBytes.size() != kWidth * kHeight * 4 ||
        rotationBytes.size() != kRotationSize * kRotationSize * 4) {
      fail("Fixture size mismatch");
    }

    Buffer depthStaging = createBuffer(device, physicalDevice, depthBytes.size(),
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buffer colorStaging = createBuffer(device, physicalDevice, colorBytes.size(),
                                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    Buffer rotationStaging = createBuffer(device, physicalDevice, rotationBytes.size(),
                                          VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    auto uploadBytes = [&](Buffer& buffer, const std::vector<uint8_t>& bytes) {
      void* mapped = nullptr;
      check(vkMapMemory(device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory");
      std::memcpy(mapped, bytes.data(), bytes.size());
      vkUnmapMemory(device, buffer.memory);
    };
    uploadBytes(depthStaging, depthBytes);
    uploadBytes(colorStaging, colorBytes);
    uploadBytes(rotationStaging, rotationBytes);

    {
      VkCommandBuffer commandBuffer = allocateCommandBuffer();
      VkCommandBufferBeginInfo beginInfo{};
      beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
      beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
      check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer(upload)");
      uploadImage(device, commandBuffer, depthStaging, depth, depthBytes);
      uploadImage(device, commandBuffer, colorStaging, color, colorBytes);
      uploadImage(device, commandBuffer, rotationStaging, rotation, rotationBytes);
      check(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer(upload)");
      submitAndWait(commandBuffer);
      vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);
    }
    destroyBuffer(device, depthStaging);
    destroyBuffer(device, colorStaging);
    destroyBuffer(device, rotationStaging);

    const VkSampler linearSampler = createSampler(device, VK_FILTER_LINEAR);
    const VkSampler nearestSampler = createSampler(device, VK_FILTER_NEAREST);

    VkDescriptorSetLayoutBinding ssaoBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    std::vector<VkDescriptorSetLayoutBinding> ssaoBindingList(ssaoBindings, ssaoBindings + 3);
    VkDescriptorSetLayout ssaoLayout = createDescriptorSetLayout(device, ssaoBindingList);
    VkPipelineLayout ssaoPipelineLayout = createPipelineLayout(device, ssaoLayout, 5 * sizeof(float));

    VkDescriptorSetLayoutBinding blurBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    std::vector<VkDescriptorSetLayoutBinding> blurBindingList(blurBindings, blurBindings + 3);
    VkDescriptorSetLayout blurLayout = createDescriptorSetLayout(device, blurBindingList);
    VkPipelineLayout blurPipelineLayout = createPipelineLayout(device, blurLayout, sizeof(float));

    VkDescriptorSetLayoutBinding combineBindings[] = {
        {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
        {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr},
    };
    std::vector<VkDescriptorSetLayoutBinding> combineBindingList(combineBindings, combineBindings + 3);
    VkDescriptorSetLayout combineLayout = createDescriptorSetLayout(device, combineBindingList);
    VkPipelineLayout combinePipelineLayout = createPipelineLayout(device, combineLayout, 2 * sizeof(float));

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 8},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 8},
    };
    VkDescriptorPoolCreateInfo descriptorPoolInfo{};
    descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descriptorPoolInfo.poolSizeCount = 2;
    descriptorPoolInfo.pPoolSizes = poolSizes;
    descriptorPoolInfo.maxSets = 4;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    check(vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool), "vkCreateDescriptorPool");

    VkDescriptorSetAllocateInfo allocateSsao{};
    allocateSsao.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateSsao.descriptorPool = descriptorPool;
    allocateSsao.descriptorSetCount = 1;
    allocateSsao.pSetLayouts = &ssaoLayout;
    VkDescriptorSet ssaoSet = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &allocateSsao, &ssaoSet), "vkAllocateDescriptorSets(ssao)");

    VkDescriptorSetAllocateInfo allocateBlur{};
    allocateBlur.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateBlur.descriptorPool = descriptorPool;
    allocateBlur.descriptorSetCount = 1;
    allocateBlur.pSetLayouts = &blurLayout;
    VkDescriptorSet blurHSet = VK_NULL_HANDLE;
    VkDescriptorSet blurVSet = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &allocateBlur, &blurHSet), "vkAllocateDescriptorSets(blurH)");
    check(vkAllocateDescriptorSets(device, &allocateBlur, &blurVSet), "vkAllocateDescriptorSets(blurV)");

    VkDescriptorSetAllocateInfo allocateCombine{};
    allocateCombine.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocateCombine.descriptorPool = descriptorPool;
    allocateCombine.descriptorSetCount = 1;
    allocateCombine.pSetLayouts = &combineLayout;
    VkDescriptorSet combineSet = VK_NULL_HANDLE;
    check(vkAllocateDescriptorSets(device, &allocateCombine, &combineSet), "vkAllocateDescriptorSets(combine)");

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

    updateImageDescriptor(ssaoSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, depth.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(ssaoSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, rotation.view, nearestSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(ssaoSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, ssao.view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);

    updateImageDescriptor(blurHSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, depth.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(blurHSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, ssao.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(blurHSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, blurH.view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);

    updateImageDescriptor(blurVSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, depth.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(blurVSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, blurH.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(blurVSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, blurV.view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);

    updateImageDescriptor(combineSet, 0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, color.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(combineSet, 1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, blurV.view, linearSampler, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    updateImageDescriptor(combineSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, finalImage.view, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_GENERAL);

    VkPipeline ssaoPipeline = createPipeline(device, shaderDir + "/ssao.comp.spv", ssaoPipelineLayout);

    VkBool32 horizontal = VK_TRUE;
    VkSpecializationMapEntry horizontalEntry{0, 0, sizeof(VkBool32)};
    VkSpecializationInfo horizontalInfo{1, &horizontalEntry, sizeof(VkBool32), &horizontal};
    VkPipeline blurHPipeline = createPipeline(device, shaderDir + "/blur.comp.spv", blurPipelineLayout, &horizontalInfo);

    VkBool32 vertical = VK_FALSE;
    VkSpecializationMapEntry verticalEntry{0, 0, sizeof(VkBool32)};
    VkSpecializationInfo verticalInfo{1, &verticalEntry, sizeof(VkBool32), &vertical};
    VkPipeline blurVPipeline = createPipeline(device, shaderDir + "/blur.comp.spv", blurPipelineLayout, &verticalInfo);

    VkPipeline combinePipeline = createPipeline(device, shaderDir + "/combine.comp.spv", combinePipelineLayout);

    Buffer readback = createBuffer(device, physicalDevice, kWidth * kHeight * 4,
                                   VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    VkCommandBuffer commandBuffer = allocateCommandBuffer();
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    for (VkImage image : {ssao.image, blurH.image, blurV.image, finalImage.image}) {
      imageBarrier(commandBuffer, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                   0, VK_ACCESS_SHADER_WRITE_BIT,
                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    const uint32_t groupsX = (kWidth + 15) / 16;
    const uint32_t groupsY = (kHeight + 15) / 16;

    struct SsaoPush { float zNear, zFar, radius, attScale, distScale; };
    const SsaoPush ssaoPush{0.1f, 100.0f, 1.0f, 1.0f, 10.0f};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ssaoPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, ssaoPipelineLayout, 0, 1, &ssaoSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, ssaoPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(SsaoPush), &ssaoPush);
    vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

    imageBarrier(commandBuffer, ssao.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    const float blurThreshold = 100.0f * 30.0f;
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, blurHPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, blurPipelineLayout, 0, 1, &blurHSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, blurPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &blurThreshold);
    vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

    imageBarrier(commandBuffer, blurH.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, blurVPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, blurPipelineLayout, 0, 1, &blurVSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, blurPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(float), &blurThreshold);
    vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

    imageBarrier(commandBuffer, blurV.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

    struct CombinePush { float scale, bias; };
    const CombinePush combinePush{2.0f, 0.0f};
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, combinePipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, combinePipelineLayout, 0, 1, &combineSet, 0, nullptr);
    vkCmdPushConstants(commandBuffer, combinePipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(CombinePush), &combinePush);
    vkCmdDispatch(commandBuffer, groupsX, groupsY, 1);

    imageBarrier(commandBuffer, finalImage.image, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                 VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy copyRegion{};
    copyRegion.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    copyRegion.imageExtent = {kWidth, kHeight, 1};
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
    submitAndWait(commandBuffer);
    vkFreeCommandBuffers(device, commandPool, 1, &commandBuffer);

    void* mapped = nullptr;
    check(vkMapMemory(device, readback.memory, 0, VK_WHOLE_SIZE, 0, &mapped), "vkMapMemory(readback)");
    writePPM(output, static_cast<const uint8_t*>(mapped), kWidth, kHeight);
    vkUnmapMemory(device, readback.memory);

    std::cout << "Wrote " << output << " (" << kWidth << "x" << kHeight << ")\n";

    vkDestroyBuffer(device, readback.buffer, nullptr);
    vkFreeMemory(device, readback.memory, nullptr);
    vkDestroyPipeline(device, combinePipeline, nullptr);
    vkDestroyPipeline(device, blurVPipeline, nullptr);
    vkDestroyPipeline(device, blurHPipeline, nullptr);
    vkDestroyPipeline(device, ssaoPipeline, nullptr);
    vkDestroyDescriptorPool(device, descriptorPool, nullptr);
    vkDestroyPipelineLayout(device, combinePipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, blurPipelineLayout, nullptr);
    vkDestroyPipelineLayout(device, ssaoPipelineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, combineLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, blurLayout, nullptr);
    vkDestroyDescriptorSetLayout(device, ssaoLayout, nullptr);
    vkDestroySampler(device, nearestSampler, nullptr);
    vkDestroySampler(device, linearSampler, nullptr);
    destroyImage(device, finalImage);
    destroyImage(device, blurV);
    destroyImage(device, blurH);
    destroyImage(device, ssao);
    destroyImage(device, rotation);
    destroyImage(device, color);
    destroyImage(device, depth);
    vkDestroyCommandPool(device, commandPool, nullptr);
    vkDestroyDevice(device, nullptr);
    vkDestroyInstance(instance, nullptr);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    return 1;
  }
}
