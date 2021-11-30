// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Misc/ScopeRWLock.h"

namespace UE::Online {

/**
 * A net id registry suitable for use with trivial immutable keys
 */
template<typename IdType, typename IdValueType, EOnlineServices SubsystemType>
class TOnlineBasicIdRegistry
{
public:
	using HandleType = TOnlineIdHandle<IdType>;

	static TOnlineBasicIdRegistry& Get();

	HandleType FindOrAddHandle(const IdValueType& IdValue)
	{
		HandleType Handle;
		// Take a read lock and check if we already have a handle
		{
			FReadScopeLock ReadLock(Lock);
			if (const uint32* FoundHandle = IdValueToHandleMap.Find(IdValue))
			{
				Handle.Handle = *FoundHandle;
			}
		}

		if (!Handle.IsValid())
		{
			// Take a write lock, check again if we already have a handle, or insert a new element.
			FWriteScopeLock WriteLock(Lock);
			if (const HandleType* FoundHandle = IdValueToHandleMap.Find(IdValue))
			{
				Handle.Handle = *FoundHandle;
			}

			if(!Handle.IsValid())
			{
				IdValues.Emplace(IdValue);
				Handle.Handle = HandleType(SubsystemType, IdValues.Num());
				IdValueToHandleMap.Emplace(IdValue, Handle.Handle);
			}
		}

		return Handle;
	}

	// Returns a copy as it's not thread safe to return a pointer/ref to an element of an array that can be relocated by another thread.
	IdValueType GetIdValue(const HandleType Handle) const
	{
		if (Handle.Type == SubsystemType
			&& Handle.IsValid()
			&& IdValues.IsValidIndex(Handle.Handle - 1))
		{
			FReadScopeLock ReadLock(Lock);
			return IdValues[Handle.Handle - 1];
		}
		return IdValueType();
	}

	IdValueType GetIdValueChecked(const HandleType Handle) const
	{
		check(Handle.Type == SubsystemType
			&& Handle.IsValid()
			&& IdValues.IsValidIndex(Handle.Handle - 1));
		return GetIdValue(Handle);
	}

private:
	mutable FRWLock Lock;

	TArray<IdValueType> IdValues;
	TMap<IdValueType, uint32> IdValueToHandleMap;
};

/* UE::Online */ }

