// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

enum ELimits
{
	MaxTextureShareItemSessionName = 128,
	MaxTextureShareItemSessionsCount = 100,

	MaxTextureShareItemTexturesCount = 10,
	MaxTextureShareItemNameLength = 128,

	DefaultMinMillisecondsToWait = 1500
};

enum class ESharedResourceProcessState : uint8
{
	Undefined = 0,
	Used
};

enum class ETextureShareSource : uint8
{
	Undefined = 0,
	Unreal,
	SDK
};

enum class ETextureShareFrameState: uint8
{
	None = 0, // Share now outside of frame lock/unlock scope
	Locked,   // Entered to frame lock
	LockedOp  // Entered to next phase: Server(write->read) or Client(read->write)
};

enum class ESharedResourceTextureState : uint8
{
	Undefined = 0, // Empty surface slot
	Ready,         // Defined surface slot (FTextureShareSurfaceDesc from client or server side)
	Enabled,       // Shared texture is created, sharing enabled
	Disabled,      // Force disable this slot (from logic at any side)
	INVALID
};
