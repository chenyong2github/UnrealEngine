// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SessionMonitorPCH.h"
#include "Utils.h"
#include "StringUtils.h"


// To allow using some deprecated string functions
#pragma warning(disable :4996)

#if EG_PLATFORM == EG_PLATFORM_WINDOWS
std::string GetProcessPath(std::string* Filename)
{
	wchar_t Buf[MAX_PATH];
	GetModuleFileNameW(NULL, Buf, MAX_PATH);

	std::string Res = Narrow(Buf);
	std::string::size_type Index = Res.rfind("\\");

	if (Index != std::string::npos)
	{
		if (Filename)
		{
			*Filename = Res.substr(Index + 1);
		}

		Res = Res.substr(0, Index + 1);
	}
	else
	{
		return "";
	}

	return Res;
}
#elif EG_PLATFORM == EG_PLATFORM_LINUX
#error Not implemented yet
#else
#error Unknown platform
#endif


std::string GetExtension(const std::string& FullFilename, std::string* Basename)
{
	size_t SlashPos = FullFilename.find_last_of("/\\");
	size_t P = FullFilename.find_last_of(".");

	// Where the file name starts (we ignore directories)
	size_t NameStart = SlashPos != std::string::npos ? SlashPos + 1 : 0;

	// Account for the fact there might not be an extension, but there is a dot character,
	// as for example in relative paths. E.g:  ..\SomeFile
	if (P == std::string::npos || (SlashPos != std::string::npos && P < SlashPos))
	{
		if (Basename)
		{
			*Basename = FullFilename.substr(NameStart);
		}

		return "";
	}
	else
	{
		std::string Res = FullFilename.substr(P + 1);
		if (Basename)
		{
			*Basename = FullFilename.substr(NameStart, P - NameStart);
		}

		return Res;
	}
}

std::pair<std::string, std::string> GetFolderAndFile(const std::string& FullFilename)
{
	auto Slash = std::find_if(FullFilename.rbegin(), FullFilename.rend(), [](const char& ch)
	{
		return ch == '/' || ch == '\\';
	});

	std::pair<std::string, std::string> Res;
	Res.first = std::string(FullFilename.begin(), Slash.base());
	Res.second = std::string(Slash.base(), FullFilename.end());
	return Res;
}

std::string GetCWD()
{
	wchar_t Buf[MAX_PATH + 1];
	Buf[0] = 0;
	verify(GetCurrentDirectoryW(MAX_PATH, Buf) != 0);
	return Narrow(std::wstring(Buf) + L"\\");
}

bool FullPath(std::string& Dst, const std::string& Path, std::string Root)
{
	if (Root.empty())
		Root = GetCWD();
	std::string Tmp = PathIsRelativeW(Widen(Path).c_str()) ? Root + Path : Path;

	char SrcFullPath[MAX_PATH+1];
	strcpy(SrcFullPath, Tmp.c_str());

	// Clean up the string:
	// - Replace '/' with '\'
	// - Remove repeated '/' or '\'
	char* DstCh = SrcFullPath;
	char* SrcCh = SrcFullPath;
	while(*SrcCh)
	{
		if (*SrcCh == '/')
		{
			*SrcCh = '\\';
		}
		*DstCh++= *SrcCh;

		if (*SrcCh == '\\')
		{
			// Skip repeated
			SrcCh++;
			while (*SrcCh && (*SrcCh == '\\' || *SrcCh == '/'))
			{
				SrcCh++;
			}
		}
		else
		{
			SrcCh++;
		}
	}
	*DstCh = 0;

	wchar_t DstFullPath[MAX_PATH+1];
	if (PathCanonicalizeW(DstFullPath, Widen(SrcFullPath).c_str()))
	{
		Dst = Narrow(DstFullPath);
		return true;
	}
	else
	{
		return false;
	}
}

std::string Win32ErrorMsg(const char* FuncName)
{
	LPVOID PtrMsgBuf;
	LPVOID PtrDisplayBuf;
	DWORD Err = GetLastError();

	FormatMessageA(
	    FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	    NULL,
	    Err,
	    MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
	    (char*)&PtrMsgBuf,
	    0,
	    NULL);

	int FuncNameLen = FuncName ? (int)strlen(FuncName) : 0;
	PtrDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT, (strlen((char*)PtrMsgBuf) + FuncNameLen + 50));
	StringCchPrintfA(
	    (char*)PtrDisplayBuf,
	    LocalSize(PtrDisplayBuf),
	    "%s failed with error %d: %s",
	    FuncName ? FuncName : "",
	    Err,
	    PtrMsgBuf);

	std::string Msg = (char*)PtrDisplayBuf;
	LocalFree(PtrMsgBuf);
	LocalFree(PtrDisplayBuf);

	// Remove the \r\n at the end
	while (Msg.size() && Msg.back() < ' ')
	{
		Msg.pop_back();
	}

	return Msg;
}

std::string AddrToString(const boost::asio::ip::tcp::endpoint& Addr)
{
	std::ostringstream OStr;
	OStr << Addr;
	return OStr.str();
}
