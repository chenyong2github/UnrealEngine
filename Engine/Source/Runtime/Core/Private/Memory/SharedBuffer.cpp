// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/SharedBuffer.h"

FSharedBufferRef FSharedBuffer::NewBuffer(
	void* const Data,
	const uint64 Size,
	const ESharedBufferFlags Flags,
	const uint32 OwnerSize)
{
	checkf((OwnerSize > 0) == EnumHasAnyFlags(Flags, ESharedBufferFlags::HasBufferOwner),
		TEXT("Mismatch between OwnerSize %u and Flags."), OwnerSize);

	void* const BufferMemory = FMemory::Malloc(sizeof(FSharedBuffer) + OwnerSize, alignof(FSharedBuffer));

	FSharedBuffer* const Buffer = new(BufferMemory) FSharedBuffer;
	Buffer->Data = Data;
	Buffer->Size = Size;
	Buffer->ReferenceCountAndFlags = SetFlags(Flags);

	return FSharedBufferRef(SharedBufferPrivate::TSharedBufferPtr</*bAllowNull*/ false, /*bIsConst*/ false, /*bIsWeak*/ false>(Buffer));
}

void FSharedBuffer::DeleteBuffer(FSharedBuffer* const Buffer)
{
	checkSlow(!Buffer->Data && !Buffer->Size);
	checkSlow(Buffer->ReferenceCountAndFlags == 0);
	Buffer->~FSharedBuffer();
	FMemory::Free(Buffer);
}

void FSharedBuffer::ReleaseData()
{
	const ESharedBufferFlags Flags = GetFlags(ReferenceCountAndFlags);
	if (EnumHasAnyFlags(Flags, ESharedBufferFlags::HasBufferOwner))
	{
		FBufferOwner& BufferOwner = *reinterpret_cast<FBufferOwner*>(this + 1);
		BufferOwner.Free(Data, Size);
		BufferOwner.~FBufferOwner();
	}
	else if (EnumHasAnyFlags(Flags, ESharedBufferFlags::Owned))
	{
		FMemory::Free(Data);
	}
	Data = nullptr;
	Size = 0;
	ReferenceCountAndFlags.fetch_and(~SetFlags(~ESharedBufferFlags::None));
}

bool FSharedBuffer::TryMakeReadOnly() const
{
	for (uint64 Value = ReferenceCountAndFlags.load(std::memory_order_relaxed);;)
	{
		if (EnumHasAnyFlags(GetFlags(Value), ESharedBufferFlags::ReadOnly))
		{
			return true;
		}
		if (GetSharedRefCount(Value) != 1 || GetWeakRefCount(Value) != 1 ||
			!EnumHasAnyFlags(GetFlags(Value), ESharedBufferFlags::Owned))
		{
			return false;
		}
		if (ReferenceCountAndFlags.compare_exchange_weak(Value, Value | SetFlags(ESharedBufferFlags::ReadOnly),
			std::memory_order_relaxed, std::memory_order_relaxed))
		{
			return true;
		}
	}
}

bool FSharedBuffer::TryMakeWritable() const
{
	for (uint64 Value = ReferenceCountAndFlags.load(std::memory_order_relaxed);;)
	{
		if (GetSharedRefCount(Value) != 1 || GetWeakRefCount(Value) != 1 ||
			!EnumHasAnyFlags(GetFlags(Value), ESharedBufferFlags::Owned))
		{
			return false;
		}
		if (!EnumHasAnyFlags(GetFlags(Value), ESharedBufferFlags::ReadOnly))
		{
			return true;
		}
		if (ReferenceCountAndFlags.compare_exchange_weak(Value, Value & ~SetFlags(ESharedBufferFlags::ReadOnly),
			std::memory_order_relaxed, std::memory_order_relaxed))
		{
			return true;
		}
	}
}
