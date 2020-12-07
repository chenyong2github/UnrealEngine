// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "WorldPartition/WorldPartition.h"

#if WITH_EDITOR
template <typename Impl>
class TWorldPartitionHandleBase
{
	friend struct FWorldPartitionHandleHelpers;

public:
	TWorldPartitionHandleBase()
		: ActorDesc(nullptr)
	{}

	TWorldPartitionHandleBase(UWorldPartition* WorldPartition, const FGuid& ActorGuid)
	{
		if (TUniquePtr<FWorldPartitionActorDesc>** ActorDescPtr = WorldPartition->Actors.Find(ActorGuid))
		{
			ActorDesc = *ActorDescPtr;

			if (IsValid())
			{
				IncRefCount();
			}
		}
		else
		{
			ActorDesc = nullptr;
		}
	}

	~TWorldPartitionHandleBase()
	{
		if (IsValid())
		{
			DecRefCount();
		}
	}

	template <typename T>
	TWorldPartitionHandleBase(const TWorldPartitionHandleBase& Other)
	{
		*this = Other;
	}

	template <typename T>
	TWorldPartitionHandleBase& operator=(const T& Other)
	{
		ActorDesc = Other.ActorDesc;

		if (IsValid())
		{
			IncRefCount();
		}

		return *this;
	}

	template <typename T>
	TWorldPartitionHandleBase(T&& Other)
	{
		*this = Other;
	}

	template <typename T>
	TWorldPartitionHandleBase& operator=(T&& Other)
	{
		ActorDesc = Other.ActorDesc;

		if (IsValid())
		{
			IncRefCount();
			Other.DecRefCount();
			Other.ActorDesc = nullptr;
		}

		return *this;
	}

	inline FWorldPartitionActorDesc* operator->() const
	{
		return Get();
	}

	inline bool IsValid() const
	{
		return ActorDesc && ActorDesc->IsValid();
	}

	inline FWorldPartitionActorDesc* Get() const
	{
		return IsValid() ? ActorDesc->Get() : nullptr;
	}

protected:
	inline void IncRefCount()
	{
		Impl::IncRefCount(ActorDesc->Get());
	}

	inline void DecRefCount()
	{
		Impl::DecRefCount(ActorDesc->Get());
	}

	TUniquePtr<FWorldPartitionActorDesc>* ActorDesc;
};

struct FWorldPartitionHandleImpl
{
	static void IncRefCount(FWorldPartitionActorDesc* ActorDesc);
	static void DecRefCount(FWorldPartitionActorDesc* ActorDesc);
};

struct FWorldPartitionReferenceImpl
{
	static void IncRefCount(FWorldPartitionActorDesc* ActorDesc);
	static void DecRefCount(FWorldPartitionActorDesc* ActorDesc);
};

typedef TWorldPartitionHandleBase<FWorldPartitionHandleImpl> FWorldPartitionHandle;
typedef TWorldPartitionHandleBase<FWorldPartitionReferenceImpl> FWorldPartitionReference;

struct FWorldPartitionHandleHelpers
{
	static FWorldPartitionReference ConvertHandleToReference(const FWorldPartitionHandle& Handle);
	static FWorldPartitionHandle ConvertReferenceToHandle(const FWorldPartitionReference& Handle);
};
#endif