// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioStore.h"

#if TRACE_WITH_ASIO

#include "HAL/PlatformFile.h"
#include "Containers/UnrealString.h"
#include "Misc/CString.h"
#include "Misc/DateTime.h"

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
const TCHAR* FAsioStore::FTrace::GetName() const
{
	return *Name;
}

////////////////////////////////////////////////////////////////////////////////
uint32 FAsioStore::FTrace::GetId() const
{
	return Id;
}

////////////////////////////////////////////////////////////////////////////////
uint64 FAsioStore::FTrace::GetSize() const
{
	HANDLE Inner = HANDLE(Handle);
	LARGE_INTEGER FileSize;
	GetFileSizeEx(Inner, &FileSize);
	return FileSize.QuadPart;
}



////////////////////////////////////////////////////////////////////////////////
FAsioStore::FAsioStore(asio::io_context& IoContext, const TCHAR* InStoreDir)
: FAsioObject(IoContext)
, StoreDir(InStoreDir)
{
	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();
	FileSystem.IterateDirectory(InStoreDir, [this] (const TCHAR* Path, bool IsDirectory)
	{
#if 0
		if (IsDirectory)
		{
			int32 Id = FCString::Atoi(Path);
			LastTraceId = (Id < LastTraceId) ? Id : LastTraceId;
		}
#else
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

////////////////////////////////////////////////////////////////////////////////
FAsioStore::~FAsioStore()
{
	for (FTrace* Trace : Traces)
	{
		delete Trace;
	}
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
	HANDLE Handle = CreateFileW(Path, 0, FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
		nullptr, OPEN_EXISTING, 0, nullptr);
	if (Handle == INVALID_HANDLE_VALUE)
	{
		return nullptr;
	}

	const TCHAR* Dot = FCString::Strrchr(Path, '.');
	if (Dot == nullptr)
	{
		return nullptr;
	}

	FStringView Name;
	for (const TCHAR* c = Dot; c > Path; --c)
	{
		if (c[-1] == '\\' || c[-1] == '/')
		{
			Name = FStringView(c, int32(Dot - c));
			break;
		}
	}

	FTrace* Trace = new FTrace();
	Trace->Name = Name;
	Trace->Id = QuickStoreHash(Name);
	Trace->Handle = UPTRINT(Handle);

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
	TracePath.Appendf(TEXT("/%s.utrace"), Trace->GetName());
#endif // 0

	return FAsioFile::ReadFile(GetIoContext(), *TracePath);
}

} // namespace Trace

#endif // TRACE_WITH_ASIO
