#include "Common.h"

#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace
{
	std::mutex GLogMutex;
	FILE* GLogFile = nullptr;
}

void LogOpen(const std::wstring& Path)
{
	std::lock_guard<std::mutex> Lock(GLogMutex);
	if (!Path.empty() && !GLogFile)
	{
		_wfopen_s(&GLogFile, Path.c_str(), L"w");
	}
}

void LogLine(const char* Level, const char* Format, ...)
{
	char Message[2048];
	va_list Args;
	va_start(Args, Format);
	vsnprintf(Message, sizeof(Message), Format, Args);
	va_end(Args);

	SYSTEMTIME Time;
	GetLocalTime(&Time);
	std::lock_guard<std::mutex> Lock(GLogMutex);
	const char* Pattern = "[%02d:%02d:%02d.%03d] %-5s %s\n";
	fprintf(stderr, Pattern, Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds, Level, Message);
	if (GLogFile)
	{
		fprintf(GLogFile, Pattern, Time.wHour, Time.wMinute, Time.wSecond, Time.wMilliseconds, Level, Message);
		fflush(GLogFile);
	}
}

std::wstring Widen(const std::string& Utf8)
{
	if (Utf8.empty()) return {};
	const int Size = MultiByteToWideChar(CP_UTF8, 0, Utf8.data(), (int)Utf8.size(), nullptr, 0);
	std::wstring Out(Size, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, Utf8.data(), (int)Utf8.size(), Out.data(), Size);
	return Out;
}

std::string Narrow(const std::wstring& Wide)
{
	if (Wide.empty()) return {};
	const int Size = WideCharToMultiByte(CP_UTF8, 0, Wide.data(), (int)Wide.size(), nullptr, 0, nullptr, nullptr);
	std::string Out(Size, '\0');
	WideCharToMultiByte(CP_UTF8, 0, Wide.data(), (int)Wide.size(), Out.data(), Size, nullptr, nullptr);
	return Out;
}

double NowSeconds()
{
	static const double Frequency = []
	{
		LARGE_INTEGER F;
		QueryPerformanceFrequency(&F);
		return double(F.QuadPart);
	}();
	LARGE_INTEGER Counter;
	QueryPerformanceCounter(&Counter);
	return double(Counter.QuadPart) / Frequency;
}
