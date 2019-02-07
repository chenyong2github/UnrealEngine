// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Interfaces/IBackgroundHttpRequest.h"
#include "Interfaces/IHttpRequest.h"

#include "BackgroundHttpRequestImpl.h"

/**
 * Contains implementation of Apple specific background http requests
 */
class BACKGROUNDHTTP_API FApplePlatformBackgroundHttpRequest 
	: public FBackgroundHttpRequestImpl
{
public:
	FApplePlatformBackgroundHttpRequest();
	virtual ~FApplePlatformBackgroundHttpRequest() {}

	virtual void CompleteWithExistingResponseData(FBackgroundHttpResponsePtr BackgroundResponse) override;
    virtual void PauseRequest() override;
    virtual void ResumeRequest() override;
    
    virtual bool IsTaskComplete() const;

    
private:
    //Super simple linked list with volatile pointers to next element
    struct FTaskNode
    {
        NSURLSessionTask* OurTask;
        volatile FTaskNode* NextNode;
    };
    
private:
    const FString& GetURLForRetry(bool bShouldIncrementRetryCountFirst);
    void AssociateWithTask(NSURLSessionTask* ExistingTask);
	void SetRequestAsSuccess(const FString& CompletedTempDownloadLocation);
    void SetRequestAsFailed();
    void CompleteRequest_Internal(bool bWasRequestSuccess, const FString& CompletedTempDownloadLocation);
	void CancelActiveTask();
	void UpdateDownloadProgress(int64_t TotalDownloaded, int64_t DownloadedSinceLastUpdate);
    void ResetProgressTracking();
    
    void ActivateUnderlyingTask();
    bool IsUnderlyingTaskActive();
    void PauseUnderlyingTask();
    bool IsUnderlyingTaskPaused();
    bool TickTimeOutTimer(float DeltaTime);
    void ResetTimeOutTimer();
    
    FString CompletedTempDownloadLocation;
    volatile float ActiveTimeOutTimer;
    
	FThreadSafeCounter RetryCount;
	FThreadSafeCounter ResumeDataRetryCount;

	volatile FTaskNode* FirstTask;

    volatile int32 bIsTaskActive;
    volatile int32 bIsTaskPaused;
	volatile int32 bIsCompleted;
	volatile int32 bIsFailed;
	volatile int32 bWasTaskStartedInBG;
    volatile int32 bHasAlreadyFinishedRequest;
	volatile int64 DownloadProgress;
    
    friend class FApplePlatformBackgroundHttpManager;
};

typedef TSharedPtr<class FApplePlatformBackgroundHttpRequest, ESPMode::ThreadSafe> FAppleBackgroundHttpRequestPtr;
