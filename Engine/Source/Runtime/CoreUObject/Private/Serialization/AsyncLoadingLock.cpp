// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/AsyncLoadingLock.h"

#include "Containers/Ticker.h"
#include "Serialization/AsyncPackageLoader.h"

DEFINE_LOG_CATEGORY(AsyncLoadingLock);

bool IsAsyncLoadingCoreUObjectInternal();
void SuspendAsyncLoadingInternal();
void ResumeAsyncLoadingInternal();

int32 FAsyncLoadingLock::SuspendCount = 0;
double FAsyncLoadingLock::SuspendStartTime = 0;
int32 FAsyncLoadingLock::NextLockId = 0;

FAsyncLoadingLock::FAsyncLoadingLock(const FString& Context)
	: Context(Context)
	, LockId(NextLockId++)
{
}

FAsyncLoadingLock::FAsyncLoadingLock(FString&& Context)
	: Context(MoveTemp(Context))
	, LockId(NextLockId++)
{
}

FAsyncLoadingLock::~FAsyncLoadingLock()
{
	if (State == LockState::Acquiring)
	{
		Abandon();
	}
	else if (State == LockState::Acquired)
	{
		Release();
	}
}

void FAsyncLoadingLock::Acquire(const FOnAsyncLoadingLockAcquired& OnLockAcquired)
{
	check(State == LockState::Released);
	check(IsInGameThread() && !IsInSlateThread());

	OnLockAcquiredDelegate = OnLockAcquired;
	check(OnLockAcquiredDelegate.IsBound());

	// Check each frame whether async loading has completed.
	LoadingCompleteCheckDelegateHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FAsyncLoadingLock::OnAsyncLoadingCheck));

	// Periodically warn when waiting for async loading to complete.
	WaitingWarnDelegateHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FAsyncLoadingLock::OnAsyncLoadingWarn), LockAcquireWarnIntervalSeconds);

	StartTime = FPlatformTime::Seconds();
	UE_LOG(AsyncLoadingLock, Verbose, TEXT("Context[%s:%d] Acquiring loading scoped lock."), *Context, LockId);

	State = LockState::Acquiring;
}

void FAsyncLoadingLock::Release()
{
	check(State == LockState::Acquired);
	check(IsInGameThread() && !IsInSlateThread());

	CleanupTickers();

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(AsyncLoadingLock, Verbose, TEXT("Context[%s:%d] Releasing async loading lock. TimeLockHeld: %f TotalTime: %f"),
		*Context, LockId, EndTime - LockAcquiredTime, EndTime - StartTime);

	if (--SuspendCount == 0)
	{
		UE_LOG(AsyncLoadingLock, Log, TEXT("Context[%s:%d] Resuming async loading after suspension of %f seconds."), *Context, LockId, EndTime - SuspendStartTime);
		ResumeAsyncLoadingInternal();
	}
	else
	{
		UE_LOG(AsyncLoadingLock, Verbose, TEXT("Context[%s:%d] Async loading suspension remains active."), *Context, LockId);
	}

	State = LockState::Released;
}

void FAsyncLoadingLock::Abandon()
{
	check(State == LockState::Acquiring);
	check(IsInGameThread() && !IsInSlateThread());

	CleanupTickers();

	const double EndTime = FPlatformTime::Seconds();

	UE_LOG(AsyncLoadingLock, Verbose, TEXT("Context[%s:%d] Abandoning async loading lock. TotalTime: %f"),
		*Context, LockId, EndTime - StartTime);

	State = LockState::Released;
}

void FAsyncLoadingLock::CleanupTickers()
{
	FTicker::GetCoreTicker().RemoveTicker(LoadingCompleteCheckDelegateHandle);
	FTicker::GetCoreTicker().RemoveTicker(WaitingWarnDelegateHandle);
	FTicker::GetCoreTicker().RemoveTicker(LockHeldWarnDelegateHandle);
}

bool FAsyncLoadingLock::OnAsyncLoadingCheck(float DeltaTime)
{
	// Abandon lock if delegate is no longer valid to be called.
	// Delegates bound to a UObject will fail the below check once they have been marked for delete.
	if (!OnLockAcquiredDelegate.IsBound())
	{
		Abandon();
		return false;
	}

	if (IsAsyncLoadingCoreUObjectInternal())
	{
		// Continue rescheduling the check.
		return true;
	}

	// Async loading completed, remove warning logger.
	FTicker::GetCoreTicker().RemoveTicker(WaitingWarnDelegateHandle);

	const double CurrentTimeSeconds = FPlatformTime::Seconds();
	UE_LOG(AsyncLoadingLock, Verbose, TEXT("Context[%s:%d] Lock acquired in %f seconds."), *Context, LockId, CurrentTimeSeconds - StartTime);

	// Mark lock as acquired so the suspension count will be decremented correctly.
	State = LockState::Acquired;
	LockAcquiredTime = CurrentTimeSeconds;

	// Async loading is now idle. Suspend async loads.
	if (SuspendCount++ == 0)
	{
		UE_LOG(AsyncLoadingLock, Log, TEXT("Context[%s:%d] Suspending async loading."), *Context, LockId);
		SuspendStartTime = CurrentTimeSeconds;
		SuspendAsyncLoadingInternal();
		UE_LOG(AsyncLoadingLock, Verbose, TEXT("Context[%s:%d] Async loading suspended in %f seconds."), *Context, LockId, FPlatformTime::Seconds() - SuspendStartTime);
	}
	else
	{
		UE_LOG(AsyncLoadingLock, Verbose, TEXT("Context[%s:%d] Async loading previously suspended."), *Context, LockId);
	}

	// Periodically warn when the lock has been held for a significant amount of time.
	LockHeldWarnDelegateHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FAsyncLoadingLock::OnLockHeldWarn), LockHeldWarnIntervalSeconds);

	// Signal to user that lock has been acquired.
	FOnAsyncLoadingLockAcquired OnLockAcquired = MoveTemp(OnLockAcquiredDelegate);
	OnLockAcquired.ExecuteIfBound();
	// do not access member variables below this point in case firing the delegate deleted this lock.

	// Unregister check.
	return false;
}

bool FAsyncLoadingLock::OnAsyncLoadingWarn(float DeltaTime)
{
	UE_LOG(AsyncLoadingLock, Warning, TEXT("Context[%s:%d] Waiting on async loading to complete. Total lock wait time: %f seconds."), *Context, LockId, FPlatformTime::Seconds() - StartTime);
	return true;
}

bool FAsyncLoadingLock::OnLockHeldWarn(float DeltaTime)
{
	UE_LOG(AsyncLoadingLock, Warning, TEXT("Context[%s:%d] Async loading has been suspended for %f seconds."), *Context, LockId, FPlatformTime::Seconds() - SuspendStartTime);
	return true;
}
