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
	TWorldPartitionHandleBase()
		: ActorDesc(nullptr)
	{}

	TWorldPartitionHandleBase(TUniquePtr<FWorldPartitionActorDesc>* InActorDesc)
	{
		ActorDesc = InActorDesc;

		if (IsValid())
		{
			IncRefCount();
		}
	}

	TWorldPartitionHandleBase(UWorldPartition* WorldPartition, const FGuid& ActorGuid)
	{
		ActorDesc = GetActorDesc(WorldPartition, ActorGuid);

		if (IsValid())
		{
			IncRefCount();
		}
	}

	TWorldPartitionHandleBase(const TWorldPartitionHandleBase& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	TWorldPartitionHandleBase(TWorldPartitionHandleBase&& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	// Conversions
	template <typename T>
	TWorldPartitionHandleBase<Impl>(const TWorldPartitionHandleBase<T>& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	template <typename T>
	TWorldPartitionHandleBase<Impl>(TWorldPartitionHandleBase<T>&& Other)
		: ActorDesc(nullptr)
	{
		*this = Other;
	}

	~TWorldPartitionHandleBase()
	{
		if (IsValid())
		{
			DecRefCount();
		}
	}

	TWorldPartitionHandleBase& operator=(const TWorldPartitionHandleBase& Other)
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

	TWorldPartitionHandleBase<Impl>& operator=(TWorldPartitionHandleBase&& Other)
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
	TWorldPartitionHandleBase<Impl>& operator=(const TWorldPartitionHandleBase<T>& Other)
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
	TWorldPartitionHandleBase<Impl>& operator=(TWorldPartitionHandleBase<T>&& Other)
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

	inline FWorldPartitionActorDesc* operator->() const
	{
		return Get();
	}

	inline FWorldPartitionActorDesc* operator*() const
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
		
	friend uint32 GetTypeHash(const TWorldPartitionHandleBase<Impl>& HandleBase)
	{
		return ::PointerHash(HandleBase.ActorDesc);
	}

	inline bool operator==(const TWorldPartitionHandleBase& Other) const
	{
		return ActorDesc == Other.ActorDesc;
	}

	inline bool operator!=(const TWorldPartitionHandleBase& Other) const
	{
		return !(*this == Other);
	}

	// Conversions
	template <typename T>
	inline bool operator==(const TWorldPartitionHandleBase<T>& Other) const
	{
		return Get() == *Other;
	}

	template <typename T>
	inline bool operator!=(const TWorldPartitionHandleBase<T>& Other) const
	{
		return !(*this == Other);
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