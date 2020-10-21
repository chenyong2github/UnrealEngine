// Copyright Epic Games, Inc. All Rights Reserved.

#include "Memory/SharedBuffer.h"

FSharedBuffer::FSharedBuffer(const uint64 InSize)
{
	checkf(InSize < (uint64(1) << 48), TEXT("Size %" UINT64_FMT " exceeds maximum shared buffer size of 256 TiB"), InSize);
	Data = FMemory::Malloc(InSize);
	DataSize = InSize;
	Flags = uint16(ESharedBufferFlags::Owned);
}

FSharedBuffer::FSharedBuffer(EAssumeOwnershipTag, void* const InData, const uint64 InSize)
{
	checkf(InSize < (uint64(1) << 48), TEXT("Size %" UINT64_FMT " exceeds maximum shared buffer size of 256 TiB"), InSize);
	Data = InData;
	DataSize = InSize;
	Flags = uint16(ESharedBufferFlags::Owned);
}

FSharedBuffer::FSharedBuffer(ECloneTag, const void* const InData, const uint64 InSize)
	: FSharedBuffer(InSize)
{
	FMemory::Memcpy(Data, InData, InSize);
}

FSharedBuffer::FSharedBuffer(EWrapTag, void* const InData, const uint64 InSize)
{
	checkf(InSize < (uint64(1) << 48), TEXT("Size %" UINT64_FMT " exceeds maximum shared buffer size of 256 TiB"), InSize);
	Data = InData;
	DataSize = InSize;
	Flags = uint16(ESharedBufferFlags::None);
}

FSharedBuffer::~FSharedBuffer()
{
	if (IsOwned())
	{
		FMemory::Free(Data);
	}
}
