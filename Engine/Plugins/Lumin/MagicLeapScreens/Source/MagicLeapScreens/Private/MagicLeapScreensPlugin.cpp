// Copyright Epic Games, Inc. All Rights Reserved.

#include "MagicLeapScreensPlugin.h"
#include "Async/Async.h"
#include "RenderUtils.h"
#include "IMagicLeapPlugin.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "MagicLeapScreensRunnable.h"
#include "Misc/CoreDelegates.h"
#include "MagicLeapMath.h"
#include "MagicLeapHandle.h"
#include "Misc/CommandLine.h"

#define MAX_TEXTURE_SIZE 450 * 450 * 4 // currently limited by binder implementation

DEFINE_LOG_CATEGORY_STATIC(LogScreensPlugin, Display, All);

void FMagicLeapScreensPlugin::StartupModule()
{
	IModuleInterface::StartupModule();

#if WITH_MLSDK
	DefaultThumbnail = MLImage();
	DefaultThumbnail.width = 2;
	DefaultThumbnail.height = 2;
	DefaultThumbnail.image_type = MLImageType_RGBA32;
	DefaultThumbnail.alignment = 1;
	const SIZE_T DataSize = DefaultThumbnail.width * DefaultThumbnail.height * 4;
	DefaultThumbnail.data = new uint8[DataSize];
	FMemory::Memset(DefaultThumbnail.data, 255, DataSize);
#endif // WITH_MLSDK

	if (!Runnable)
	{
		Runnable = new FScreensRunnable();
	}
	TickDelegate = FTickerDelegate::CreateRaw(this, &FMagicLeapScreensPlugin::Tick);
	TickDelegateHandle = FTicker::GetCoreTicker().AddTicker(TickDelegate);

	PixelDataMemPool.Reserve(MAX_TEXTURE_SIZE);
}

void FMagicLeapScreensPlugin::ShutdownModule()
{
	FScreensRunnable* RunnableToDestroy = Runnable;
	Runnable = nullptr;
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [RunnableToDestroy]()
	{
		delete RunnableToDestroy;
	});
#if WITH_MLSDK
	delete[] DefaultThumbnail.data;
	DefaultThumbnail.data = nullptr;
	MLScreensReleaseWatchHistoryThumbnail(&DefaultThumbnail);
#endif // WITH_MLSDK
	FTicker::GetCoreTicker().RemoveTicker(TickDelegateHandle);
	IModuleInterface::ShutdownModule();
}

bool FMagicLeapScreensPlugin::IsSupportedFormat(EPixelFormat InPixelFormat)
{
	if (InPixelFormat == PF_B8G8R8A8 || InPixelFormat == PF_R8G8B8A8)
	{
		return true;
	}

	UE_LOG(LogScreensPlugin, Error, TEXT("Unsupported pixel format!"));

	return false;
}

#if WITH_MLSDK
UTexture2D* FMagicLeapScreensPlugin::MLImageToUTexture2D(const MLImage& Source)
{
	UTexture2D* Thumbnail = UTexture2D::CreateTransient(Source.width, Source.height, EPixelFormat::PF_R8G8B8A8);
	FTexture2DMipMap& Mip = Thumbnail->PlatformData->Mips[0];
	void* PixelData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	const uint32 PixelDataSize = Mip.BulkData.GetBulkDataSize();
	FMemory::Memcpy(PixelData, Source.data, PixelDataSize);
	UE_LOG(LogScreensPlugin, Log, TEXT("MLImageToUTexture2D width = %u height = %u size = %u"), Source.width, Source.height, PixelDataSize);
	Thumbnail->SRGB = true;
	Mip.BulkData.Unlock();
	Thumbnail->UpdateResource();

	return Thumbnail;
}

void FMagicLeapScreensPlugin::MLWatchHistoryEntryToUnreal(const MLScreensWatchHistoryEntry& InEntry, FMagicLeapScreensWatchHistoryEntry& OutEntry)
{
	OutEntry.ID = MagicLeap::MLHandleToFGuid(InEntry.id);
	OutEntry.Title = FString(UTF8_TO_TCHAR(InEntry.title));
	OutEntry.Subtitle = FString(UTF8_TO_TCHAR(InEntry.subtitle));
	OutEntry.PlaybackPosition = FTimespan(InEntry.playback_position_ms * ETimespan::TicksPerMillisecond);
	OutEntry.PlaybackDuration = FTimespan(InEntry.playback_duration_ms * ETimespan::TicksPerMillisecond);
	OutEntry.CustomData = FString(UTF8_TO_TCHAR(InEntry.custom_data));

	MLImage MLThumbnail;
	MLResult Result = MLScreensGetWatchHistoryThumbnail(InEntry.id, &MLThumbnail);
	if (Result != MLResult_Ok)
	{
		UE_LOG(LogScreensPlugin, Log, TEXT("MLScreensGetWatchHistoryThumbnail failed for screen ID %u with error %s!"), (uint32)InEntry.id, UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
		OutEntry.Thumbnail = MLImageToUTexture2D(DefaultThumbnail);
	}
	else
	{
		OutEntry.Thumbnail = MLImageToUTexture2D(MLThumbnail);
		// Only release when default thumbnail is not used, default thumbnail should only be released when plugin shuts down
		Result = MLScreensReleaseWatchHistoryThumbnail(&MLThumbnail);
		UE_CLOG(Result != MLResult_Ok, LogScreensPlugin, Error, TEXT("MLScreensReleaseWatchHistoryThumbnail failed for with error %s!"), UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
	}
}

bool FMagicLeapScreensPlugin::MLScreenInfoToUnreal(const MLScreensScreenInfoEx& InInfo, FMagicLeapScreenTransform& OutScreenTransform, const FTransform& TrackingToWorld, const float WorldToMetersScale)
{
	FTransform OutTransform(MagicLeap::ToFMatrix2(InInfo.transform, WorldToMetersScale));

	if (OutTransform.ContainsNaN())
	{
		UE_LOG(LogScreensPlugin, Error, TEXT("Screens info entry %d transform contains NaN."), InInfo.screen_id);
		return false;
	}
	if (!OutTransform.GetRotation().IsNormalized())
	{
		FQuat OutRotation = OutTransform.GetRotation();
		OutRotation.Normalize();
		OutTransform.SetRotation(OutRotation);
	}

	MLVec3f Scale = MagicLeap::ExtractScale3D(InInfo.transform);
	MLVec3f Dimensions = InInfo.dimensions;
	Dimensions.x *= Scale.x;
	Dimensions.y *= Scale.y;
	Dimensions.z *= Scale.z;

	OutTransform = OutTransform * TrackingToWorld;

	OutScreenTransform.ID = MagicLeap::MLHandleToFGuid(InInfo.screen_id);
	OutScreenTransform.VersionNumber = InInfo.version;
	OutScreenTransform.ScreenPosition = OutTransform.GetLocation();
	OutScreenTransform.ScreenOrientation = OutTransform.Rotator();
	OutScreenTransform.ScreenDimensions = MagicLeap::ToFVectorExtents(Dimensions, WorldToMetersScale);
	OutScreenTransform.ScreenScale3D = MagicLeap::ToFVectorNoScale(Scale);
	return true;
}

bool FMagicLeapScreensPlugin::UTexture2DToMLImage(const UTexture2D& Source, MLImage& Target)
{
	bool bSuccess = false;
	FTexture2DMipMap& Mip = Source.PlatformData->Mips[0];
	void* PixelData = Mip.BulkData.Lock(LOCK_READ_ONLY);
	const int32 size = Mip.BulkData.GetBulkDataSize();

	if ((size <= MAX_TEXTURE_SIZE) && (size != 0) && (PixelData != nullptr))
	{
		UE_LOG(LogScreensPlugin, Log, TEXT("UTexture2DToMLImage width = %d height = %d size = %d"), Mip.SizeX, Mip.SizeY, size);
		Target.width = Mip.SizeX;
		Target.height = Mip.SizeY;
		Target.image_type = MLImageType_RGBA32;
		Target.alignment = 1;
		Target.data = PixelDataMemPool.GetData();
		FMemory::Memcpy(Target.data, PixelData, size);

		if (Source.GetPixelFormat() == EPixelFormat::PF_B8G8R8A8)
		{
			check(((size % 4) == 0) && (size != 0));

			for (int32 i = 0; i < size - 4; i += 4)
			{
				Swap<uint8>(Target.data[i], Target.data[i + 2]);
			}
		}

		bSuccess = true;
	}
	else
	{
		UE_CLOG(size > MAX_TEXTURE_SIZE, LogScreensPlugin, Error, TEXT("Texture size (%d) exceeds max texture size (%d)"), size, MAX_TEXTURE_SIZE);
		UE_CLOG(size == 0, LogScreensPlugin, Error, TEXT("Texture size is zero"));
	}

	Mip.BulkData.Unlock();

	return bSuccess;
}

bool FMagicLeapScreensPlugin::ShouldUseDefaultThumbnail(const FMagicLeapScreensWatchHistoryEntry& Entry,  MLImage& MLImage)
{
	return ((Entry.Thumbnail == nullptr) ||
		(IsSupportedFormat(Entry.Thumbnail->GetPixelFormat()) == false) ||
		(UTexture2DToMLImage(*Entry.Thumbnail, MLImage) == false)) ? true : false;
}
#endif // WITH_MLSDK

bool FMagicLeapScreensPlugin::RemoveWatchHistoryEntry(const FGuid& ID)
{
#if WITH_MLSDK
	MLResult Result = MLResult_UnspecifiedFailure;
	{
		// TODO: Remove locks once all code is made async
		FScopeLock Lock(&CriticalSection);

		const int64 EntryID = MagicLeap::FGuidToMLHandle(ID);
		Result = MLScreensRemoveWatchHistoryEntry(EntryID);
		UE_CLOG(Result != MLResult_Ok, LogScreensPlugin, Error, TEXT("MLScreensRemoveWatchHistoryEntry failed with error %s for entry with id %d!"), UTF8_TO_TCHAR(MLGetResultString(Result)), EntryID);
	}
	return Result == MLResult_Ok;
#else
	return false;
#endif // WITH_MLSDK
}

void FMagicLeapScreensPlugin::GetWatchHistoryEntriesAsync(const TOptional<FMagicLeapScreensHistoryRequestResultDelegate>& OptionalResultDelegate)
{
	FScreensTask Task;
	Task.TaskRequestType = FScreensTask::ETaskRequestType::Request;
	Task.TaskType = FScreensTask::ETaskType::GetHistory;
	if (OptionalResultDelegate.IsSet())
	{
		Task.HistoryRequestDelegate = OptionalResultDelegate.GetValue();
	}
	Runnable->PushNewTask(Task);
}

void FMagicLeapScreensPlugin::AddToWatchHistoryAsync(const FMagicLeapScreensWatchHistoryEntry& NewEntry, const TOptional<FMagicLeapScreensEntryRequestResultDelegate>& OptionalResultDelegate)
{
	FScreensTask Task;
	Task.TaskRequestType = FScreensTask::ETaskRequestType::Request;
	Task.TaskType = FScreensTask::ETaskType::AddToHistory;
	if (OptionalResultDelegate.IsSet())
	{
		Task.EntryRequestDelegate = OptionalResultDelegate.GetValue();
	}
	Task.WatchHistory.Add(NewEntry);
	Runnable->PushNewTask(Task);
}

void FMagicLeapScreensPlugin::UpdateWatchHistoryEntryAsync(const FMagicLeapScreensWatchHistoryEntry& UpdateEntry, const TOptional<FMagicLeapScreensEntryRequestResultDelegate>& OptionalResultDelegate)
{
	FScreensTask Task;
	Task.TaskRequestType = FScreensTask::ETaskRequestType::Request;
	Task.TaskType = FScreensTask::ETaskType::UpdateHistoryEntry;
	if (OptionalResultDelegate.IsSet())
	{
		Task.EntryRequestDelegate = OptionalResultDelegate.GetValue();
	}
	Task.WatchHistory.Add(UpdateEntry);
	Runnable->PushNewTask(Task);
}

void FMagicLeapScreensPlugin::UpdateScreenTransformAsync(const FMagicLeapScreenTransform& UpdateTransform, const FMagicLeapScreenTransformRequestResultDelegate& ResultDelegate)
{
	FScreensTask Task;
	Task.TaskRequestType = FScreensTask::ETaskRequestType::Request;
	Task.TaskType = FScreensTask::ETaskType::UpdateInfoEntry;
	Task.TransformRequestDelegate = ResultDelegate;
	Task.ScreenTransform = UpdateTransform;
	Runnable->PushNewTask(Task);
}

FScreensTask FMagicLeapScreensPlugin::GetWatchHistoryEntries()
{
	FScreensTask Task;
	Task.TaskRequestType = FScreensTask::ETaskRequestType::Response;
	Task.TaskType = FScreensTask::ETaskType::GetHistory;
	#if WITH_MLSDK
	{
		FScopeLock Lock(&CriticalSection);
		MLScreensWatchHistoryList WatchHistoryList;
		MLResult Result = MLScreensGetWatchHistoryList(&WatchHistoryList);
		if (Result == MLResult_Ok)
		{
			Task.WatchHistory.Reserve(WatchHistoryList.count);
			for (uint32 i = 0; i < WatchHistoryList.count; ++i)
			{
				FMagicLeapScreensWatchHistoryEntry WatchHistoryEntry;
				MLWatchHistoryEntryToUnreal(WatchHistoryList.entries[i], WatchHistoryEntry);
				Task.WatchHistory.Add(WatchHistoryEntry);
			}
			Result = MLScreensReleaseWatchHistoryList(&WatchHistoryList);
			UE_CLOG(Result != MLResult_Ok, LogScreensPlugin, Error, TEXT("MLScreensReleaseWatchHistoryList failed with error %s!"), UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
			Task.bSuccess = true;
		}
		else
		{
			Task.bSuccess = false;
			UE_LOG(LogScreensPlugin, Error, TEXT("MLScreensGetWatchHistoryList failed with error %s!"), UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
		}
	}
	#endif // WITH_MLSDK
	return Task;
}

bool FMagicLeapScreensPlugin::ClearWatchHistory()
{
#if WITH_MLSDK
	{
		FScopeLock Lock(&CriticalSection);
		MLScreensWatchHistoryList WatchHistoryList;
		MLResult Result = MLScreensGetWatchHistoryList(&WatchHistoryList);
		if (Result == MLResult_Ok)
		{
			for (uint32 i = 0; i < WatchHistoryList.count; ++i)
			{
				Result = MLScreensRemoveWatchHistoryEntry(WatchHistoryList.entries[i].id);
				if (Result != MLResult_Ok)
				{
					UE_LOG(LogScreensPlugin, Error, TEXT("MLScreensRemoveWatchHistoryEntry failed with error %s for entry %d!"), UTF8_TO_TCHAR(MLScreensGetResultString(Result)), WatchHistoryList.entries[i].id);
					return false;
				}
			}

			Result = MLScreensReleaseWatchHistoryList(&WatchHistoryList);
			if (Result != MLResult_Ok)
			{
				UE_LOG(LogScreensPlugin, Error, TEXT("MLScreensReleaseWatchHistoryList failed with error %s!"), UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
				return false;
			}
		}
		else
		{
			UE_LOG(LogScreensPlugin, Error, TEXT("MLScreensGetWatchHistoryList failed with error %s!"), UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
			return false;
		}
	}
#endif // WITH_MLSDK
	return false;
}

FScreensTask FMagicLeapScreensPlugin::AddToWatchHistory(const FMagicLeapScreensWatchHistoryEntry& WatchHistoryEntry)
{
	FScreensTask Task;
	Task.TaskRequestType = FScreensTask::ETaskRequestType::Response;
	Task.TaskType = FScreensTask::ETaskType::AddToHistory;

#if WITH_MLSDK
	{
		FScopeLock Lock(&CriticalSection);
		MLScreensWatchHistoryEntry Entry;
		Entry.title = TCHAR_TO_UTF8(*WatchHistoryEntry.Title);
		Entry.subtitle = TCHAR_TO_UTF8(*WatchHistoryEntry.Subtitle);
		Entry.playback_position_ms = static_cast<uint32>(WatchHistoryEntry.PlaybackPosition.GetTotalMilliseconds());
		Entry.playback_duration_ms = static_cast<uint32>(WatchHistoryEntry.PlaybackDuration.GetTotalMilliseconds());
		Entry.custom_data = TCHAR_TO_UTF8(*WatchHistoryEntry.CustomData);
		MLImage MLThumbnail;

		if (ShouldUseDefaultThumbnail(WatchHistoryEntry, MLThumbnail))
		{
			MLThumbnail = DefaultThumbnail;
		}

		MLResult Result = MLScreensInsertWatchHistoryEntry(&Entry, &MLThumbnail);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogScreensPlugin, Error, TEXT("MLScreensInsertWatchHistoryEntry failed with error %s!"), UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
			Task.WatchHistory.Add(WatchHistoryEntry);
			Task.bSuccess = false;
		}
		else
		{
			FMagicLeapScreensWatchHistoryEntry NewEntry;
			MLWatchHistoryEntryToUnreal(Entry, NewEntry);
			Task.WatchHistory.Add(NewEntry);
			Task.bSuccess = true;
		}
	}
#endif // WITH_MLSDK
	return Task;
}

FScreensTask FMagicLeapScreensPlugin::UpdateWatchHistoryEntry(const FMagicLeapScreensWatchHistoryEntry& WatchHistoryEntry)
{
	FScreensTask Task;
	Task.TaskRequestType = FScreensTask::ETaskRequestType::Response;
	Task.TaskType = FScreensTask::ETaskType::UpdateHistoryEntry;

#if WITH_MLSDK
	{
		FScopeLock Lock(&CriticalSection);
		MLScreensWatchHistoryEntry Entry;
		Entry.id = MagicLeap::FGuidToMLHandle(WatchHistoryEntry.ID);
		Entry.title = TCHAR_TO_UTF8(*WatchHistoryEntry.Title);
		Entry.subtitle = TCHAR_TO_UTF8(*WatchHistoryEntry.Subtitle);
		Entry.playback_position_ms = static_cast<uint32>(WatchHistoryEntry.PlaybackPosition.GetTotalMilliseconds());
		Entry.playback_duration_ms = static_cast<uint32>(WatchHistoryEntry.PlaybackDuration.GetTotalMilliseconds());
		Entry.custom_data = TCHAR_TO_UTF8(*WatchHistoryEntry.CustomData);
		MLImage MLThumbnail;

		if (ShouldUseDefaultThumbnail(WatchHistoryEntry, MLThumbnail))
		{
			MLThumbnail = DefaultThumbnail;
		}

		MLResult Result = MLScreensUpdateWatchHistoryEntry(&Entry, &MLThumbnail);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogScreensPlugin, Error, TEXT("MLScreensUpdateWatchHistoryEntry failed with error %s!"), UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
			Task.WatchHistory.Add(WatchHistoryEntry);
			Task.bSuccess = false;
		}
		else
		{
			FMagicLeapScreensWatchHistoryEntry UpdatedEntry;
			MLWatchHistoryEntryToUnreal(Entry, UpdatedEntry);
			Task.WatchHistory.Add(UpdatedEntry);
			Task.bSuccess = true;
		}
	}
#endif // WITH_MLSDK
	return Task;
}

bool FMagicLeapScreensPlugin::Tick(float DeltaTime)
{
	if (!Runnable->CompletedTaskQueueIsEmpty())
	{
		FScreensTask CompletedTask;
		Runnable->PeekCompletedTasks(CompletedTask);
		if (!Runnable->TryGetCompletedTask(CompletedTask))
		{
			return true;
		}

		if (CompletedTask.TaskRequestType == FScreensTask::ETaskRequestType::Request)
		{
			UE_LOG(LogScreensPlugin, Error, TEXT("Unexpected request received from worker thread!"));
		}
		else if (CompletedTask.TaskRequestType == FScreensTask::ETaskRequestType::Response)
		{
			switch (CompletedTask.TaskType)
			{
			case FScreensTask::ETaskType::None: break;
			case FScreensTask::ETaskType::GetHistory:
			{
				CompletedTask.HistoryRequestDelegate.ExecuteIfBound(CompletedTask.bSuccess, CompletedTask.WatchHistory);
			}
			break;
			case FScreensTask::ETaskType::AddToHistory:
			{
				if (CompletedTask.WatchHistory.Num() > 0)
				{
					CompletedTask.EntryRequestDelegate.ExecuteIfBound(CompletedTask.bSuccess, CompletedTask.WatchHistory[0]);
				}
				else
				{
					UE_LOG(LogScreensPlugin, Error, TEXT("Unexpected empty watch history in an AddToHistory response from the worker thread!"));
				}
			}
			break;
			case FScreensTask::ETaskType::UpdateHistoryEntry:
			{
				if (CompletedTask.WatchHistory.Num() > 0)
				{
					CompletedTask.EntryRequestDelegate.ExecuteIfBound(CompletedTask.bSuccess, CompletedTask.WatchHistory[0]);
				}
				else
				{
					UE_LOG(LogScreensPlugin, Error, TEXT("Unexpected empty watch history in an UpdateHistoryEntry response from the worker thread!"));
				}
			}
			break;
			case FScreensTask::ETaskType::UpdateInfoEntry:
			{
				CompletedTask.TransformRequestDelegate.ExecuteIfBound(CompletedTask.bSuccess);
			}
			break;
			}
		}
	}
	return true;
}

FScreensTask FMagicLeapScreensPlugin::UpdateScreensTransform(const FMagicLeapScreenTransform& InScreenTransform)
{
	FScreensTask Task;
	Task.TaskRequestType = FScreensTask::ETaskRequestType::Response;
	Task.TaskType = FScreensTask::ETaskType::UpdateInfoEntry;
#if WITH_MLSDK
	const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::GetModuleChecked();
	if (!MLPlugin.IsMagicLeapHMDValid())
	{
		return Task;
	}

	float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();
	const FTransform WorldToTracking = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr).Inverse();

	MLScreensScreenInfoEx ScreensInfo;
	ScreensInfo.screen_id = MagicLeap::FGuidToMLHandle(InScreenTransform.ID);
	ScreensInfo.version = InScreenTransform.VersionNumber;
	ScreensInfo.dimensions = MagicLeap::ToMLVectorExtents(InScreenTransform.ScreenDimensions, WorldToMetersScale);
	ScreensInfo.transform = MagicLeap::ToMLMat4f(WorldToTracking.TransformPosition(InScreenTransform.ScreenPosition), WorldToTracking.TransformRotation(InScreenTransform.ScreenOrientation.Quaternion()), InScreenTransform.ScreenScale3D, WorldToMetersScale);

	MLResult Result = MLScreensUpdateScreenInfo(&ScreensInfo);
	UE_CLOG(Result != MLResult_Ok, LogScreensPlugin, Error, TEXT("MLScreensUpdateScreenInfo failed for entry with ID %d with error %s"), ScreensInfo.screen_id, UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
	Task.ScreenTransform = InScreenTransform;
	Task.bSuccess = (Result == MLResult_Ok);
#endif // WITH_MLSDK
	return Task;
}

bool FMagicLeapScreensPlugin::GetScreenTransform(FMagicLeapScreenTransform& ScreenTransform)
{
	bool bSuccess = false;
#if WITH_MLSDK
	const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::GetModuleChecked();
	if (!MLPlugin.IsMagicLeapHMDValid())
	{
		return false;
	}

	float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();

	// ID used to identify a ScreenInfoEx, must be passed via command line
	FString IDParam;
	if (!FParse::Value(FCommandLine::Get(), TEXT("screenId="), IDParam))
	{
		UE_LOG(LogScreensPlugin, Error, TEXT("A valid Screen ID was not passed via command line!"));
		return false;
	}

	uint64 ScreenID = FCString::Strtoui64(*IDParam, nullptr, 10);
	ensure(ScreenID != 0);

	MLScreensScreenInfoEx OutScreenInfo;
	MLScreensScreenInfoExInit(&OutScreenInfo);
	MLResult Result = MLScreensGetScreenInfo(ScreenID, &OutScreenInfo);
	OutScreenInfo.screen_id = ScreenID;
	if (Result == MLResult_Ok)
	{
		const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
		if (!MLScreenInfoToUnreal(OutScreenInfo, ScreenTransform, TrackingToWorld, WorldToMetersScale))
		{
			return false;
		}

		bSuccess = true;
	}
	else
	{
		UE_LOG(LogScreensPlugin, Error, TEXT("MLScreensGetScreenInfo failed for screen ID %s with error %s"), *IDParam, UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
	}

#endif // WITH_MLSDK
	return bSuccess;
}

bool FMagicLeapScreensPlugin::GetScreensTransforms(TArray<FMagicLeapScreenTransform>& ScreensTransforms)
{
#if WITH_MLSDK
	ScreensTransforms.Empty();

	const IMagicLeapPlugin& MLPlugin = IMagicLeapPlugin::GetModuleChecked();
	if (!MLPlugin.IsMagicLeapHMDValid())
	{
		return false;
	}

	float WorldToMetersScale = MLPlugin.GetWorldToMetersScale();

	MLScreensScreenInfoListEx ScreensInfoList;
	MLScreensScreenInfoListExInit(&ScreensInfoList);
	MLResult Result = MLScreensGetScreenInfoListEx(&ScreensInfoList);
	if (Result == MLResult_Ok)
	{
		const FTransform TrackingToWorld = UHeadMountedDisplayFunctionLibrary::GetTrackingToWorldTransform(nullptr);
		for (uint32 i = 0; i < ScreensInfoList.count; ++i)
		{
			MLScreensScreenInfoEx& Entry = ScreensInfoList.entries[i];
			FMagicLeapScreenTransform ScreenTransform;
			if (!MLScreenInfoToUnreal(Entry, ScreenTransform, TrackingToWorld, WorldToMetersScale))
			{
				continue;
			}

			ScreensTransforms.Add(ScreenTransform);
		}
		Result = MLScreensReleaseScreenInfoListEx(&ScreensInfoList);
		if (Result != MLResult_Ok)
		{
			UE_LOG(LogScreensPlugin, Error, TEXT("MLScreensReleaseScreenInfoListEx failed with error %s!"), UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
			return false;
		}
	}
	else
	{
		UE_LOG(LogScreensPlugin, Error, TEXT("MLScreensGetScreenInfoListEx failed with error %s!"), UTF8_TO_TCHAR(MLScreensGetResultString(Result)));
		return false;
	}
#endif // WITH_MLSDK
	return true;
}

FScreensRunnable* FMagicLeapScreensPlugin::GetRunnable() const
{
	return Runnable;
}

IMPLEMENT_MODULE(FMagicLeapScreensPlugin, MagicLeapScreens);
