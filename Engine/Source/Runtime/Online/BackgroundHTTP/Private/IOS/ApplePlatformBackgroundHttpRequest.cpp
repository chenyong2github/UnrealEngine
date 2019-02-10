// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IOS/ApplePlatformBackgroundHttpRequest.h"
#include "IOS/ApplePlatformBackgroundHttpManager.h"
#include "Interfaces/IBackgroundHttpResponse.h"
#include "BackgroundHttpModule.h"

FApplePlatformBackgroundHttpRequest::FApplePlatformBackgroundHttpRequest()
    : CompletedTempDownloadLocation()
    , ActiveTimeOutTimer(30.f)
    , RetryCount(0)
	, ResumeDataRetryCount(0)
    , FirstTask(nullptr)
    , bIsTaskActive(false)
    , bIsTaskPaused(false)
    , bIsCompleted(false)
    , bIsFailed(false)
    , bWasTaskStartedInBG(false)
    , bHasAlreadyFinishedRequest(false)
    , DownloadProgress(0)
{    
}

void FApplePlatformBackgroundHttpRequest::CompleteWithExistingResponseData(FBackgroundHttpResponsePtr BackgroundResponse)
{
    if (ensureAlwaysMsgf(BackgroundResponse.IsValid(), TEXT("Call to CompleteWithExistingResponseData with an invalid response!")))
    {
        FBackgroundHttpRequestImpl::CompleteWithExistingResponseData(BackgroundResponse);
        CompleteRequest_Internal(true, BackgroundResponse->GetTempContentFilePath());
    }
}

void FApplePlatformBackgroundHttpRequest::SetRequestAsSuccess(const FString& CompletedTempDownloadLocationIn)
{
    CompleteRequest_Internal(true, CompletedTempDownloadLocationIn);
}

void FApplePlatformBackgroundHttpRequest::SetRequestAsFailed()
{
    CompleteRequest_Internal(false, FString());
}

void FApplePlatformBackgroundHttpRequest::CompleteRequest_Internal(bool bWasRequestSuccess, const FString& CompletedTempDownloadLocationIn)
{
    UE_LOG(LogBackgroundHttpRequest, Display, TEXT("Marking Request Complete -- RequestID:%s | bWasRequestSuccess:%d | CompletedTempDownloadLocation:%s"), *GetRequestID(), (int)bWasRequestSuccess, *CompletedTempDownloadLocationIn);
    
    FPlatformAtomics::InterlockedExchange(&bIsTaskActive, false);
	FPlatformAtomics::InterlockedExchange(&bIsCompleted, true);
	FPlatformAtomics::InterlockedExchange(&bIsFailed, !bWasRequestSuccess);
    
    if (!CompletedTempDownloadLocationIn.IsEmpty())
    {
        CompletedTempDownloadLocation = CompletedTempDownloadLocationIn;
    }
    
	NotifyNotificationObjectOfComplete(bWasRequestSuccess);
}

const FString& FApplePlatformBackgroundHttpRequest::GetURLForRetry(bool bShouldIncrementRetryCountFirst)
{
	const int NewRetryCount = bShouldIncrementRetryCountFirst ? RetryCount.Increment() : RetryCount.GetValue();
	
	//If we are out of Retries, just send an empty string
	if (NewRetryCount > NumberOfTotalRetries)
	{
		UE_LOG(LogBackgroundHttpRequest, Display, TEXT("GetURLForRetry is out of Retries for Request -- RequestID:%s"), *GetRequestID());
        
        static FString EmptyResponse = TEXT("");
        return EmptyResponse;
	}
	//Still have remaining retries
	else
	{
		const int URLIndex = NewRetryCount % URLList.Num();
		const FString& URLToReturn = URLList[URLIndex];

		UE_LOG(LogBackgroundHttpRequest, Display, TEXT("GetURLForRetry found valid URL for current retry -- RequestID:%s | NewRetryCount:%d | URLToReturn:%s"), *GetRequestID(), NewRetryCount, *URLToReturn);
        return URLToReturn;
    }
}

void FApplePlatformBackgroundHttpRequest::ResetProgressTracking()
{
    FPlatformAtomics::InterlockedExchange(&DownloadProgress, 0);
}

void FApplePlatformBackgroundHttpRequest::ActivateUnderlyingTask()
{
    volatile FTaskNode* ActiveTaskNode = FirstTask;
    if (ensureAlwaysMsgf((nullptr != ActiveTaskNode), TEXT("Call to ActivateUnderlyingTask with an invalid node! Need to create underlying task(and node) before activating!")))
    {
        NSURLSessionTask* UnderlyingTask = ActiveTaskNode->OurTask;
        if (ensureAlwaysMsgf((nullptr != UnderlyingTask), TEXT("Call to ActivateUnderlyingTask with an invalid task! Need to create underlying task before activating!")))
        {
            FString TaskURL = [[[UnderlyingTask currentRequest] URL] absoluteString];
            UE_LOG(LogBackgroundHttpRequest, Display, TEXT("Activating Task for Request -- RequestID:%s | TaskURL:%s"), *GetRequestID(), *TaskURL);
        
            FPlatformAtomics::InterlockedExchange(&bIsTaskActive, true);
            FPlatformAtomics::InterlockedExchange(&bIsTaskPaused, false);

            [UnderlyingTask resume];
            
            ResetTimeOutTimer();
            ResetProgressTracking();
        }
    }
}

void FApplePlatformBackgroundHttpRequest::PauseUnderlyingTask()
{
    volatile FTaskNode* ActiveTaskNode = FirstTask;
    if (ensureAlwaysMsgf((nullptr != ActiveTaskNode), TEXT("Call to PauseUnderlyingTask with an invalid node! Need to create underlying task(and node) before trying to pause!")))
    {
        NSURLSessionTask* UnderlyingTask = ActiveTaskNode->OurTask;
        if (ensureAlwaysMsgf((nullptr != UnderlyingTask), TEXT("Call to PauseUnderlyingTask with an invalid task! Need to create underlying task before trying to pause!")))
        {
            FString TaskURL = [[[UnderlyingTask currentRequest] URL] absoluteString];
            UE_LOG(LogBackgroundHttpRequest, Display, TEXT("Pausing Task for Request -- RequestID:%s | TaskURL:%s"), *GetRequestID(), *TaskURL);
            
            FPlatformAtomics::InterlockedExchange(&bIsTaskActive, false);
            FPlatformAtomics::InterlockedExchange(&bIsTaskPaused, true);
            [UnderlyingTask suspend];
            
            ResetTimeOutTimer();
            ResetProgressTracking();
        }
    }
}

bool FApplePlatformBackgroundHttpRequest::IsUnderlyingTaskActive()
{
    return FPlatformAtomics::AtomicRead(&bIsTaskActive);
}

bool FApplePlatformBackgroundHttpRequest::IsUnderlyingTaskPaused()
{
    return FPlatformAtomics::AtomicRead(&bIsTaskPaused);
}

bool FApplePlatformBackgroundHttpRequest::TickTimeOutTimer(float DeltaTime)
{
    ActiveTimeOutTimer -= DeltaTime;
    return (ActiveTimeOutTimer <= 0.f) ? true : false;
}

void FApplePlatformBackgroundHttpRequest::ResetTimeOutTimer()
{
    ActiveTimeOutTimer = FApplePlatformBackgroundHttpManager::ActiveTimeOutSetting;
}

void FApplePlatformBackgroundHttpRequest::AssociateWithTask(NSURLSessionTask* ExistingTask)
{
	if (ensureAlwaysMsgf((nullptr != ExistingTask), TEXT("Call to AssociateWithTask with an invalid Task! RequestID:%s"), *GetRequestID()))
	{
		volatile FTaskNode* NewNode = new FTaskNode();
		NewNode->OurTask = ExistingTask;
		
		//Add a count to our task's reference list so it doesn't get deleted while in our Request's task list
		[ExistingTask retain];

        //Swap our new node and the first one in the list
		NewNode->NextNode = (FTaskNode*)FPlatformAtomics::InterlockedExchangePtr((void**)(&FirstTask), (void*)NewNode);

		FString TaskURL = [[[ExistingTask currentRequest] URL] absoluteString];
		UE_LOG(LogBackgroundHttpRequest, Display, TEXT("Associated Request With New Task -- RequestID:%s | TaskURL:%s"), *GetRequestID(), *TaskURL);
        
        ResetTimeOutTimer();
        ResetProgressTracking();
	}
}

void FApplePlatformBackgroundHttpRequest::PauseRequest()
{
    PauseUnderlyingTask();
}

void FApplePlatformBackgroundHttpRequest::ResumeRequest()
{
    ActivateUnderlyingTask();
}

void FApplePlatformBackgroundHttpRequest::CancelActiveTask()
{
    volatile FTaskNode* TaskNodeWeAreCancelling = FirstTask;
	if (nullptr != TaskNodeWeAreCancelling)
	{
        if (nullptr != TaskNodeWeAreCancelling->OurTask)
        {
            FString TaskURL = [[[TaskNodeWeAreCancelling->OurTask currentRequest] URL] absoluteString];
            UE_LOG(LogBackgroundHttpRequest, Display, TEXT("Cancelling Task -- RequestID:%s | TaskURL:%s"), *GetRequestID(), *TaskURL);

            [TaskNodeWeAreCancelling->OurTask cancel];
        }
	}
}

void FApplePlatformBackgroundHttpRequest::UpdateDownloadProgress(int64_t TotalDownloaded,int64_t DownloadedSinceLastUpdate)
{
    UE_LOG(LogBackgroundHttpRequest, VeryVerbose, TEXT("Request Update Progress -- RequestID:%s | OldProgress:%lld | NewProgress:%lld | ProgressSinceLastUpdate:%lld"), *GetRequestID(), DownloadProgress, TotalDownloaded, DownloadedSinceLastUpdate);
	
    FPlatformAtomics::AtomicStore(&DownloadProgress, TotalDownloaded);
    ResetTimeOutTimer();
    
    OnProgressUpdated().ExecuteIfBound(SharedThis(this), TotalDownloaded, DownloadedSinceLastUpdate);
}

bool FApplePlatformBackgroundHttpRequest::IsTaskComplete() const
{
    const bool bDidRequestFail = FPlatformAtomics::AtomicRead(&bIsFailed);
    const bool bDidRequestComplete = FPlatformAtomics::AtomicRead(&bIsCompleted);
    return (bDidRequestFail || bDidRequestComplete);
}
