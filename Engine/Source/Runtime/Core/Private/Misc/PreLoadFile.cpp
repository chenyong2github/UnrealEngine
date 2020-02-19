// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/PreLoadFile.h"
#include "HAL/Event.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "Async/AsyncFileHandle.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"

bool FPreLoadFile::bSystemNoLongerTakingRequests = false;

//
struct FPreloadFileRegistry
{
	FCriticalSection FileLock;
	TMap<FString, FPreLoadFile*> Files;

	static FPreloadFileRegistry& Get()
	{
		static FPreloadFileRegistry Instance;
		return Instance;
	}
};

#ifndef PLATFORM_CAN_ASYNC_PRELOAD_FILES
	#define PLATFORM_CAN_ASYNC_PRELOAD_FILES 0
#endif

FPreLoadFile::FPreLoadFile(const TCHAR* InPath)
	: FDelayedAutoRegisterHelper(STATS ? EDelayedRegisterRunPhase::StatSystemReady : EDelayedRegisterRunPhase::FileSystemReady, [this] { bSystemNoLongerTakingRequests = false; KickOffRead(); })
	, bIsComplete(false)
	, Data(nullptr)
	, FileSize(0)
	, Path(InPath)
	, CompletionEvent(nullptr)
{
	checkf(bSystemNoLongerTakingRequests == false, TEXT("Created a PreLoadFile object after it is no longer valid"));
	FPreloadFileRegistry& Registry = FPreloadFileRegistry::Get();
	FScopeLock Lock(&Registry.FileLock);
	Registry.Files.Add(Path, this);
}

void FPreLoadFile::KickOffRead()
{
	SCOPED_BOOT_TIMING("FPreLoadFile::KickOffRead");

	if (Path.StartsWith(TEXT("{PROJECT}")))
	{
		Path = Path.Replace(TEXT("{PROJECT}"), *FPaths::ProjectDir());
	}

	check(CompletionEvent == nullptr);
	CompletionEvent = FPlatformProcess::GetSynchEventFromPool();
	check(CompletionEvent != nullptr);

#if PLATFORM_CAN_ASYNC_PRELOAD_FILES
	FAsyncFileCallBack SizeCallbackFunction = [this](bool bWasCancelled, IAsyncReadRequest* SizeRequest)
	{
		FileSize = (int32)SizeRequest->GetSizeResults();
//		printf("Preloading %s, size = %d\n", TCHAR_TO_ANSI(*Path), FileSize);
		if (FileSize > 0)
		{
			FAsyncFileCallBack ReadCallbackFunction = [this](bool bWasCancelledInner, IAsyncReadRequest* ReadRequest)
			{
				Data = ReadRequest->GetReadResults();
				CompletionEvent->Trigger();
				delete AsyncReadHandle;
			};
			AsyncReadHandle->ReadRequest(0, FileSize, EAsyncIOPriorityAndFlags::AIOP_High, &ReadCallbackFunction);
		}
		else
		{
			FileSize = -1;
			CompletionEvent->Trigger();
			delete AsyncReadHandle;
		}
	};

	AsyncReadHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*Path);
	AsyncReadHandle->SizeRequest(&SizeCallbackFunction);

#else

	FArchive* Reader = IFileManager::Get().CreateFileReader(*Path);
	if (Reader != nullptr)
	{
		FileSize = Reader->TotalSize();
		Data = FMemory::Malloc(FileSize);
		Reader->Serialize(Data, FileSize);
		delete Reader;
	}
	CompletionEvent->Trigger();
#endif
}	


void* FPreLoadFile::TakeOwnershipOfLoadedData(int64* OutFileSize)
{
	check(CompletionEvent != nullptr);

	if (CompletionEvent->Wait(0) == false)
	{
		printf("PreLoadFile %s wasn't ready...\n", TCHAR_TO_ANSI(*Path));

		// wait until we are done
		CompletionEvent->Wait();
	}
	FPlatformProcess::ReturnSynchEventToPool(CompletionEvent);

	// return the size of Data if needed
	if (OutFileSize)
	{
		*OutFileSize = FileSize;
	}

	void* ReturnData = Data;
	Data = nullptr;

	// remove ourself from registry
	{
		FPreloadFileRegistry& Registry = FPreloadFileRegistry::Get();
		FScopeLock Lock(&Registry.FileLock);
		Registry.Files.Remove(Path);
	}

	return ReturnData;
}

void* FPreLoadFile::TakeOwnershipOfLoadedDataByPath(const TCHAR* Filename, int64* OutFileSize)
{
	FPreLoadFile* ExistingPreLoad;
	{
		FPreloadFileRegistry& Registry = FPreloadFileRegistry::Get();
		FScopeLock Lock(&Registry.FileLock);
		ExistingPreLoad = Registry.Files.FindRef(Filename);
	}

	// if we never attempted to read this, return 0 as filesize
	if (ExistingPreLoad == nullptr)
	{
		*OutFileSize = 0;
		return nullptr;

	}
	return ExistingPreLoad->TakeOwnershipOfLoadedData(OutFileSize);
}
