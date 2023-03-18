// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDPlaybackController.h"

#include "ChaosVDRecording.h"
#include "ChaosVDScene.h"
#include "Misc/FileHelper.h"
#include "Serialization/MemoryReader.h"

FChaosVDPlaybackController::FChaosVDPlaybackController(TWeakPtr<FChaosVDScene> InSceneToControl)
	: CurrentFrame(0),
	CurrentStep(0)
{
	SceneToControl = InSceneToControl;
}

bool FChaosVDPlaybackController::LoadChaosVDRecording(const FString& RecordingPath)
{
	if (RecordingPath.IsEmpty())
	{
		return false;
	}

	if (LoadedRecording.IsValid())
	{
		UnloadCurrentRecording();
	}

	//TODO: Show a notification or loading bar. The plan is support streaming of the file so maybe we don't need this at all

	TArray<uint8> SerializedDebugData;
	if (!ensure(FFileHelper::LoadFileToArray(SerializedDebugData, *RecordingPath)))
	{
		return false;
	}

	LoadedRecording = MakeShared<FChaosVDRecording>();

	FMemoryReader SerializedDataReader(SerializedDebugData, false);					
	FChaosVDRecording::StaticStruct()->SerializeBin(SerializedDataReader, LoadedRecording.Get());

	if (!ensure(!SerializedDataReader.IsError()))
	{
		//TODO: Pop up an error message informing the user
		LoadedRecording.Reset();
		return false;
	}

	GoToRecordedStep(0,0);

	OnControllerUpdated().ExecuteIfBound(this);

	return true;
}

void FChaosVDPlaybackController::UnloadCurrentRecording()
{
	CurrentFrame = 0;
	CurrentStep = 0;
	LoadedRecording.Reset();
	
	if (const TSharedPtr<FChaosVDScene> SceneToControlSharedPtr = SceneToControl.Pin())
	{
		SceneToControlSharedPtr->CleanUpScene();
	}
	
	OnControllerUpdated().ExecuteIfBound(this);
}

void FChaosVDPlaybackController::GoToRecordedStep(int32 FrameNumber, int32 Step)
{
	if (const TSharedPtr<FChaosVDScene> SceneToControlSharedPtr = SceneToControl.Pin())
	{
		if (ensure(LoadedRecording.IsValid()))
		{
			if (!LoadedRecording->RecordedFramesData.IsValidIndex(FrameNumber))
			{
				return;
			}

			//TODO: Handle different steps per solver if that is something that can happen
			for (TPair<EChaosVDSolverType, FChaosVDSolverData>& SolverData : LoadedRecording->RecordedFramesData[FrameNumber].RecordedSolvers)
			{
				SceneToControlSharedPtr->UpdateFromRecordedStepData(SolverData.Key, SolverData.Value.SolverSteps[Step]);
			}

			CurrentStep = Step;
			CurrentFrame = FrameNumber;
		}
	}
	else
	{
		ensureMsgf(false, TEXT("GoToRecordedStep Called without a valid scene to control"));	
	}
}

int32 FChaosVDPlaybackController::GetStepsForFrame(int32 FrameNumber) const
{
	if (LoadedRecording.IsValid())
	{
		if (!LoadedRecording->RecordedFramesData.IsValidIndex(FrameNumber))
		{
			return INDEX_NONE;
		}

		for (const TPair<EChaosVDSolverType, FChaosVDSolverData>& SolverData : LoadedRecording->RecordedFramesData[FrameNumber].RecordedSolvers)
		{
			//TODO: Handle different steps per solver if that is something that can happen
			return SolverData.Value.SolverSteps.Num();
		}
	}

	return	INDEX_NONE;
}

int32 FChaosVDPlaybackController::GetAvailableFramesNumber() const
{
	if (!LoadedRecording.IsValid())
	{
		return INDEX_NONE;
	}

	return LoadedRecording->RecordedFramesData.Num();
}

