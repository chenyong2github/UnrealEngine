// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVisualDebugger/Public/ChaosVDRecording.h"

FChaosVDFrameData& FChaosVDRecording::AddFrame()
{
	RecordedFramesData.Add(FChaosVDFrameData());

	return RecordedFramesData.Last();
}
