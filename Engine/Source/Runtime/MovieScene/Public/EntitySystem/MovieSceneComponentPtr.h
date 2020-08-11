// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/IsConst.h"
#include "HAL/CriticalSection.h"
#include "EntitySystem/MovieSceneEntitySystemTypes.h"

namespace UE
{
namespace MovieScene
{


template<typename ComponentType>
struct TComponentPtr
{
	static constexpr bool bReadOnly = TIsConst<ComponentType>::Value;

	TComponentPtr()
		: Header(nullptr)
		, ComponentPtr(nullptr)
		, SystemSerialNumber(0)
	{}

	explicit TComponentPtr(FComponentHeader& InHeader, uint16 EntityOffset, uint64 InSystemSerialNumber)
	{
		SystemSerialNumber = InSystemSerialNumber;
		Header = &InHeader;

		if (bReadOnly)
		{
			InHeader.ReadWriteLock.ReadLock();
		}
		else
		{
			InHeader.ReadWriteLock.WriteLock();
		}

		ComponentPtr = static_cast<ComponentType*>(InHeader.GetValuePtr(EntityOffset));
	}

	~TComponentPtr()
	{
		Release();
	}

	TComponentPtr(TComponentPtr&& RHS)
		: Header(RHS.Header)
		, ComponentPtr(RHS.ComponentPtr)
		, SystemSerialNumber(RHS.SystemSerialNumber)
	{
		RHS.ComponentPtr = nullptr;
		RHS.Header = nullptr;
	}
	TComponentPtr& operator=(TComponentPtr&& RHS)
	{
		Release();

		ComponentPtr  = RHS.ComponentPtr;
		Header = RHS.Header;
		SystemSerialNumber = RHS.SystemSerialNumber;

		RHS.ComponentPtr = nullptr;
		RHS.Header = nullptr;
		return *this;
	}

	TComponentPtr(const TComponentPtr&) = delete;
	void operator=(const TComponentPtr&) = delete;

	explicit operator bool()
	{
		return ComponentPtr != nullptr;
	}

	operator ComponentType*() const
	{
		return ComponentPtr;
	}

	ComponentType* operator->() const
	{
		return ComponentPtr;
	}

	ComponentType& operator*() const
	{
		checkSlow(ComponentPtr);
		return *ComponentPtr;
	}

private:

	void Release()
	{
		if (Header)
		{
			if (bReadOnly)
			{
				Header->ReadWriteLock.ReadUnlock();
			}
			else
			{
				Header->PostWriteComponents(SystemSerialNumber);
				Header->ReadWriteLock.WriteUnlock();
			}
		}
	}

	FComponentHeader* Header;
	ComponentType* ComponentPtr;
	uint64 SystemSerialNumber;
};


} // namespace UE
} // namespace MovieScene