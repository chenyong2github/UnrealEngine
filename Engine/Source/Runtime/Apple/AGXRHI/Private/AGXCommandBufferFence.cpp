// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCommandBufferFence.cpp: AGX RHI Command Buffer Fence Implementation.
=============================================================================*/

#include "AGXRHIPrivate.h"
#include "AGXCommandBufferFence.h"


//------------------------------------------------------------------------------

#pragma mark - AGX RHI Command Buffer Fence Routines - 


bool FAGXCommandBufferFence::Wait(uint64 Millis)
{
	@autoreleasepool {
		if (CommandBufferFence)
		{
			bool bFinished = CommandBufferFence.Wait(Millis);
			FPlatformMisc::MemoryBarrier();
			return bFinished;
		}
		else
		{
			return true;
		}
	}
}
