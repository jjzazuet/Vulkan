/*
* Benchmark class - Can be used for automated benchmarks
*
* Copyright (C) 2016-2025 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <vector>
#include <string>
#include <algorithm>
#include <limits>
#include <functional>
#include <chrono>
#include <iomanip>
#include <cmath>
#include <cstdlib>

#include "ztgfx_json.h"

namespace vks
{
	class Benchmark {
	public:
		// ZTGFX_SCAL (#216): deterministic scalability capture shared by the
		// SCAL demos. It reuses the benchmark loop and is inert unless
		// ZTGFX_SCAL is set, so normal example behaviour is unchanged.
		class Scal {
		public:
			struct Series {
				std::string name;
				std::vector<double> values;
			};

			// Per-frame histograms (e.g. LOD level counts). Reduced per bucket
			// with the median and written as a JSON array.
			struct Histogram {
				std::string name;
				std::vector<std::vector<uint32_t>> samples;
			};

			// Capture-scoped values (e.g. per-mip data) that are produced
			// once instead of once per frame. Written verbatim.
			struct Constant {
				std::string name;
				double value;
			};

			struct Array {
				std::string name;
				std::vector<double> values;
			};

			struct TextArray {
				std::string name;
				std::vector<std::string> values;
			};

			bool active = false;
			std::string dir = ".";
			std::string demo;
			std::string commit;
			std::string image;
			int frames = 300;
			int warmupSeconds = 1;
			uint32_t width = 0;
			uint32_t height = 0;
			bool recording = false;
			std::vector<double> frameTimes;
			std::vector<Series> series;
			std::vector<Histogram> histograms;
			std::vector<Constant> constants;
			std::vector<Array> arrays;
			std::vector<TextArray> textArrays;

			static Scal fromEnvironment() {
				Scal scal;
				auto value = std::getenv(SCAL_ENV);
				scal.active = value != nullptr
					&& std::string(value) != ""
					&& std::string(value) != "0";
				if (!scal.active) return scal;

				auto dir = std::getenv(SCAL_DIR_ENV);
				scal.dir = dir != nullptr ? dir : ".";
				if (!scal.dir.empty() && scal.dir.back() != '/') {
					scal.dir += '/';
				}
				auto demo = std::getenv(SCAL_DEMO_ENV);
				scal.demo = demo != nullptr ? demo : "";
				auto commit = std::getenv(SCAL_COMMIT_ENV);
				scal.commit = commit != nullptr ? commit : "";
				auto image = std::getenv(SCAL_IMAGE_ENV);
				if (image == nullptr || *image == '\0') {
					image = std::getenv(SCAL_GROUND_TRUTH_IMAGE_ENV);
				}
				scal.image = image != nullptr ? image : "";
				scal.frames = envInt(SCAL_FRAMES_ENV, scal.frames);
				scal.warmupSeconds = envInt(SCAL_WARMUP_ENV, scal.warmupSeconds);
				return scal;
			}

			// Records one sample of a named metric during the measurement
			// window. Call once per frame from the demo's render path.
			void metric(const std::string& name, double value) {
				if (!recording) return;
				for (auto& entry : series) {
					if (entry.name == name) {
						entry.values.push_back(value);
						return;
					}
				}
				series.push_back({ name, { value } });
			}

			// Records one sample of a named per-frame histogram. Call once per
			// frame from the demo's render path while the measurement window is
			// active.
			void histogram(const std::string& name, const uint32_t* values, size_t count) {
				if (!recording) return;
				for (auto& entry : histograms) {
					if (entry.name == name) {
						entry.samples.emplace_back(values, values + count);
						return;
					}
				}
				histograms.push_back({ name, { std::vector<uint32_t>(values, values + count) } });
			}

			// Records a value produced once per capture (outside the
			// measurement window), e.g. mip-generation cost or texture size.
			void constant(const std::string& name, double value) {
				for (auto& entry : constants) {
					if (entry.name == name) {
						entry.value = value;
						return;
					}
				}
				constants.push_back({ name, value });
			}

			// Records a capture-scoped numeric array, e.g. per-mip timings.
			void array(const std::string& name, const std::vector<double>& values) {
				for (auto& entry : arrays) {
					if (entry.name == name) {
						entry.values = values;
						return;
					}
				}
				arrays.push_back({ name, values });
			}

			// Records a capture-scoped string array, e.g. per-mip hashes.
			void strings(const std::string& name, const std::vector<std::string>& values) {
				for (auto& entry : textArrays) {
					if (entry.name == name) {
						entry.values = values;
						return;
					}
				}
				textArrays.push_back({ name, values });
			}

			std::string path(const std::string& file) const {
				return dir + file;
			}

			void write(const VkPhysicalDeviceProperties& deviceProps) const {
				auto frameStats = stats(frameTimes);
				ztgfx::JsonWriter out(
					path(demo.empty() ? "scal.json" : demo + ".json"), 1
				);
				out.key("demo").value(demo);
				out.key("commit").value(commit);
				out.key("device").beginObject();
				out.key("name").value(deviceProps.deviceName);
				out.key("driverVersion").value(deviceProps.driverVersion);
				out.key("apiVersion").value(deviceProps.apiVersion);
				out.key("vendorID").value(deviceProps.vendorID);
				out.key("deviceID").value(deviceProps.deviceID);
				out.endObject();
				out.key("resolution").beginObject();
				out.key("width").value(width);
				out.key("height").value(height);
				out.endObject();
				out.key("frames").value(static_cast<unsigned int>(frameTimes.size()));
				out.key("frameTimeMs").beginObject();
				out.key("min").value(frameStats.min);
				out.key("median").value(frameStats.median);
				out.key("p95").value(frameStats.p95);
				out.endObject();
				for (const auto& entry : series) {
					out.key(entry.name).value(median(entry.values));
				}
				for (const auto& entry : histograms) {
					out.key(entry.name).beginArray();
					const size_t bucketCount = entry.samples.empty() ? 0 : entry.samples.front().size();
					for (size_t bucket = 0; bucket < bucketCount; bucket++) {
						std::vector<double> values;
						values.reserve(entry.samples.size());
						for (const auto& sample : entry.samples) {
							values.push_back(static_cast<double>(sample[bucket]));
						}
						out.value(static_cast<unsigned int>(std::llround(median(values))));
					}
					out.endArray();
				}
				for (const auto& entry : constants) {
					out.key(entry.name).value(entry.value);
				}
				for (const auto& entry : arrays) {
					out.key(entry.name).beginArray();
					for (auto value : entry.values) {
						out.value(value);
					}
					out.endArray();
				}
				for (const auto& entry : textArrays) {
					out.key(entry.name).beginArray();
					for (const auto& value : entry.values) {
						out.value(value);
					}
					out.endArray();
				}
				out.close();
			}

		private:
			static constexpr const char* SCAL_ENV = "ZTGFX_SCAL";
			static constexpr const char* SCAL_DIR_ENV = "ZTGFX_SCAL_DIR";
			static constexpr const char* SCAL_DEMO_ENV = "ZTGFX_SCAL_DEMO";
			static constexpr const char* SCAL_COMMIT_ENV = "ZTGFX_SCAL_COMMIT";
			static constexpr const char* SCAL_IMAGE_ENV = "ZTGFX_PARITY_IMAGE";
			static constexpr const char* SCAL_GROUND_TRUTH_IMAGE_ENV =
				"ZTGFX_GROUND_TRUTH_IMAGE";
			static constexpr const char* SCAL_FRAMES_ENV = "ZTGFX_SCAL_FRAMES";
			static constexpr const char* SCAL_WARMUP_ENV = "ZTGFX_SCAL_WARMUP";

			struct Stats {
				double min;
				double median;
				double p95;
			};

			static int envInt(const char* name, int fallback) {
				auto value = std::getenv(name);
				return value != nullptr ? std::atoi(value) : fallback;
			}

			static double percentile(
				const std::vector<double>& sorted, double p
			) {
				if (sorted.empty()) return 0.0;
				double index = p * (sorted.size() - 1);
				size_t low = static_cast<size_t>(std::floor(index));
				size_t high = static_cast<size_t>(std::ceil(index));
				if (low == high) return sorted[low];
				return sorted[low] + (sorted[high] - sorted[low]) * (index - low);
			}

			static double median(const std::vector<double>& values) {
				auto sorted = values;
				std::sort(sorted.begin(), sorted.end());
				return percentile(sorted, 0.5);
			}

			static Stats stats(const std::vector<double>& values) {
				auto sorted = values;
				std::sort(sorted.begin(), sorted.end());
				if (sorted.empty()) return { 0.0, 0.0, 0.0 };
				return {
					sorted.front(),
					percentile(sorted, 0.5),
					percentile(sorted, 0.95)
				};
			}
		};

	private:
		FILE* stream{ nullptr };
		VkPhysicalDeviceProperties deviceProps{};
	public:
		bool active = false;
		bool outputFrameTimes = false;
		int outputFrames = -1; // -1 means no frames limit
		uint32_t warmup = 1;   // Default to 1 sec of warm-up
		uint32_t duration = 10;
		std::vector<double> frameTimes;
		std::string filename = "";

		double runtime = 0.0;
		uint32_t frameCount = 0;

		Scal scal = Scal::fromEnvironment();

		void run(std::function<void()> renderFunc, VkPhysicalDeviceProperties deviceProps) {
			active = true;
			this->deviceProps = deviceProps;
			scal.recording = false;
#if defined(_WIN32)
			bool consoleAttached = AttachConsole(ATTACH_PARENT_PROCESS);
			if (!consoleAttached) {
				consoleAttached = AttachConsole(GetCurrentProcessId());
			}
			if (consoleAttached) {
				freopen_s(&stream, "CONOUT$", "w+", stdout);
				freopen_s(&stream, "CONOUT$", "w+", stderr);
			}
#endif
			// Warm up phase to get more stable frame rates
			{
				double tMeasured = 0.0;
				while (tMeasured < (warmup * 1000)) {
					auto tStart = std::chrono::high_resolution_clock::now();
					renderFunc();
					auto tDiff = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - tStart).count();
					tMeasured += tDiff;
				};
			}

			// Benchmark phase
			{
				scal.recording = scal.active;
				// A SCAL capture is governed by the fixed frame count, never
				// truncated by the default time budget.
				const bool frameGoverned = scal.active && outputFrames != -1;
				while (frameGoverned || runtime < (duration * 1000.0)) {
					auto tStart = std::chrono::high_resolution_clock::now();
					renderFunc();
					auto tDiff = std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - tStart).count();
					runtime += tDiff;
					frameTimes.push_back(tDiff);
					frameCount++;
					if (outputFrames != -1 && outputFrames == frameCount) break;
				};
				scal.recording = false;
				std::cout << std::fixed << std::setprecision(3);
				std::cout << "Benchmark finished\n";
				std::cout << "device : " << deviceProps.deviceName << " (driver version: " << deviceProps.driverVersion << ")" << "\n";
				std::cout << "runtime: " << (runtime / 1000.0) << "\n";
				std::cout << "frames : " << frameCount << "\n";
				std::cout << "fps    : " << frameCount / (runtime / 1000.0) << "\n";
				if (scal.active) {
					scal.frameTimes = frameTimes;
					scal.write(deviceProps);
					std::cout << "scal   : " << scal.path(scal.demo.empty() ? "scal.json" : scal.demo + ".json") << "\n";
				}
			}
		}

		void saveResults() {
			std::ofstream result(filename, std::ios::out);
			if (result.is_open()) {
				result << std::fixed << std::setprecision(4);

				result << "device,driverversion,duration (ms),frames,fps" << "\n";
				result << deviceProps.deviceName << "," << deviceProps.driverVersion << "," << runtime << "," << frameCount << "," << frameCount / (runtime / 1000.0) << "\n";

				if (outputFrameTimes) {
					result << "\n" << "frame,ms" << "\n";
					for (size_t i = 0; i < frameTimes.size(); i++) {
						result << i << "," << frameTimes[i] << "\n";
					}
					double tMin = *std::min_element(frameTimes.begin(), frameTimes.end());
					double tMax = *std::max_element(frameTimes.begin(), frameTimes.end());
					double tAvg = std::accumulate(frameTimes.begin(), frameTimes.end(), 0.0) / (double)frameTimes.size();
					std::cout << "best   : " << (1000.0 / tMin) << " fps (" << tMin << " ms)" << "\n";
					std::cout << "worst  : " << (1000.0 / tMax) << " fps (" << tMax << " ms)" << "\n";
					std::cout << "avg    : " << (1000.0 / tAvg) << " fps (" << tAvg << " ms)" << "\n";
					std::cout << "\n";
				}

				result.flush();
#if defined(_WIN32)
				FreeConsole();
#endif
			}
		}
	};
}
