// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioStore.h"
#include "HAL/PlatformFile.h"
#include "Containers/StringConv.h"
#include "Containers/UnrealString.h"
#include "Misc/CString.h"
#include "Misc/DateTime.h"

#if PLATFORM_WINDOWS
#	include "Windows/AllowWindowsPlatformTypes.h"
#	include <Windows.h>
#	include "Windows/HideWindowsPlatformTypes.h"
#endif // PLATFORM_WINDOWS

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
#if PLATFORM_WINDOWS
class FAsioStore::FDirWatcher
	: public asio::windows::object_handle
{
public:
	using asio::windows::object_handle::object_handle;
};
#else
class FAsioStore::FDirWatcher
	//: public asio::posix::stream_descriptor
{
public:
	void async_wait(...) {}
	void cancel() {}
	void close() {}
};
#endif // PLATFORM_WINDOWS



////////////////////////////////////////////////////////////////////////////////
FAsioStore::FTrace::FTrace(const TCHAR* InPath)
: Path(InPath)
, Id(QuickStoreHash(InPath))
{
	// Extract the trace's name
	const TCHAR* Dot = FCString::Strrchr(*Path, '.');
	if (Dot == nullptr)
	{
		Dot = *Path;
	}

	for (const TCHAR* c = Dot; c > *Path; --c)
	{
		if (c[-1] == '\\' || c[-1] == '/')
		{
			Name = FStringView(c, int32(Dot - c));
			break;
		}
	}

	// Calculate that trace's timestamp
	uint64 InTimestamp = 0;
#if PLATFORM_WINDOWS
	HANDLE Handle = CreateFileW(InPath, 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, 0, nullptr);
	if (Handle != INVALID_HANDLE_VALUE)
	{
		FILETIME Time;
		if (GetFileTime(Handle, &Time, nullptr, nullptr))
		{
			// Windows FILETIME is a 64-bit value that represents the number of 100-nanosecond intervals that have elapsed since 12:00 A.M.January 1, 1601 Coordinated Universal Time(UTC).
			// We adjust it to be compatible with the FDateTime ticks number of 100-nanosecond intervals that have elapsed since 12:00 A.M.January 1, 0001 Coordinated Universal Time(UTC).
			const uint64 WinTicks = (static_cast<uint64>(Time.dwHighDateTime) << 32ull) | static_cast<uint64>(Time.dwLowDateTime);
			const uint64 Year1601 = 504911232000000000ULL; // FDateTime(1601, 1, 1).GetTicks()
			InTimestamp = Year1601 + WinTicks;
		}

		CloseHandle(Handle);
	}
#else
	struct stat FileStat;
	if (stat(TCHAR_TO_UTF8(InPath), &FileStat) == 0)
	{
		InTimestamp = (uint64(FileStat.st_ctim.tv_sec) * 1000 * 1000 * 1000) + FileStat.st_ctim.tv_nsec;
		InTimestamp /= 100;
		InTimestamp += 0x004d5bd15e978000ull; // k = unix_epoch - FDateTime_epoch (in 100-nanoseconds)
	}
#endif
	Timestamp = InTimestamp;
}

////////////////////////////////////////////////////////////////////////////////
const FStringView& FAsioStore::FTrace::GetName() const
{
	return Name;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAsioStore::FTrace::GetId() const
{
	return Id;
}

////////////////////////////////////////////////////////////////////////////////
uint64 FAsioStore::FTrace::GetSize() const
{
#if PLATFORM_WINDOWS
	LARGE_INTEGER FileSize = {};
	HANDLE Handle = CreateFileW(*Path, 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, 0, nullptr);
	if (Handle != INVALID_HANDLE_VALUE)
	{
		GetFileSizeEx(Handle, &FileSize);
		CloseHandle(Handle);
	}
	return FileSize.QuadPart;
#else
	struct stat FileStat;
	if (stat(TCHAR_TO_UTF8(*Path), &FileStat) == 0)
	{
		return uint64(FileStat.st_size);
	}
	return 0;
#endif
}

////////////////////////////////////////////////////////////////////////////////
uint64 FAsioStore::FTrace::GetTimestamp() const
{
	return Timestamp;
}



////////////////////////////////////////////////////////////////////////////////
FAsioStore::FAsioStore(asio::io_context& IoContext, const TCHAR* InStoreDir)
: FAsioObject(IoContext)
, StoreDir(InStoreDir)
{
	Refresh();

#if PLATFORM_WINDOWS
	HANDLE DirWatchHandle = FindFirstChangeNotificationW(InStoreDir, false, FILE_NOTIFY_CHANGE_FILE_NAME);
	if (DirWatchHandle == INVALID_HANDLE_VALUE)
	{
		DirWatchHandle = 0;
	}
	DirWatcher = new FDirWatcher(IoContext, DirWatchHandle);
#endif

	WatchDir();
}

////////////////////////////////////////////////////////////////////////////////
FAsioStore::~FAsioStore()
{
	Close();
	delete DirWatcher;
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStore::Close()
{
	if (DirWatcher != nullptr)
	{
		DirWatcher->cancel();
		DirWatcher->close();
	}

	ClearTraces();
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStore::ClearTraces()
{
	for (FTrace* Trace : Traces)
	{
		delete Trace;
	}

	Traces.Empty();
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStore::WatchDir()
{
	if (DirWatcher == nullptr)
	{
		return;
	}
	
	DirWatcher->async_wait([this] (asio::error_code ErrorCode)
	{
		if (ErrorCode)
		{
			return;
		}

#if PLATFORM_WINDOWS
		FindNextChangeNotification(DirWatcher->native_handle());
#endif
		Refresh();
		WatchDir();
	});
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAsioStore::GetTraceCount() const
{
	return Traces.Num();
}

////////////////////////////////////////////////////////////////////////////////
const FAsioStore::FTrace* FAsioStore::GetTraceInfo(uint32 Index) const
{
	if (Index >= uint32(Traces.Num()))
	{
		return nullptr;
	}

	return Traces[Index];
}

////////////////////////////////////////////////////////////////////////////////
FAsioStore::FTrace* FAsioStore::GetTrace(uint32 Id)
{
	for (FTrace* Trace : Traces)
	{
		if (Trace->GetId() == Id)
		{
			return Trace;
		}
	}

	return nullptr;
}

////////////////////////////////////////////////////////////////////////////////
FAsioStore::FTrace* FAsioStore::AddTrace(const TCHAR* Path)
{
	FTrace NewTrace(Path);

	uint32 Id = NewTrace.GetId();
	if (FTrace* Existing = GetTrace(Id))
	{
		return Existing;
	}

	FTrace* Trace = new FTrace(MoveTemp(NewTrace));
	Traces.Add(Trace);
	return Trace;
}

////////////////////////////////////////////////////////////////////////////////
FAsioStore::FNewTrace FAsioStore::CreateTrace()
{
	IPlatformFile& PlatformFile = IPlatformFile::GetPlatformPhysical();

	FString TracePath;
#if 0
	bool bOk = false;
	for (int i = 0; i < 256; ++i)
	{
		uint32 TraceId = ++LastTraceId;

		TracePath = StoreDir;
		TracePath.Appendf(TEXT("/%05d"), TraceId);

		if (!PlatformFile.DirectoryExists(*TracePath) && PlatformFile.CreateDirectory(*TracePath))
		{
			bOk = true;
			break;
		}
	}

	if (!bOk)
	{
		return {};
	}

	TracePath += TEXT("/data.utrace");
#else
	FString Prefix = FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"));

	TracePath = StoreDir;
	TracePath.Appendf(TEXT("/%s.utrace"), *Prefix);
	for (uint64 Index = 0; PlatformFile.FileExists(*TracePath); ++Index)
	{
		TracePath = StoreDir;
		TracePath.Appendf(TEXT("/%s_%d.utrace"), *Prefix, Index);
	}
#endif // 0

	FAsioWriteable* File = FAsioFile::WriteFile(GetIoContext(), *TracePath);
	if (File == nullptr)
	{
		return {};
	}

	FTrace* Trace = AddTrace(*TracePath);
	if (Trace == nullptr)
	{
		delete File;
		return {};
	}

	return { Trace->GetId(), File };
}

////////////////////////////////////////////////////////////////////////////////
FAsioReadable* FAsioStore::OpenTrace(uint32 Id)
{
	FTrace* Trace = GetTrace(Id);
	if (Trace == nullptr)
	{
		return nullptr;
	}

	FString TracePath;
	TracePath = StoreDir;
#if 0
	TracePath.Appendf(TEXT("/%05d/data.utrace"), Id);
#else
	const FStringView& Name = Trace->GetName();
	TracePath.Appendf(TEXT("/%.*s.utrace"), Name.Len(), Name.GetData());
#endif // 0

	return FAsioFile::ReadFile(GetIoContext(), *TracePath);
}

////////////////////////////////////////////////////////////////////////////////
void FAsioStore::Refresh()
{
	ClearTraces();

	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();
	FileSystem.IterateDirectory(*StoreDir, [this] (const TCHAR* Path, bool IsDirectory)
	{
#if 0
		if (!IsDirectory)
		{
			return true;
		}

		int32 Id = FCString::Atoi(Path);
		LastTraceId = (Id < LastTraceId) ? Id : LastTraceId;
#else
		if (IsDirectory)
		{
			return true;
		}

		const TCHAR* Dot = FCString::Strrchr(Path, '.');
		if (Dot == nullptr || FCString::Strcmp(Dot, TEXT(".utrace")))
		{
			return true;
		}

		AddTrace(Path);
#endif // 0

		return true;
	});
}

} // namespace Trace
