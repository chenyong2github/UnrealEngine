// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Delegates/DelegateCombinations.h"
#include "Delegates/Delegate.h"

class FChaosVDScene;
struct FChaosVDRecording;
class FChaosVDPlaybackController;
class FString;

DECLARE_DELEGATE_OneParam(FChaosVDPlaybackControllerUpdated, FChaosVDPlaybackController*)

/** Loads,unloads and owns a Chaos VD recording file */
class FChaosVDPlaybackController
{
public:

	FChaosVDPlaybackController(TWeakPtr<FChaosVDScene> InSceneToControl);

	bool LoadChaosVDRecording(const FString& RecordingPath);
	
	void UnloadCurrentRecording();

	void GoToRecordedStep(int32 FrameNumber, int32 Step);

	TWeakPtr<FChaosVDRecording> GetCurrentRecording() { return LoadedRecording; }

	int32 GetStepsForFrame(int32 FrameNumber) const;
	int32 GetAvailableFramesNumber() const;

	int32 GetCurrentFrame() const { return CurrentFrame; }
	int32 GetCurrentStep() const { return CurrentStep; }

	TWeakPtr<FChaosVDScene> GetControllerScene() { return SceneToControl; }

	FChaosVDPlaybackControllerUpdated& OnControllerUpdated() { return ControllerUpdatedDelegate; }

protected:

	int32 CurrentFrame;
	int32 CurrentStep;
	
	TSharedPtr<FChaosVDRecording> LoadedRecording;

	TWeakPtr<FChaosVDScene> SceneToControl;

	FChaosVDPlaybackControllerUpdated ControllerUpdatedDelegate;
};
