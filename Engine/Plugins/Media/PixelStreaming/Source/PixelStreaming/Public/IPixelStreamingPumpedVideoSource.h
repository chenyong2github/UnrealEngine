// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

/*
* Interface for video sources that get pumped through `OnPump`.
*/
class PIXELSTREAMING_API IPumpedVideoSource
{
public:
	virtual void OnPump(int32 FrameId) = 0;
	virtual bool IsReadyForPump() const = 0;
};