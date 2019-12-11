// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#include "BackgroundHttpManagerImpl.h"
#include "PlatformBackgroundHttp.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformAtomics.h"
#include "HAL/PlatformFile.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/ScopeRWLock.h"

DEFINE_LOG_CATEGORY(LogBackgroundHttpManager);

FBackgroundHttpManagerImpl::FBackgroundHttpManagerImpl()
	: PendingStartRequests()
	, PendingRequestLock()
	, ActiveRequests()
	, ActiveRequestLock()
	, NumCurrentlyActiveRequests(0)
	, MaxActiveDownloads(4)
{
}

FBackgroundHttpManagerImpl::~FBackgroundHttpManagerImpl()
{
}

void FBackgroundHttpManagerImpl::Initialize()
{
	ClearAnyTempFilesFromTimeOut();

	// Can't read into an atomic int directly
	int MaxActiveDownloadsConfig = 4;
	ensureAlwaysMsgf(GConfig->GetInt(TEXT("BackgroundHttp"), TEXT("BackgroundHttp.MaxActiveDownloads"), MaxActiveDownloadsConfig, GEngineIni), TEXT("No value found for MaxActiveDownloads! Defaulting to 4!"));
	MaxActiveDownloads = MaxActiveDownloadsConfig;
}

void FBackgroundHttpManagerImpl::Shutdown()
{
	//Pending Requests Clear
	{
		FRWScopeLock ScopeLock(PendingRequestLock, SLT_Write);
		PendingStartRequests.Empty();
	}

	//Active Requests Clear
	{
		FRWScopeLock ScopeLock(ActiveRequestLock, SLT_Write);
		ActiveRequests.Empty();
		NumCurrentlyActiveRequests = 0;
	}
}

void FBackgroundHttpManagerImpl::ClearAnyTempFilesFromTimeOut()
{
	const FString DirectoryToCheck = FPlatformBackgroundHttp::GetTemporaryRootPath();

	//Find all files in our temp folder
	TArray<FString> FilesToCheck;
	IFileManager::Get().FindFiles(FilesToCheck, *DirectoryToCheck, nullptr);

	double FileAgeTimeOutSettings = -1;
	GConfig->GetDouble(TEXT("BackgroundHttp"), TEXT("BackgroundHttp.TempFileTimeOutSeconds"), FileAgeTimeOutSettings, GEngineIni);

	UE_LOG(LogBackgroundHttpManager, Log, TEXT("Checking for BackgroundHTTP temp files that should be deleted due to time out. NumTempFilesFound:%d | TempFileTimeOutSeconds:%lf"), FilesToCheck.Num(), FileAgeTimeOutSettings);

	if (FileAgeTimeOutSettings >= 0)
	{
		for (const FString& File : FilesToCheck)
		{
			const FString FullFilePath = FPaths::Combine(DirectoryToCheck, File);
			
			FFileStatData FileData = IFileManager::Get().GetStatData(*FullFilePath);
			FTimespan TimeSinceCreate = FDateTime::UtcNow() - FileData.CreationTime;

			const double FileAge = TimeSinceCreate.GetTotalSeconds();
			const bool bShouldDelete = (FileAge > FileAgeTimeOutSettings);

			UE_LOG(LogBackgroundHttpManager, Log, TEXT("FoundTempFile: %s with age %lf -- bShouldDelete:%d"), *FullFilePath, FileAge, (int)bShouldDelete);

			if (bShouldDelete)
			{
				if (IFileManager::Get().Delete(*FullFilePath))
				{
					UE_LOG(LogBackgroundHttpManager, Log, TEXT("Successfully deleted %s due to time out settings"), *FullFilePath);
				}
				else
				{
					UE_LOG(LogBackgroundHttpManager, Error, TEXT("File %s failed to delete, but should have as as it is %lld seconds old!"), *FullFilePath, FileAge);
				}
			}
			else
			{
				UE_LOG(LogBackgroundHttpManager, Log, TEXT("SKipping Delete of %s as it is more recent then the time out settings."), *FullFilePath);
			}
		}
	}
}

void FBackgroundHttpManagerImpl::CleanUpTemporaryFiles()
{
	UE_LOG(LogBackgroundHttpManager, Log, TEXT("Cleaning Up Temporary Files"));

	TArray<FString> FilesToDelete;

	//Default implementation is to just delete everything in the Root Folder non-recursively.
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.FindFiles(FilesToDelete, *FPlatformBackgroundHttp::GetTemporaryRootPath(), nullptr);

	for (const FString& File : FilesToDelete)
	{
		UE_LOG(LogBackgroundHttpManager, Log, TEXT("Deleting File:%s"), *File);
		const bool bDidDelete = PlatformFile.DeleteFile(*File);

		if (!bDidDelete)
		{
			UE_LOG(LogBackgroundHttpManager, Warning, TEXT("Failure to Delete Temp File:%s"), *File);
		}
	}
}

int FBackgroundHttpManagerImpl::GetMaxActiveDownloads() const
{
	return MaxActiveDownloads;
}

void FBackgroundHttpManagerImpl::SetMaxActiveDownloads(int InMaxActiveDownloads)
{
	MaxActiveDownloads = InMaxActiveDownloads;
}

void FBackgroundHttpManagerImpl::AddRequest(const FBackgroundHttpRequestPtr Request)
{
	UE_LOG(LogBackgroundHttpManager, Log, TEXT("AddRequest Called - RequestID:%s"), *Request->GetRequestID());

	//If we don't associate with any existing requests, go into our pending list. These will be moved into the ActiveRequest list during our Tick
	if (!AssociateWithAnyExistingRequest(Request))
	{
		FRWScopeLock ScopeLock(PendingRequestLock, SLT_Write);
		PendingStartRequests.Add(Request);

		UE_LOG(LogBackgroundHttpManager, Log, TEXT("Adding BackgroundHttpRequest to PendingStartRequests - RequestID:%s"), *Request->GetRequestID());
	}
}

void FBackgroundHttpManagerImpl::RemoveRequest(const FBackgroundHttpRequestPtr Request)
{
	int NumRequestsRemoved = 0;

	//Check if this request was in active list first
	{
		FRWScopeLock ScopeLock(ActiveRequestLock, SLT_Write);
		NumRequestsRemoved = ActiveRequests.Remove(Request);

		//If we removed an active request, lets decrement the NumCurrentlyActiveRequests accordingly
		if (NumRequestsRemoved != 0)
		{
			NumCurrentlyActiveRequests = NumCurrentlyActiveRequests - NumRequestsRemoved;
		}
	}

	//Only search the PendingRequestList if we didn't remove it in our ActiveRequest List
	if (NumRequestsRemoved == 0)
	{
		FRWScopeLock ScopeLock(PendingRequestLock, SLT_Write);
		NumRequestsRemoved = PendingStartRequests.Remove(Request);
	}

	UE_LOG(LogBackgroundHttpManager, Log, TEXT("FGenericPlatformBackgroundHttpManager::RemoveRequest Called - RequestID:%s | NumRequestsActuallyRemoved:%d | NumCurrentlyActiveRequests:%d"), *Request->GetRequestID(), NumRequestsRemoved, NumCurrentlyActiveRequests);
}

bool FBackgroundHttpManagerImpl::AssociateWithAnyExistingRequest(const FBackgroundHttpRequestPtr Request)
{
	bool bDidAssociateWithExistingRequest = false;

	FString ExistingFilePath;
	int64 ExistingFileSize;
	if (CheckForExistingCompletedDownload(Request, ExistingFilePath, ExistingFileSize))
	{
		FBackgroundHttpResponsePtr NewResponseWithExistingFile = FPlatformBackgroundHttp::ConstructBackgroundResponse(EHttpResponseCodes::Ok, ExistingFilePath);
		if (ensureAlwaysMsgf(NewResponseWithExistingFile.IsValid(), TEXT("Failure to create FBackgroundHttpResponsePtr in FPlatformBackgroundHttp::ConstructBackgroundResponse! Can not associate new download with found finished download!")))
		{
			bDidAssociateWithExistingRequest = true;
			UE_LOG(LogBackgroundHttpManager, Log, TEXT("Found existing background task to associate with! RequestID:%s | ExistingFileSize:%lld | ExistingFilePath:%s"), *Request->GetRequestID(), ExistingFileSize, *ExistingFilePath);

			//First send progress update for the file size so anything monitoring this download knows we are about to update this progress
			Request->OnProgressUpdated().ExecuteIfBound(Request, ExistingFileSize, ExistingFileSize);

			//Now complete with this completed response data
			Request->CompleteWithExistingResponseData(NewResponseWithExistingFile);
		}
	}

	return bDidAssociateWithExistingRequest;
}

bool FBackgroundHttpManagerImpl::CheckForExistingCompletedDownload(const FBackgroundHttpRequestPtr Request, FString& ExistingFilePathOut, int64& ExistingFileSizeOut)
{
	bool bDidFindExistingDownload = false;

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	const TArray<FString>& URLList = Request->GetURLList();
	for (const FString& URL : URLList)
	{
		const FString& FileDestination = FPlatformBackgroundHttp::GetTemporaryFilePathFromURL(URL);
		if (PlatformFile.FileExists(*FileDestination))
		{
			bDidFindExistingDownload = true;
			ExistingFilePathOut = FileDestination;

			ExistingFileSizeOut = PlatformFile.FileSize(*FileDestination);
			break;
		}
	}	

	return bDidFindExistingDownload;
}

bool FBackgroundHttpManagerImpl::Tick(float DeltaTime)
{
	ActivatePendingRequests();

	//Keep ticking in all cases, so just return true
	return true;
}

void FBackgroundHttpManagerImpl::ActivatePendingRequests()
{
	FBackgroundHttpRequestPtr HighestPriorityRequestToStart = nullptr;
	EBackgroundHTTPPriority HighestRequestPriority = EBackgroundHTTPPriority::Num;

	//Go through and find the highest priority request
	{
		FRWScopeLock ActiveScopeLock(ActiveRequestLock, SLT_ReadOnly);
		FRWScopeLock PendingScopeLock(PendingRequestLock, SLT_ReadOnly);
		const int NumRequestsWeCanProcess = (MaxActiveDownloads - NumCurrentlyActiveRequests);
		if (NumRequestsWeCanProcess > 0)
		{
			if (PendingStartRequests.Num() > 0)
			{
				UE_LOG(LogBackgroundHttpManager, Verbose, TEXT("Populating Requests to Start from PendingStartRequests - MaxActiveDownloads:%d | NumCurrentlyActiveRequests:%d | NumPendingStartRequests:%d"), MaxActiveDownloads.Load(), NumCurrentlyActiveRequests, PendingStartRequests.Num());

				HighestPriorityRequestToStart = PendingStartRequests[0];
				HighestRequestPriority = HighestPriorityRequestToStart.IsValid() ? HighestPriorityRequestToStart->GetRequestPriority() : EBackgroundHTTPPriority::Num;

				//See how many more requests we can process and only do anything if we can still process more
				{
					for (int RequestIndex = 1; RequestIndex < PendingStartRequests.Num(); ++RequestIndex)
					{
						FBackgroundHttpRequestPtr PendingRequestToCheck = PendingStartRequests[RequestIndex];
						EBackgroundHTTPPriority PendingRequestPriority = PendingRequestToCheck.IsValid() ? PendingRequestToCheck->GetRequestPriority() : EBackgroundHTTPPriority::Num;

						//Found a higher priority request, so track that one
						if (PendingRequestPriority < HighestRequestPriority)
						{
							HighestPriorityRequestToStart = PendingRequestToCheck;
							HighestRequestPriority = PendingRequestPriority;
						}
					}
				}
			}
		}
	}

	if (HighestPriorityRequestToStart.IsValid())
	{
		UE_LOG(LogBackgroundHttpManager, Verbose, TEXT("Starting Request: %s Priority:%s"), *HighestPriorityRequestToStart->GetRequestID(), LexToString(HighestRequestPriority));

		//Actually move request to Active list now
		FRWScopeLock ActiveScopeLock(ActiveRequestLock, SLT_Write);
		FRWScopeLock PendingScopeLock(PendingRequestLock, SLT_Write);
		ActiveRequests.Add(HighestPriorityRequestToStart);
		PendingStartRequests.RemoveSingle(HighestPriorityRequestToStart);

		++NumCurrentlyActiveRequests;

		//Call Handle for that task to now kick itself off
		HighestPriorityRequestToStart->HandleDelayedProcess();
	}
}
