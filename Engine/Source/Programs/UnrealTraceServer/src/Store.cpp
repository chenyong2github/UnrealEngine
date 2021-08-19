// Copyright Epic Games, Inc. All Rights Reserved.

#include "Pch.h"
#include "Store.h"

////////////////////////////////////////////////////////////////////////////////
// Pre-C++20 there is now way to convert between clocks. C++20 onwards it is
// possible to create a clock with an Unreal epoch and convert file times to it.
// But at time of writing, C++20 isn't complete enough across the board.
static const uint64	UnrealEpochYear		= 1;
#if TS_USING(TS_PLATFORM_WINDOWS)
static const uint64	FsEpochYear			= 1601;
#else
static const uint64	FsEpochYear			= 1970;
#endif
static int64 FsToUnrealEpochBiasSeconds	= uint64(double(FsEpochYear - UnrealEpochYear) * 365.2425) * 86400;

#if TS_USING(TS_WITH_DIR_WATCHER)
////////////////////////////////////////////////////////////////////////////////
#if TS_USING(TS_PLATFORM_WINDOWS)
class FStore::FDirWatcher
	: public asio::windows::object_handle
{
public:
	using asio::windows::object_handle::object_handle;
};
#else
class FStore::FDirWatcher
	//: public asio::posix::stream_descriptor
{
public:
	void async_wait(...) {}
	void cancel() {}
	void close() {}
	bool is_open() { return false; }
};
#endif // PLATFORM_WINDOWS
#endif // TS_WITH_DIR_WATCHER



////////////////////////////////////////////////////////////////////////////////
FStore::FTrace::FTrace(const char* InPath)
: Path(InPath)
{
	// Extract the trace's name
	const char* Dot = std::strrchr(*Path, '.');
	if (Dot == nullptr)
	{
		Dot = *Path;
	}

	for (const char* c = Dot; c > *Path; --c)
	{
		if (c[-1] == '\\' || c[-1] == '/')
		{
			Name = FStringView(c, int32(Dot - c));
			break;
		}
	}

	Id = QuickStoreHash(Name);

	// Calculate that trace's timestamp. Bias in seconds then convert to 0.1us.
	std::filesystem::file_time_type LastWriteTime = std::filesystem::last_write_time(InPath);
	auto LastWriteDuration = LastWriteTime.time_since_epoch();
	Timestamp = std::chrono::duration_cast<std::chrono::seconds>(LastWriteDuration).count();
	Timestamp += FsToUnrealEpochBiasSeconds;
	Timestamp *= 10'000'000;
}

////////////////////////////////////////////////////////////////////////////////
const FStringView& FStore::FTrace::GetName() const
{
	return Name;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FStore::FTrace::GetId() const
{
	return Id;
}

////////////////////////////////////////////////////////////////////////////////
uint64 FStore::FTrace::GetSize() const
{
	return std::filesystem::file_size(*Path);
}

////////////////////////////////////////////////////////////////////////////////
uint64 FStore::FTrace::GetTimestamp() const
{
	return Timestamp;
}



////////////////////////////////////////////////////////////////////////////////
FStore::FStore(asio::io_context& InIoContext, const char* InStoreDir)
: IoContext(InIoContext)
, StoreDir(InStoreDir)
{
	StoreDir += "/001";
	std::filesystem::create_directories(*StoreDir);

	Refresh();

#if TS_USING(TS_WITH_DIR_WATCHER)
#if TS_USING(TS_PLATFORM_WINDOWS)
	FWinApiStr StoreDirW(*StoreDir);
	HANDLE DirWatchHandle = FindFirstChangeNotificationW(StoreDirW, false, FILE_NOTIFY_CHANGE_FILE_NAME);
	if (DirWatchHandle == INVALID_HANDLE_VALUE)
	{
		DirWatchHandle = 0;
	}
	DirWatcher = new FDirWatcher(IoContext, DirWatchHandle);
#else
	/* NOTE TO SELF - DELETE FWinApiStr NOW! */
#endif // TS_PLATFORM_WINDOWS

	WatchDir();
#endif // TS_WITH_DIR_WATCHER
}

////////////////////////////////////////////////////////////////////////////////
FStore::~FStore()
{
#if TS_USING(TS_WITH_DIR_WATCHER)
	if (DirWatcher != nullptr)
	{
		check(!DirWatcher->is_open());
		delete DirWatcher;
	}
#endif // TS_WITH_DIR_WATCHER
}

////////////////////////////////////////////////////////////////////////////////
void FStore::Close()
{
#if TS_USING(TS_WITH_DIR_WATCHER)
	if (DirWatcher != nullptr)
	{
		DirWatcher->cancel();
		DirWatcher->close();
	}
#endif // TS_WITH_DIR_WATCHER

	ClearTraces();
}

////////////////////////////////////////////////////////////////////////////////
void FStore::ClearTraces()
{
	for (FTrace* Trace : Traces)
	{
		delete Trace;
	}

	Traces.Empty();
	ChangeSerial = 0;
}

#if TS_USING(TS_WITH_DIR_WATCHER)
////////////////////////////////////////////////////////////////////////////////
void FStore::WatchDir()
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

#if TS_USING(TS_PLATFORM_WINDOWS)
		// Windows doesn't update modified timestamps in a timely fashion when
		// copying files (or it could be Explorer that doesn't update it until
		// later). This is a not-so-pretty "wait for a little bit" workaround.
		auto* DelayTimer = new asio::steady_timer(IoContext);
		DelayTimer->expires_after(std::chrono::seconds(2));
		DelayTimer->async_wait([this, DelayTimer] (const asio::error_code& ErrorCode)
		{
			delete DelayTimer;

			Refresh();

			FindNextChangeNotification(DirWatcher->native_handle());
			WatchDir();
		});
#else
		Refresh();
		WatchDir();
#endif
	});
}
#endif // TS_WITH_DIR_WATCHER

////////////////////////////////////////////////////////////////////////////////
const char* FStore::GetStoreDir() const
{
	return *StoreDir;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FStore::GetChangeSerial() const
{
	return ChangeSerial;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FStore::GetTraceCount() const
{
	return Traces.Num();
}

////////////////////////////////////////////////////////////////////////////////
const FStore::FTrace* FStore::GetTraceInfo(uint32 Index) const
{
	if (Index >= uint32(Traces.Num()))
	{
		return nullptr;
	}

	return Traces[Index];
}

////////////////////////////////////////////////////////////////////////////////
FStore::FTrace* FStore::GetTrace(uint32 Id) const
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
FStore::FTrace* FStore::AddTrace(const char* Path)
{
	FTrace NewTrace(Path);

	uint32 Id = NewTrace.GetId();
	if (FTrace* Existing = GetTrace(Id))
	{
		return Existing;
	}

	ChangeSerial += Id;

	FTrace* Trace = new FTrace(MoveTemp(NewTrace));
	Traces.Add(Trace);
	return Trace;
}

////////////////////////////////////////////////////////////////////////////////
FStore::FNewTrace FStore::CreateTrace()
{
	FString TracePath;
#if 0
	bool bOk = false;
	for (int i = 0; i < 256; ++i)
	{
		uint32 TraceId = ++LastTraceId;

		TracePath = StoreDir;
		TracePath.Appendf("/%05d", TraceId);

		if (!std::filesystem::is_directory(*TracePath) && std::filesystem::create_directories(*TracePath))
		{
			bOk = true;
			break;
		}
	}

	if (!bOk)
	{
		return {};
	}

	TracePath += "/data.utrace";
#else

	// N.B. Not thread safe!?
	char Prefix[24];
	std::time_t Now = std::time(nullptr);
	std::tm* LocalNow = std::localtime(&Now);
	std::strftime(Prefix, TS_ARRAY_COUNT(Prefix), "%Y%m%d_%H%M%S", LocalNow);

	TracePath = StoreDir;
	TracePath += "/";
	TracePath += Prefix;
	TracePath += ".utrace";
	for (uint32 Index = 0; std::filesystem::is_regular_file(*TracePath); ++Index)
	{
		char Suffix[64];
		std::snprintf(Suffix, TS_ARRAY_COUNT(Suffix), "/%s_%02d.utrace", Prefix, Index);

		TracePath = StoreDir;
		TracePath += *Suffix;
	}
#endif // 0

	FAsioWriteable* File = FAsioFile::WriteFile(IoContext, *TracePath);
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
bool FStore::HasTrace(uint32 Id) const
{
	return GetTrace(Id) != nullptr;
}

////////////////////////////////////////////////////////////////////////////////
FAsioReadable* FStore::OpenTrace(uint32 Id)
{
	FTrace* Trace = GetTrace(Id);
	if (Trace == nullptr)
	{
		return nullptr;
	}

	FString TracePath;
	TracePath = StoreDir;
#if 0
	TracePath.Appendf("/%05d/data.utrace", Id);
#else
	TracePath += "/";
	TracePath += Trace->GetName();
	TracePath += ".utrace";
#endif // 0

	return FAsioFile::ReadFile(IoContext, *TracePath);
}

////////////////////////////////////////////////////////////////////////////////
void FStore::Refresh()
{
	ClearTraces();

	for (auto& DirItem : std::filesystem::directory_iterator(*StoreDir))
	{
#if 0
		if (!DirItem.is_directory())
		{
			continue;
		}

		int32 Id = FCString::Atoi(Path);
		LastTraceId = (Id < LastTraceId) ? Id : LastTraceId;
#else
		if (DirItem.is_directory())
		{
			continue;
		}

		std::filesystem::path Extension = DirItem.path().extension();
		if (Extension != ".utrace")
		{
			continue;
		}

		AddTrace(DirItem.path().string().c_str());
#endif // 0
	};
}

/* vim: set noexpandtab : */
