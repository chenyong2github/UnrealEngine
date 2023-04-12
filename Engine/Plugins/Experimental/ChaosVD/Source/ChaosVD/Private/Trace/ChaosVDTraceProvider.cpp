// Copyright Epic Games, Inc. All Rights Reserved.

#include "Trace/ChaosVDTraceProvider.h"

#include "ChaosVDRecording.h"

FName FChaosVDTraceProvider::ProviderName("ChaosVDProvider");

FChaosVDTraceProvider::FChaosVDTraceProvider(TraceServices::IAnalysisSession& InSession): Session(InSession)
{
}

void FChaosVDTraceProvider::CreateRecordingInstanceForSession(const FString& InSessionName)
{
	DeleteRecordingInstanceForSession();

	InternalRecording = MakeShared<FChaosVDRecording>();
	InternalRecording->SessionName = InSessionName;
}

void FChaosVDTraceProvider::DeleteRecordingInstanceForSession()
{
	InternalRecording.Reset();
}

void FChaosVDTraceProvider::AddFrame(const int32 InSolverID, FChaosVDSolverFrameData&& FrameData)
{
	if (InternalRecording.IsValid())
	{
		InternalRecording->AddFrameForSolver(InSolverID, MoveTemp(FrameData));
	}
}

FChaosVDSolverFrameData* FChaosVDTraceProvider::GetFrame(const int32 InSolverID, const int32 FrameNumber) const
{
	return InternalRecording.IsValid() ? InternalRecording->GetFrameForSolver(InSolverID, FrameNumber) : nullptr;
}

FChaosVDSolverFrameData* FChaosVDTraceProvider::GetLastFrame(const int32 InSolverID) const
{
	if (InternalRecording.IsValid() && InternalRecording->GetAvailableFramesNumber(InSolverID) > 0)
	{
		const int32 AvailableFramesNumber = InternalRecording->GetAvailableFramesNumber(InSolverID);

		if (AvailableFramesNumber != INDEX_NONE)
		{
			return GetFrame(InSolverID, InternalRecording->GetAvailableFramesNumber(InSolverID) - 1);
		}
	}

	return nullptr;
}

TSharedPtr<FChaosVDRecording> FChaosVDTraceProvider::GetRecordingForSession() const
{
	return InternalRecording;
}
