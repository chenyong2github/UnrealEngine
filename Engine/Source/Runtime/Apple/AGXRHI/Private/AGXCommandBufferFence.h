// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	AGXCommandBufferFence.h: AGX RHI Command Buffer Fence Definition.
=============================================================================*/

#pragma once

struct FAGXCommandBufferFence
{
	bool Wait(uint64 Millis);

	mtlpp::CommandBufferFence CommandBufferFence;
};
