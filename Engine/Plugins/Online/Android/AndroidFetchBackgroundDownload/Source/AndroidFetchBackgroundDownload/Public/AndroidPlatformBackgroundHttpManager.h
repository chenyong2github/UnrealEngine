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

	const FString GetFullFileNameForDownloadDescriptionList() const;
	const FString GetBaseFileNameForDownloadDescriptionListWithAppendedInt(int IntToAppend) const;

	//Handlers for our download progressing in the underlying java implementation so that we can bubble it up to UE code.
	void Java_OnDownloadProgress(jobject UnderlyingWorker, FString RequestID, int64_t BytesWrittenSinceLastCall, int64_t TotalBytesWritten);
	void Java_OnDownloadComplete(jobject UnderlyingWorker, FString RequestID, FString CompleteLocation, bool bWasSuccess);
	void Java_OnAllDownloadsComplete(jobject UnderlyingWorker, bool bDidAllRequestsSucceed);
	void Java_OnTick(JNIEnv* Env, jobject UnderlyingWorker);

	//Helper function to determine if a background http request was completed in the underlying layer
	//but is still in our ActiveRequests lists as its pending being a complete being sent on our game thread tick
	bool HasUnderlyingJavaCompletedRequest(FBackgroundHttpRequestPtr Request);
	bool HasUnderlyingJavaCompletedRequest(FAndroidBackgroundHttpRequestPtr Request);

	//Flag our underlying request as completed in a thread-safe way as this needs to be able to called on the background worker thread or game thread
	void MarkUnderlyingJavaRequestAsCompleted(FBackgroundHttpRequestPtr Request, bool bSuccess = true );
	void MarkUnderlyingJavaRequestAsCompleted(FAndroidBackgroundHttpRequestPtr Request, bool bSuccess = true);

	//returns true if this request is a valid request to send through to our underlying Java worker. Makes sure the request is not completed or paused
	bool IsValidRequestToEnqueue(FBackgroundHttpRequestPtr Request);
	bool IsValidRequestToEnqueue(FAndroidBackgroundHttpRequestPtr Request);

	FDelegateHandle Java_OnDownloadProgressHandle;
	FDelegateHandle Java_OnDownloadCompleteHandle;
	FDelegateHandle Java_OnAllDownloadsCompleteHandle;
	FDelegateHandle Java_OnTickHandle;

	//Array used to store Pause/Resume/Cancel requests in a thread-safe non-locking way. This way we can utilize the _Java lists in our Java_OnTick
	//without worrying about blocking the java thread
	TArray<FString> RequestsToPauseByID_GT;
	TArray<FString> RequestsToPauseByID_Java;
	TArray<FString> RequestsToResumeByID_GT;
	TArray<FString> RequestsToResumeByID_Java;
	TArray<FString> RequestsToCancelByID_GT;
	TArray<FString> RequestsToCancelByID_Java;

	//Used to flag if we have any downloads completed in our ActiveRequests list that are 
	//completed in java but waiting to send their completion handler on the GameThread
	volatile int32 bHasPendingCompletes;
	
	//Used to ensure that we can skip past the Pause/Resume/Cancel sections if one of the threads is using them
	//Don't want to just use RWLocks as we can't block the WorkerThread
	volatile int32 bIsModifyingPauseList;
	volatile int32 bIsModifyingResumeList;
	volatile int32 bIsModifyingCancelList;

	//Rechecks any _GT lists to try and move them to _Java lists if its safe to do so
	void HandleRequestsWaitingOnJavaThreadsafety();

private:
	//struct holding all our Java class, method, and field information in one location.
	//Must call initialize on this before it is useful. Future calls to Initialize will not recalculate information
	struct FJavaClassInfo
	{
		bool bHasInitialized = false;

		jclass UEDownloadWorkerClass;
		jclass DownloadDescriptionClass;

		//Necessary JNI methods we will need to create our DownloadDescriptions
		jmethodID CreateArrayStaticMethod;
		jmethodID WriteDownloadDescriptionListToFileMethod;
		jmethodID CreateDownloadDescriptionFromJsonMethod;

		void Initialize();

		FJavaClassInfo()
			: UEDownloadWorkerClass(0)
			, DownloadDescriptionClass(0)
			, CreateArrayStaticMethod(0)
			, WriteDownloadDescriptionListToFileMethod(0)
		{}
};

	static FJavaClassInfo JavaInfo;
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

	static const FString NOTIFICATION_ID_KEY;
	static const int NOTIFICATION_DEFAULT_ID_KEY;

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

	//We use this bool to check if we need to actually route work done on the BG java side to UE, if we haven't scheduled any BGWork
	//then we don't need to try and respond to results as its from a previous worker that we didn't schedule yet, and thus have no
	//matching BG Download Requests. We should associate with those downloads once we request BG work if they are requested and still active
	static volatile int32 bHasManagerScheduledBGWork;

	//Delegates called by JNI functions to bubble up underlying java work to the manager
	static FAndroidBackgroundDownload_OnProgress AndroidBackgroundDownload_OnProgress;
	static FAndroidBackgroundDownload_OnComplete AndroidBackgroundDownload_OnComplete;
	static FAndroidBackgroundDownload_OnAllComplete AndroidBackgroundDownload_OnAllComplete;
	static FAndroidBackgroundDownload_OnTickWorkerThread AndroidBackgroundDownload_OnTickWorkerThread;
};

typedef TSharedPtr<FAndroidPlatformBackgroundHttpManager, ESPMode::ThreadSafe> FAndroidPlatformBackgroundHttpManagerPtr;