// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpManager.h"
#include "HttpModule.h"
#include "HAL/PlatformTime.h"
#include "HAL/PlatformProcess.h"
#include "Misc/ScopeLock.h"
#include "Http.h"
#include "Misc/Guid.h"
#include "Misc/Fork.h"

#include "HttpThread.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/CommandLine.h"

#include "Stats/Stats.h"
#include "Containers/BackgroundableTicker.h"

// FHttpManager

FCriticalSection FHttpManager::RequestLock;

FHttpManager::FHttpManager()
	: FTSTickerObjectBase(0.0f, FTSBackgroundableTicker::GetCoreTicker())
	, Thread(nullptr)
	, CorrelationIdMethod(FHttpManager::GetDefaultCorrelationIdMethod())
{
	bFlushing = false;
}

FHttpManager::~FHttpManager()
{
	if (Thread)
	{
		Thread->StopThread();
		delete Thread;
	}
}

void FHttpManager::Initialize()
{
	if (FPlatformHttp::UsesThreadedHttp())
	{
		Thread = CreateHttpThread();
		Thread->StartThread();
	}
}

void FHttpManager::SetCorrelationIdMethod(TFunction<FString()> InCorrelationIdMethod)
{
	check(InCorrelationIdMethod);
	CorrelationIdMethod = MoveTemp(InCorrelationIdMethod);
}

FString FHttpManager::CreateCorrelationId() const
{
	return CorrelationIdMethod();
}

bool FHttpManager::IsDomainAllowed(const FString& Url) const
{
#if !UE_BUILD_SHIPPING
#if !(UE_GAME || UE_SERVER)
	// Allowed domain filtering is opt-in in non-shipping non-game/server builds
	static const bool bForceUseAllowList = FParse::Param(FCommandLine::Get(), TEXT("EnableHttpDomainRestrictions"));
	if (!bForceUseAllowList)
	{
		return true;
	}
#else
	// The check is on by default but allow non-shipping game/server builds to disable the filtering
	static const bool bIgnoreAllowList = FParse::Param(FCommandLine::Get(), TEXT("DisableHttpDomainRestrictions"));
	if (bIgnoreAllowList)
	{
		return true;
	}
#endif
#endif // !UE_BUILD_SHIPPING

	// check to see if the Domain is allowed (either on the list or the list was empty)
	const TArray<FString>& AllowedDomains = FHttpModule::Get().GetAllowedDomains();
	if (AllowedDomains.Num() > 0)
	{
		const FString Domain = FPlatformHttp::GetUrlDomain(Url);
		for (const FString& AllowedDomain : AllowedDomains)
		{
			if (Domain.EndsWith(AllowedDomain))
			{
				return true;
			}
		}
		return false;
	}
	return true;
}

/*static*/
TFunction<FString()> FHttpManager::GetDefaultCorrelationIdMethod()
{
	return []{ return FGuid::NewGuid().ToString(); };
}

void FHttpManager::OnBeforeFork()
{
	Flush(false);
}

void FHttpManager::OnAfterFork()
{

}

void FHttpManager::OnEndFramePostFork()
{
	// nothing
}


void FHttpManager::UpdateConfigs()
{
	if (Thread)
	{
		Thread->UpdateConfigs();
	}
}

void FHttpManager::AddGameThreadTask(TFunction<void()>&& Task)
{
	if (Task)
	{
		GameThreadQueue.Enqueue(MoveTemp(Task));
	}
}

FHttpThread* FHttpManager::CreateHttpThread()
{
	return new FHttpThread();
}

void FHttpManager::Flush(bool bShutdown)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FHttpManager_Flush);
	bFlushing = true;

	FScopeLock ScopeLock(&RequestLock);
	// designates the amount of time we will wait during a flush before we try to cancel the request.
	// This MUST be strictly < FlushTimeHardLimitSeconds for the logic to work and actually cancel the request,
	// since we must Tick at least one time for the cancel to work.
	// Setting this to < 0 will disable the cancel, but FlushTimeHardLimitSeconds can still be used to stop waiting on requests.
	double FlushTimeSoftLimitSeconds = 2.0; // default to generous limit
	GConfig->GetDouble(TEXT("HTTP"), TEXT("FlushTimeSoftLimitSeconds"), FlushTimeSoftLimitSeconds, GEngineIni);

	// After we hit the soft time limit and cancel the requests, we wait some additional time for the canceled requests to go away.
	// if they don't go away in time, we will hit this "hard" time limit that will just stop waiting.
	// If we are shutting down, this is probably fine. If we are flushing for other reasons, 
	// this could indicate things lying around, and we'll put out some warning log messages to indicate this.
	// Setting this to < 0 will disable all time limits and the code will wait infinitely for all requests to complete.
	double FlushTimeHardLimitSeconds = 4.0; // Don't use -1. Waiting infinitely on HTTP requests is probably a bad idea.
	GConfig->GetDouble(TEXT("HTTP"), TEXT("FlushTimeHardLimitSeconds"), FlushTimeHardLimitSeconds, GEngineIni);

	// Instead of waiting for the soft time limit, we might want to always cancel requests IMMEDIATELY on flush,
	// usually due to platform requirements to go to sleep in a timely manner, etc.
	// In this case, set this bool to true and the code will immediately cancel the requests.
	// You must still set a hard time limit > 0 for this bool to work.
	bool bAlwaysCancelRequestsOnFlush = false; // Default to not immediately canceling
	GConfig->GetBool(TEXT("HTTP"), TEXT("bAlwaysCancelRequestsOnFlush"), bAlwaysCancelRequestsOnFlush, GEngineIni);

	// this specifies how long to sleep between calls to tick.
	// The smaller the value, the more quickly we may find out that all requests have completed, but the more work may
	// be done in the meantime.
	float SecondsToSleepForOutstandingRequests = 0.5f;
	GConfig->GetFloat(TEXT("HTTP"), TEXT("RequestCleanupDelaySec"), SecondsToSleepForOutstandingRequests, GEngineIni);
	if (bShutdown)
	{
		if (Requests.Num())
		{
			// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
			UE_CLOG(!IsRunningCommandlet(), LogHttp, Warning, TEXT("Http module shutting down, but needs to wait on %d outstanding Http requests:"), Requests.Num());
		}
		// Clear delegates since they may point to deleted instances
		for (TArray<FHttpRequestRef>::TIterator It(Requests); It; ++It)
		{
			FHttpRequestRef& Request = *It;
			Request->OnProcessRequestComplete().Unbind();
			Request->OnRequestProgress().Unbind();
			Request->OnHeaderReceived().Unbind();
			// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
			UE_CLOG(!IsRunningCommandlet(), LogHttp, Warning, TEXT("	verb=[%s] url=[%s] refs=[%d] status=%s"), *Request->GetVerb(), *Request->GetURL(), Request.GetSharedReferenceCount(), EHttpRequestStatus::ToString(Request->GetStatus()));
		}
	}

	// block until all active requests have completed
	double BeginWaitTime = FPlatformTime::Seconds();
	double LastTime = BeginWaitTime;
	double StallWarnTime = BeginWaitTime + 0.5;
	// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
	UE_CLOG(!IsRunningCommandlet(), LogHttp, Warning, TEXT("cleaning up %d outstanding Http requests."), Requests.Num());
	double AppTime = FPlatformTime::Seconds();
	while (Requests.Num() > 0 && (FlushTimeHardLimitSeconds < 0 || (AppTime - BeginWaitTime < FlushTimeHardLimitSeconds)))
	{
		SCOPED_ENTER_BACKGROUND_EVENT(STAT_FHttpManager_Flush_Iteration);
		AppTime = FPlatformTime::Seconds();
		//UE_LOG(LogHttp, Display, TEXT("Waiting for %0.2f seconds. Limit:%0.2f seconds"), (AppTime - BeginWaitTime), FlushTimeSoftLimitSeconds);
		if (bAlwaysCancelRequestsOnFlush || (bShutdown && FlushTimeSoftLimitSeconds > 0 && (AppTime - BeginWaitTime > FlushTimeSoftLimitSeconds)))
		{
			if (bAlwaysCancelRequestsOnFlush)
			{
				// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
				UE_CLOG(!IsRunningCommandlet(), LogHttp, Warning, TEXT("Immediately cancelling %d active HTTP requests:"), Requests.Num());
			}
			else
			{
				// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
				UE_CLOG(!IsRunningCommandlet(), LogHttp, Warning, TEXT("Canceling remaining %d HTTP requests after waiting %0.2f seconds:"), Requests.Num(), (AppTime - BeginWaitTime));
			}

			for (TArray<FHttpRequestRef>::TIterator It(Requests); It; ++It)
			{
				FHttpRequestRef& Request = *It;

				// if we are shutting down, list the outstanding requests that had to be canceled.
				// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
				UE_CLOG(!IsRunningCommandlet() && bShutdown, LogHttp, Warning, TEXT("	verb=[%s] url=[%s] refs=[%d] status=%s"), *Request->GetVerb(), *Request->GetURL(), Request.GetSharedReferenceCount(), EHttpRequestStatus::ToString(Request->GetStatus()));
				FScopedEnterBackgroundEvent(*Request->GetURL());
				Request->CancelRequest();
			}
		}
		FlushTick(AppTime - LastTime);
		LastTime = AppTime;
		if (Requests.Num() > 0)
		{
			if (Thread)
			{
				if( Thread->NeedsSingleThreadTick() )
				{
					if (AppTime >= StallWarnTime)
					{
						// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
						UE_CLOG(!IsRunningCommandlet(), LogHttp, Warning, TEXT("Ticking HTTPThread for %d outstanding Http requests."), Requests.Num());
						StallWarnTime = AppTime + 0.5;
					}
					Thread->Tick();
				}
				else
				{
					// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
					UE_CLOG(!IsRunningCommandlet(), LogHttp, Warning, TEXT("Sleeping %.3fs to wait for %d outstanding Http requests."), SecondsToSleepForOutstandingRequests, Requests.Num());
					FPlatformProcess::Sleep(SecondsToSleepForOutstandingRequests);
				}
			}
			else
			{
				check(!FPlatformHttp::UsesThreadedHttp());
			}
		}
		AppTime = FPlatformTime::Seconds();
	}

	// Don't emit these tracking logs in commandlet runs. Build system traps warnings during cook, and these are not truly fatal, but useful for tracking down shutdown issues.
	if (Requests.Num() > 0 && (FlushTimeHardLimitSeconds > 0 && (AppTime - BeginWaitTime > FlushTimeHardLimitSeconds)) && !IsRunningCommandlet())
	{
		UE_LOG(LogHttp, Warning, TEXT("HTTTManager::Flush exceeded hard limit %.3fs time  %.3fs. These requets are being abandoned without being flushed:"), FlushTimeHardLimitSeconds, AppTime - BeginWaitTime);
		for (TArray<FHttpRequestRef>::TIterator It(Requests); It; ++It)
		{
			FHttpRequestRef& Request = *It;
			// if we are shutting down, list the outstanding requests that are being abandoned without being canceled.
			UE_LOG(LogHttp, Warning, TEXT("	verb=[%s] url=[%s] refs=[%d] status=%s"), *Request->GetVerb(), *Request->GetURL(), Request.GetSharedReferenceCount(), EHttpRequestStatus::ToString(Request->GetStatus()));
		}
	}
	bFlushing = false;
}

bool FHttpManager::Tick(float DeltaSeconds)
{
    QUICK_SCOPE_CYCLE_COUNTER(STAT_FHttpManager_Tick);

	// Run GameThread tasks
	TFunction<void()> Task = nullptr;
	while (GameThreadQueue.Dequeue(Task))
	{
		check(Task);
		Task();
	}

	FScopeLock ScopeLock(&RequestLock);

	// Tick each active request
	for (TArray<FHttpRequestRef>::TIterator It(Requests); It; ++It)
	{
		FHttpRequestRef Request = *It;
		Request->Tick(DeltaSeconds);
	}

	if (Thread)
	{
		TArray<IHttpThreadedRequest*> CompletedThreadedRequests;
		Thread->GetCompletedRequests(CompletedThreadedRequests);

		// Finish and remove any completed requests
		for (IHttpThreadedRequest* CompletedRequest : CompletedThreadedRequests)
		{
			FHttpRequestRef CompletedRequestRef = CompletedRequest->AsShared();
			Requests.Remove(CompletedRequestRef);
			CompletedRequest->FinishRequest();
		}
	}
	// keep ticking
	return true;
}

void FHttpManager::FlushTick(float DeltaSeconds)
{
	Tick(DeltaSeconds);
}

void FHttpManager::AddRequest(const FHttpRequestRef& Request)
{
	FScopeLock ScopeLock(&RequestLock);
	check(!bFlushing);
	Requests.Add(Request);
}

void FHttpManager::RemoveRequest(const FHttpRequestRef& Request)
{
	FScopeLock ScopeLock(&RequestLock);

	Requests.Remove(Request);
}

void FHttpManager::AddThreadedRequest(const TSharedRef<IHttpThreadedRequest, ESPMode::ThreadSafe>& Request)
{
	check(!bFlushing);
	check(Thread);
	{
		FScopeLock ScopeLock(&RequestLock);
		Requests.Add(Request);
	}
	Thread->AddRequest(&Request.Get());
}

void FHttpManager::CancelThreadedRequest(const TSharedRef<IHttpThreadedRequest, ESPMode::ThreadSafe>& Request)
{
	check(Thread);
	Thread->CancelRequest(&Request.Get());
}

bool FHttpManager::IsValidRequest(const IHttpRequest* RequestPtr) const
{
	FScopeLock ScopeLock(&RequestLock);

	bool bResult = false;
	for (const FHttpRequestRef& Request : Requests)
	{
		if (&Request.Get() == RequestPtr)
		{
			bResult = true;
			break;
		}
	}

	return bResult;
}

void FHttpManager::DumpRequests(FOutputDevice& Ar) const
{
	FScopeLock ScopeLock(&RequestLock);

	Ar.Logf(TEXT("------- (%d) Http Requests"), Requests.Num());
	for (const FHttpRequestRef& Request : Requests)
	{
		Ar.Logf(TEXT("	verb=[%s] url=[%s] status=%s"),
			*Request->GetVerb(), *Request->GetURL(), EHttpRequestStatus::ToString(Request->GetStatus()));
	}
}

bool FHttpManager::SupportsDynamicProxy() const
{
	return false;
}
