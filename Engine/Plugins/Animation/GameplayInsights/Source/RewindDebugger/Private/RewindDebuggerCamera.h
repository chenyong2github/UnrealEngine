// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "IRewindDebuggerExtension.h"
#include "UObject/WeakObjectPtr.h"

class ACameraActor;

// Rewind debugger extension for camera support
//  replay of recorded camera data
//  follow selected actor

class FRewindDebuggerCamera : public IRewindDebuggerExtension
{
public:
	enum class ECameraMode
	{
		Replay,
		FollowTargetActor,
		Disabled,
	};


	FRewindDebuggerCamera();
	virtual ~FRewindDebuggerCamera() {};
	void Initialize();

	virtual void Update(float DeltaTime, IRewindDebugger* RewindDebugger) override;

	ECameraMode CameraMode();
	void SetCameraMode(ECameraMode Mode);
private:
	bool LastPositionValid;
	FVector LastPosition;

	ECameraMode Mode = ECameraMode::Replay;
	TWeakObjectPtr<ACameraActor> CameraActor; 
};
