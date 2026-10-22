#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <string>
#include <vector>

#define HOST_VERSION "0.1.0"

// Opens the log file (appends); safe to call once at startup.
void LogOpen(const std::wstring& Path);

// Writes one timestamped line to the log file and stderr.
void LogLine(const char* Level, const char* Format, ...);

#define LOG_INFO(...) LogLine("INFO", __VA_ARGS__)
#define LOG_WARN(...) LogLine("WARN", __VA_ARGS__)
#define LOG_ERROR(...) LogLine("ERROR", __VA_ARGS__)

std::wstring Widen(const std::string& Utf8);
std::string Narrow(const std::wstring& Wide);

// Seconds since an arbitrary fixed point, from the performance counter.
double NowSeconds();

// Appends little-endian fields to a message payload.
struct FPayloadWriter
{
	std::vector<uint8_t> Bytes;

	void U8(uint8_t V) { Bytes.push_back(V); }
	void U32(uint32_t V) { Append(&V, 4); }
	void F64(double V) { Append(&V, 8); }
	void Str(const std::string& V)
	{
		U32((uint32_t)V.size());
		Append(V.data(), V.size());
	}
	void Append(const void* Data, size_t Size)
	{
		const uint8_t* P = static_cast<const uint8_t*>(Data);
		Bytes.insert(Bytes.end(), P, P + Size);
	}
};

// Reads little-endian fields from a message payload; Ok turns false on any overrun.
struct FPayloadReader
{
	const std::vector<uint8_t>& Bytes;
	size_t Pos = 0;
	bool Ok = true;

	explicit FPayloadReader(const std::vector<uint8_t>& InBytes) : Bytes(InBytes) {}

	uint8_t U8()
	{
		uint8_t V = 0;
		Read(&V, 1);
		return V;
	}
	uint32_t U32()
	{
		uint32_t V = 0;
		Read(&V, 4);
		return V;
	}
	double F64()
	{
		double V = 0;
		Read(&V, 8);
		return V;
	}
	std::string Str()
	{
		const uint32_t Size = U32();
		if (!Ok || Pos + Size > Bytes.size())
		{
			Ok = false;
			return {};
		}
		std::string V(reinterpret_cast<const char*>(Bytes.data() + Pos), Size);
		Pos += Size;
		return V;
	}
	void Read(void* Out, size_t Size)
	{
		if (!Ok || Pos + Size > Bytes.size())
		{
			Ok = false;
			return;
		}
		memcpy(Out, Bytes.data() + Pos, Size);
		Pos += Size;
	}
};
