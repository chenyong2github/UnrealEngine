// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDRecording.h"

FChaosVDFrameData& FChaosVDRecording::AddFrame()
{
	RecordedFramesData.Add(FChaosVDFrameData());

	return RecordedFramesData.Last();
}
