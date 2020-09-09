// Copyright Epic Games, Inc. All Rights Reserved.FTexture2DMipDataProvider_IO

/*=============================================================================
Texture2DMipDataProvider_IO.cpp : Implementation of FTextureMipDataProvider using cooked file IO.
=============================================================================*/

#include "Texture2DMipDataProvider_IO.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Streaming/TextureStreamingHelpers.h"

FTexture2DMipDataProvider_IO::FTexture2DMipDataProvider_IO(const UTexture* InTexture, bool InPrioritizedIORequest)
	: FTextureMipDataProvider(InTexture, ETickState::Init, ETickThread::Async)
	, bPrioritizedIORequest(InPrioritizedIORequest)
{
}

FTexture2DMipDataProvider_IO::~FTexture2DMipDataProvider_IO()
{
	check(!IORequests.Num());
}

void FTexture2DMipDataProvider_IO::Init(const FTextureUpdateContext& Context, const FTextureUpdateSyncOptions& SyncOptions)
{
	int32 CurrentFileIndex = INDEX_NONE;

	for (int32 MipIndex = PendingFirstLODIdx; MipIndex < CurrentFirstLODIdx; ++MipIndex)
	{
		const FTexture2DMipMap& OwnerMip = *Context.MipsView[MipIndex];
		if (OwnerMip.BulkData.IsStoredCompressedOnDisk())
		{
			// Compression at the package level is no longer supported
			continue;
		}
		else if (OwnerMip.BulkData.GetBulkDataSize() <= 0)
		{
			// Invalid bulk data size.
			continue;
		}

		FString IOFilename;
		IOFilename = OwnerMip.BulkData.GetFilename();
		if (FileInfos.IsValidIndex(CurrentFileIndex))
		{
			FFileInfo& FileInfo = FileInfos[CurrentFileIndex];
			if (FileInfo.IOFilename == IOFilename && FileInfo.LastMipIndex + 1 == MipIndex)
			{
				FileInfo.LastMipIndex = MipIndex;
			}
			else // Otherwise create a new entry
			{
				CurrentFileIndex = INDEX_NONE;
			}
		}

		if (CurrentFileIndex == INDEX_NONE)
		{
			int64 IOFileOffset = 0;
			if (GEventDrivenLoaderEnabled)
			{
				if (IOFilename.EndsWith(TEXT(".uasset")) || IOFilename.EndsWith(TEXT(".umap")))
				{
					IOFileOffset = -IFileManager::Get().FileSize(*IOFilename);
					check(IOFileOffset < 0);
					IOFilename = FPaths::GetBaseFilename(IOFilename, false) + TEXT(".uexp");
					UE_LOG(LogTexture, Error, TEXT("Streaming from the .uexp file '%s' this MUST be in a ubulk instead for best performance."), *IOFilename);
				}
			}

			CurrentFileIndex = FileInfos.AddDefaulted();
			FFileInfo& FileInfo = FileInfos[CurrentFileIndex];
			FileInfo.IOFilename = MoveTemp(IOFilename);
			FileInfo.IOFileOffset = IOFileOffset;
			FileInfo.FirstMipIndex = MipIndex;
			FileInfo.LastMipIndex = MipIndex;
		}
	}

	AdvanceTo(ETickState::GetMips, ETickThread::Async);
}

int32 FTexture2DMipDataProvider_IO::GetMips(
	const FTextureUpdateContext& Context, 
	int32 StartingMipIndex, 
	const FTextureMipInfoArray& MipInfos, 
	const FTextureUpdateSyncOptions& SyncOptions)
{
	SetAsyncFileCallback(SyncOptions);
	check(SyncOptions.Counter && !IORequests.Num());
	
	IORequests.AddDefaulted(CurrentFirstLODIdx);

	for (FFileInfo& FileInfo : FileInfos)
	{
		while (StartingMipIndex >= FileInfo.FirstMipIndex && StartingMipIndex <= FileInfo.LastMipIndex && StartingMipIndex < CurrentFirstLODIdx)
		{
			if (!MipInfos.IsValidIndex(StartingMipIndex))
			{
				break;
			}
			const FTextureMipInfo& MipInfo = MipInfos[StartingMipIndex];

			if (!FileInfo.IOFileHandle)
			{
				FileInfo.IOFileHandle.Reset(FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*FileInfo.IOFilename));
				if (!FileInfo.IOFileHandle)
				{
					break;
				}
			}

			const FTexture2DMipMap& OwnerMip = *Context.MipsView[StartingMipIndex];
			// If Data size is specified check compatibility for safety
			if (MipInfo.DataSize && OwnerMip.BulkData.GetBulkDataSize() > MipInfo.DataSize)
			{
				break;
			}

			// Increment as we push the requests. If a requests complete immediately, then it will call the callback
			// but that won't do anything because the tick would not try to acquire the lock since it is already locked.
			SyncOptions.Counter->Increment();

			IORequests[StartingMipIndex].Reset(FileInfo.IOFileHandle->ReadRequest(
				OwnerMip.BulkData.GetBulkDataOffsetInFile() + FileInfo.IOFileOffset, 
				OwnerMip.BulkData.GetBulkDataSize(), 
				bPrioritizedIORequest ? AIOP_BelowNormal : AIOP_Low, 
				&AsyncFileCallBack, 
				(uint8*)MipInfo.DestData));

			++StartingMipIndex;
		}
	}

	AdvanceTo(ETickState::PollMips, ETickThread::Async);
	return StartingMipIndex;
}

bool FTexture2DMipDataProvider_IO::PollMips(const FTextureUpdateSyncOptions& SyncOptions)
{
	ClearIORequests();
	AdvanceTo(ETickState::Done, ETickThread::None);
	return !bIORequestCancelled;
}

void FTexture2DMipDataProvider_IO::AbortPollMips() 
{
	for (TUniquePtr<IAsyncReadRequest>& IORequest : IORequests)
	{
		if (IORequest)
		{
			// Calling IAsyncReadRequest::cancel() here will trigger the AsyncFileCallBack and precipitate the execution of Cancel().
			IORequest->Cancel();
		}
	}
}


void FTexture2DMipDataProvider_IO::CleanUp(const FTextureUpdateSyncOptions& SyncOptions)
{
	AdvanceTo(ETickState::Done, ETickThread::None);
}

void FTexture2DMipDataProvider_IO::Cancel(const FTextureUpdateSyncOptions& SyncOptions)
{
	ClearIORequests();
}

FTextureMipDataProvider::ETickThread FTexture2DMipDataProvider_IO::GetCancelThread() const
{
	return IORequests.Num() ? FTextureMipDataProvider::ETickThread::Async : FTextureMipDataProvider::ETickThread::None;
}

void FTexture2DMipDataProvider_IO::SetAsyncFileCallback(const FTextureUpdateSyncOptions& SyncOptions)
{
	FThreadSafeCounter* Counter = SyncOptions.Counter;
	FTextureUpdateSyncOptions::FCallback RescheduleCallback = SyncOptions.RescheduleCallback;
	check(Counter && RescheduleCallback);

	AsyncFileCallBack = [this, Counter, RescheduleCallback](bool bWasCancelled, IAsyncReadRequest* Req)
	{
		// At this point task synchronization would hold the number of pending requests.
		Counter->Decrement();
		
		if (bWasCancelled)
		{
			bIORequestCancelled = true;
		}

		if (Counter->GetValue() == 0)
		{
#if !UE_BUILD_SHIPPING
			// On some platforms the IO is too fast to test cancelation requests timing issues.
			if (FRenderAssetStreamingSettings::ExtraIOLatency > 0)
			{
				FPlatformProcess::Sleep(FRenderAssetStreamingSettings::ExtraIOLatency * .001f); // Slow down the streaming.
			}
#endif
			RescheduleCallback();
		}
	};
}

void FTexture2DMipDataProvider_IO::ClearIORequests()
{
	for (TUniquePtr<IAsyncReadRequest>& IORequest : IORequests)
	{
		if (IORequest)
		{
			// If clearing requests not yet completed, cancel and wait.
			if (!IORequest->PollCompletion())
			{
				IORequest->Cancel();
				IORequest->WaitCompletion();
			}
		}
	}
	IORequests.Empty();
}
