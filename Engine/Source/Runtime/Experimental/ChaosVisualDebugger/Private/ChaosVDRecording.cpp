// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDRecording.h"

int32 FChaosVDRecording::GetAvailableFramesNumber(const int32 SolverID) const
{
	if (const TArray<FChaosVDSolverFrameData>* SolverData = RecordedFramesDataPerSolver.Find(SolverID))
	{
		return SolverData->Num();
	}

	return INDEX_NONE;
}

FChaosVDSolverFrameData* FChaosVDRecording::GetFrameForSolver(const int32 SolverID, const int32 FrameNumber)
{
	if (TArray<FChaosVDSolverFrameData>* SolverFrames = RecordedFramesDataPerSolver.Find(SolverID))
	{
		//TODO: Find a safer way of do this. If someone stores this ptr bad things will happen
		return SolverFrames->IsValidIndex(FrameNumber) ? &(*SolverFrames)[FrameNumber] : nullptr;
	}

	return nullptr;
}

void FChaosVDRecording::AddFrameForSolver(const int32 SolverID, FChaosVDSolverFrameData&& InFrameData)
{
	if (TArray<FChaosVDSolverFrameData>* SolverFrames = RecordedFramesDataPerSolver.Find(SolverID))
	{
		SolverFrames->Add(MoveTemp(InFrameData));
	}
	else
	{	
		RecordedFramesDataPerSolver.Add(SolverID, { MoveTemp(InFrameData) });
	}

	OnRecordingUpdated().Broadcast();
}

EChaosVDFrameLoadState FChaosVDRecording::GetFrameState(const int32 SolverID, const int32 FrameNumber)
{
	const TArray<FChaosVDSolverFrameData>* SolverFrames = RecordedFramesDataPerSolver.Find(SolverID);
	if (SolverFrames && SolverFrames->IsValidIndex(FrameNumber))
	{
		return EChaosVDFrameLoadState::Loaded;
	}
	//TODO: Implement "Buffering" state handling
	// I already implemented this on the version where we didn't use trace and CVD files were streamed from disk
	// But removed the code for this CL - Leaving this getter here as it is used by the controller and I also left part
	// of the implementation to handle different states there

	return EChaosVDFrameLoadState::Unknown;
}
