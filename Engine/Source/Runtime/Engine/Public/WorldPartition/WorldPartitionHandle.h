// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Templates/UniquePtr.h"

#if WITH_EDITOR
class UWorldPartition;
class FWorldPartitionActorDesc;

class ENGINE_API FWorldPartitionHandleBase
{
public:
	static TUniquePtr<FWorldPartitionActorDesc>* GetActorDesc(UWorldPartition* WorldPartition, const FGuid& ActorGuid);
};

template <typename Impl>
class ENGINE_API TWorldPartitionHandleBase : public FWorldPartitionHandleBase
{
	friend struct FWorldPartitionHandleHelpers;

public:
	FORCEINLINE TWorldPartitionHandleBase()
		: ActorDesc(nullptr)
	{}

	FORCEINLINE TWorldPartitionHandleBase(TUniquePtr<FWorldPartitionActorDesc>* InActorDesc)
	{
		ActorDesc = InActorDesc;

		if (IsValid())
		{
			IncRefCount();
		}
	}

	FORCEINLINE TWorldPartitionHandleBase(UWorldPartition* WorldPartition, const FGuid& ActorGuid)
	{
		ActorDesc = GetActorDesc(WorldPartition, ActorGuid);

		if (IsValid())
		{
			IncRefCount();
		}
	}

	FORCEINLINE TWorldPartitionHandleBase(const TWorldPartitionHandleBase& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	FORCEINLINE TWorldPartitionHandleBase(TWorldPartitionHandleBase&& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	// Conversions
	template <typename T>
	FORCEINLINE TWorldPartitionHandleBase<Impl>(const TWorldPartitionHandleBase<T>& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	template <typename T>
	FORCEINLINE TWorldPartitionHandleBase<Impl>(TWorldPartitionHandleBase<T>&& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	FORCEINLINE ~TWorldPartitionHandleBase()
	{
		if (IsValid())
		{
			DecRefCount();
		}
	}

	FORCEINLINE TWorldPartitionHandleBase& operator=(const TWorldPartitionHandleBase& Other)
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

	FORCEINLINE TWorldPartitionHandleBase<Impl>& operator=(TWorldPartitionHandleBase&& Other)
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
	FORCEINLINE TWorldPartitionHandleBase<Impl>& operator=(const TWorldPartitionHandleBase<T>& Other)
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
	FORCEINLINE TWorldPartitionHandleBase<Impl>& operator=(TWorldPartitionHandleBase<T>&& Other)
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

	FORCEINLINE FWorldPartitionActorDesc* Get() const
	{
		return IsValid() ? ActorDesc->Get() : nullptr;
	}
		
	friend FORCEINLINE uint32 GetTypeHash(const TWorldPartitionHandleBase<Impl>& HandleBase)
	{
		return ::PointerHash(HandleBase.ActorDesc);
	}

	FORCEINLINE bool operator==(const TWorldPartitionHandleBase& Other) const
	{
		return ActorDesc == Other.ActorDesc;
	}

	FORCEINLINE bool operator!=(const TWorldPartitionHandleBase& Other) const
	{
		return !(*this == Other);
	}

	// Conversions
	template <typename T>
	FORCEINLINE bool operator==(const TWorldPartitionHandleBase<T>& Other) const
	{
		return Get() == *Other;
	}

	template <typename T>
	FORCEINLINE bool operator!=(const TWorldPartitionHandleBase<T>& Other) const
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

typedef TWorldPartitionHandleBase<FWorldPartitionHandleImpl> FWorldPartitionHandle;
typedef TWorldPartitionHandleBase<FWorldPartitionReferenceImpl> FWorldPartitionReference;

struct ENGINE_API FWorldPartitionHandleHelpers
{
	static FWorldPartitionReference ConvertHandleToReference(const FWorldPartitionHandle& Handle);
	static FWorldPartitionHandle ConvertReferenceToHandle(const FWorldPartitionReference& Handle);
};
#endif