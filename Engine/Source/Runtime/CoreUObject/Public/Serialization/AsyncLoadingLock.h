// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Delegates/Delegate.h"
#include "Logging/LogMacros.h"

DECLARE_LOG_CATEGORY_EXTERN(AsyncLoadingLock, Log, All);

DECLARE_DELEGATE(FOnAsyncLoadingLockAcquired);

/**
 * Lock for suspending the async loading thread without hitching the game thread. To work properly the lock must be released
 * from within the lock acquired delegate. Calls to FlushLoading will fail while async loading is suspended.
 */
class COREUOBJECT_API FAsyncLoadingLock final : FNoncopyable
{
public:
	FAsyncLoadingLock(const FString& Context);
	FAsyncLoadingLock(FString&& Context);
	~FAsyncLoadingLock();

	void Acquire(const FOnAsyncLoadingLockAcquired& OnLockAcquired);
	void Release();

	int32 GetId() const { return LockId; }

private:
	FAsyncLoadingLock() = delete;

	void Abandon();
	void CleanupTickers();

	bool OnAsyncLoadingCheck(float DeltaTime);
	bool OnAsyncLoadingWarn(float DeltaTime);
	bool OnLockHeldWarn(float DeltaTime);

	enum class LockState : uint8
	{
		Acquiring,
		Acquired,
		Released,
	};

	/** How often to warn while waiting for async loading to complete. */
	static constexpr float LockAcquireWarnIntervalSeconds = 10.f;

	/** How often to warn while holding the async loading lock. */
	static constexpr float LockHeldWarnIntervalSeconds = 30.f;

	/** Context for logging */
	FString Context;

	/** Unique id for lock. */
	int32 LockId;

	/** Delegate to be fired on lock. */
	FOnAsyncLoadingLockAcquired OnLockAcquiredDelegate;

	/** Whether async loading was suspended */
	LockState State = LockState::Released;

	/** Record event timing for logging. */
	double StartTime = 0;
	double LockAcquiredTime = 0;

	/** Tick delegates. */
	FDelegateHandle LoadingCompleteCheckDelegateHandle;
	FDelegateHandle WaitingWarnDelegateHandle;
	FDelegateHandle LockHeldWarnDelegateHandle;

	/** Track the number of suspensions to preserve suspended state with multiple active FAsyncLoadingLock instances. */
	static int32 SuspendCount;
	static double SuspendStartTime;

	/** Increment lock ids so each instance has a unique id. */
	static int32 NextLockId;
};
