// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "CoreMinimal.h"

#include "BackgroundHttpManagerImpl.h"
#include "Interfaces/IBackgroundHttpRequest.h"

#include "AndroidPlatformBackgroundHttpRequest.h"

#include "Android/AndroidPlatform.h"
#include "Android/AndroidJava.h"
#include "Android/AndroidJavaEnv.h"
#include "Android/AndroidJNI.h"
#include "Android/AndroidApplication.h"

/**
 * Manages Background Http request that are currently being processed if we are on an Android Platform
 */
class FAndroidPlatformBackgroundHttpManager
	: public FBackgroundHttpManagerImpl
{
public:
	FAndroidPlatformBackgroundHttpManager();
	virtual ~FAndroidPlatformBackgroundHttpManager() {};
	
	/**
	* FBackgroundHttpManagerImpl overrides
	*/
public:
	virtual void Initialize() override;
	virtual void Shutdown() override;
	virtual bool IsGenericImplementation() const override { return false; }
	bool Tick(float DeltaTime);
	
protected:
	virtual void ActivatePendingRequests() override;

public :
	void PauseRequest(FBackgroundHttpRequestPtr Request);
	void ResumeRequest(FBackgroundHttpRequestPtr Request);
	void CancelRequest(FBackgroundHttpRequestPtr Request);

protected:
	void UpdateRequestProgress();

	FAndroidBackgroundHttpRequestPtr FindRequestByID(FString RequestID);
	void HandlePendingCompletes();
	void VerifyIfAllDownloadsComplete();

	const FString GetFullFileNameForDownloadDescriptionList() const;
	const FString GetBaseFileNameForDownloadDescriptionListWithAppendedInt(int IntToAppend) const;

	//Handlers for our download progressing in the underlying java implementation so that we can bubble it up to UE code.
	void Java_OnDownloadProgress(jobject UnderlyingWorker, FString RequestID, int64_t BytesWrittenSinceLastCall, int64_t TotalBytesWritten);
	void Java_OnDownloadComplete(jobject UnderlyingWorker, FString RequestID, FString CompleteLocation, bool bWasSuccess);
	void Java_OnAllDownloadsComplete(jobject UnderlyingWorker, bool bDidAllRequestsSucceed);
	void Java_OnTick(JNIEnv* Env, jobject UnderlyingWorker);

	FDelegateHandle Java_OnDownloadProgressHandle;
	FDelegateHandle Java_OnDownloadCompleteHandle;
	FDelegateHandle Java_OnAllDownloadsCompleteHandle;
	FDelegateHandle Java_OnTickHandle;

	volatile int32 bHasPendingCompletes;
	volatile int32 bUnderlyingJavaAllDownloadsComplete;

	//Array used to store Pause/Resume/Cancel requests in a thread-safe non-locking way. This way we can utilize the _Java lists in our Java_OnTick
	//without worrying about blocking the java thread
	TArray<FString> RequestsToPauseByID_GT;
	TArray<FString> RequestsToPauseByID_Java;
	TArray<FString> RequestsToResumeByID_GT;
	TArray<FString> RequestsToResumeByID_Java;
	TArray<FString> RequestsToCancelByID_GT;
	TArray<FString> RequestsToCancelByID_Java;

	//Used to ensure that we can skip past the Pause/Resume/Cancel sections if one of the threads is using them
	//Don't want to just use RWLocks as we can't block the WorkerThread
	volatile int32 bIsModifyingPauseList;
	volatile int32 bIsModifyingResumeList;
	volatile int32 bIsModifyingCancelList;

	//Rechecks any _GT lists to try and move them to _Java lists if its safe to do so
	void HandleRequestsWaitingOnJavaThreadsafety();
};

//WARNING: These values MUST stay in sync with their values in DownloadWorkerParameterKeys.java!
class FAndroidNativeDownloadWorkerParameterKeys
{
public:
	static const FString DOWNLOAD_DESCRIPTION_LIST_KEY;
	static const FString DOWNLOAD_MAX_CONCURRENT_REQUESTS_KEY;

	static const FString NOTIFICATION_CHANNEL_ID_KEY;
	static const FString NOTIFICATION_CHANNEL_NAME_KEY;
	static const FString NOTIFICATION_CHANNEL_IMPORTANCE_KEY;

	static const FString NOTIFICATION_CONTENT_TITLE_KEY;
	static const FString NOTIFICATION_CONTENT_TEXT_KEY;
	static const FString NOTIFICATION_CONTENT_CANCEL_DOWNLOAD_TEXT_KEY;
	static const FString NOTIFICATION_CONTENT_COMPLETE_TEXT_KEY;

	static const FString NOTIFICATION_RESOURCE_CANCEL_ICON_NAME;
	static const FString NOTIFICATION_RESOURCE_CANCEL_ICON_TYPE;
	static const FString NOTIFICATION_RESOURCE_CANCEL_ICON_PACKAGE;

	static const FString NOTIFICATION_RESOURCE_SMALL_ICON_NAME;
	static const FString NOTIFICATION_RESOURCE_SMALL_ICON_TYPE;
	static const FString NOTIFICATION_RESOURCE_SMALL_ICON_PACKAGE;

};
//Call backs called by the bellow FBackgroundURLSessionHandler so higher-level systems can respond to task updates.
class FAndroidBackgroundDownloadDelegates
{
public:
	DECLARE_MULTICAST_DELEGATE_FourParams(FAndroidBackgroundDownload_OnProgress, jobject /*UnderlyingWorker*/, FString /*RequestID*/, int64_t /*BytesWrittenSinceLastCall*/, int64_t /*TotalBytesWritten*/);
	DECLARE_MULTICAST_DELEGATE_FourParams(FAndroidBackgroundDownload_OnComplete, jobject /*UnderlyingWorker*/, FString /*RequestID*/, FString /*CompleteLocation*/, bool /*bWasSuccess*/);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FAndroidBackgroundDownload_OnAllComplete, jobject /*UnderlyingWorker*/, bool /*bDidAllRequestsSucceed*/);
	DECLARE_MULTICAST_DELEGATE_TwoParams(FAndroidBackgroundDownload_OnTickWorkerThread, JNIEnv*, jobject /*UnderlyingWorker*/);

	//We use this bool to ensure it is safe to try and broadcast to these static delegates.
	//This may not be needed and we might be able to fall back to the normal behavior of just broadcasting
	//but this is a safety measure as we can end up in the calling JNI functions before the engine has begun ANY loading
	//and we aren't even guaranteed the engine WILL load depending on how WorkManager calls our process to handle the background work.
	static volatile int32 bHasManagerInitialized;

	//Delegates called by JNI functions to bubble up underlying java work to the manager
	static FAndroidBackgroundDownload_OnProgress AndroidBackgroundDownload_OnProgress;
	static FAndroidBackgroundDownload_OnComplete AndroidBackgroundDownload_OnComplete;
	static FAndroidBackgroundDownload_OnAllComplete AndroidBackgroundDownload_OnAllComplete;
	static FAndroidBackgroundDownload_OnTickWorkerThread AndroidBackgroundDownload_OnTickWorkerThread;
};

typedef TSharedPtr<FAndroidPlatformBackgroundHttpManager, ESPMode::ThreadSafe> FAndroidPlatformBackgroundHttpManagerPtr;