// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#if __UNREAL__
#include "CoreMinimal.h"
#else
#include "windows.h"
#endif

/**
 * Constant names for nDisplay texture resources
 */
namespace TextureShareDisplayClusterStrings
{
	namespace Viewport
	{
		// FinalColor, but can be overrided
		static constexpr auto FinalColor = TEXT("ViewportFinalColor");

		// internal resolved shader resources, used as warp&blend source
		static constexpr auto Input = TEXT("ViewportInput");
		static constexpr auto Mips = TEXT("ViewportMips");

		// After warp viewport (before output remap)
		static constexpr auto Warped = TEXT("ViewportWarpBlend");
	}

	// final frame resource (after warp, output remaps)
	namespace Output
	{
		// name mapped as viewport resource
		// Ex.: "vp_0.BackbufferRect"
		static constexpr auto Viewport = TEXT("FrameBackbufferRect");

		// access to nDisplay frame backbuffer
		static constexpr auto Backbuffer     = TEXT("FrameBackbuffer");
		static constexpr auto BackbufferTemp = TEXT("FrameBackbufferTemp");
	}

	namespace Default
	{
		// Default ShareName for nDisplay integration object
		static constexpr auto ShareName = TEXT("nDisplay");
	}
};
