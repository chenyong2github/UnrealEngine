// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Math/NumericLimits.h"
#include "Templates/Function.h"

#define UE_API CORE_API

namespace UE
{

struct FParkingLotWaitState final
{
	/** Did this thread wait? True only if CanWait returned true. */
	bool bDidWait = false;
	/** Did this wake from a wait? True only if a Wake call woke the thread, false for timeouts. */
	bool bDidWake = false;
	/** Optional value that was provided by the callback in WakeOne. */
	uint64 WakeToken = 0;
};

struct FParkingLotWakeState final
{
	/** Did a thread wake up? */
	bool bDidWake = false;
	/** Does the queue have another thread waiting? */
	bool bHasWaitingThreads = false;
};

/**
 * A global table of queues of waiting threads keyed by memory address.
 */
class FParkingLot final
{
public:
	/**
	 * Queue the calling thread to wait if CanWait returns true. BeforeWait is only called if CanWait returns true.
	 *
	 * @param Address      Address to use as the key for the queue. The same address is used to wake the thread.
	 * @param CanWait      Function called while the queue is locked. A return of false cancels the wait.
	 * @param BeforeWait   Function called after the queue is unlocked and before the thread waits.
	 */
	template <typename CanWaitType, typename BeforeWaitType>
	inline static FParkingLotWaitState Wait(const void* Address, CanWaitType&& CanWait, BeforeWaitType&& BeforeWait)
	{
		return PrivateWaitFor(Address, Forward<CanWaitType>(CanWait), Forward<BeforeWaitType>(BeforeWait), MAX_uint32);
	}

	/**
	 * Queue the calling thread to wait if CanWait returns true. BeforeWait is only called if CanWait returns true.
	 *
	 * @param Address      Address to use as the key for the queue. The same address is used to wake the thread.
	 * @param CanWait      Function called while the queue is locked. A return of false cancels the wait.
	 * @param BeforeWait   Function called after the queue is unlocked and before the thread waits.
	 * @param WaitMs       Minimum wait time after which waiting is automatically canceled and the thread wakes.
	 */
	template <typename CanWaitType, typename BeforeWaitType>
	inline static FParkingLotWaitState WaitFor(const void* Address, CanWaitType&& CanWait, BeforeWaitType&& BeforeWait, uint32 WaitMs)
	{
		return PrivateWaitFor(Address, Forward<CanWaitType>(CanWait), Forward<BeforeWaitType>(BeforeWait), WaitMs);
	}

	/**
	 * Wake one thread from the queue of threads waiting on the address.
	 *
	 * @param Address       Address to use as the key for the queue. Must match the address used in Wait.
	 * @param OnWakeState   Function called while the queue is locked. Receives the wake state. Returns WakeToken.
	 */
	template <typename OnWakeStateType>
	inline static void WakeOne(const void* Address, OnWakeStateType&& OnWakeState)
	{
		return PrivateWakeOne(Address, Forward<OnWakeStateType>(OnWakeState));
	}

	/**
	 * Wake one thread from the queue of threads waiting on the address.
	 *
	 * @param Address   Address to use as the key for the queue. Must match the address used in Wait.
	 * @return The wake state, which includes whether a thread woke up and whether there are more queued.
	 */
	UE_API static FParkingLotWakeState WakeOne(const void* Address);

	/**
	 * Wake up to WakeCount threads from the queue of threads waiting on the address.
	 *
	 * @param Address     Address to use as the key for the queue. Must match the address used in Wait.
	 * @param WakeCount   The maximum number of threads to wake.
	 * @return The number of threads that this call woke up.
	 */
	UE_API static uint32 WakeMultiple(const void* Address, uint32 WakeCount);

	/**
	 * Wake all threads from the queue of threads waiting on the address.
	 *
	 * @param Address   Address to use as the key for the queue. Must match the address used in Wait.
	 */
	UE_API static void WakeAll(const void* Address);

private:
	UE_API static FParkingLotWaitState PrivateWaitFor(const void* Address, TFunctionRef<bool()> CanWait, TFunctionRef<void()> BeforeWait, uint32 WaitMs);
	UE_API static void PrivateWakeOne(const void* Address, TFunctionRef<uint64(FParkingLotWakeState)> OnWakeState);

	FParkingLot() = delete;
	FParkingLot(const FParkingLot&) = delete;
};

} // UE

#undef UE_API
