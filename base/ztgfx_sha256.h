/*
 * Minimal SHA-256 used by the zt-gfx capture harnesses to fingerprint
 * generated GPU output (e.g. per-mip texture contents).
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace ztgfx
{

class Sha256
{
public:
	static std::string hex(const uint8_t* data, size_t size)
	{
		auto digest = digestOf(data, size);
		static constexpr char digits[] = "0123456789abcdef";
		std::string out;
		out.reserve(digest.size() * 2);
		for (auto byte : digest)
		{
			out.push_back(digits[byte >> 4]);
			out.push_back(digits[byte & 0x0f]);
		}
		return out;
	}

private:
	static constexpr std::array<uint32_t, 64> ROUND_CONSTANTS = {
		0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
		0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
		0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
		0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
		0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
		0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
		0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
		0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
		0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
		0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
		0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
		0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
		0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
		0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
		0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
		0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
	};

	static uint32_t rotateRight(uint32_t value, uint32_t bits)
	{
		return (value >> bits) | (value << (32 - bits));
	}

	static std::array<uint8_t, 32> digestOf(const uint8_t* data, size_t size)
	{
		std::array<uint32_t, 8> hash = {
			0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
			0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
		};

		const size_t paddedSize = ((size + 8) / 64 + 1) * 64;
		std::vector<uint8_t> message(paddedSize, 0);
		std::memcpy(message.data(), data, size);
		message[size] = 0x80;
		const uint64_t bitLength = static_cast<uint64_t>(size) * 8;
		for (size_t i = 0; i < 8; i++)
		{
			message[paddedSize - 1 - i] = static_cast<uint8_t>(bitLength >> (8 * i));
		}

		for (size_t offset = 0; offset < paddedSize; offset += 64)
		{
			std::array<uint32_t, 64> schedule{};
			for (size_t i = 0; i < 16; i++)
			{
				const auto* block = message.data() + offset + i * 4;
				schedule[i] =
					(static_cast<uint32_t>(block[0]) << 24) |
					(static_cast<uint32_t>(block[1]) << 16) |
					(static_cast<uint32_t>(block[2]) << 8) |
					static_cast<uint32_t>(block[3]);
			}
			for (size_t i = 16; i < 64; i++)
			{
				const uint32_t s0 =
					rotateRight(schedule[i - 15], 7) ^
					rotateRight(schedule[i - 15], 18) ^
					(schedule[i - 15] >> 3);
				const uint32_t s1 =
					rotateRight(schedule[i - 2], 17) ^
					rotateRight(schedule[i - 2], 19) ^
					(schedule[i - 2] >> 10);
				schedule[i] = schedule[i - 16] + s0 + schedule[i - 7] + s1;
			}

			auto a = hash[0];
			auto b = hash[1];
			auto c = hash[2];
			auto d = hash[3];
			auto e = hash[4];
			auto f = hash[5];
			auto g = hash[6];
			auto h = hash[7];

			for (size_t i = 0; i < 64; i++)
			{
				const uint32_t s1 = rotateRight(e, 6) ^ rotateRight(e, 11) ^ rotateRight(e, 25);
				const uint32_t choice = (e & f) ^ (~e & g);
				const uint32_t temp1 = h + s1 + choice + ROUND_CONSTANTS[i] + schedule[i];
				const uint32_t s0 = rotateRight(a, 2) ^ rotateRight(a, 13) ^ rotateRight(a, 22);
				const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
				const uint32_t temp2 = s0 + majority;

				h = g;
				g = f;
				f = e;
				e = d + temp1;
				d = c;
				c = b;
				b = a;
				a = temp1 + temp2;
			}

			hash[0] += a;
			hash[1] += b;
			hash[2] += c;
			hash[3] += d;
			hash[4] += e;
			hash[5] += f;
			hash[6] += g;
			hash[7] += h;
		}

		std::array<uint8_t, 32> digest{};
		for (size_t i = 0; i < hash.size(); i++)
		{
			digest[i * 4] = static_cast<uint8_t>(hash[i] >> 24);
			digest[i * 4 + 1] = static_cast<uint8_t>(hash[i] >> 16);
			digest[i * 4 + 2] = static_cast<uint8_t>(hash[i] >> 8);
			digest[i * 4 + 3] = static_cast<uint8_t>(hash[i]);
		}
		return digest;
	}
};

} // namespace ztgfx
