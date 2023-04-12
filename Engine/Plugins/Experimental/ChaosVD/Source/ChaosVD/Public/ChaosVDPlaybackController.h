// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"
#include "Delegates/DelegateCombinations.h"
#include "Delegates/Delegate.h"

class FChaosVDScene;
struct FChaosVDRecording;
class FChaosVDPlaybackController;
class FString;

DECLARE_DELEGATE_OneParam(FChaosVDPlaybackControllerUpdated, TWeakPtr<FChaosVDPlaybackController>)

/** Loads,unloads and owns a Chaos VD recording file */
class FChaosVDPlaybackController : public TSharedFromThis<FChaosVDPlaybackController>
{
public:

	FChaosVDPlaybackController(const TWeakPtr<FChaosVDScene>& InSceneToControl);
	~FChaosVDPlaybackController();
	
	bool LoadChaosVDRecordingFromTraceSession(const FString& InSessionName);
	
	void UnloadCurrentRecording(const bool bBroadcastUpdate = true);

	void GoToRecordedStep(const int32 InTrackID, const int32 FrameNumber, const int32 Step);

	TWeakPtr<FChaosVDRecording> GetCurrentRecording() { return LoadedRecording; }

	int32 GetStepsForFrame(const int32 InTrackID, const int32 FrameNumber) const;
	int32 GetAvailableFramesNumber(const int32 InTrackID) const;
	int32 GetAvailableSolversNumber() const;

	int32 GetActiveSolverTrackID() const;

	int32 GetCurrentFrame(const int32 InTrackID) const;
	int32 GetCurrentStep(const int32 InTrackID) const;

	bool IsRecordingLoaded() const { return LoadedRecording.IsValid(); }

	TWeakPtr<FChaosVDScene> GetControllerScene() { return SceneToControl; }

	FChaosVDPlaybackControllerUpdated& OnControllerUpdated() { return ControllerUpdatedDelegate; }

protected:

	void HandleCurrentRecordingUpdated();

	TMap<int32,int32> CurrentFramePerTrack;
	TMap<int32,int32> CurrentStepPerTrack;
	
	TSharedPtr<FChaosVDRecording> LoadedRecording;

	TWeakPtr<FChaosVDScene> SceneToControl;

	FChaosVDPlaybackControllerUpdated ControllerUpdatedDelegate;
};
