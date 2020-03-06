
// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if WITH_MLSDK

#include "MagicLeapGraphics.h"

namespace MagicLeap
{
	// Clears the extents info and puts it into a safe initial state.
	void ResetClipExtentsInfoArray(MLGraphicsClipExtentsInfoArrayEx& UpdateInfoArray);

	// Clears the virtual camera info and puts it into a safe initial state.
	void ResetFrameInfo(MLGraphicsFrameInfo& FrameInfo);
}
#endif //WITH_MLSDK
