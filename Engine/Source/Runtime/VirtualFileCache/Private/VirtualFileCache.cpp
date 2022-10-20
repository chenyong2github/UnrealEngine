// Copyright Epic Games, Inc. All Rights Reserved.

#include "VirtualFileCache.h"
#include "VirtualFileCacheInternal.h"

#include "CoreMinimal.h"
#include "HAL/FileManager.h"
#include "Logging/LogMacros.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Async/MappedFileHandle.h"

DECLARE_STATS_GROUP(TEXT("VFC"), STATGROUP_VFC, STATCAT_Advanced);

DECLARE_DWORD_COUNTER_STAT(TEXT("Files Added"), STAT_FilesAdded, STATGROUP_VFC);
DECLARE_DWORD_COUNTER_STAT(TEXT("Bytes Added"), STAT_BytesAdded, STATGROUP_VFC);
DECLARE_DWORD_COUNTER_STAT(TEXT("Files Removed"), STAT_FilesRemoved, STATGROUP_VFC);
DECLARE_DWORD_COUNTER_STAT(TEXT("Bytes Removed"), STAT_BytesRemoved, STATGROUP_VFC);
DECLARE_DWORD_COUNTER_STAT(TEXT("Files Evicted"), STAT_FilesEvicted, STATGROUP_VFC);
DECLARE_DWORD_COUNTER_STAT(TEXT("Bytes Evicted"), STAT_BytesEvicted, STATGROUP_VFC);

DECLARE_LOG_CATEGORY_EXTERN(LogVFC, Log, All);
DEFINE_LOG_CATEGORY(LogVFC);

static const TCHAR VFC_CACHE_FILE_BASE_NAME[] = TEXT("vfc_");
static const TCHAR VFC_CACHE_FILE_EXTENSION[] = TEXT("data");
static const TCHAR VFC_META_FILE_NAME[] = TEXT("vfc.meta");

TSharedRef<IVirtualFileCache> IVirtualFileCache::CreateVirtualFileCache()
{
	static TWeakPtr<FVirtualFileCache> GVFC;
	TSharedPtr<FVirtualFileCache> SharedVFC;
	if (!GVFC.IsValid())
	{
		SharedVFC = MakeShared<FVirtualFileCache>();
		GVFC = SharedVFC;
	}
	return GVFC.IsValid() ? GVFC.Pin().ToSharedRef() : SharedVFC.ToSharedRef();
}

void FVirtualFileCache::Shutdown()
{
	Thread.Shutdown();
}

void FVirtualFileCache::Initialize(const FVirtualFileCacheSettings& InSettings)
{
	Settings = InSettings;
	IFileManager& FileManager = IFileManager::Get();
	BasePath = !Settings.OverrideDefaultDirectory.IsEmpty() ? Settings.OverrideDefaultDirectory : GetVFCDirectory();
	if (!FileManager.DirectoryExists(*BasePath))
	{
		FileManager.MakeDirectory(*BasePath, true);
	}

	FFileTableWriter FileTable = Thread.ModifyFileTable();
	FileTable->Initialize(Settings);
}

FIoStatus FVirtualFileCache::WriteData(VFCKey Id, const uint8* Data, uint64 DataSize)
{
	Thread.RequestWrite(Id, MakeArrayView(Data, DataSize));
	return FIoStatus(EIoErrorCode::Ok);
}

TFuture<TArray<uint8>> FVirtualFileCache::ReadData(VFCKey Id, int64 ReadOffset, int64 ReadSizeOrZero)
{
	return Thread.RequestRead(Id, ReadOffset, ReadSizeOrZero);
}

bool FVirtualFileCache::DoesChunkExist(const VFCKey& Id) const
{
	FFileTableReader FileTable = Thread.ReadFileTable();
	return FileTable->DoesChunkExist(Id);
}

TIoStatusOr<uint64> FVirtualFileCache::GetSizeForChunk(const VFCKey& Id) const
{
	FFileTableReader FileTable = Thread.ReadFileTable();
	return FileTable->GetSizeForChunk(Id);
}

void FVirtualFileCache::EraseData(VFCKey Id)
{
	Thread.RequestErase(Id);
}

double FVirtualFileCache::CurrentFragmentation() const
{
	FFileTableReader FileTable = Thread.ReadFileTable();
	return FileTable->CurrentFragmentation();
}

void FVirtualFileCache::Defragment()
{
	FFileTableWriter FileTable = Thread.ModifyFileTable();
	return FileTable->Defragment();
}
