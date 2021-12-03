// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "PlayerId.h"
#include "HAL/Thread.h"

class FPixelStreamingFrameSource;
class FPlayerVideoSource;

// responsible for regularly supplying webrtc with frames independent of the game framerate
// runs its own thread
class FPixelStreamingFramePump
{
public:
	FPixelStreamingFramePump(FPixelStreamingFrameSource* InFrameSource);
	~FPixelStreamingFramePump();

	FPlayerVideoSource* CreatePlayerVideoSource(FPlayerId PlayerId);
	void RemovePlayerVideoSource(FPlayerId PlayerId);
	void SetQualityController(FPlayerId PlayerId);

private:
	FCriticalSection SourcesGuard;
	TMap<FPlayerId, TUniquePtr<FPlayerVideoSource>> PlayerVideoSources;
	FPlayerId QualityControllerId = INVALID_PLAYER_ID;

	FPixelStreamingFrameSource* FrameSource = nullptr;

	TUniquePtr<FThread> PumpThread;
	bool bThreadRunning = true;
	FEvent* QualityControllerEvent;
	FEvent* NextPumpEvent;
	double FrameDeltaMs = 1000.0 / 60.0; // default to 60fps
	int32 NextFrameId = 0;

	void PumpLoop();
};
