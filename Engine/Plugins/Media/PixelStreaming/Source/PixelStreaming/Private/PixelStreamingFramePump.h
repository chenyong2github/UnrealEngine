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

	FPlayerVideoSource* CreatePlayerVideoSource(FPlayerId PlayerId, int Flags);
	void RemovePlayerVideoSource(FPlayerId PlayerId);
	void SetQualityController(FPlayerId PlayerId);

private:
	FCriticalSection SourcesGuard;
	TMap<FPlayerId, TUniquePtr<FPlayerVideoSource>> PlayerVideoSources;

	FCriticalSection ExtraSourcesGuard;
	TArray<TUniquePtr<FPlayerVideoSource>> ExtraVideoSources;

	FPlayerId ElevatedPlayerId = INVALID_PLAYER_ID; // higher priority than quality controller
	FPlayerId QualityControllerId = INVALID_PLAYER_ID;

	FPixelStreamingFrameSource* FrameSource = nullptr;

	TUniquePtr<FThread> PumpThread;
	bool bThreadRunning = true;
	FEvent* PlayersChangedEvent;
	FEvent* NextPumpEvent;
	int32 NextFrameId = 0;

	void PumpLoop();
};
