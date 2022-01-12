// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UObject/WeakObjectPtr.h"

#if WITH_EDITOR
class UActorDescContainer;
class FWorldPartitionActorDesc;

struct ENGINE_API FWorldPartitionHandleUtils
{
	static TUniquePtr<FWorldPartitionActorDesc>* GetActorDesc(UActorDescContainer* Container, const FGuid& ActorGuid);
	static UActorDescContainer* GetActorDescContainer(TUniquePtr<FWorldPartitionActorDesc>* ActorDesc);
	static bool IsActorDescLoaded(FWorldPartitionActorDesc* ActorDesc);
};

template <typename Impl>
class TWorldPartitionHandle
{
public:
	FORCEINLINE TWorldPartitionHandle()
		: ActorDesc(nullptr)
	{}

	FORCEINLINE TWorldPartitionHandle(TUniquePtr<FWorldPartitionActorDesc>* InActorDesc)
		: Container(FWorldPartitionHandleUtils::GetActorDescContainer(InActorDesc))
		, ActorDesc(InActorDesc)
	{
		if (IsValid())
		{
			IncRefCount();
		}
	}

	FORCEINLINE TWorldPartitionHandle(UActorDescContainer* Container, const FGuid& ActorGuid)
		: Container(Container)
		, ActorDesc(FWorldPartitionHandleUtils::GetActorDesc(Container, ActorGuid))
	{
		if (IsValid())
		{
			IncRefCount();
		}
	}

	FORCEINLINE TWorldPartitionHandle(const TWorldPartitionHandle& Other)
		: Container(nullptr)
		, ActorDesc(nullptr)
	{
		*this = Other;
	}

	FORCEINLINE TWorldPartitionHandle(TWorldPartitionHandle&& Other)
		: Container(nullptr)
		, ActorDesc(nullptr)
	{
		*this = MoveTemp(Other);
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
		*this = MoveTemp(Other);
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
		if (this != &Other)
		{
			if (IsValid())
			{
				DecRefCount();
			}

			Container = Other.Container;
			ActorDesc = Other.ActorDesc;

			if (IsValid())
			{
				IncRefCount();
			}
		}

		return *this;
	}

	FORCEINLINE TWorldPartitionHandle<Impl>& operator=(TWorldPartitionHandle&& Other)
	{
		if (this != &Other)
		{
			if (IsValid())
			{
				DecRefCount();
			}

			Container = Other.Container;
			ActorDesc = Other.ActorDesc;
			
			Other.Container = nullptr;
			Other.ActorDesc = nullptr;
		}

		return *this;
	}

	// Conversions
	template <typename T>
	FORCEINLINE TWorldPartitionHandle<Impl>& operator=(const TWorldPartitionHandle<T>& Other)
	{
		if (IsValid())
		{
			DecRefCount();
		}

		Container = Other.Container;
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
		if (IsValid())
		{
			DecRefCount();
		}

		Container = Other.Container;
		ActorDesc = Other.ActorDesc;

		if (IsValid())
		{
			IncRefCount();
			Other.DecRefCount();

			Other.Container = nullptr;
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
		return Container.IsValid() && ActorDesc && ActorDesc->IsValid();
	}

	FORCEINLINE bool IsLoaded() const
	{
		return IsValid() && FWorldPartitionHandleUtils::IsActorDescLoaded(ActorDesc->Get());
	}

	FORCEINLINE FWorldPartitionActorDesc* Get() const
	{
		return IsValid() ? ActorDesc->Get() : nullptr;
	}

	FORCEINLINE void Reset()
	{
		Container = nullptr;
		ActorDesc = nullptr;
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

public:
	FORCEINLINE void IncRefCount()
	{
		Impl::IncRefCount(ActorDesc->Get());
	}

	FORCEINLINE void DecRefCount()
	{
		Impl::DecRefCount(ActorDesc->Get());
	}

	TWeakObjectPtr<UActorDescContainer> Container;
	TUniquePtr<FWorldPartitionActorDesc>* ActorDesc;
};

struct ENGINE_API FWorldPartitionHandleImpl
{
	static void IncRefCount(FWorldPartitionActorDesc* ActorDesc);
	static void DecRefCount(FWorldPartitionActorDesc* ActorDesc);
};

struct ENGINE_API FWorldPartitionReferenceImpl
{
	static void IncRefCount(FWorldPartitionActorDesc* ActorDesc);
	static void DecRefCount(FWorldPartitionActorDesc* ActorDesc);
};

/**
 * FWorldPartitionHandle will increment/decrement the soft reference count on the actor descriptor.
 * This won't trigger any loading, but will prevent cleanup of the actor descriptor when destroying an
 * actor in the editor.
 */
typedef TWorldPartitionHandle<FWorldPartitionHandleImpl> FWorldPartitionHandle;

/**
 * FWorldPartitionReference will increment/decrement the hard reference count on the actor descriptor.
 * This will trigger actor loading/unloading when the hard reference counts gets to one/zero.
 */
typedef TWorldPartitionHandle<FWorldPartitionReferenceImpl> FWorldPartitionReference;

/**
 * FWorldPartitionHandlePinRefScope will keep a reference if the actor is already loaded. This is useful 
 * when you want to keep an actor loaded during special operations, but don't trigger loading when the 
 * actor is not loaded. Intended to be on stack, as a scoped operation.
 */
struct FWorldPartitionHandlePinRefScope
{
	FWorldPartitionHandlePinRefScope(const FWorldPartitionHandle& Handle)
	{
		if (Handle.IsLoaded())
		{
			Reference = Handle;
		}
	}
	FWorldPartitionReference Reference;
};
#endif