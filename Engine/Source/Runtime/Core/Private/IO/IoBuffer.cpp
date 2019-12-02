// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IO/IoDispatcher.h"

//////////////////////////////////////////////////////////////////////////

FIoBuffer::BufCore::BufCore()
{
}

FIoBuffer::BufCore::~BufCore()
{
	if (IsMemoryOwned())
	{
		FMemory::Free(Data());
	}
}

FIoBuffer::BufCore::BufCore(const uint8* InData, uint64 InSize, bool InOwnsMemory)
{
	SetDataAndSize(InData, InSize);

	SetIsOwned(InOwnsMemory);
}

FIoBuffer::BufCore::BufCore(const uint8* InData, uint64 InSize, const BufCore* InOuter)
:	OuterCore(InOuter)
{
	SetDataAndSize(InData, InSize);
}

FIoBuffer::BufCore::BufCore(uint64 InSize)
{
	uint8* NewBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(InSize));

	SetDataAndSize(NewBuffer, InSize);

	SetIsOwned(true);
}

FIoBuffer::BufCore::BufCore(ECloneTag, uint8* InData, uint64 InSize)
:	FIoBuffer::BufCore(InSize)
{
	FMemory::Memcpy(Data(), InData, InSize);
}

void
FIoBuffer::BufCore::CheckRefCount() const
{
	// Verify that Release() is not being called on an object which is already at a zero refcount
	check(NumRefs != 0);
}

void
FIoBuffer::BufCore::SetDataAndSize(const uint8* InData, uint64 InSize)
{
	// This is intentionally not split into SetData and SetSize to enable different storage
	// strategies for flags in the future (in unused pointer bits)

	DataPtr			= const_cast<uint8*>(InData);
	DataSizeLow		= uint32(InSize & 0xffffffffu);
	DataSizeHigh	= (InSize >> 32) & 0xffu;
}

void
FIoBuffer::BufCore::SetSize(uint64 InSize)
{
	SetDataAndSize(Data(), InSize);
}

void
FIoBuffer::BufCore::MakeOwned()
{
	if (IsMemoryOwned())
		return;

	const uint64 BufferSize = DataSize();
	uint8* NewBuffer		= reinterpret_cast<uint8*>(FMemory::Malloc(BufferSize));

	FMemory::Memcpy(NewBuffer, Data(), BufferSize);

	SetDataAndSize(NewBuffer, BufferSize);

	SetIsOwned(true);
}

//////////////////////////////////////////////////////////////////////////

FIoBuffer::FIoBuffer()
:	CorePtr(new BufCore)
{
}

FIoBuffer::FIoBuffer(uint64 InSize)
:	CorePtr(new BufCore(InSize))
{
}

FIoBuffer::FIoBuffer(const void* Data, uint64 InSize, const FIoBuffer& OuterBuffer)
:	CorePtr(new BufCore((uint8*) Data, InSize, OuterBuffer.CorePtr))
{
}

FIoBuffer::FIoBuffer(FIoBuffer::EWrapTag, const void* Data, uint64 InSize)
:	CorePtr(new BufCore((uint8*)Data, InSize, /* ownership */ false))
{
}

FIoBuffer::FIoBuffer(FIoBuffer::EAssumeOwnershipTag, const void* Data, uint64 InSize)
:	CorePtr(new BufCore((uint8*)Data, InSize, /* ownership */ true))
{
}

FIoBuffer::FIoBuffer(FIoBuffer::ECloneTag, const void* Data, uint64 InSize)
:	CorePtr(new BufCore(Clone, (uint8*)Data, InSize))
{
}

void		
FIoBuffer::MakeOwned() const
{
	CorePtr->MakeOwned();
}
