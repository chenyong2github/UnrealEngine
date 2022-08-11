// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Online/CoreOnline.h"
#include "OnlineSubsystemTypes.h"
#include "Misc/ScopeRWLock.h"

namespace UE::Online {

/**
 * A net id registry suitable for use with OSS FUniqueNetIds
 */
template<typename IdType>
class TOnlineUniqueNetIdRegistry : public IOnlineIdRegistry<IdType>
{
public:
	TOnlineUniqueNetIdRegistry(EOnlineServices InOnlineServicesType)
		: OnlineServicesType(InOnlineServicesType)
	{
	}

	virtual ~TOnlineUniqueNetIdRegistry() {}

	TOnlineIdHandle<IdType> FindOrAddHandle(const FUniqueNetIdRef& IdValue)
	{
		TOnlineIdHandle<IdType> Handle;
		if (!ensure(IdValue->IsValid()))
		{
			return Handle;
		}

		// Take a read lock and check if we already have a handle
		{
			FReadScopeLock ReadLock(Lock);
			if (const uint32* FoundHandle = IdValueToHandleMap.Find(IdValue))
			{
				Handle = TOnlineIdHandle<IdType>(OnlineServicesType, *FoundHandle);
			}
		}

		if (!Handle.IsValid())
		{
			// Take a write lock, check again if we already have a handle, or insert a new element.
			FWriteScopeLock WriteLock(Lock);
			if (const uint32* FoundHandle = IdValueToHandleMap.Find(IdValue))
			{
				Handle = TOnlineIdHandle<IdType>(OnlineServicesType, *FoundHandle);
			}

			if (!Handle.IsValid())
			{
				IdValues.Emplace(IdValue);
				Handle = TOnlineIdHandle<IdType>(OnlineServicesType, IdValues.Num());
				IdValueToHandleMap.Emplace(IdValue, Handle.GetHandle());
			}
		}

		return Handle;
	}

	// Returns a copy as it's not thread safe to return a pointer/ref to an element of an array that can be relocated by another thread.
	FUniqueNetIdPtr GetIdValue(const TOnlineIdHandle<IdType> Handle) const
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

	FUniqueNetIdRef GetIdValueChecked(const TOnlineIdHandle<IdType> Handle) const
	{
		return GetIdValue(Handle).ToSharedRef();
	}

	bool IsHandleExpired(const FOnlineSessionIdHandle& InHandle) const
	{
		return GetIdValue(InHandle).IsValid();
	}

	// Begin IOnlineAccountIdRegistry
	virtual FString ToLogString(const TOnlineIdHandle<IdType>& Handle) const override
	{
		FUniqueNetIdPtr IdValue = GetIdValue(Handle);
		return IdValue ? IdValue->ToDebugString() : FString(TEXT("invalid_id"));
	}

	virtual TArray<uint8> ToReplicationData(const TOnlineIdHandle<IdType>& Handle) const override
	{
		return TArray<uint8>();
	}

	virtual TOnlineIdHandle<IdType> FromReplicationData(const TArray<uint8>& Handle) override
	{
		return TOnlineIdHandle<IdType>();
	}

	// End IOnlineAccountIdRegistry

private:
	mutable FRWLock Lock;

	EOnlineServices OnlineServicesType;
	TArray<FUniqueNetIdRef> IdValues;
	TUniqueNetIdMap<uint32> IdValueToHandleMap;
};

using FOnlineAccountIdRegistryOSSAdapter = TOnlineUniqueNetIdRegistry<OnlineIdHandleTags::FAccount>;

/* UE::Online */ }
