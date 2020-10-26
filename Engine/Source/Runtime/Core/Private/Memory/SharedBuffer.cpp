// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/SharedBuffer.h"

FSharedBufferRef FSharedBuffer::NewBuffer(
	void* const Data,
	const uint64 Size,
	const ESharedBufferFlags Flags,
	const uint32 DeleterSize)
{
	checkf((DeleterSize == 0) == !(Flags & ESharedBufferFlags::HasDeleter), TEXT("Mismatch between DeleterSize %u and Flags."), DeleterSize);

	void* const BufferMemory = FMemory::Malloc(sizeof(FSharedBuffer) + DeleterSize, alignof(FSharedBuffer));

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
	if (IsOwned())
	{
		if (GetFlags(ReferenceCountAndFlags) & ESharedBufferFlags::HasDeleter)
		{
			reinterpret_cast<FSharedBufferDeleter*>(this + 1)->Free(Data);
		}
		else
		{
			FMemory::Free(Data);
		}
	}
	Data = nullptr;
	Size = 0;
	ReferenceCountAndFlags.fetch_and(~SetFlags(~ESharedBufferFlags::None));
}

bool FSharedBuffer::TryMakeImmutable() const
{
	for (uint64 Value = ReferenceCountAndFlags.load(std::memory_order_relaxed);;)
	{
		if (EnumHasAnyFlags(GetFlags(Value), ESharedBufferFlags::Immutable))
		{
			return true;
		}
		if (GetSharedRefCount(Value) != 1 || GetWeakRefCount(Value) != 1 ||
			!EnumHasAnyFlags(GetFlags(Value), ESharedBufferFlags::Owned))
		{
			return false;
		}
		if (ReferenceCountAndFlags.compare_exchange_weak(Value, Value | SetFlags(ESharedBufferFlags::Immutable),
			std::memory_order_relaxed, std::memory_order_relaxed))
		{
			return true;
		}
	}
}

bool FSharedBuffer::TryMakeMutable() const
{
	for (uint64 Value = ReferenceCountAndFlags.load(std::memory_order_relaxed);;)
	{
		if (GetSharedRefCount(Value) != 1 || GetWeakRefCount(Value) != 1 ||
			!EnumHasAnyFlags(GetFlags(Value), ESharedBufferFlags::Owned))
		{
			return false;
		}
		if (!EnumHasAnyFlags(GetFlags(Value), ESharedBufferFlags::Immutable))
		{
			return true;
		}
		if (ReferenceCountAndFlags.compare_exchange_weak(Value, Value & ~SetFlags(ESharedBufferFlags::Immutable),
			std::memory_order_relaxed, std::memory_order_relaxed))
		{
			return true;
		}
	}
}
