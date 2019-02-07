// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IOS/ApplePlatformBackgroundHttpManager.h"
#include "IOS/ApplePlatformBackgroundHttpRequest.h"
#include "IOS/ApplePlatformBackgroundHttpResponse.h"

#include "HAL/PlatformFilemanager.h"
#include "HAL/PlatformAtomics.h"

#include "Misc/ConfigCacheIni.h"
#include "Misc/CoreDelegates.h"
#include "Misc/ScopeRWLock.h"

#include "IOS/IOSBackgroundURLSessionHandler.h"

FApplePlatformBackgroundHttpManager::~FApplePlatformBackgroundHttpManager()
{
    [UnAssociatedTasks release];
    UnAssociatedTasks = nullptr;
    
    CleanUpNSURLSessionResponseDelegates();
}

FString FApplePlatformBackgroundHttpManager::BackgroundSessionIdentifier = FString("");
float FApplePlatformBackgroundHttpManager::ActiveTimeOutSetting = 30.0f;
int FApplePlatformBackgroundHttpManager::RetryResumeDataLimitSetting = -1.f;

FApplePlatformBackgroundHttpManager::FApplePlatformBackgroundHttpManager()
	: bHasFinishedPopulatingUnassociatedTasks(false)
    , bIsInBackground(false)
    , bIsIteratingThroughSessionTasks(false)
    , RequestsPendingRemove()
{
}

void FApplePlatformBackgroundHttpManager::Initialize()
{
	UnAssociatedTasks = [[NSMutableDictionary alloc] init];
	PopulateUnAssociatedTasks();

    GConfig->GetFloat(TEXT("BackgroundHttp.iOSSettings"), TEXT("BackgroundHttp.ActiveReceiveTimeout"), ActiveTimeOutSetting, GEngineIni);
	GConfig->GetInt(TEXT("BackgroundHttp.iOSSettings"), TEXT("BackgroundHttp.RetryResumeDataLimit"), RetryResumeDataLimitSetting, GEngineIni);

	SetupNSURLSessionResponseDelegates();
}

void FApplePlatformBackgroundHttpManager::PopulateUnAssociatedTasks()
{
	if (ensureAlwaysMsgf((nullptr != UnAssociatedTasks), TEXT("Call to PopulateUnAssociatedTasks without initializing UnAssociatedTasks Dictionary!")))
	{
		NSURLSession* BackgroundDownloadSession = FBackgroundURLSessionHandler::GetBackgroundSession();
		if (ensureAlwaysMsgf((nullptr != BackgroundDownloadSession), TEXT("Invalid Background Download NSURLSession during AppleBackgroundHttp Init! Should have already Initialized the NSURLSession by this point!")))
		{
			[BackgroundDownloadSession getAllTasksWithCompletionHandler : ^ (NSArray<__kindof NSURLSessionTask*> *tasks)
			{
				//Store all existing tasks by their URL
				for (id task in tasks)
				{
					[UnAssociatedTasks setObject:task forKey:[[[task currentRequest] URL] absoluteString]];
				}

				bHasFinishedPopulatingUnassociatedTasks = true;
			}];
		}
	}
}

void FApplePlatformBackgroundHttpManager::PauseAllUnassociatedTasks()
{
    for (id Key in UnAssociatedTasks)
    {
        NSURLSessionDownloadTask* Task = (NSURLSessionDownloadTask*)([UnAssociatedTasks objectForKey:Key]);
        if (nullptr != Task)
        {
            if ([Task state] == NSURLSessionTaskStateRunning)
            {
                [Task suspend];
            }
        }
    }
}

void FApplePlatformBackgroundHttpManager::UnpauseAllUnassociatedTasks()
{
    for (id Key in UnAssociatedTasks)
    {
        NSURLSessionDownloadTask* Task = (NSURLSessionDownloadTask*)([UnAssociatedTasks objectForKey:Key]);
        if (nullptr != Task)
        {
            if ([Task state] == NSURLSessionTaskStateSuspended)
            {
                [Task resume];
            }
        }
    }
}

void FApplePlatformBackgroundHttpManager::Shutdown()
{
	[UnAssociatedTasks release];
	UnAssociatedTasks = nullptr;

	CleanUpNSURLSessionResponseDelegates();
    FBackgroundURLSessionHandler::ShutdownBackgroundSession();
}

void FApplePlatformBackgroundHttpManager::AddRequest(const FBackgroundHttpRequestPtr Request)
{
	UE_LOG(LogBackgroundHttpManager, Verbose, TEXT("AddRequest Called - RequestID:%s"), *Request->GetRequestID());
    
    //See if our request is an AppleBackgroundHttpRequest so we can do more detailed checks on it.
    FAppleBackgroundHttpRequestPtr AppleRequest = StaticCastSharedPtr<FApplePlatformBackgroundHttpRequest>(Request);
    if (ensureAlwaysMsgf(AppleRequest.IsValid(), TEXT("Adding a non-Apple background request to our Apple Background Http Manager!")))
    {
        GenerateURLMapEntriesForRequest(AppleRequest);
    }
    
	if (!AssociateWithAnyExistingRequest(Request))
	{
        //ensures above if this is false, but need to re-check it before adding tasks
        if (AppleRequest.IsValid())
        {
            StartRequest(AppleRequest);
        }
        
		//Just always add un-associated requests to ActiveRequests in the Apple implementation.
		FRWScopeLock ScopeLock(ActiveRequestLock, SLT_Write);
		ActiveRequests.Add(Request);
	}
}

void FApplePlatformBackgroundHttpManager::GenerateURLMapEntriesForRequest( FAppleBackgroundHttpRequestPtr Request)
{
    FRWScopeLock ScopeLock(URLToRequestMapLock, SLT_Write);
    for (const FString& URL : Request->GetURLList())
    {
        FBackgroundHttpURLMappedRequestPtr& FoundRequest = URLToRequestMap.FindOrAdd(URL);
        
        const bool bRequestAlreadyExistsForURL = ((FoundRequest.IsValid()) && (Request != FoundRequest));
        if (ensureAlwaysMsgf(!bRequestAlreadyExistsForURL, TEXT("URL is represented by 2 different Requests! Immediately completing new request with error.")))
        {
            FoundRequest = Request;
        }
        else
        {
            FBackgroundHttpResponsePtr NewResponse = FPlatformBackgroundHttp::ConstructBackgroundResponse(EHttpResponseCodes::Unknown, FString());
            Request->CompleteWithExistingResponseData(NewResponse);
        }
    }
}

void FApplePlatformBackgroundHttpManager::RemoveURLMapEntriesForRequest(FAppleBackgroundHttpRequestPtr Request)
{
    FRWScopeLock ScopeLock(URLToRequestMapLock, SLT_Write);
    for (const FString& URL : Request->GetURLList())
    {
        FBackgroundHttpURLMappedRequestPtr& FoundRequest = URLToRequestMap.FindOrAdd(URL);
        
        if (FoundRequest == Request)
        {
            UE_LOG(LogBackgroundHttpManager, Display, TEXT("Removing URL Entry -- RequestID:%s | URL:%s"), *Request->GetRequestID(), *URL);
            URLToRequestMap.Remove(URL);
        }
    }
}

void FApplePlatformBackgroundHttpManager::StartRequest(FAppleBackgroundHttpRequestPtr Request)
{
    //Just count it as a retry that won't increment the retry counter before giving us the URL as our RetryCount 0 should start this up.
    RetryRequest(Request,false, false, nullptr);
}

void FApplePlatformBackgroundHttpManager::RemoveRequest(const FBackgroundHttpRequestPtr Request)
{
	FAppleBackgroundHttpRequestPtr AppleRequest = StaticCastSharedPtr<FApplePlatformBackgroundHttpRequest>(Request);
	if (AppleRequest.IsValid())
	{
		RemoveSessionTasksForRequest(AppleRequest);
	}
	
    RequestsPendingRemove.Add(Request);
}

void FApplePlatformBackgroundHttpManager::DeletePendingRemoveRequests()
{
    for (const FBackgroundHttpRequestPtr& Request : RequestsPendingRemove)
    {
        FBackgroundHttpManagerImpl::RemoveRequest(Request);
    }
    
    RequestsPendingRemove.Empty();
}

void FApplePlatformBackgroundHttpManager::RemoveSessionTasksForRequest(FAppleBackgroundHttpRequestPtr Request)
{
    //First remove map entries. That way we won't send a completion handler when we cancel
    RemoveURLMapEntriesForRequest(Request);
    
    //Now cancel our active task
    Request->CancelActiveTask();
}

bool FApplePlatformBackgroundHttpManager::AssociateWithAnyExistingRequest(const FBackgroundHttpRequestPtr Request)
{
	if (!bHasFinishedPopulatingUnassociatedTasks)
	{
		//@TODO: TRoss, might want to look at trying to associate these after bHasFinishedPopulatingUnassociatedTasks. At this point I don't think its really worth it as it SHOULD be done before we get here, however
		//the PopulateUnAssociatedTasks() function has an asynch component so it could technically be unfinished with some tight timing.
		UE_LOG(LogBackgroundHttpManager, Warning, TEXT("Call to AssociateWithAnyExistingRequest before we have finished populating unassociated tasks! Might have an unassociated task for this request that we won't associate with."));
	}
	
	//First check if we Associate using the base implementation.
	bool bDidAssociateWithExistingRequest = FBackgroundHttpManagerImpl::AssociateWithAnyExistingRequest(Request);

	//Didn't associate, so lets check more Apple specific stuff
	if (!bDidAssociateWithExistingRequest)
	{
		//See if our request is an AppleBackgroundHttpRequest so we can do more detailed checks on it.
		FAppleBackgroundHttpRequestPtr AppleRequest = StaticCastSharedPtr<FApplePlatformBackgroundHttpRequest>(Request);
		if (AppleRequest.IsValid())
		{
			bDidAssociateWithExistingRequest = CheckForExistingUnAssociatedTask(AppleRequest);
		}
	}

	return bDidAssociateWithExistingRequest;
}

bool FApplePlatformBackgroundHttpManager::CheckForExistingUnAssociatedTask(const FAppleBackgroundHttpRequestPtr Request)
{
    bool bDidFindExistingTask = false;
    
	if (ensureAlwaysMsgf(Request.IsValid(), TEXT("CheckForExistingUnAssociatedTask called with invalid Request!")))
	{
		const TArray<FString>& URLList = Request->GetURLList();
		for (const FString& URL : URLList)
		{
			NSURLSessionTask* FoundTask = [UnAssociatedTasks valueForKey:URL.GetNSString()];
			if (nullptr != FoundTask)
			{
                if ([FoundTask state] != NSURLSessionTaskStateCanceling)
                {
                    UE_LOG(LogBackgroundHttpManager, Display, TEXT("Existing UnAssociateTask found for Request! Attempting to Associate! RequestId:%s"), *(Request->GetRequestID()));
                    Request->AssociateWithTask(FoundTask);
                    
                    bDidFindExistingTask = true;
                }
			}
		}
	}
    
    return bDidFindExistingTask;
}

void FApplePlatformBackgroundHttpManager::SetupNSURLSessionResponseDelegates()
{
	OnApp_EnteringBackgroundHandle = FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddRaw(this, &FApplePlatformBackgroundHttpManager::OnApp_EnteringBackground);
	OnApp_EnteringForegroundHandle = FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddRaw(this, &FApplePlatformBackgroundHttpManager::OnApp_EnteringForeground);
	OnTask_DidFinishDownloadingToURLHandle = FIOSBackgroundDownloadCoreDelegates::OnIOSBackgroundDownload_DidFinishDownloadingToURL.AddRaw(this, &FApplePlatformBackgroundHttpManager::OnTask_DidFinishDownloadingToURL);
	OnTask_DidWriteDataHandle = FIOSBackgroundDownloadCoreDelegates::OnIOSBackgroundDownload_DidWriteData.AddRaw(this, &FApplePlatformBackgroundHttpManager::OnTask_DidWriteData);
	OnTask_DidCompleteWithErrorHandle = FIOSBackgroundDownloadCoreDelegates::OnIOSBackgroundDownload_DidCompleteWithError.AddRaw(this, &FApplePlatformBackgroundHttpManager::OnTask_DidCompleteWithError);
	OnSession_SessionDidFinishAllEventsHandle = FIOSBackgroundDownloadCoreDelegates::OnIOSBackgroundDownload_SessionDidFinishAllEvents.AddRaw(this, &FApplePlatformBackgroundHttpManager::OnSession_SessionDidFinishAllEvents);
}

void FApplePlatformBackgroundHttpManager::CleanUpNSURLSessionResponseDelegates()
{
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Remove(OnApp_EnteringBackgroundHandle);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Remove(OnApp_EnteringForegroundHandle);
	FIOSBackgroundDownloadCoreDelegates::OnIOSBackgroundDownload_DidFinishDownloadingToURL.Remove(OnTask_DidFinishDownloadingToURLHandle);
	FIOSBackgroundDownloadCoreDelegates::OnIOSBackgroundDownload_DidWriteData.Remove(OnTask_DidWriteDataHandle);
	FIOSBackgroundDownloadCoreDelegates::OnIOSBackgroundDownload_DidCompleteWithError.Remove(OnTask_DidCompleteWithErrorHandle);
	FIOSBackgroundDownloadCoreDelegates::OnIOSBackgroundDownload_SessionDidFinishAllEvents.Remove(OnSession_SessionDidFinishAllEventsHandle);
}

void FApplePlatformBackgroundHttpManager::OnApp_EnteringForeground()
{
    FPlatformAtomics::InterlockedExchange(&bIsInBackground,false);
	PauseAllActiveTasks();
}

void FApplePlatformBackgroundHttpManager::OnApp_EnteringBackground()
{
	FPlatformAtomics::InterlockedExchange(&bIsInBackground,true);
	ResumeAllTasks();
}

void FApplePlatformBackgroundHttpManager::PauseAllActiveTasks()
{
    UE_LOG(LogBackgroundHttpManager, Display, TEXT("Attempting to Pause All Active Tasks"));
    
	NSURLSession* BackgroundDownloadSession = FBackgroundURLSessionHandler::GetBackgroundSession();
	if (nullptr != BackgroundDownloadSession)
	{
		[BackgroundDownloadSession getTasksWithCompletionHandler:^(NSArray<__kindof NSURLSessionDataTask*>* DataTasks, NSArray<__kindof NSURLSessionUploadTask*>* UploadTasks, NSArray<__kindof NSURLSessionDownloadTask*>* DownloadTasks)
		{
			for (NSURLSessionDownloadTask* DownloadTask : DownloadTasks)
			{
				if ([DownloadTask state] == NSURLSessionTaskStateRunning)
				{
                    FString TaskURL = [[[DownloadTask currentRequest] URL] absoluteString];
                    UE_LOG(LogBackgroundHttpManager, Display, TEXT("Pausing Task for URL:%s"), *TaskURL);
                    
					[DownloadTask suspend];
				}
			}
            
            //Reset our active requests to 0 now that we are pausing everything
            FPlatformAtomics::InterlockedExchange(&NumCurrentlyActiveRequests, 0);
		}];
	}
}

void FApplePlatformBackgroundHttpManager::ResumeAllTasks()
{
    UE_LOG(LogBackgroundHttpManager, Display, TEXT("Attempting to Resume All Active Tasks"));
    
	NSURLSession* BackgroundDownloadSession = FBackgroundURLSessionHandler::GetBackgroundSession();
	if (nullptr != BackgroundDownloadSession)
	{
		[BackgroundDownloadSession getTasksWithCompletionHandler : ^ (NSArray<__kindof NSURLSessionDataTask*>* DataTasks, NSArray<__kindof NSURLSessionUploadTask*>* UploadTasks, NSArray<__kindof NSURLSessionDownloadTask*>* DownloadTasks)
		{
			for (NSURLSessionDownloadTask* DownloadTask : DownloadTasks)
			{
				if ([DownloadTask state] == NSURLSessionTaskStateSuspended)
				{
                    FString TaskURL = [[[DownloadTask currentRequest] URL] absoluteString];
                    UE_LOG(LogBackgroundHttpManager, Display, TEXT("Resuming Task for URL:%s"), *TaskURL);
                    
					[DownloadTask resume];
				}
			}
		}];
	}
}

void FApplePlatformBackgroundHttpManager::OnTask_DidFinishDownloadingToURL(NSURLSessionDownloadTask* Task, NSError* Error, const FString& TempFilePath)
{
	FString TaskURL = [[[Task currentRequest] URL] absoluteString];
    const int ErrorCode = [Error code];
    const FString ErrorDescription = [Error localizedDescription];
    
    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	const bool bFileExists = PlatformFile.FileExists(*TempFilePath);
	
    UE_LOG(LogBackgroundHttpManager, Display, TEXT("Recieved Background Session Callback for URL:%s | bFileExists:%d | ErrorCode:%d | ErrorDescription:%s | Location:%s"), *TaskURL, (int)(bFileExists), ErrorCode, *ErrorDescription, *TempFilePath);

	//Find request for this task and mark it complete
	{
        FRWScopeLock ScopeLock(URLToRequestMapLock, SLT_ReadOnly);
        FBackgroundHttpURLMappedRequestPtr* WeakRequestInMap = URLToRequestMap.Find(TaskURL);
        FAppleBackgroundHttpRequestPtr FoundRequest = (nullptr != WeakRequestInMap) ? WeakRequestInMap->Pin() : nullptr;
        
        if (FoundRequest.IsValid())
		{
			FoundRequest->SetRequestAsSuccess(TempFilePath);
		}
        
		UE_LOG(LogBackgroundHttpManager, Display, TEXT("Attempt To Mark Task Complete -- URL:%s | bDidFindTask:%d"), *TaskURL, (int)(FoundRequest.IsValid()));
	}
}

void FApplePlatformBackgroundHttpManager::FinishRequest(FAppleBackgroundHttpRequestPtr Request)
{
    //Make sure we another thread hasn't already finished this request
    bool bHasAlreadyFinishedRequest = FPlatformAtomics::InterlockedExchange(&(Request->bHasAlreadyFinishedRequest), true);
    if (!bHasAlreadyFinishedRequest)
    {
        //by default we will be finishing this request in this function, but some errors might prompt a retry out of this function
        bool bIsRequestActuallyFinished = true;
        
        if (ensureAlwaysMsgf(Request.IsValid(), TEXT("Call to FinishRequest with invalid request!")))
        {
            const FString& TempFilePath = Request->CompletedTempDownloadLocation;
            IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
            const bool bFileExists = PlatformFile.FileExists(*TempFilePath);

            int ResponseCode = bFileExists ? EHttpResponseCodes::Created : EHttpResponseCodes::Unknown;

            if (bFileExists)
            {
                UE_LOG(LogBackgroundHttpManager,Display, TEXT("Task Completed Successfully. RequestID:%s TempFileLocation:%s"), *(Request->GetRequestID()), *TempFilePath);
                FBackgroundHttpResponsePtr NewResponse = FPlatformBackgroundHttp::ConstructBackgroundResponse(ResponseCode, *TempFilePath);
                Request->CompleteWithExistingResponseData(NewResponse);
            }
            else
            {
                volatile bool bDidFail = FPlatformAtomics::AtomicRead(&(Request->bIsFailed));
                
                //Unexpected case where we didn't find a valid download, but we thought this completed
                //successfully. Handle this unexpected failure
                if (!bDidFail)
                {
                    UE_LOG(LogBackgroundHttpManager,Warning, TEXT("Task finished downloading, but finished temp file was not found! -- RequestId:%s | TempFileLocation:%s"), *(Request->GetRequestID()), *TempFilePath);
                    
                    //Mark our download as not completed as we hit an error so that we don't just keep trying to call FinishRequest
                    FPlatformAtomics::InterlockedExchange(&(Request->bIsCompleted), false);
                    FPlatformAtomics::InterlockedExchange(&(Request->bHasAlreadyFinishedRequest), false);
                    
                    //Just cancel the task. This will lead to it getting a callback to OnTask_DidCompleteWithError where we will re-create it
                    Request->CancelActiveTask();
                    
                    bIsRequestActuallyFinished = false;
                }
                //Expected case where we failed, but expected to fail
                else
                {
                    UE_LOG(LogBackgroundHttpManager, Display, TEXT("Task failed due to being out of retries. -- RequestID:%s"), *(Request->GetRequestID()));
                    
                    FBackgroundHttpResponsePtr NewResponse = FPlatformBackgroundHttp::ConstructBackgroundResponse(ResponseCode, TEXT(""));
                    Request->CompleteWithExistingResponseData(NewResponse);
                }
            }
            
            //If we are actually finishing this request, lets decrement our NumCurrentlyActiveRequests counter
            if (bIsRequestActuallyFinished)
            {
                //See if we are in the BG or FG. In the BG, we don't want to decrement NumCurrentlyActiveRequests
                const bool bWasFinishedInBG = FPlatformAtomics::AtomicRead(&bIsInBackground);
                if (!bWasFinishedInBG)
                {
                    int NumActualRequests = FPlatformAtomics::InterlockedDecrement(&NumCurrentlyActiveRequests);

                    //Sanity check that our data is valid. Shouldn't ever trip if everything is working as intended.
                    const bool bNumActualRequestsValid = ((NumActualRequests > 0) && (NumActualRequests <= FPlatformBackgroundHttp::GetPlatformMaxActiveDownloads()));
                    ensureMsgf(bNumActualRequestsValid, TEXT("Number of Requests we think are active is invalid! -- NumCurrentlyActiveRequests:%d"), NumActualRequests);
                }
            }
        }
    }
    else
    {
        UE_LOG(LogBackgroundHttpManager,Display, TEXT("Not finishing Request as its already sending a finish notification -- RequestID:%s"), *(Request->GetRequestID()));
    }
}

void FApplePlatformBackgroundHttpManager::RetryRequest(FAppleBackgroundHttpRequestPtr Request, bool bShouldIncreaseRetryCount, bool bShouldStartImmediately, NSData* RetryData)
{
	NSURLSessionDownloadTask* NewTask = nullptr;

	if (ensureAlwaysMsgf(Request.IsValid(), TEXT("Call to RetryRequest with an invalid request!")))
	{

		NSURLSession* BackgroundDownloadSession = FBackgroundURLSessionHandler::GetBackgroundSession();
		if (ensureAlwaysMsgf((nullptr != BackgroundDownloadSession), TEXT("Invalid Background Download NSURLSession during RetryRequest! Should have already Initialized the NSURLSession by this point!")))
		{
			//First, lets see if we should base this task of existing RetryData
			const bool bShouldUseRetryData = ShouldUseRequestRetryData(Request, RetryData);
			if (bShouldUseRetryData)
			{
				UE_LOG(LogBackgroundHttpManager, Display, TEXT("Resuming Task With Resume Data -- RequestID:%s | RetryData Length:%d"), *(Request->GetRequestID()), [RetryData length]);
				NewTask = [BackgroundDownloadSession downloadTaskWithResumeData:RetryData];
			}

			//If not retry data, lets try and just retry on the next CDN
			else
			{
                //Since we created a new task instead of using retry data, reset resume data's retry count on the request
                Request->ResumeDataRetryCount.Reset();
                
				const FString& NewRetryURL = Request->GetURLForRetry(bShouldIncreaseRetryCount);
				const bool bShouldStartNewRequest = !NewRetryURL.IsEmpty();
				if (bShouldStartNewRequest)
				{
					NSURL* URL = [NSURL URLWithString:NewRetryURL.GetNSString()];
					NewTask = [BackgroundDownloadSession downloadTaskWithURL:URL];
				}
			}

			if (nullptr != NewTask)
			{
				Request->AssociateWithTask(NewTask);

				//If we are in BG or flagged this as an immediate start, resume right now without waiting for the FG tick
				volatile bool bCopyOfBGState = FPlatformAtomics::AtomicRead(&bIsInBackground);
				if (bCopyOfBGState || bShouldStartImmediately)
				{
					Request->ActivateUnderlyingTask();
				}

				UE_LOG(LogBackgroundHttpManager, Display, TEXT("Created Task for Request -- RequestID:%s | bStartImmediately:%d | bIsAppInBG:%d"), *(Request->GetRequestID()), (int)bShouldStartImmediately, (int)bCopyOfBGState);

				//Always set our bWasTaskStartedInBG flag on our Request so we will know if we need to restart this task next FG Tick.
				FPlatformAtomics::InterlockedExchange(&(Request->bWasTaskStartedInBG), bCopyOfBGState);
			}
			else
			{
				UE_LOG(LogBackgroundHttpManager, Display, TEXT("Marking Request Failed. Out of Retries -- RequestID:%s | bShouldUseRetryData:%d"), *(Request->GetRequestID()), (int)bShouldUseRetryData);
				Request->SetRequestAsFailed();
			}
		}
	}
}

bool FApplePlatformBackgroundHttpManager::ShouldUseRequestRetryData(FAppleBackgroundHttpRequestPtr Request, NSData* RetryData) const
{
	bool bShouldUseData = false;

	if (ensureAlwaysMsgf(Request.IsValid(), TEXT("Call to ShouldUseRequestRetryData with an invalid request!")))
	{
		if (IsRetryDataValid(RetryData))
		{
			const int CurrentResumeDataRetryCount = Request->ResumeDataRetryCount.Increment();
			if ((RetryResumeDataLimitSetting < 0) || (CurrentResumeDataRetryCount <= RetryResumeDataLimitSetting))
			{
				bShouldUseData = true;
			}
		}
	}
    
    return bShouldUseData;
}

bool FApplePlatformBackgroundHttpManager::IsRetryDataValid(NSData* RetryData) const
{
	return ((nullptr != RetryData) && ([RetryData length] > 0));
}

void FApplePlatformBackgroundHttpManager::OnTask_DidWriteData(NSURLSessionDownloadTask* Task, int64_t BytesWrittenSinceLastCall, int64_t TotalBytesWritten, int64_t TotalBytesExpectedToWrite)
{
    if (ensureAlwaysMsgf((nullptr != Task), TEXT("Call to DidWriteData with invalid Task!")))
    {
        FString TaskURL = [[[Task currentRequest] URL] absoluteString];
        
        //Find task and update it's download progress
        {
            FRWScopeLock ScopeLock(URLToRequestMapLock, SLT_ReadOnly);
            FBackgroundHttpURLMappedRequestPtr* WeakRequestInMap = URLToRequestMap.Find(TaskURL);
            FAppleBackgroundHttpRequestPtr FoundRequest = (nullptr != WeakRequestInMap) ? WeakRequestInMap->Pin() : nullptr;
            
            if (FoundRequest.IsValid())
            {
                if (FoundRequest->DownloadProgress < TotalBytesWritten)
                {
                    int64 DownloadProgress = FPlatformAtomics::AtomicRead(&(FoundRequest->DownloadProgress));
                    UE_LOG(LogBackgroundHttpManager, Verbose, TEXT("Updating Task Progress! -- RequestID:%s | Current Progress:%lld | New Progress:%lld"), *(FoundRequest->GetRequestID()), DownloadProgress, TotalBytesWritten);
                }
                else
                {
                    UE_LOG(LogBackgroundHttpManager, Warning, TEXT("Download Progress tried to go down not up unexpectidly! This could mean a task was unknowingly duplicated! -- RequestID:%s | Current Progress:%lld | New Progress:%lld"), *(FoundRequest->GetRequestID()),FoundRequest->DownloadProgress, TotalBytesWritten);
                }

                FoundRequest->UpdateDownloadProgress(TotalBytesWritten, BytesWrittenSinceLastCall);
            }
        }
    }
}

void FApplePlatformBackgroundHttpManager::OnTask_DidCompleteWithError(NSURLSessionTask* Task, NSError* Error)
{
	if (ensureAlwaysMsgf((nullptr != Task), TEXT("Call to OnTask_DidCompleteWithError delegate with an invalid task!")))
	{
		FString TaskURL = [[[Task currentRequest] URL] absoluteString];
		const bool bDidCompleteWithError = (nullptr != Error);
		const int ErrorCode = [Error code];
        const FString ErrorDescription = [Error localizedDescription];
        
		NSData* ResumeData = Error ? [Error.userInfo objectForKey:NSURLSessionDownloadTaskResumeData] : nullptr;
		const bool bHasResumeData = (ResumeData && ([ResumeData  length] > 0));

		NSNumber* CancelledReasonKey = [Error.userInfo objectForKey:NSURLErrorBackgroundTaskCancelledReasonKey];
		int CancelledReasonInt = (nullptr != CancelledReasonKey) ? [CancelledReasonKey intValue] : -1;

		FString DebugRetryOverrideReason;

		if (bDidCompleteWithError)
		{
            FRWScopeLock ScopeLock(URLToRequestMapLock, SLT_ReadOnly);
            FBackgroundHttpURLMappedRequestPtr* WeakRequestInMap = URLToRequestMap.Find(TaskURL);
            FAppleBackgroundHttpRequestPtr FoundRequest = (nullptr != WeakRequestInMap) ? WeakRequestInMap->Pin() : nullptr;
            const bool bDidFindValidRequest = FoundRequest.IsValid();
         
            //by default increase error count. Special cases below will overwrite this
            bool bShouldRetryIncreaseRetryCount = true;
            
            //If we don't have internet, we don't want to move through our CDNs, but rather chain recreate download tasks until we regain internet
            if ([Error code] == NSURLErrorNotConnectedToInternet)
            {
                bShouldRetryIncreaseRetryCount = false;
                DebugRetryOverrideReason = TEXT("Not Connected To Internet");
            }
            
            UE_LOG(LogBackgroundHttpManager, Display, TEXT("DidCompleteWithError for Task. -- URL:%s | bDidFindVaildRequest:%d | bDidCompleteWithError:%d | ErrorCode:%d | bHasResumeData:%d | CancelledReasonKey:%d | RetryOverrideReason:%s | bShouldRetryIncreaseRetryCount:%d | ErrorDescription:%s"), *TaskURL, (int)bDidFindValidRequest, (int)bDidCompleteWithError, ErrorCode, (int)bHasResumeData, CancelledReasonInt, *DebugRetryOverrideReason, (int)bShouldRetryIncreaseRetryCount, *ErrorDescription);
            
			if (bDidFindValidRequest)
			{
                RetryRequest(FoundRequest, bShouldRetryIncreaseRetryCount, true, ResumeData);
			}
			else
			{
				//This can legitimately happen in the case of UnAssociatedTasks completing, so don't error
				UE_LOG(LogBackgroundHttpManager, Display, TEXT("No Request Found for Errored Task -- TaskURL:%s"), *TaskURL);
			}
		}
	}
}

void FApplePlatformBackgroundHttpManager::OnSession_SessionDidFinishAllEvents(NSURLSession* Session)
{
	//@TODO: TRoss, For now not using this. Will be using this to track how long we take in BG Handling BG Downloads in analytics
    UE_LOG(LogBackgroundHttpManager, Verbose, TEXT("NSURLSession done sending background events"));
}

bool FApplePlatformBackgroundHttpManager::Tick(float DeltaTime)
{
    TickRequests(DeltaTime);
    TickTasks(DeltaTime);
    TickUnassociatedTasks(DeltaTime);
    
    //Always keep ticking
    return true;
}

void FApplePlatformBackgroundHttpManager::TickRequests(float DeltaTime)
{
    //First lets go through all our Requests to see if we need to complete or recreate any requests
    {
        FRWScopeLock ScopeLock(ActiveRequestLock, SLT_Write);
        for (FBackgroundHttpRequestPtr& Request : ActiveRequests)
        {
            FAppleBackgroundHttpRequestPtr AppleRequest = StaticCastSharedPtr<FApplePlatformBackgroundHttpRequest>(Request);
            if (ensureAlwaysMsgf(AppleRequest.IsValid(), TEXT("Invalid Request Pointer in ActiveRequests list!")))
            {
                const bool bIsTaskActive = AppleRequest->IsUnderlyingTaskActive();
                const bool bIsTaskPaused = AppleRequest->IsUnderlyingTaskPaused();
                const bool bIsTaskComplete = AppleRequest->IsTaskComplete();
                const bool bWasStartedInBG = FPlatformAtomics::AtomicRead(&(AppleRequest->bWasTaskStartedInBG));
                
                UE_LOG(LogBackgroundHttpManager, VeryVerbose, TEXT("Checking Status of Request on Tick -- RequestID::%s | bIsTaskComplete:%d | bWasStartedInBG:%d"), *(AppleRequest->GetRequestID()), (int)bIsTaskComplete, (int)bWasStartedInBG);
                
                if (bIsTaskComplete)
                {
                    UE_LOG(LogBackgroundHttpManager, Display, TEXT("Calling FinishRequest On -- RequestID::%s | bIsTaskComplete:%d | bWasStartedInBG:%d"), *(AppleRequest->GetRequestID()), (int)bIsTaskComplete, (int)bWasStartedInBG);
                    FinishRequest(AppleRequest);
                }
                else if (bWasStartedInBG)
                {
                    UE_LOG(LogBackgroundHttpManager, Display, TEXT("Cancelling Request Created In BG To Re-Create In FG -- RequestID:%s"), *(AppleRequest->GetRequestID()));
                    
                    //Just cancel the task. This will lead to it getting a callback to OnTask_DidCompleteWithError where we will re-create it
                    //We want to recreate any task spun up in the background as it will not respect our session settings if created in BG.
                    AppleRequest->CancelActiveTask();
                }
                else if (bIsTaskActive && !bIsTaskPaused)
                {
                    const bool bShouldTimeOut = AppleRequest->TickTimeOutTimer(DeltaTime);
                    if (bShouldTimeOut)
                    {
                        UE_LOG(LogBackgroundHttpManager, Display, TEXT("Timing out Request Due To Lack of Server Response -- RequestID:%s"), *(AppleRequest->GetRequestID()));
                        
                        //Just cancel the task and let the OnTask_DidCompleteWithError callback handle retrying it if appropriate.
                        AppleRequest->CancelActiveTask();
                    }
                }
            }
        }
    }
    
    //Now that we have gone through and finished all the requests, go ahead and delete any pending removes
    DeletePendingRemoveRequests();
}

void FApplePlatformBackgroundHttpManager::TickTasks(float DeltaTime)
{
    //Go through all our session's tasks and unpause as many tasks as our platform's limit allows
    
    NSURLSession* BackgroundDownloadSession = FBackgroundURLSessionHandler::GetBackgroundSession();
    if (nullptr != BackgroundDownloadSession)
    {
        //Make sure we only queue up 1 tick's worth of task parsing at a time as this is an async callback
        const bool IsIterateAlreadyQueued = FPlatformAtomics::InterlockedExchange(&bIsIteratingThroughSessionTasks, true);
        if (!IsIterateAlreadyQueued)
        {
            [BackgroundDownloadSession getAllTasksWithCompletionHandler:^(NSArray<__kindof NSURLSessionTask*> *tasks)
             {
                 //Check to make sure we have room for more tasks to be active first
                 int CurrentCount = FPlatformAtomics::AtomicRead(&NumCurrentlyActiveRequests);
                 if (CurrentCount < FPlatformBackgroundHttp::GetPlatformMaxActiveDownloads())
                 {
                     //Go through our tasks and try and activate as many as possible
                     for (NSURLSessionTask* task in tasks)
                     {
                         //Only resume state if the task is suspended, not intersted in already active, canceling, or completed tasks here
                         if ([task state] == NSURLSessionTaskStateSuspended)
                         {
                             //Check to make sure we didn't have another task increment our count over the limit since we checked above
                             int NewRequestCount = FPlatformAtomics::InterlockedIncrement(&NumCurrentlyActiveRequests);
                             if (NewRequestCount <= FPlatformBackgroundHttp::GetPlatformMaxActiveDownloads())
                             {
                                 FString TaskURL = [[[task currentRequest] URL] absoluteString];
                                 
                                 //Try and find Request in map that matches this Task
                                 {
                                     FRWScopeLock ScopeLock(URLToRequestMapLock, SLT_ReadOnly);
                                     FBackgroundHttpURLMappedRequestPtr* WeakRequestInMap = URLToRequestMap.Find(TaskURL);
                                     FAppleBackgroundHttpRequestPtr FoundRequest = (nullptr != WeakRequestInMap) ? WeakRequestInMap->Pin() : nullptr;
                                     
                                     const bool bIsPaused = FoundRequest.IsValid() ? FoundRequest->IsUnderlyingTaskPaused() : false;
                                     
                                     if (FoundRequest.IsValid())
                                     {
                                         UE_LOG(LogBackgroundHttpManager, Display, TEXT("Activating Task For Requets -- RequestID:%s | TaskURL:%s | CurrentlyActiveRequests:%d"), *(FoundRequest->GetRequestID()), *TaskURL, NewRequestCount);
                                         FoundRequest->ActivateUnderlyingTask();
                                     }
                                     else
                                     {
                                         UE_LOG(LogBackgroundHttpManager, Display, TEXT("Skipping Activating Task as there is no associated Request or Request is paused. Once a Request associates with this task, it can then be activated. -- TaskURL:%s | bIsPaused:%d"), *TaskURL, (int)bIsPaused);
                                         
                                         //Don't activate and remove our increment from above because something put us over the limit before we resumed
                                         FPlatformAtomics::InterlockedDecrement(&NumCurrentlyActiveRequests);
                                     }
                                 }
                             }
                             else
                             {
                                 FString TaskURL = [[[task currentRequest] URL] absoluteString];
                                 UE_LOG(LogBackgroundHttpManager, Log, TEXT("Task failed to activate as we passed the platform max from another task before we could resume. Task -- TaskURL:%s | CurrentlyActiveRequests:%d"), *TaskURL, NewRequestCount);
                                 
                                 //Don't activate and remove our increment from above because something put us over the limit before we resumed
                                 FPlatformAtomics::InterlockedDecrement(&NumCurrentlyActiveRequests);
                                 break;
                             }
                             
                             //We now have enough requests queued up that we can stop looking for more
                             if (NewRequestCount >= FPlatformBackgroundHttp::GetPlatformMaxActiveDownloads())
                             {
                                 break;
                             }
                         }
                     }
                 }
                 
                 //If this wasn't true when we exited this async callback, we have a logic error somewhere and have queued this async callback twice. Should always be true as this async callback ends
                 //Also setting this to false so that future ticks will call this callback again
                 const bool bWasLogicError = FPlatformAtomics::InterlockedExchange(&bIsIteratingThroughSessionTasks, false);
                 ensureAlwaysMsgf(bWasLogicError, TEXT("Leaving Tick's getAllTasksWithCompletionHandler and bIsIteratingThroughSessionTasks was false before we finished. Something else has set bIsIteratingThroughSessionTasks during our callback!"));
             }];
        }
    }
}

void FApplePlatformBackgroundHttpManager::TickUnassociatedTasks(float DeltaTime)
{
    //If we don't have anything queued, lets resume any un-associated tasks
    int CurrentCount = FPlatformAtomics::AtomicRead(&NumCurrentlyActiveRequests);
    if (CurrentCount == 0)
    {
        UnpauseAllUnassociatedTasks();
    }
    else
    {
        //we have something queued, lets pause unassociated tasks
        PauseAllUnassociatedTasks();
    }
}
