/*
 * Minimal JSON writer shared by the zt-gfx C++ capture harnesses.
 *
 * Kept free of Vulkan dependencies so both ztgfx_parity.h (image captures) and
 * benchmark.hpp (ZTGFX_SCAL captures) can emit JSON without pulling in the
 * Vulkan capture classes.
 */
#pragma once

#include <fstream>
#include <iomanip>
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

	JsonWriter& value(double value)
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

} // namespace ztgfx
