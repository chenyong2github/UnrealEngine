// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDPlaybackController.h"

#include "ChaosVDModule.h"
#include "ChaosVDRecording.h"
#include "ChaosVDScene.h"
#include "Trace/ChaosVDTraceManager.h"
#include "Trace/ChaosVDTraceProvider.h"
#include "TraceServices/Model/AnalysisSession.h"


FChaosVDPlaybackController::FChaosVDPlaybackController(const TWeakPtr<FChaosVDScene>& InSceneToControl)
{
	SceneToControl = InSceneToControl;
}

FChaosVDPlaybackController::~FChaosVDPlaybackController()
{
	constexpr bool bBroadcastControllerUpdate = false;
	UnloadCurrentRecording(bBroadcastControllerUpdate);
}

bool FChaosVDPlaybackController::LoadChaosVDRecordingFromTraceSession(const FString& InSessionName)
{
	if (InSessionName.IsEmpty())
	{
		return false;
	}

	if (LoadedRecording.IsValid())
	{
		UnloadCurrentRecording();
	}

	if (const TSharedPtr<const TraceServices::IAnalysisSession> TraceSession = FChaosVDModule::Get().GetTraceManager()->GetSession(InSessionName))
	{
		if (const FChaosVDTraceProvider* ChaosVDProvider = TraceSession->ReadProvider<FChaosVDTraceProvider>(FChaosVDTraceProvider::ProviderName))
		{
			LoadedRecording = ChaosVDProvider->GetRecordingForSession();
		}
	}

	if (!LoadedRecording.IsValid())
	{
		return false;
	}

	LoadedRecording->OnRecordingUpdated().AddRaw(this, &FChaosVDPlaybackController::HandleCurrentRecordingUpdated);

	for (TMap<int32, TArray<FChaosVDSolverFrameData>>::TConstIterator SolversIterator = LoadedRecording->GetAvailableSolvers().CreateConstIterator(); SolversIterator; ++SolversIterator)
	{
		GoToRecordedStep(SolversIterator->Key, 0, 0);
	}

	LoadedRecording->OnGeometryDataLoaded().AddLambda([this](const TSharedPtr<const Chaos::FImplicitObject>& NewGeometry, const int32 GeometryID)
	{
		if (const TSharedPtr<FChaosVDScene> ScenePtr = SceneToControl.Pin())
		{
			ScenePtr->HandleNewGeometryData(NewGeometry, GeometryID);
		}
	});
	
	if (TSharedPtr<FChaosVDScene> ScenePtr = SceneToControl.Pin())
	{
		ScenePtr->LoadedRecording = LoadedRecording;
	}
	
	OnControllerUpdated().ExecuteIfBound(AsWeak());

	return true;
}

void FChaosVDPlaybackController::UnloadCurrentRecording(const bool bBroadcastUpdate)
{
	CurrentFramePerTrack.Reset();
	CurrentStepPerTrack.Reset();

	if (LoadedRecording.IsValid())
	{
		LoadedRecording->OnRecordingUpdated().RemoveAll(this);
		LoadedRecording.Reset();
	}

	if (const TSharedPtr<FChaosVDScene> SceneToControlSharedPtr = SceneToControl.Pin())
	{
		if (SceneToControlSharedPtr->IsInitialized())
		{
			SceneToControlSharedPtr->CleanUpScene();
		}	
	}

	if (bBroadcastUpdate)
	{
		const TWeakPtr<FChaosVDPlaybackController> ThisWeakPtr = DoesSharedInstanceExist() ? AsWeak() : nullptr;
		OnControllerUpdated().ExecuteIfBound(ThisWeakPtr);
	}
}

void FChaosVDPlaybackController::GoToRecordedStep(const int32 InTrackID, const int32 FrameNumber, const int32 Step)
{
	if (const TSharedPtr<FChaosVDScene> SceneToControlSharedPtr = SceneToControl.Pin())
	{
		if (ensure(LoadedRecording.IsValid()))
		{
			const EChaosVDFrameLoadState FrameState = LoadedRecording->GetFrameState(InTrackID, FrameNumber);
			if (FrameState == EChaosVDFrameLoadState::Unknown)
			{
				// Invalid Frame Number
				return;
			}

			{
				const TSharedPtr<const TraceServices::IAnalysisSession> TraceSession = FChaosVDModule::Get().GetTraceManager()->GetSession(LoadedRecording->SessionName);
				if (!ensure(TraceSession))
				{
					return;	
				}

				// TODO: This will not cover a future case where the recording is not own/populated by Trace
				// If in the future we decide implement the CVD format standalone with streaming support again,
				// we will need to add a lock to the file. A feature that might need that is Recording Clips
				TraceServices::FAnalysisSessionReadScope SessionReadScope(*TraceSession);

				const FChaosVDSolverFrameData* SolverFrameData = LoadedRecording->GetFrameForSolver(InTrackID, FrameNumber);
				if (SolverFrameData && ensure(SolverFrameData->SolverSteps.IsValidIndex(Step)))
				{
					SceneToControlSharedPtr->UpdateFromRecordedStepData(InTrackID, SolverFrameData->DebugName, SolverFrameData->SolverSteps[Step], *SolverFrameData);
				}
			}

			if (int32* CurrentFrameNumber = CurrentFramePerTrack.Find(InTrackID))
			{
				 *CurrentFrameNumber = FrameNumber;
			}
			else
			{
				CurrentFramePerTrack.Add(InTrackID, FrameNumber);
			}

			if (int32* CurrentStepNumber = CurrentStepPerTrack.Find(InTrackID))
			{
				*CurrentStepNumber = Step;
			}
			else
			{
				CurrentStepPerTrack.Add(InTrackID, Step);
			}
		}
	}
	else
	{
		ensureMsgf(false, TEXT("GoToRecordedStep Called without a valid scene to control"));	
	}
}

int32 FChaosVDPlaybackController::GetStepsForFrame(const int32 InTrackID, const int32 FrameNumber) const
{
	if (LoadedRecording.IsValid())
	{
		if (const FChaosVDSolverFrameData* FrameData = LoadedRecording->GetFrameForSolver(InTrackID, FrameNumber))
		{
			return FrameData->SolverSteps.Num();
		}
		else
		{
			return INDEX_NONE;
		}
	}

	return	INDEX_NONE;
}

int32 FChaosVDPlaybackController::GetAvailableFramesNumber(const int32 InTrackID) const
{
	if (!LoadedRecording.IsValid())
	{
		return INDEX_NONE;
	}

	return LoadedRecording->GetAvailableFramesNumber(InTrackID);
}

int32 FChaosVDPlaybackController::GetAvailableSolversNumber() const
{
	if (!LoadedRecording.IsValid())
	{
		return INDEX_NONE;
	}

	return LoadedRecording->GetAvailableSolvers().Num();
}

int32 FChaosVDPlaybackController::GetActiveSolverTrackID() const
{
	//For Now use the Last Solver if any
	int32 SolverID = INDEX_NONE;
	for (const TPair<int32, TArray<FChaosVDSolverFrameData>>& SolverData : LoadedRecording->GetAvailableSolvers())
	{
		SolverID = SolverData.Key;
	}

	return SolverID;
}

int32 FChaosVDPlaybackController::GetCurrentFrame(const int32 InTrackID) const
{
	if (const int32* FrameNumber = CurrentFramePerTrack.Find(InTrackID))
	{
		return *FrameNumber;
	}

	return INDEX_NONE;
}

int32 FChaosVDPlaybackController::GetCurrentStep(const int32 InTrackID) const
{
	if (const int32* StepNumber = CurrentStepPerTrack.Find(InTrackID))
	{
		return *StepNumber;
	}

	return INDEX_NONE;
}

void FChaosVDPlaybackController::HandleCurrentRecordingUpdated()
{
	OnControllerUpdated().ExecuteIfBound(AsWeak());
}

