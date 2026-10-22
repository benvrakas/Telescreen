#include "YtDlp.h"

#include <sstream>

namespace
{
	constexpr DWORD TimeoutMs = 45000;
	std::wstring GQuickJsPath;

	// Quotes one argument by the CommandLineToArgvW rules.
	std::wstring QuoteArg(const std::wstring& Arg)
	{
		if (!Arg.empty() && Arg.find_first_of(L" \t\n\v\"") == std::wstring::npos)
		{
			return Arg;
		}
		std::wstring Out = L"\"";
		for (size_t I = 0; I < Arg.size(); ++I)
		{
			size_t Backslashes = 0;
			while (I < Arg.size() && Arg[I] == L'\\')
			{
				++I;
				++Backslashes;
			}
			if (I == Arg.size())
			{
				Out.append(Backslashes * 2, L'\\');
				break;
			}
			if (Arg[I] == L'"')
			{
				Out.append(Backslashes * 2 + 1, L'\\');
				Out.push_back(L'"');
			}
			else
			{
				Out.append(Backslashes, L'\\');
				Out.push_back(Arg[I]);
			}
		}
		Out.push_back(L'"');
		return Out;
	}

	// Runs Exe with Args, returns its exit code and collects stdout and stderr.
	int RunProcess(const std::wstring& Exe, const std::vector<std::wstring>& Args, std::string& StdOut, std::string& StdErr)
	{
		std::wstring CommandLine = QuoteArg(Exe);
		for (const std::wstring& Arg : Args) CommandLine += L" " + QuoteArg(Arg);

		SECURITY_ATTRIBUTES Sa{sizeof(Sa), nullptr, TRUE};
		HANDLE OutRead = nullptr, OutWrite = nullptr, ErrRead = nullptr, ErrWrite = nullptr;
		if (!CreatePipe(&OutRead, &OutWrite, &Sa, 0) || !CreatePipe(&ErrRead, &ErrWrite, &Sa, 0))
		{
			StdErr = "CreatePipe failed";
			return -1;
		}
		SetHandleInformation(OutRead, HANDLE_FLAG_INHERIT, 0);
		SetHandleInformation(ErrRead, HANDLE_FLAG_INHERIT, 0);

		STARTUPINFOW Si{sizeof(Si)};
		Si.dwFlags = STARTF_USESTDHANDLES;
		Si.hStdOutput = OutWrite;
		Si.hStdError = ErrWrite;
		Si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
		PROCESS_INFORMATION Pi{};

		// yt-dlp reads PYTHONIOENCODING, so its output is UTF-8 whatever the console code page.
		SetEnvironmentVariableW(L"PYTHONIOENCODING", L"utf-8");
		const BOOL Started = CreateProcessW(nullptr, CommandLine.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &Si, &Pi);
		CloseHandle(OutWrite);
		CloseHandle(ErrWrite);
		if (!Started)
		{
			StdErr = "could not start yt-dlp (error " + std::to_string(GetLastError()) + ")";
			CloseHandle(OutRead);
			CloseHandle(ErrRead);
			return -1;
		}

		auto Drain = [](HANDLE Pipe, std::string* Out)
		{
			char Buffer[4096];
			DWORD Read = 0;
			while (ReadFile(Pipe, Buffer, sizeof(Buffer), &Read, nullptr) && Read > 0) Out->append(Buffer, Read);
		};
		std::pair<HANDLE, std::string*> ErrContext(ErrRead, &StdErr);
		HANDLE ErrThread = CreateThread(
			nullptr, 0,
			[](LPVOID Param) -> DWORD
			{
				auto* Ctx = static_cast<std::pair<HANDLE, std::string*>*>(Param);
				char Buffer[4096];
				DWORD Read = 0;
				while (ReadFile(Ctx->first, Buffer, sizeof(Buffer), &Read, nullptr) && Read > 0) Ctx->second->append(Buffer, Read);
				return 0;
			},
			&ErrContext, 0, nullptr);
		Drain(OutRead, &StdOut);

		DWORD ExitCode = DWORD(-1);
		if (WaitForSingleObject(Pi.hProcess, TimeoutMs) == WAIT_TIMEOUT)
		{
			TerminateProcess(Pi.hProcess, 1);
			StdErr += "\nyt-dlp timed out";
		}
		GetExitCodeProcess(Pi.hProcess, &ExitCode);
		if (ErrThread)
		{
			WaitForSingleObject(ErrThread, INFINITE);
			CloseHandle(ErrThread);
		}
		CloseHandle(Pi.hThread);
		CloseHandle(Pi.hProcess);
		CloseHandle(OutRead);
		CloseHandle(ErrRead);
		return int(ExitCode);
	}

	std::vector<std::string> SplitLines(const std::string& Text)
	{
		std::vector<std::string> Lines;
		std::istringstream Stream(Text);
		std::string Line;
		while (std::getline(Stream, Line))
		{
			while (!Line.empty() && (Line.back() == '\r' || Line.back() == ' ')) Line.pop_back();
			Lines.push_back(Line);
		}
		return Lines;
	}

	std::string LastErrorLine(const std::string& StdErr)
	{
		std::string Last;
		for (const std::string& Line : SplitLines(StdErr))
		{
			if (!Line.empty()) Last = Line;
		}
		return Last.empty() ? "yt-dlp failed" : Last;
	}
}

void SetYtDlpJsRuntime(const std::wstring& QuickJsPath)
{
	GQuickJsPath = QuickJsPath;
}

FResolvedMedia ResolveWithYtDlp(const std::wstring& YtDlpPath, const std::string& Url, bool WantStreams, uint32_t MaxHeight)
{
	FResolvedMedia Result;
	if (YtDlpPath.empty() || GetFileAttributesW(YtDlpPath.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		Result.Error = "yt-dlp is not installed";
		return Result;
	}

	std::vector<std::wstring> Args = {L"--ignore-config", L"--no-playlist", L"--no-warnings", L"--encoding", L"utf-8",
		L"--print", L"%(title)s", L"--print", L"%(duration)s", L"--print", L"%(is_live)s"};
	if (!GQuickJsPath.empty())
	{
		Args.insert(Args.end(), {L"--js-runtimes", L"quickjs:" + GQuickJsPath});
	}
	if (WantStreams)
	{
		const std::wstring Height = std::to_wstring(MaxHeight ? MaxHeight : 720);
		Args.insert(Args.end(), {L"-S", L"res:" + Height + L",proto", L"-f", L"bv*+ba/b", L"--print", L"%(language)s", L"--print", L"%(subtitles)j",
			L"--print", L"%(automatic_captions)j", L"--print", L"urls"});
	}
	Args.push_back(L"--");
	Args.push_back(Widen(Url));

	const double Start = NowSeconds();
	std::string Out, Err;
	const int Code = RunProcess(YtDlpPath, Args, Out, Err);
	const std::vector<std::string> Lines = SplitLines(Out);
	LOG_INFO("yt-dlp %s: exit %d in %.1fs, %zu lines", WantStreams ? "resolve" : "probe", Code, NowSeconds() - Start, Lines.size());
	if (Code != 0 || Lines.size() < 3)
	{
		Result.Error = LastErrorLine(Err);
		LOG_WARN("yt-dlp failed for %s: %s", Url.c_str(), Result.Error.c_str());
		return Result;
	}

	Result.Title = Lines[0];
	Result.Duration = (Lines[1] == "NA" || Lines[1].empty()) ? 0.0 : atof(Lines[1].c_str());
	Result.IsLive = Lines[2] == "True";
	size_t First = 3;
	if (WantStreams && Lines.size() >= 6)
	{
		Result.Language = Lines[3] == "NA" ? std::string() : Lines[3];
		Result.SubtitlesJson = Lines[4];
		Result.AutomaticJson = Lines[5];
		First = 6;
	}
	for (size_t I = First; I < Lines.size(); ++I)
	{
		if (Lines[I].rfind("http", 0) == 0) Result.StreamUrls.push_back(Lines[I]);
	}
	if (WantStreams && Result.StreamUrls.empty())
	{
		Result.Error = "yt-dlp returned no stream URLs";
		return Result;
	}
	Result.Ok = true;
	return Result;
}
