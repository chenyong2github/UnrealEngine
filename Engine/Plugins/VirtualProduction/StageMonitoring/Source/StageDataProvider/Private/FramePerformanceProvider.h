// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Handles sending frame performance messages periodically and hitch messages if detected
 * Either one can be enabled / disabled using project settings
 */
class FFramePerformanceProvider
{
public:
	FFramePerformanceProvider();
	~FFramePerformanceProvider();

private:
	/** At end of frame, send a message, if interval met, about this frame data */
	void OnEndFrame();

	/** Callback by the stat module to detect if a hitch happened */
	void CheckHitches(int64 Frame);

	/** Sends a message containing information about the frame performane */
	void UpdateFramePerformance();

private:

	/** Timestamp when last frame performance message was sent */
	double LastFramePerformanceSent = 0.0;
};
