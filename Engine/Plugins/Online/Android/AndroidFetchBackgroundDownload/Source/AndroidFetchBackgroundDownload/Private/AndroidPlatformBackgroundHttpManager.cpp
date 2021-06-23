// Copyright Epic Games, Inc. All Rights Reserved.

#include "AndroidPlatformBackgroundHttpManager.h"

#include "Misc/ScopeRWLock.h"

#include "UEWorkManagerNativeWrapper.h"
#include "PlatformBackgroundHttp.h"
#include "AndroidPlatformBackgroundHttpRequest.h"

#include "Interfaces/IHttpResponse.h"

#include "Android/AndroidPlatform.h"
#include "Android/AndroidJava.h"
#include "Android/AndroidJavaEnv.h"
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"

#include "Misc/Paths.h"

#if UE_BUILD_SHIPPING
// always clear any exceptions in shipping
#define CHECK_JNI_RESULT(Id) if (Id == 0) { Env->ExceptionClear(); }
#else
#define CHECK_JNI_RESULT(Id)								\
	if (Id == 0)											\
	{														\
		if (bIsOptional)									\
		{													\
			Env->ExceptionClear();							\
		}													\
		else												\
		{													\
			Env->ExceptionDescribe();						\
			checkf(Id != 0, TEXT("Failed to find " #Id));	\
		}													\
	}
#endif // UE_BUILD_SHIPPING

#define LOCTEXT_NAMESPACE "AndroidBackgroundHttpManager"

volatile int32 FAndroidBackgroundDownloadDelegates::bHasManagerInitialized = false;
FAndroidBackgroundDownloadDelegates::FAndroidBackgroundDownload_OnProgress FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnProgress;
FAndroidBackgroundDownloadDelegates::FAndroidBackgroundDownload_OnComplete FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnComplete;
FAndroidBackgroundDownloadDelegates::FAndroidBackgroundDownload_OnAllComplete FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnAllComplete;
FAndroidBackgroundDownloadDelegates::FAndroidBackgroundDownload_OnTickWorkerThread FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnTickWorkerThread;

const FString FAndroidNativeDownloadWorkerParameterKeys::DOWNLOAD_DESCRIPTION_LIST_KEY = TEXT("DownloadDescriptionList");
const FString FAndroidNativeDownloadWorkerParameterKeys::DOWNLOAD_MAX_CONCURRENT_REQUESTS_KEY = TEXT("MaxConcurrentDownloadRequests");

const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CHANNEL_ID_KEY = TEXT("NotificationChannelId");
const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CHANNEL_NAME_KEY = TEXT("NotificationChannelName");
const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CHANNEL_IMPORTANCE_KEY = TEXT("NotificationChannelImportance");

const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CONTENT_TITLE_KEY = TEXT("NotificationContentTitle");
const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CONTENT_TEXT_KEY = TEXT("NotificationContentText");
const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CONTENT_CANCEL_DOWNLOAD_TEXT_KEY = TEXT("NotificationContentCancelDownloadText");
const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CONTENT_COMPLETE_TEXT_KEY = TEXT("NotificationContentCompleteText");

const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_RESOURCE_CANCEL_ICON_NAME = TEXT("NotificationResourceCancelIconName");
const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_RESOURCE_CANCEL_ICON_TYPE = TEXT("NotificationResourceCancelIconType");
const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_RESOURCE_CANCEL_ICON_PACKAGE = TEXT("NotificationResourceCancelIconPackage");

const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_RESOURCE_SMALL_ICON_NAME = TEXT("NotificationResourceSmallIconName");
const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_RESOURCE_SMALL_ICON_TYPE = TEXT("NotificationResourceSmallIconType");
const FString FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_RESOURCE_SMALL_ICON_PACKAGE = TEXT("NotificationResourceSmallIconPackage");

FAndroidPlatformBackgroundHttpManager::FAndroidPlatformBackgroundHttpManager()
	: bHasPendingCompletes(false)
	, bUnderlyingJavaAllDownloadsComplete(false)
{

}

void FAndroidPlatformBackgroundHttpManager::Initialize()
{
	FPlatformAtomics::InterlockedExchange(&FAndroidBackgroundDownloadDelegates::bHasManagerInitialized, true);
	
	Java_OnDownloadProgressHandle = FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnProgress.AddRaw(this, &FAndroidPlatformBackgroundHttpManager::Java_OnDownloadProgress);
	Java_OnDownloadCompleteHandle = FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnComplete.AddRaw(this, &FAndroidPlatformBackgroundHttpManager::Java_OnDownloadComplete);
	Java_OnAllDownloadsCompleteHandle = FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnAllComplete.AddRaw(this, &FAndroidPlatformBackgroundHttpManager::Java_OnAllDownloadsComplete);
	Java_OnTickHandle = FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnTickWorkerThread.AddRaw(this, &FAndroidPlatformBackgroundHttpManager::Java_OnTick);

	FBackgroundHttpManagerImpl::Initialize();
}

void FAndroidPlatformBackgroundHttpManager::Shutdown()
{
	FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnProgress.Remove(Java_OnDownloadProgressHandle);
	FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnComplete.Remove(Java_OnDownloadCompleteHandle);
	FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnAllComplete.Remove(Java_OnAllDownloadsCompleteHandle);
	FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnTickWorkerThread.Remove(Java_OnTickHandle);

	FBackgroundHttpManagerImpl::Shutdown();
}

bool FAndroidPlatformBackgroundHttpManager::Tick(float DeltaTime)
{
	FBackgroundHttpManagerImpl::Tick(DeltaTime);

	HandlePendingCompletes();
	VerifyIfAllDownloadsComplete();

	UpdateRequestProgress();

	return true;
}

void FAndroidPlatformBackgroundHttpManager::ActivatePendingRequests()
{
	TArray<FBackgroundHttpRequestPtr> NewlyActivatedRequests;

	//Queue downloads through a new UEDownloadWorker and WorkManager
	{
		FRWScopeLock ScopeLock(PendingRequestLock, SLT_ReadOnly);
	
		if (PendingStartRequests.Num() > 0)
		{
			bool bIsOptional = false;

			JNIEnv* Env = FAndroidApplication::GetJavaEnv();
			if (ensureAlwaysMsgf(Env, TEXT("Invalid JavaEnv! Can not activate any pending requests!")))
			{
				//Grab a bunch of necessary JNI methods we will need to create our DownloadDescriptions
				jmethodID CreateArrayStaticMethod = FJavaWrapper::FindStaticMethod(Env, FJavaWrapper::DownloadDescriptionClass, "BuildDescriptionArray", "()Ljava/util/ArrayList;", bIsOptional);
				CHECK_JNI_RESULT(CreateArrayStaticMethod);

				jmethodID WriteDownloadDescriptionListToFileMethod = FJavaWrapper::FindStaticMethod(Env, FJavaWrapper::DownloadDescriptionClass, "WriteDownloadDescriptionListToFile", "(Ljava/lang/String;Ljava/util/ArrayList;)Z", bIsOptional);
				CHECK_JNI_RESULT(WriteDownloadDescriptionListToFileMethod);

				jmethodID CreateDownloadDescriptionFromJsonMethod = FJavaWrapper::FindStaticMethod(Env, FJavaWrapper::DownloadDescriptionClass, "FromJSON", "(Ljava/lang/String;)Lcom/epicgames/ue4/download/datastructs/DownloadDescription;", bIsOptional);
				CHECK_JNI_RESULT(CreateDownloadDescriptionFromJsonMethod);

				FScopedJavaObject<jobject> DescriptionArray = NewScopedJavaObject(Env, Env->CallStaticObjectMethod(FJavaWrapper::DownloadDescriptionClass, CreateArrayStaticMethod));
				check(DescriptionArray);

				jclass ArrayClass = Env->GetObjectClass(*DescriptionArray);
				CHECK_JNI_RESULT(ArrayClass);

				jmethodID ArrayPutMethod = Env->GetMethodID(ArrayClass, "add", "(Ljava/lang/Object;)Z");
				CHECK_JNI_RESULT(ArrayPutMethod);

				//Go through each actual request and create the DownloadDescription, and add it to a ArrayList<DownloadDescription> in java
				for (FBackgroundHttpRequestPtr& Request : PendingStartRequests)
				{
					FAndroidBackgroundHttpRequestPtr AndroidRequest = StaticCastSharedPtr<FAndroidPlatformBackgroundHttpRequest>(Request);
					if (ensureAlwaysMsgf(AndroidRequest.IsValid(), TEXT("Unexpected illegal non-Android request in PendingStartRequests!")))
					{
						FScopedJavaObject<jstring> JavaJSONString = FJavaHelper::ToJavaString(Env, AndroidRequest->ToJSon());
						FScopedJavaObject<jobject> Description = NewScopedJavaObject(Env, Env->CallStaticObjectMethod(FJavaWrapper::DownloadDescriptionClass, CreateDownloadDescriptionFromJsonMethod, *JavaJSONString));

						bool bAddedRequestAsDescription = FJavaWrapper::CallBooleanMethod(Env, *DescriptionArray, ArrayPutMethod, *Description);
						if (ensureAlwaysMsgf(bAddedRequestAsDescription, TEXT("Failed to create and add valid DownloadDescription for request %s"), *AndroidRequest->GetRequestID()))
						{
							NewlyActivatedRequests.Add(AndroidRequest);
						}
					}
				}

				//Call JNI function that saves our passed in ArrayList<DownloadDescription> to a file
				const FString FileNameForDownloadDescList = GetFullFileNameForDownloadDescriptionList();
				FScopedJavaObject<jstring> JavaFileNameString = FJavaHelper::ToJavaString(Env, FileNameForDownloadDescList);	
				bool bDidWriteSucceed = Env->CallStaticBooleanMethod(FJavaWrapper::DownloadDescriptionClass, WriteDownloadDescriptionListToFileMethod, *JavaFileNameString, *DescriptionArray);
				
				//If we successfully created the JSON DownloadDescription file, 
				//then we can actually fill out the rest of our worker parameters and schedule the worker to begin downloading
				if (ensureAlwaysMsgf((bDidWriteSucceed), TEXT("Failed to create download description manifest for the WorkManager UEDownloadWorker. Can not schedule download work without the file as nothing will be downloaded!")))
				{
					FUEWorkManagerNativeWrapper::FWorkRequestParametersNative WorkParams;
					WorkParams.WorkerJavaClass = FJavaWrapper::UEDownloadWorkerClass;
					WorkParams.bRequireAnyInternet = true;
					WorkParams.bStartAsForegroundService = true;
					
					//Set our DownloadDescription file so that it can be parsed by the worker
					WorkParams.AddDataToWorkerParameters(FAndroidNativeDownloadWorkerParameterKeys::DOWNLOAD_DESCRIPTION_LIST_KEY, FileNameForDownloadDescList);
					
					//Set our MaxActiveDownloads in the underlying java layer to match our expectation
					WorkParams.AddDataToWorkerParameters(FAndroidNativeDownloadWorkerParameterKeys::DOWNLOAD_MAX_CONCURRENT_REQUESTS_KEY, MaxActiveDownloads);

					//Make sure we pass in localized notification text bits for the important worker keys
					WorkParams.AddDataToWorkerParameters(FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CONTENT_TITLE_KEY, LOCTEXT("AndroidBackgroundHttpManager.Notification.Title", "Downloading"));
					WorkParams.AddDataToWorkerParameters(FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CONTENT_TEXT_KEY, LOCTEXT("AndroidBackgroundHttpManager.Notification.ContentText", "Download in Progress"));
					WorkParams.AddDataToWorkerParameters(FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CONTENT_COMPLETE_TEXT_KEY, LOCTEXT("AndroidBackgroundHttpManager.Notification.CompletedContentText", "Download Complete"));
					WorkParams.AddDataToWorkerParameters(FAndroidNativeDownloadWorkerParameterKeys::NOTIFICATION_CONTENT_CANCEL_DOWNLOAD_TEXT_KEY, LOCTEXT("AndroidBackgroundHttpManager.Notification.CancelText", "Cancel"));

					FUEWorkManagerNativeWrapper::ScheduleBackgroundWork("BackgroundHttpDownload", WorkParams);
				}				
			}
		}
	}

	//Remove now queued requests and add them to active requests
	if (NewlyActivatedRequests.Num() > 0)
	{
		//First remove from PendingStartRequests
		{
			FRWScopeLock ScopeLock(PendingRequestLock, SLT_Write);
			for (FBackgroundHttpRequestPtr& Request : NewlyActivatedRequests) 
			{
                PendingStartRequests.Remove(Request);
            }
		}

		//Now add to ActiveRequests
		{
			FRWScopeLock ScopeLock(ActiveRequestLock, SLT_Write);
			for (FBackgroundHttpRequestPtr& Request : NewlyActivatedRequests)
			{
				ActiveRequests.Add(Request);
			}
		}

		//Fix up NumCurrentlyActiveRequests
		NumCurrentlyActiveRequests = ActiveRequests.Num();
	}
}

void FAndroidPlatformBackgroundHttpManager::HandlePendingCompletes()
{
	const bool bShouldProcessCompletes = FPlatformAtomics::InterlockedExchange(&bHasPendingCompletes, false);
	if (bShouldProcessCompletes)
	{
		TArray<FBackgroundHttpRequestPtr> RequestsToComplete;
		//Populate list of requests to complete
		{
			FRWScopeLock ScopeLock(ActiveRequestLock, SLT_ReadOnly);

			//Go through and look for bIsCompleted flags on requests
			for (FBackgroundHttpRequestPtr Request : ActiveRequests)
			{
				FAndroidBackgroundHttpRequestPtr AndroidRequest = StaticCastSharedPtr<FAndroidPlatformBackgroundHttpRequest>(Request);
				if (ensureAlwaysMsgf(AndroidRequest.IsValid(), TEXT("Invalid request in Active Requests!")))
				{
					if (FPlatformAtomics::AtomicRead(&(AndroidRequest->bIsCompleted)))
					{
						RequestsToComplete.Add(Request);
					}					
				}
			}	
		}

		//Actually complete requests and remove them from active requests
		if (RequestsToComplete.Num() > 0)
		{
			//FRWScopeLock ScopeLock(ActiveRequestLock, SLT_Write);
			
			for (FBackgroundHttpRequestPtr Request : RequestsToComplete)
			{
				FString ExistingFilePath;
				int64 ExistingFileSize;
				const bool bDoesCompleteFileExist = CheckForExistingCompletedDownload(Request, ExistingFilePath, ExistingFileSize);

				EHttpResponseCodes::Type ResponseCodeToUse = bDoesCompleteFileExist ? EHttpResponseCodes::Ok : EHttpResponseCodes::Unknown;
				FBackgroundHttpResponsePtr NewResponseWithExistingFile = FPlatformBackgroundHttp::ConstructBackgroundResponse(ResponseCodeToUse, ExistingFilePath);
				Request->CompleteWithExistingResponseData(NewResponseWithExistingFile);
			}
		}
	}
}

void FAndroidPlatformBackgroundHttpManager::VerifyIfAllDownloadsComplete()
{
	const bool bWasAllWorkComplete = FPlatformAtomics::InterlockedExchange(&bUnderlyingJavaAllDownloadsComplete, false);
	if (bWasAllWorkComplete)
	{
		UE_LOG(LogBackgroundHttpManager, Log, TEXT("Verifying all downloads are complete as underlying java layer is finished..."));

		////Go through and find all non-complete requests from our ActiveRequestList
		TArray<FBackgroundHttpRequestPtr> IncompleteDownloads;
		{
			FRWScopeLock ScopeLock(ActiveRequestLock, SLT_ReadOnly);
						
			for (FBackgroundHttpRequestPtr& Request : ActiveRequests)
			{
				FAndroidBackgroundHttpRequestPtr AndroidRequest = StaticCastSharedPtr<FAndroidPlatformBackgroundHttpRequest>(Request);
				if (ensureAlwaysMsgf(AndroidRequest.IsValid(), TEXT("Invalid request in Active Requests!")))
				{
					if (false == FPlatformAtomics::AtomicRead(&(AndroidRequest->bIsCompleted)))
					{
						IncompleteDownloads.Add(Request);
					}
				}
			}
		}

		if (IncompleteDownloads.Num() > 0)
		{
			UE_LOG(LogBackgroundHttpManager, Log, TEXT("Found %d ActiveRequests that are not complete. Migrating to Pending list to re-queue"), IncompleteDownloads.Num());

			//Move Incomplete Downloads to our PendingRequest list so they get re - queued
			{
				FRWScopeLock ScopeLock(PendingRequestLock, SLT_ReadOnly);
				for (FBackgroundHttpRequestPtr& Request : IncompleteDownloads)
				{
					PendingStartRequests.Add(Request);
				}
			}

			//Remove requests from active list
			{
				FRWScopeLock ScopeLock(PendingRequestLock, SLT_Write);

				for (FBackgroundHttpRequestPtr& Request : IncompleteDownloads)
				{
					ActiveRequests.Remove(Request);
				}

				//Fix up NumCurrentlyActiveRequests
				NumCurrentlyActiveRequests = ActiveRequests.Num();
			}
		}
		else
		{
			UE_LOG(LogBackgroundHttpManager, Log, TEXT("All ActiveRequests have been completed or pending complete when underlying java layer finished."));
		}
	}
}

void FAndroidPlatformBackgroundHttpManager::UpdateRequestProgress()
{
	FRWScopeLock ScopeLock(ActiveRequestLock, SLT_ReadOnly);
	for (FBackgroundHttpRequestPtr& Request : ActiveRequests)
	{
		FAndroidBackgroundHttpRequestPtr AndroidRequest = StaticCastSharedPtr<FAndroidPlatformBackgroundHttpRequest>(Request);
		if (ensureAlwaysMsgf(AndroidRequest.IsValid(), TEXT("Invalid request in Active Requests!")))
		{
			AndroidRequest->SendDownloadProgressUpdate();
		}
	}
}

FAndroidBackgroundHttpRequestPtr FAndroidPlatformBackgroundHttpManager::FindRequestByID(FString RequestID)
{
	FAndroidBackgroundHttpRequestPtr ReturnedPtr = nullptr;

	FRWScopeLock ScopeLock(ActiveRequestLock, SLT_ReadOnly);
	for (FBackgroundHttpRequestPtr& Request : ActiveRequests)
	{
		if (Request->GetRequestID().Equals(RequestID))
		{
			ReturnedPtr = StaticCastSharedPtr<FAndroidPlatformBackgroundHttpRequest>(Request);
			break;
		}
	}

	ensureAlwaysMsgf(ReturnedPtr.IsValid(), TEXT("No matching valid request found for %s"), *RequestID);

	return ReturnedPtr;
}

void FAndroidPlatformBackgroundHttpManager::PauseRequest(FBackgroundHttpRequestPtr Request)
{
	if (ensureAlwaysMsgf(IsInGameThread(), TEXT("Should only ever call PauseRequest from GameThread! Can not pause!")))
	{
		const FString RequestID = Request->GetRequestID();
		
		//Check if the Java list is safe to modify right now, and if so go ahead and add directly to it
		const bool bIsThreadsafeToHandle = !FPlatformAtomics::InterlockedExchange(&bIsModifyingPauseList, true);
		if (bIsThreadsafeToHandle)
		{
			RequestsToPauseByID_Java.Add(RequestID);

			const bool bSanityCheck = FPlatformAtomics::InterlockedExchange(&bIsModifyingPauseList, false);
			ensureAlwaysMsgf(bSanityCheck, TEXT("Error in bIsModifyingPauseList! Was false when we expected it to still be true from our lock!"));
		}
		//If its not safe cache off this pause so we can do it the next time it's safe to access the list
		else
		{
			RequestsToPauseByID_GT.Add(RequestID);
		}
	}
}

void FAndroidPlatformBackgroundHttpManager::ResumeRequest(FBackgroundHttpRequestPtr Request)
{
	if (ensureAlwaysMsgf(IsInGameThread(), TEXT("Should only ever call ResumeRequest from GameThread! Can not Resume!")))
	{
		const FString RequestID = Request->GetRequestID();

		//Check if the Java list is safe to modify right now, and if so go ahead and add directly to it
		const bool bIsThreadsafeToHandle = !FPlatformAtomics::InterlockedExchange(&bIsModifyingResumeList, true);
		if (bIsThreadsafeToHandle)
		{
			RequestsToResumeByID_Java.Add(RequestID);

			const bool bSanityCheck = FPlatformAtomics::InterlockedExchange(&bIsModifyingResumeList, false);
			ensureAlwaysMsgf(bSanityCheck, TEXT("Error in bIsModifyingResumeList! Was false when we expected it to still be true from our lock!"));
		}
		//If its not safe cache off this pause so we can do it the next time it's safe to access the list
		else
		{
			RequestsToResumeByID_GT.Add(RequestID);
		}
	}
}

void FAndroidPlatformBackgroundHttpManager::CancelRequest(FBackgroundHttpRequestPtr Request)
{
	if (ensureAlwaysMsgf(IsInGameThread(), TEXT("Should only ever call CancelRequest from GameThread! Can not Cancel!")))
	{
		const FString RequestID = Request->GetRequestID();

		//Check if the Java list is safe to modify right now, and if so go ahead and add directly to it
		const bool bIsThreadsafeToHandle = !FPlatformAtomics::InterlockedExchange(&bIsModifyingCancelList, true);
		if (bIsThreadsafeToHandle)
		{
			RequestsToCancelByID_Java.Add(RequestID);

			const bool bSanityCheck = FPlatformAtomics::InterlockedExchange(&bIsModifyingCancelList, false);
			ensureAlwaysMsgf(bSanityCheck, TEXT("Error in bIsModifyingCancelList! Was false when we expected it to still be true from our lock!"));
		}
		//If its not safe cache off this pause so we can do it the next time it's safe to access the list
		else
		{
			RequestsToCancelByID_GT.Add(RequestID);
		}
	}
}

void FAndroidPlatformBackgroundHttpManager::HandleRequestsWaitingOnJavaThreadsafety()
{
	//Check for pause requests
	if (RequestsToPauseByID_GT.Num() > 0)
	{
		const bool bIsThreadsafeToHandle = !FPlatformAtomics::InterlockedExchange(&bIsModifyingPauseList, true);
		if (bIsThreadsafeToHandle)
		{
			for (const FString& RequestID : RequestsToPauseByID_GT)
			{
				RequestsToPauseByID_Java.Add(RequestID);
			}

			RequestsToPauseByID_GT.Empty();

			const bool bSanityCheck = FPlatformAtomics::InterlockedExchange(&bIsModifyingPauseList, false);
			ensureAlwaysMsgf(bSanityCheck, TEXT("Error in bIsModifyingPauseList! Was false when we expected it to still be true from our lock!"));
		}
	}

	//Check for resume requests
	if (RequestsToResumeByID_GT.Num() > 0)
	{
		const bool bIsThreadsafeToHandle = !FPlatformAtomics::InterlockedExchange(&bIsModifyingResumeList, true);
		if (bIsThreadsafeToHandle)
		{
			for (const FString& RequestID : RequestsToResumeByID_GT)
			{
				RequestsToResumeByID_Java.Add(RequestID);
			}

			RequestsToResumeByID_GT.Empty();

			const bool bSanityCheck = FPlatformAtomics::InterlockedExchange(&bIsModifyingResumeList, false);
			ensureAlwaysMsgf(bSanityCheck, TEXT("Error in bIsModifyingResumeList! Was false when we expected it to still be true from our lock!"));
		}
	}

	//Check for cancel requests
	if (RequestsToCancelByID_GT.Num() > 0)
	{
		const bool bIsThreadsafeToHandle = !FPlatformAtomics::InterlockedExchange(&bIsModifyingCancelList, true);
		if (bIsThreadsafeToHandle)
		{
			for (const FString& RequestID : RequestsToCancelByID_GT)
			{
				RequestsToCancelByID_Java.Add(RequestID);
			}

			RequestsToCancelByID_GT.Empty();

			const bool bSanityCheck = FPlatformAtomics::InterlockedExchange(&bIsModifyingCancelList, false);
			ensureAlwaysMsgf(bSanityCheck, TEXT("Error in bIsModifyingCancelList! Was false when we expected it to still be true from our lock!"));
		}
	}
}

const FString FAndroidPlatformBackgroundHttpManager::GetFullFileNameForDownloadDescriptionList() const
{
	//We should never really hit this bundle, but worth inserting a cap on how many we check to sanity deletion behavior
	static const int MAX_NUM_DOWNLOAD_DESC_FILES = 10;
	
	//Find the next available filename
	int AppendedFileNameInt = 0;
	for (AppendedFileNameInt = 0; AppendedFileNameInt < MAX_NUM_DOWNLOAD_DESC_FILES; ++AppendedFileNameInt)
	{
		const FString FileToCheck = GetBaseFileNameForDownloadDescriptionListWithAppendedInt(AppendedFileNameInt);
		if (!FPaths::FileExists(FileToCheck))
		{
			break;
		}
	}

	if (!ensureAlwaysMsgf((AppendedFileNameInt < MAX_NUM_DOWNLOAD_DESC_FILES), TEXT("DownloadDescriptionList folder full of files! May lead to cases where we stomp expected .ini files for other workers!")))
	{
		static int StompNum = 0;
		AppendedFileNameInt = (StompNum % MAX_NUM_DOWNLOAD_DESC_FILES);
	}
	
	return GetBaseFileNameForDownloadDescriptionListWithAppendedInt(AppendedFileNameInt);;
}

const FString FAndroidPlatformBackgroundHttpManager::GetBaseFileNameForDownloadDescriptionListWithAppendedInt(int IntToAppend) const
{
	static const FString BASE_FILE_NAME = TEXT("DownloadDescriptionListJSON");
	static const FString FILE_EXTENSION = TEXT(".ini");

	static const FString RootBGTempDir = GetFileHashHelper()->GetTemporaryRootPath();
	static const FString BGDownloadDescriptionFolder = FPaths::Combine(RootBGTempDir, TEXT("DownloadDescriptionJSONs"));

	FString FileName = FString::Printf(TEXT("%s%d%s"), *BASE_FILE_NAME, IntToAppend, *FILE_EXTENSION);
	
	return FPaths::Combine(BGDownloadDescriptionFolder, FileName);
}

void FAndroidPlatformBackgroundHttpManager::Java_OnDownloadProgress(jobject UnderlyingWorker, FString RequestID, int64_t BytesWrittenSinceLastCall, int64_t TotalBytesWritten)
{
	UE_LOG(LogBackgroundHttpManager, Log, TEXT("Download Progress... RequestID:%s BytesWrittenSinceLastCall:%lld TotalBytesWritten:%lld"), *RequestID, BytesWrittenSinceLastCall, TotalBytesWritten);

	FAndroidBackgroundHttpRequestPtr FoundRequest = FindRequestByID(RequestID);
	if (FoundRequest.IsValid())
	{
		FoundRequest->UpdateDownloadProgress(TotalBytesWritten, BytesWrittenSinceLastCall);
	}
}

void FAndroidPlatformBackgroundHttpManager::Java_OnDownloadComplete(jobject UnderlyingWorker, FString RequestID, FString CompleteLocation, bool bWasSuccess)
{
	UE_LOG(LogBackgroundHttpManager, Log, TEXT("DownloadComplete... RequestID:%s bWasSuccess:%d"), *RequestID, (int)bWasSuccess);

	//Mark associated request as completed so that we can complete it on our next tick
	FAndroidBackgroundHttpRequestPtr CompletedRequest = FindRequestByID(RequestID);
	if (CompletedRequest.IsValid())
	{
		//Mark as complete so that on our next Tick we can process this request in particular as completed
		FPlatformAtomics::InterlockedExchange(&(CompletedRequest->bIsCompleted), true);
		
		//Mark as pending completes so we know to process completed requests at the manager level during our next tick
		FPlatformAtomics::InterlockedExchange(&bHasPendingCompletes, true);
	}
}

void FAndroidPlatformBackgroundHttpManager::Java_OnAllDownloadsComplete(jobject UnderlyingWorker, bool bDidAllRequestsSucceed)
{
	UE_LOG(LogBackgroundHttpManager, Log, TEXT("OnAllDownloadComplete... bWasSuccess:%d"), (int)bDidAllRequestsSucceed);
	
	//Mark as pending completes so we know to process completed requests at the manager level during our next tick
	FPlatformAtomics::InterlockedExchange(&bUnderlyingJavaAllDownloadsComplete, true);
}

//Allows us to do things while the Worker is running on that worker's thread since our normal GameThread may be suspended.
//Need to be VERY careful about thread safety here!
void FAndroidPlatformBackgroundHttpManager::Java_OnTick(JNIEnv* Env, jobject UnderlyingWorker)
{
	//None of our CHECK_JNI_RESULT macros should think their results are optional
	const bool bIsOptional = false;

	if (ensureAlwaysMsgf((nullptr != Env), TEXT("Invalid JNIEnv for Java_OnTick!")))
	{
		//Get worker class so we can query for java methods we need to call
		jclass UnderlyingWorkerClass = Env->GetObjectClass(UnderlyingWorker);
		CHECK_JNI_RESULT(UnderlyingWorkerClass);

		//Handle Pause Requests
		{
			const bool bIsThreadsafeToHandle = !FPlatformAtomics::InterlockedExchange(&bIsModifyingPauseList, true);
			if (bIsThreadsafeToHandle)
			{
				//GetPauseMethod from UnderlyingWorker
				jmethodID PauseMethod = Env->GetMethodID(UnderlyingWorkerClass, "PauseRequest", "(Ljava/lang/String;)V");
				CHECK_JNI_RESULT(PauseMethod);

				for (const FString& RequestID : RequestsToPauseByID_Java)
				{
					//Get java version of our RequestID
					FScopedJavaObject<jstring> ConvertedRequestID = FJavaHelper::ToJavaString(Env, RequestID);

					//Call pause method on RequestID
					FJavaWrapper::CallVoidMethod(Env, UnderlyingWorker, PauseMethod, *ConvertedRequestID);
				}

				RequestsToPauseByID_Java.Empty();

				const bool bSanityCheck = FPlatformAtomics::InterlockedExchange(&bIsModifyingPauseList, false);
				ensureAlwaysMsgf(bSanityCheck, TEXT("Error in bIsModifyingPauseList! Was false when we expected it to still be true from our lock!"));
			}
		}

		//Handle resume requests
		{
			const bool bIsThreadsafeToHandle = !FPlatformAtomics::InterlockedExchange(&bIsModifyingResumeList, true);
			if (bIsThreadsafeToHandle)
			{
				//GetPauseMethod from UnderlyingWorker
				jmethodID ResumeMethod = Env->GetMethodID(UnderlyingWorkerClass, "ResumeRequest", "(Ljava/lang/String;)V");
				CHECK_JNI_RESULT(ResumeMethod);

				for (const FString& RequestID : RequestsToResumeByID_Java)
				{
					//Get java version of our RequestID
					FScopedJavaObject<jstring> ConvertedRequestID = FJavaHelper::ToJavaString(Env, RequestID);

					//Call pause method on RequestID
					FJavaWrapper::CallVoidMethod(Env, UnderlyingWorker, ResumeMethod, *ConvertedRequestID);
				}

				RequestsToResumeByID_Java.Empty();

				const bool bSanityCheck = FPlatformAtomics::InterlockedExchange(&bIsModifyingResumeList, false);
				ensureAlwaysMsgf(bSanityCheck, TEXT("Error in bIsModifyingResumeList! Was false when we expected it to still be true from our lock!"));
			}
		}

		//Handle cancel requests
		{
			const bool bIsThreadsafeToHandle = !FPlatformAtomics::InterlockedExchange(&bIsModifyingCancelList, true);
			if (bIsThreadsafeToHandle)
			{
				jmethodID CancelMethod = Env->GetMethodID(UnderlyingWorkerClass, "CancelRequest", "(Ljava/lang/String;)V");

				for (const FString& RequestID : RequestsToCancelByID_Java)
				{
					//Get java version of our RequestID
					FScopedJavaObject<jstring> ConvertedRequestID = FJavaHelper::ToJavaString(Env, RequestID);

					// Call cancel method on RequestID
					FJavaWrapper::CallVoidMethod(Env, UnderlyingWorker, CancelMethod, *ConvertedRequestID);
				}

				RequestsToCancelByID_Java.Empty();

				const bool bSanityCheck = FPlatformAtomics::InterlockedExchange(&bIsModifyingCancelList, false);
				ensureAlwaysMsgf(bSanityCheck, TEXT("Error in bIsModifyingCancelList! Was false when we expected it to still be true from our lock!"));
			}
		}
	}
}

//
//JNI methods coming from UEDownloadWorker
//

JNI_METHOD void Java_com_epicgames_ue4_download_UEDownloadWorker_nativeAndroidBackgroundDownloadOnProgress(JNIEnv* jenv, jobject thiz, jstring TaskID, jlong BytesWrittenSinceLastCall, jlong TotalBytesWritten)
{
	const bool bIsSafeToCallDelegates = FPlatformAtomics::AtomicRead(&(FAndroidBackgroundDownloadDelegates::bHasManagerInitialized));
	if (bIsSafeToCallDelegates)
	{
		FString RequestID = FJavaHelper::FStringFromParam(jenv, TaskID);
		int64_t ConvertedBytesWrittenSinceLastCall = static_cast<uint64_t>(BytesWrittenSinceLastCall);
		int64_t ConvertedTotalBytesWritten = static_cast<uint64_t>(TotalBytesWritten);

		FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnProgress.Broadcast(thiz, RequestID, ConvertedBytesWrittenSinceLastCall, ConvertedTotalBytesWritten);
	}
}

JNI_METHOD void Java_com_epicgames_ue4_download_UEDownloadWorker_nativeAndroidBackgroundDownloadOnComplete(JNIEnv* jenv, jobject thiz, jstring TaskID, jstring CompleteLocation, jboolean bWasSuccess)
{
	const bool bIsSafeToCallDelegates = FPlatformAtomics::AtomicRead(&(FAndroidBackgroundDownloadDelegates::bHasManagerInitialized));
	if (bIsSafeToCallDelegates)
	{
		FString RequestID = FJavaHelper::FStringFromParam(jenv, TaskID);
		FString ConvertedCompleteLocation = FJavaHelper::FStringFromParam(jenv, CompleteLocation);
		bool ConvertedbWasSuccess = static_cast<bool>(bWasSuccess);

		FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnComplete.Broadcast(thiz, RequestID, ConvertedCompleteLocation, ConvertedbWasSuccess);
	}
}

JNI_METHOD void Java_com_epicgames_ue4_download_UEDownloadWorker_nativeAndroidBackgroundDownloadOnAllComplete(JNIEnv* jenv, jobject thiz, jboolean bDidAllRequestsSucceed)
{
	const bool bIsSafeToCallDelegates = FPlatformAtomics::AtomicRead(&(FAndroidBackgroundDownloadDelegates::bHasManagerInitialized));
	if (bIsSafeToCallDelegates)
	{
		bool ConvertedbDidAllRequestsSucceed = static_cast<bool>(bDidAllRequestsSucceed);
		FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnAllComplete.Broadcast(thiz, bDidAllRequestsSucceed);
	}
}

JNI_METHOD void Java_com_epicgames_ue4_download_UEDownloadWorker_nativeAndroidBackgroundDownloadOnTick(JNIEnv* jenv, jobject thiz)
{
	const bool bIsSafeToCallDelegates = FPlatformAtomics::AtomicRead(&(FAndroidBackgroundDownloadDelegates::bHasManagerInitialized));
	if (bIsSafeToCallDelegates)
	{
		FAndroidBackgroundDownloadDelegates::AndroidBackgroundDownload_OnTickWorkerThread.Broadcast(jenv, thiz);
	}
}


#undef LOCTEXT_NAMESPACE