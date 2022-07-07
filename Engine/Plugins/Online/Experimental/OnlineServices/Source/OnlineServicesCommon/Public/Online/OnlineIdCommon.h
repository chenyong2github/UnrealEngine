// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/ScopeRWLock.h"

namespace UE::Online {

/**
 * A net id registry suitable for use with trivial immutable keys
 */
template<typename IdType, typename IdValueType, EOnlineServices OnlineServicesType>
class TOnlineBasicIdRegistry
{
public:
	virtual ~TOnlineBasicIdRegistry() = default;

	using HandleType = TOnlineIdHandle<IdType>;

	const HandleType InvalidHandle = HandleType(OnlineServicesType, 0);

	HandleType FindHandle(const IdValueType& IdValue) const
	{
		FReadScopeLock ReadLock(Lock);
		if (const HandleType* FoundHandle = IdValueToHandleMap.Find(IdValue))
		{
			return *FoundHandle;
		}
		return InvalidHandle;
	}

	HandleType FindOrAddHandle(const IdValueType& IdValue)
	{
		HandleType Handle = FindHandle(IdValue);
		if (!Handle.IsValid())
		{
			// Take a write lock, check again if we already have a handle, or insert a new element.
			FWriteScopeLock WriteLock(Lock);
			if (const HandleType* FoundHandle = IdValueToHandleMap.Find(IdValue))
			{
				Handle = *FoundHandle;
			}

			if(!Handle.IsValid())
			{
				IdValues.Emplace(IdValue);
				Handle = HandleType(OnlineServicesType, IdValues.Num());
				IdValueToHandleMap.Emplace(IdValue, Handle);
			}
		}
		return Handle;
	}

	// Returns a copy as it's not thread safe to return a pointer/ref to an element of an array that can be relocated by another thread.
	IdValueType FindIdValue(const HandleType& Handle) const
	{
		if (ValidateOnlineId(Handle))
		{
			FReadScopeLock ReadLock(Lock);
			if (IdValues.IsValidIndex(Handle.GetHandle() - 1))
			{
				return IdValues[Handle.GetHandle() -1];
			}
		}
		return IdValueType();
	}

	static inline bool ValidateOnlineId(const HandleType& Handle)
	{
		return ensure(Handle.GetOnlineServicesType() == OnlineServicesType) && Handle.IsValid();
	}

private:
	mutable FRWLock Lock;

	TArray<IdValueType> IdValues;
	TMap<IdValueType, HandleType> IdValueToHandleMap;
};

template<typename IdValueType, EOnlineServices OnlineServicesType>
using TOnlineBasicAccountIdRegistry = TOnlineBasicIdRegistry<OnlineIdHandleTags::FAccount, IdValueType, OnlineServicesType>;

template<typename IdValueType, EOnlineServices OnlineServicesType>
using TOnlineBasicSessionIdRegistry = TOnlineBasicIdRegistry<OnlineIdHandleTags::FSession, IdValueType, OnlineServicesType>;

/* UE::Online */ }

