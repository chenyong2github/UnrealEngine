// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/CoreOnline.h"
#include "OnlineSubsystemTypes.h"
#include "Misc/ScopeRWLock.h"

namespace UE::Online {

/**
 * A net id registry suitable for use with OSS FUniqueNetIds
 */
class FOnlineUniqueNetIdRegistry : public IOnlineAccountIdRegistry
{
public:
	FOnlineUniqueNetIdRegistry(EOnlineServices InOnlineServicesType)
		: OnlineServicesType(InOnlineServicesType)
	{
	}

	virtual ~FOnlineUniqueNetIdRegistry() {}

	FOnlineAccountIdHandle FindOrAddHandle(const FUniqueNetIdRef& IdValue)
	{
		FOnlineAccountIdHandle Handle;
		if (!ensure(IdValue->IsValid()))
		{
			return Handle;
		}

		// Take a read lock and check if we already have a handle
		{
			FReadScopeLock ReadLock(Lock);
			if (const uint32* FoundHandle = IdValueToHandleMap.Find(IdValue))
			{
				Handle = FOnlineAccountIdHandle(OnlineServicesType, *FoundHandle);
			}
		}

		if (!Handle.IsValid())
		{
			// Take a write lock, check again if we already have a handle, or insert a new element.
			FWriteScopeLock WriteLock(Lock);
			if (const uint32* FoundHandle = IdValueToHandleMap.Find(IdValue))
			{
				Handle = FOnlineAccountIdHandle(OnlineServicesType, *FoundHandle);
			}

			if (!Handle.IsValid())
			{
				IdValues.Emplace(IdValue);
				Handle = FOnlineAccountIdHandle(OnlineServicesType, IdValues.Num());
				IdValueToHandleMap.Emplace(IdValue, Handle.GetHandle());
			}
		}

		return Handle;
	}

	// Returns a copy as it's not thread safe to return a pointer/ref to an element of an array that can be relocated by another thread.
	FUniqueNetIdPtr GetIdValue(const FOnlineAccountIdHandle Handle) const
	{
		if (Handle.GetOnlineServicesType() == OnlineServicesType && Handle.IsValid())
		{
			FReadScopeLock ReadLock(Lock);
			if (IdValues.IsValidIndex(Handle.GetHandle() - 1))
			{
				return IdValues[Handle.GetHandle() - 1];
			}
		}
		return FUniqueNetIdPtr();
	}

	FUniqueNetIdRef GetIdValueChecked(const FOnlineAccountIdHandle Handle) const
	{
		return GetIdValue(Handle).ToSharedRef();
	}

	// Begin IOnlineAccountIdRegistry
	virtual FString ToLogString(const FOnlineAccountIdHandle& Handle) const override
	{
		FUniqueNetIdPtr IdValue = GetIdValue(Handle);
		return IdValue ? IdValue->ToDebugString() : FString(TEXT("invalid_id"));
	}

	virtual TArray<uint8> ToReplicationData(const FOnlineAccountIdHandle& Handle) const
	{
		return TArray<uint8>();
	}

	virtual FOnlineAccountIdHandle FromReplicationData(const TArray<uint8>& Handle)
	{
		return FOnlineAccountIdHandle();
	}

	// End IOnlineAccountIdRegistry

private:
	mutable FRWLock Lock;

	EOnlineServices OnlineServicesType;
	TArray<FUniqueNetIdRef> IdValues;
	TUniqueNetIdMap<uint32> IdValueToHandleMap;
};

/* UE::Online */ }
