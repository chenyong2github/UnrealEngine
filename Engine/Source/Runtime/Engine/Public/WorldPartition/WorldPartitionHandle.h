// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

#if WITH_EDITOR
class UWorldPartition;
class FWorldPartitionActorDesc;

struct ENGINE_API FWorldPartitionSoftRefUtils
{
	static TUniquePtr<FWorldPartitionActorDesc>* GetActorDesc(UWorldPartition* WorldPartition, const FGuid& ActorGuid);
};

template <typename Impl>
class ENGINE_API TWorldPartitionHandle : protected Impl
{
	friend struct FWorldPartitionSoftRefHelpers;

public:
	FORCEINLINE TWorldPartitionHandle()
		: ActorDesc(nullptr)
	{}

	FORCEINLINE TWorldPartitionHandle(TUniquePtr<FWorldPartitionActorDesc>* InActorDesc)
	{
		ActorDesc = InActorDesc;

		if (IsValid())
		{
			IncRefCount();
		}
	}

	FORCEINLINE TWorldPartitionHandle(UWorldPartition* WorldPartition, const FGuid& ActorGuid)
	{
		ActorDesc = FWorldPartitionSoftRefUtils::GetActorDesc(WorldPartition, ActorGuid);

		if (IsValid())
		{
			IncRefCount();
		}
	}

	FORCEINLINE TWorldPartitionHandle(const TWorldPartitionHandle& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	FORCEINLINE TWorldPartitionHandle(TWorldPartitionHandle&& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	// Conversions
	template <typename T>
	FORCEINLINE TWorldPartitionHandle<Impl>(const TWorldPartitionHandle<T>& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	template <typename T>
	FORCEINLINE TWorldPartitionHandle<Impl>(TWorldPartitionHandle<T>&& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	FORCEINLINE ~TWorldPartitionHandle()
	{
		if (IsValid())
		{
			DecRefCount();
		}
	}

	FORCEINLINE TWorldPartitionHandle& operator=(const TWorldPartitionHandle& Other)
	{
		if ((void*)this == (void*)&Other)
		{
			return *this;
		}

		if (IsValid())
		{
			DecRefCount();
		}

		ActorDesc = Other.ActorDesc;

		if (IsValid())
		{
			IncRefCount();
		}

		return *this;
	}

	FORCEINLINE TWorldPartitionHandle<Impl>& operator=(TWorldPartitionHandle&& Other)
	{
		if ((void*)this == (void*)&Other)
		{
			return *this;
		}

		if (IsValid())
		{
			DecRefCount();
		}

		ActorDesc = Other.ActorDesc;

		if (IsValid())
		{
			IncRefCount();
			Other.DecRefCount();
			Other.ActorDesc = nullptr;
		}

		return *this;
	}

	// Conversions
	template <typename T>
	FORCEINLINE TWorldPartitionHandle<Impl>& operator=(const TWorldPartitionHandle<T>& Other)
	{
		if ((void*)this == (void*)&Other)
		{
			return *this;
		}

		if (IsValid())
		{
			DecRefCount();
		}

		ActorDesc = Other.ActorDesc;

		if (IsValid())
		{
			IncRefCount();
		}

		return *this;
	}

	template <typename T>
	FORCEINLINE TWorldPartitionHandle<Impl>& operator=(TWorldPartitionHandle<T>&& Other)
	{
		if ((void*)this == (void*)&Other)
		{
			return *this;
		}

		if (IsValid())
		{
			DecRefCount();
		}

		ActorDesc = Other.ActorDesc;

		if (IsValid())
		{
			IncRefCount();
			Other.DecRefCount();
			Other.ActorDesc = nullptr;
		}

		return *this;
	}

	FORCEINLINE FWorldPartitionActorDesc* operator->() const
	{
		return Get();
	}

	FORCEINLINE FWorldPartitionActorDesc* operator*() const
	{
		return Get();
	}

	FORCEINLINE bool IsValid() const
	{
		return ActorDesc && ActorDesc->IsValid();
	}

	FORCEINLINE bool IsLoaded() const
	{
		return IsValid() && (*ActorDesc)->GetActor();
	}

	FORCEINLINE FWorldPartitionActorDesc* Get() const
	{
		return IsValid() ? ActorDesc->Get() : nullptr;
	}
		
	friend FORCEINLINE uint32 GetTypeHash(const TWorldPartitionHandle<Impl>& HandleBase)
	{
		return ::PointerHash(HandleBase.ActorDesc);
	}

	FORCEINLINE bool operator==(const TWorldPartitionHandle& Other) const
	{
		return ActorDesc == Other.ActorDesc;
	}

	FORCEINLINE bool operator!=(const TWorldPartitionHandle& Other) const
	{
		return !(*this == Other);
	}

	// Conversions
	template <typename T>
	FORCEINLINE bool operator==(const TWorldPartitionHandle<T>& Other) const
	{
		return Get() == *Other;
	}

	template <typename T>
	FORCEINLINE bool operator!=(const TWorldPartitionHandle<T>& Other) const
	{
		return !(*this == Other);
	}

protected:
	FORCEINLINE void IncRefCount()
	{
		Impl::IncRefCount(ActorDesc->Get());
	}

	FORCEINLINE void DecRefCount()
	{
		Impl::DecRefCount(ActorDesc->Get());
	}

public:
	TUniquePtr<FWorldPartitionActorDesc>* ActorDesc;
};

struct ENGINE_API FWorldPartitionSoftRefImpl
{
	static void IncRefCount(FWorldPartitionActorDesc* ActorDesc);
	static void DecRefCount(FWorldPartitionActorDesc* ActorDesc);
};

struct ENGINE_API FWorldPartitionHardRefImpl
{
	static void IncRefCount(FWorldPartitionActorDesc* ActorDesc);
	static void DecRefCount(FWorldPartitionActorDesc* ActorDesc);
};

struct ENGINE_API FWorldPartitionPinRefImpl
{
	FWorldPartitionPinRefImpl() : bIsReference(false) {}
	void IncRefCount(FWorldPartitionActorDesc* ActorDesc);
	void DecRefCount(FWorldPartitionActorDesc* ActorDesc);
	bool bIsReference;
};

/**
 * FWorldPartitionSoftRef will increment/decrement the soft reference count on the actor descriptor.
 * This won't trigger any loading, but will prevent cleanup of the actor descriptor when destroying an
 * actor in the editor.
 */
typedef TWorldPartitionHandle<FWorldPartitionSoftRefImpl> FWorldPartitionSoftRef;

/**
 * FWorldPartitionHardRef will increment/decrement the hard reference count on the actor descriptor.
 * This will trigger actor loading/unloading when the hard reference counts gets to one/zero.
 */
typedef TWorldPartitionHandle<FWorldPartitionHardRefImpl> FWorldPartitionHardRef;

/**
 * FWorldPartitionPinRef will act as a softref when there's not hard reference count to the actor descriptor,
 * and like a hardref when there is. This is useful when you want to keep an actor loaded during special
 * operations, and don't trigger loading when the actor is not loaded. Mainly intended to be on stack, as a
 * scoped operation.
 */
typedef TWorldPartitionHandle<FWorldPartitionPinRefImpl> FWorldPartitionPinRef;
#endif