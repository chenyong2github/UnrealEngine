// Copyright Epic Games, Inc. All Rights Reserved.

#include "AsioStore.h"

#if TRACE_WITH_ASIO

namespace Trace
{

////////////////////////////////////////////////////////////////////////////////
FAsioStore::FAsioStore(asio::io_context& IoContext, const TCHAR* InStoreDir)
: FAsioObject(IoContext)
, StoreDir(InStoreDir)
{
	IPlatformFile& FileSystem = IPlatformFile::GetPlatformPhysical();
	FileSystem.IterateDirectory(InStoreDir, [this] (const TCHAR* Name, bool IsDirectory)
	{
#if 0
		if (IsDirectory)
		{
			int32 Id = FCString::Atoi(Name);
			LastTraceId = (Id < LastTraceId) ? Id : LastTraceId;
		}
#else
		const TCHAR* Dot = FCString::Strrchr(Name, '.');
		if (Dot == nullptr || FCString::Strcmp(Dot, TEXT(".utrace")))
		{
			return true;
		}

		for (const TCHAR* c = Dot; c > Name; --c)
		{
			if (c[-1] == '\\' || c[-1] == '/')
			{
				Name = c;
				break;
			}
		}

		FTrace* Trace = new FTrace();
		Trace->Name.AppendChars(Name, int32(Dot - Name));
		Traces.Add(Trace);
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
FAsioWriteable* FAsioStore::CreateTrace()
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
		return nullptr;
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

	return FAsioFile::WriteFile(GetIoContext(), *TracePath);
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
