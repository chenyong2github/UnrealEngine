// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include <algorithm>
#include "Animation/AnimTypes.h"
#include "ReferenceSkeleton.h"
#include "BoneIndices.h"

namespace UE::AnimNext::Interface
{


namespace Private
{

using AnimDataTypeId = uint16;
static constexpr AnimDataTypeId InvalidTypeId = MAX_uint16;

struct FAllocatedBlock
{
	mutable int32 NumRefs = 0;
	void* Memory = nullptr;
	int32 NumElem = 0;
	AnimDataTypeId TypeId = InvalidTypeId;

	FAllocatedBlock(void* InMemory, int32 InNumElem, AnimDataTypeId InTypeId)
		: Memory(InMemory)
		, NumElem(InNumElem)
		, TypeId(InTypeId)
	{
	}

	inline uint32 AddRef() const
	{
		return uint32(FPlatformAtomics::InterlockedIncrement(&NumRefs));
	}

	inline uint32 Release() const
	{
		check(NumRefs > 0);

		const int32 Refs = FPlatformAtomics::InterlockedDecrement(&NumRefs);
		check(Refs >= 0);

		return uint32(Refs);
	}

	inline uint32 GetRefCount() const
	{
		return uint32(NumRefs);
	}

private:
	FAllocatedBlock() = delete;
	FAllocatedBlock(const FAllocatedBlock& Other) = delete;
	FAllocatedBlock(FAllocatedBlock&& Other) = delete;
};

} // end namespace Private

struct ANIMNEXTINTERFACE_API FAnimationDataHandle
{
	FAnimationDataHandle() = default;

	FAnimationDataHandle(Private::FAllocatedBlock* InAllocatedBlock)
		: AllocatedBlock(InAllocatedBlock)
	{
	}

	~FAnimationDataHandle();

	FAnimationDataHandle(const FAnimationDataHandle& Other)
		: AllocatedBlock(Other.AllocatedBlock)
	{
		if (AllocatedBlock != nullptr)
		{
			const int32 CurrentCount = AllocatedBlock->AddRef();
			check(CurrentCount > 1);
		}
	}

	FAnimationDataHandle& operator= (const FAnimationDataHandle& Other)
	{
		FAnimationDataHandle Tmp(Other);

		Swap(*this, Tmp);
		return *this;
	}

	FAnimationDataHandle(FAnimationDataHandle&& Other)
		: FAnimationDataHandle()
	{
		Swap(*this, Other);
	}

	FAnimationDataHandle& operator= (FAnimationDataHandle&& Other)
	{
		Swap(*this, Other);
		return *this;
	}

	inline bool IsValid() const
	{
		return AllocatedBlock != nullptr;
	}

	template<typename DataType>
	inline TArrayView<DataType> AsArrayView()
	{
		check(IsValid());
		return TArrayView<DataType>((DataType*)AllocatedBlock->Memory, AllocatedBlock->NumElem);
	}

	template<typename DataType>
	inline TArrayView<DataType> AsArrayView() const
	{
		check(IsValid());
		return TArrayView<DataType>(AllocatedBlock->Memory, AllocatedBlock->NumElem);
	}

	template<typename DataType>
	inline DataType* GetPtr()
	{
		check(IsValid());
		DataType* Data = static_cast<DataType*>(AllocatedBlock->Memory);
		check(Data != nullptr);
		return Data;
	}

	template<typename DataType>
	inline const DataType* GetPtr() const
	{
		check(IsValid());
		const DataType* Data = static_cast<const DataType*>(AllocatedBlock->Memory);
		return Data;
	}

	template<typename DataType>
	inline DataType& GetRef()
	{
		check(IsValid());
		DataType* Data = static_cast<DataType*>(AllocatedBlock->Memory);
		check(Data != nullptr);
		return *Data;
	}

	template<typename DataType>
	inline const DataType& GetRef() const
	{
		check(IsValid());
		const DataType* Data = static_cast<const DataType*>(AllocatedBlock->Memory);
		check(Data != nullptr);
		return *Data;
	}

	inline Private::AnimDataTypeId GetTypeId() const
	{
		return AllocatedBlock != nullptr ? AllocatedBlock->TypeId : Private::InvalidTypeId;
	}

private:
	Private::FAllocatedBlock* AllocatedBlock = nullptr;
};

enum class ETransformFlags : uint8
{
	None = 0,
	ComponentSpaceSet = 1 << 0
};


} // namespace UE::AnimNext::Interface
