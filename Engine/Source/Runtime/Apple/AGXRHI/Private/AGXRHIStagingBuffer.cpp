// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXRHIStagingBuffer.cpp: AGX RHI Staging Buffer Class.
=============================================================================*/


#include "AGXRHIPrivate.h"
#include "AGXRHIStagingBuffer.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Staging Buffer Class


FAGXRHIStagingBuffer::FAGXRHIStagingBuffer()
	: FRHIStagingBuffer()
{
	// void
}

FAGXRHIStagingBuffer::~FAGXRHIStagingBuffer()
{
	if (ShadowBuffer)
	{
		AGXSafeReleaseMetalBuffer(ShadowBuffer);
		ShadowBuffer = nil;
	}
}

void *FAGXRHIStagingBuffer::Lock(uint32 Offset, uint32 NumBytes)
{
	check(ShadowBuffer);
	check(!bIsLocked);
	bIsLocked = true;
	uint8* BackingPtr = (uint8*)ShadowBuffer.GetContents();
	return BackingPtr + Offset;
}

void FAGXRHIStagingBuffer::Unlock()
{
	// does nothing in metal.
	check(bIsLocked);
	bIsLocked = false;
}
