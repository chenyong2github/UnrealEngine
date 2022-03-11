// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_EDITOR
struct FObjectPostCDOCompiledContext
{
	/** True if this notification was from a 'skeleton-only' compile */
	bool bIsSkeletonOnly = false;
};
#endif
