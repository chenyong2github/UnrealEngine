// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/CoreDelegates.h"
#include "Misc/LazySingleton.h"

/** 
 * Class to wrap FCoreDelegates::FGetOnScreenMessagesDelegate for access from any thread. 
 * This avoids race conditions in registration/unregistration that would happen if using FCoreDelegates::FGetOnScreenMessagesDelegate directly from render thread.
 * Note that the Broadcast() still happens on game thread, so care needs to be taken with how data is accessed there.
 * If that becomes an issue we could change so that the proxy delegate Broadcasts on render thread and buffers to game thread.
 */
class FRendererOnScreenNotification
{
public:
	/** 
	 * Create or get singleton instance. 
	 * First call should be on game thread. After that any thread will do.
	 */
	static FRendererOnScreenNotification& Get()
	{
		return TLazySingleton<FRendererOnScreenNotification>::Get();
	}

	/** 
	 * Tear down singleton instance. 
	 * Must be called on game thread. 
	 */
	static void TearDown()
	{
		TLazySingleton<FRendererOnScreenNotification>::TearDown();
	}

	/** 
	 * Relay to AddLambda() of underlying delegate. 
	 * Thia takes a lock so that it can be called from any thread.
	 * The lambda will be called from the game thread!
	 */
	template<typename FunctorType, typename... VarTypes>
	FDelegateHandle AddLambda(FunctorType&& InFunctor, VarTypes... Vars)
	{
		FScopeLock Lock(&DelgateCS);
		return ProxyDelegate.AddLambda(MoveTemp(InFunctor), Vars...);
	}

	/** 
	 * Relay to Remove() of underlying delegate. 
	 * Thia takes a lock so that it can be called from any thread.
	 */
	bool Remove(FDelegateHandle InHandle)
	{
		FScopeLock Lock(&DelgateCS);
		return ProxyDelegate.Remove(InHandle);
	}

private:
	friend FLazySingleton;

	FRendererOnScreenNotification()
	{
		BaseDelegateHandle = FCoreDelegates::OnGetOnScreenMessages.AddLambda([this](FCoreDelegates::FSeverityMessageMap& OutMessages)
		{
			FScopeLock Lock(&DelgateCS);
			ProxyDelegate.Broadcast(OutMessages);
		});
	}

	~FRendererOnScreenNotification()
	{
		FCoreDelegates::OnGetOnScreenMessages.Remove(BaseDelegateHandle);
	}

private:
	FCriticalSection DelgateCS;
	FCoreDelegates::FGetOnScreenMessagesDelegate ProxyDelegate;
	FDelegateHandle BaseDelegateHandle;
};
