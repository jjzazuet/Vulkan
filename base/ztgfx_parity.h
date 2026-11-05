/*
 * Reusable C++ parity capture support for zt-gfx.
 *
 * This code is only active when ZTGFX_PARITY is set. It must not alter normal
 * example behavior outside parity mode.
 */
#pragma once

#include "VulkanDevice.h"

#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace ztgfx
{

class JsonWriter
{
public:
	explicit JsonWriter(const std::string& filename, int version = 2)
		: out(filename, std::ios::out | std::ios::trunc)
	{
		out << "{";
		first.push_back(true);
		key("version").value(version);
	}

	~JsonWriter()
	{
		close();
	}

	JsonWriter& key(const std::string& value)
	{
		pendingKey = value;
		hasPendingKey = true;
		return *this;
	}

	JsonWriter& beginObject()
	{
		prefix();
		out << "{";
		first.push_back(true);
		return *this;
	}

	JsonWriter& endObject()
	{
		first.pop_back();
		out << "\n" << indent() << "}";
		return *this;
	}

	JsonWriter& beginArray()
	{
		prefix();
		out << "[";
		first.push_back(true);
		return *this;
	}

	JsonWriter& endArray()
	{
		first.pop_back();
		out << "\n" << indent() << "]";
		return *this;
	}

	JsonWriter& value(bool value)
	{
		prefix();
		out << (value ? "true" : "false");
		return *this;
	}

	JsonWriter& value(int value)
	{
		prefix();
		out << value;
		return *this;
	}

	JsonWriter& value(unsigned int value)
	{
		prefix();
		out << value;
		return *this;
	}

	JsonWriter& value(float value)
	{
		prefix();
		out << std::setprecision(9) << value;
		return *this;
	}

	JsonWriter& value(const std::string& value)
	{
		prefix();
		out << quote(value);
		return *this;
	}

	JsonWriter& value(const char* value)
	{
		return this->value(std::string(value));
	}

	JsonWriter& array(const float* values, size_t count)
	{
		prefix();
		out << "[";
		for (size_t i = 0; i < count; i++)
		{
			if (i) out << ",";
			out << std::setprecision(9) << values[i];
		}
		out << "]";
		return *this;
	}

	void close()
	{
		if (closed) return;
		out << "\n}\n";
		out.flush();
		closed = true;
	}

private:
	std::ofstream out;
	std::vector<bool> first;
	std::string pendingKey;
	bool hasPendingKey = false;
	bool closed = false;

	std::string indent() const
	{
		return std::string(first.size() * 2, ' ');
	}

	void prefix()
	{
		if (first.empty()) return;
		if (!first.back()) out << ",";
		out << "\n" << indent();
		first.back() = false;
		if (hasPendingKey)
		{
			out << quote(pendingKey) << ": ";
			hasPendingKey = false;
		}
	}

	static std::string quote(const std::string& value)
	{
		std::string out = "\"";
		for (auto c : value)
		{
			switch (c)
			{
				case '"': out += "\\\""; break;
				case '\\': out += "\\\\"; break;
				case '\n': out += "\\n"; break;
				case '\r': out += "\\r"; break;
				case '\t': out += "\\t"; break;
				default: out += c; break;
			}
		}
		out += "\"";
		return out;
	}
};

struct Parity
{
	struct CameraLens
	{
		float yfov;
		float aspect;
		float nearClip;
		float farClip;
	};

	static void header(JsonWriter& out, int frame, int width, int height)
	{
		out.key("frame").value(frame);
		out.key("width").value(width);
		out.key("height").value(height);
	}

	static void timeValue(JsonWriter& out, float time)
	{
		out.key("time").value(time);
	}

	template <typename Camera>
	static void camera(
		JsonWriter& out, const Camera& camera,
		const CameraLens* lens = nullptr, const float* viewPos = nullptr
	)
	{
		out.key("camera").beginObject();
		out.key("position").array(&camera.position.x, 3);
		out.key("rotation").array(&camera.rotation.x, 3);
		if (lens != nullptr)
		{
			out.key("yfov").value(lens->yfov);
			out.key("aspect").value(lens->aspect);
			out.key("near").value(lens->nearClip);
			out.key("far").value(lens->farClip);
			out.key("flipY").value(camera.flipY);
		}
		out.key("view").array(&camera.matrices.view[0][0], 16);
		out.key("projection").array(&camera.matrices.perspective[0][0], 16);
		if (viewPos != nullptr)
		{
			out.key("viewPos").array(viewPos, 4);
		}
		out.endObject();
	}

	static void device(JsonWriter& out, const VkPhysicalDeviceProperties& properties)
	{
		out.key("device").beginObject();
		out.key("name").value(properties.deviceName);
		out.key("driverVersion").value(properties.driverVersion);
		out.key("apiVersion").value(properties.apiVersion);
		out.key("vendorID").value(properties.vendorID);
		out.key("deviceID").value(properties.deviceID);
		out.endObject();
	}

	static void light(JsonWriter& out)
	{
		out.key("light").beginObject();
		out.endObject();
	}

	template <typename Vec3>
	static void light(JsonWriter& out, const Vec3& position)
	{
		out.key("light").beginObject();
		out.key("position").array(&position.x, 3);
		out.endObject();
	}

	template <typename Vec3, typename Mat4>
	static void light(
		JsonWriter& out, const Vec3& position,
		const Mat4& view, const Mat4& projection, const Mat4& lightSpace
	)
	{
		out.key("light").beginObject();
		out.key("position").array(&position.x, 3);
		out.key("view").array(&view[0][0], 16);
		out.key("projection").array(&projection[0][0], 16);
		out.key("lightSpace").array(&lightSpace[0][0], 16);
		out.endObject();
	}

	static void emptyUniforms(JsonWriter& out)
	{
		out.key("uniforms").beginObject();
		out.endObject();
	}

	template <typename Mat4, typename Vec4>
	static void sceneUniforms(
		JsonWriter& out, const Mat4& projection, const Mat4& view,
		const Vec4& lightPos
	)
	{
		out.key("uniforms").beginObject();
		out.key("scene").beginObject();
		out.key("projection").array(&projection[0][0], 16);
		out.key("view").array(&view[0][0], 16);
		out.key("lightPos").array(&lightPos.x, 4);
		out.endObject();
		out.endObject();
	}

	template <typename Mat4, typename Vec4>
	static void shadowUniforms(
		JsonWriter& out, const Mat4& projection, const Mat4& view,
		const Mat4& model, const Mat4& lightSpace, const Vec4& lightPos,
		float zNear, float zFar, const Mat4& depthMvp
	)
	{
		out.key("uniforms").beginObject();
		out.key("scene").beginObject();
		out.key("projection").array(&projection[0][0], 16);
		out.key("view").array(&view[0][0], 16);
		out.key("model").array(&model[0][0], 16);
		out.key("lightSpace").array(&lightSpace[0][0], 16);
		out.key("lightPos").array(&lightPos.x, 4);
		out.key("zNear").value(zNear);
		out.key("zFar").value(zFar);
		out.endObject();
		out.key("offscreen").beginObject();
		out.key("depthMVP").array(&depthMvp[0][0], 16);
		out.endObject();
		out.endObject();
	}

	template <typename Skins>
	static void skins(JsonWriter& out, const Skins& skins)
	{
		out.key("skins").beginArray();
		for (const auto& skin : skins)
		{
			out.beginObject();
			out.key("name").value(skin.name);
			out.key("jointCount").value(
				static_cast<unsigned int>(skin.jointMatrices.size())
			);
			out.key("joints").beginArray();
			for (const auto& matrix : skin.jointMatrices)
			{
				out.array(&matrix[0][0], 16);
			}
			out.endArray();
			out.endObject();
		}
		out.endArray();
	}

	struct Capture
	{
		std::string name;
		std::string file;
		uint32_t width;
		uint32_t height;
		VkFormat format;
		uint32_t layer;
		int face;
		uint32_t mip;
		VkImageAspectFlags aspect;
		std::string encoding;
	};

	bool enabled = false;
	std::string dir = ".";
	int frame = 0;
	int captureFrame = 24;
	int finishFrame = 25;
	float time = 0.0f;
	std::vector<Capture> captures;

	static Parity fromEnvironment()
	{
		Parity parity;
		auto value = std::getenv("ZTGFX_PARITY");
		parity.enabled = value != nullptr
			&& std::string(value) != ""
			&& std::string(value) != "0";
		if (!parity.enabled) return parity;

		auto dir = std::getenv("ZTGFX_PARITY_DIR");
		parity.dir = dir != nullptr ? dir : ".";
		if (!parity.dir.empty() && parity.dir.back() != '/')
		{
			parity.dir += '/';
		}
		parity.captureFrame = envInt("ZTGFX_PARITY_FRAME", parity.captureFrame);
		parity.finishFrame = envInt(
			"ZTGFX_PARITY_FINISH_FRAME", parity.captureFrame + 1
		);
		auto time = std::getenv("ZTGFX_PARITY_TIME");
		parity.time = time != nullptr ? std::stof(time) : 0.0f;
		return parity;
	}

	std::string path(const std::string& file) const
	{
		return dir + file;
	}

	template <typename Camera, typename Vec3>
	static void pinCamera(Camera& camera, const Vec3& position, const Vec3& rotation)
	{
		camera.setPosition(position);
		camera.setRotation(rotation);
	}

	void saveSwapchain(
		vks::VulkanDevice* device, VkQueue queue,
		VkImage image, VkFormat format,
		uint32_t width, uint32_t height,
		const std::string& filename, const std::string& name
	)
	{
		saveImage(
			device, queue, image, format, width, height,
			VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_ASPECT_COLOR_BIT,
			0, 0, filename, name, -1
		);
	}

	void saveImage(
		vks::VulkanDevice* device, VkQueue queue,
		VkImage image, VkFormat format,
		uint32_t width, uint32_t height,
		VkImageLayout sourceLayout, VkImageAspectFlags aspect,
		uint32_t layer, uint32_t mip,
		const std::string& filename, const std::string& name, int face = -1
	)
	{
		const uint32_t bytesPerPixel = formatBytesPerPixel(format);
		const VkDeviceSize size = static_cast<VkDeviceSize>(width) * height * bytesPerPixel;
		vks::Buffer buffer{};
		VK_CHECK_RESULT(device->createBuffer(
			VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			&buffer, size
		));
		VK_CHECK_RESULT(buffer.map());

		VkCommandBuffer cmd = device->createCommandBuffer(VK_COMMAND_BUFFER_LEVEL_PRIMARY, true);
		VkImageSubresourceRange range{};
		range.aspectMask = aspect;
		range.baseMipLevel = mip;
		range.levelCount = 1;
		range.baseArrayLayer = layer;
		range.layerCount = 1;

		vks::tools::insertImageMemoryBarrier(
			cmd, image,
			VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_TRANSFER_READ_BIT,
			sourceLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			range
		);

		VkBufferImageCopy region{};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = aspect;
		region.imageSubresource.mipLevel = mip;
		region.imageSubresource.baseArrayLayer = layer;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = { 0, 0, 0 };
		region.imageExtent = { width, height, 1 };
		vkCmdCopyImageToBuffer(
			cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer.buffer, 1, &region
		);

		vks::tools::insertImageMemoryBarrier(
			cmd, image,
			VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_MEMORY_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, sourceLayout,
			VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			range
		);

		device->flushCommandBuffer(cmd, queue);

		auto* data = static_cast<uint8_t*>(buffer.mapped);
		std::ofstream file(filename, std::ios::out | std::ios::binary);
		if (is8BitColor(format))
		{
			file << "P6\n" << width << "\n" << height << "\n255\n";
			bool swizzle = isBgra8(format);
			for (uint32_t y = 0; y < height; y++)
			{
				for (uint32_t x = 0; x < width; x++)
				{
					auto* pixel = data + (static_cast<size_t>(y) * width + x) * bytesPerPixel;
					if (swizzle)
					{
						file.write(reinterpret_cast<const char*>(pixel + 2), 1);
						file.write(reinterpret_cast<const char*>(pixel + 1), 1);
						file.write(reinterpret_cast<const char*>(pixel), 1);
					}
					else
					{
						file.write(reinterpret_cast<const char*>(pixel), 3);
					}
				}
			}
		}
		else
		{
			file.write(reinterpret_cast<const char*>(data), size);
		}
		file.close();

		buffer.unmap();
		buffer.destroy();

		captures.push_back(Capture{
			name, fileName(filename), width, height, format, layer, face, mip,
			aspect, is8BitColor(format) ? "ppm" : "raw"
		});
		std::cout << "Parity capture saved: " << filename << std::endl;
	}

	void writeCaptures(const std::string& filename) const
	{
		JsonWriter out(filename);
		out.key("captures").beginArray();
		for (const auto& capture : captures)
		{
			out.beginObject();
			out.key("name").value(capture.name);
			out.key("file").value(capture.file);
			out.key("width").value(capture.width);
			out.key("height").value(capture.height);
			out.key("format").value(static_cast<unsigned int>(capture.format));
			out.key("layer").value(capture.layer);
			if (capture.face >= 0)
			{
				out.key("face").value(static_cast<unsigned int>(capture.face));
			}
			out.key("mip").value(capture.mip);
			out.key("aspect").value(
				(capture.aspect & VK_IMAGE_ASPECT_DEPTH_BIT) ? "depth" : "color"
			);
			out.key("encoding").value(capture.encoding);
			out.endObject();
		}
		out.endArray();
		out.close();
	}

private:
	static std::string fileName(const std::string& path)
	{
		auto slash = path.find_last_of("/\\");
		return slash == std::string::npos ? path : path.substr(slash + 1);
	}

	static int envInt(const char* name, int fallback)
	{
		auto value = std::getenv(name);
		return value != nullptr ? std::atoi(value) : fallback;
	}

	static bool isBgra8(VkFormat format)
	{
		return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
	}

	static bool is8BitColor(VkFormat format)
	{
		return format == VK_FORMAT_R8G8B8A8_UNORM
			|| format == VK_FORMAT_R8G8B8A8_SRGB
			|| format == VK_FORMAT_B8G8R8A8_UNORM
			|| format == VK_FORMAT_B8G8R8A8_SRGB;
	}

	static uint32_t formatBytesPerPixel(VkFormat format)
	{
		switch (format)
		{
			case VK_FORMAT_R8G8B8A8_UNORM:
			case VK_FORMAT_R8G8B8A8_SRGB:
			case VK_FORMAT_B8G8R8A8_UNORM:
			case VK_FORMAT_B8G8R8A8_SRGB:
			case VK_FORMAT_R32_SFLOAT:
			case VK_FORMAT_D32_SFLOAT:
				return 4;
			case VK_FORMAT_R16G16_SFLOAT:
				return 4;
			case VK_FORMAT_R16G16B16A16_SFLOAT:
				return 8;
			case VK_FORMAT_R32G32B32A32_SFLOAT:
				return 16;
			case VK_FORMAT_D16_UNORM:
				return 2;
			default:
				throw std::runtime_error("Unsupported parity capture format");
		}
	}
};

} // namespace ztgfx
