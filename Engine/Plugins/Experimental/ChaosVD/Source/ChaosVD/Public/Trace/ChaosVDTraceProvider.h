// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "TraceServices/Model/AnalysisSession.h"
#include "Templates/SharedPointer.h"

class FChaosVDEngine;
struct FChaosVDSolverFrameData;
struct FChaosVDRecording;

/** Provider class for Chaos VD trace recordings.
 * It stores and handles rebuilt recorded frame data from Trace events
 * dispatched by the Chaos VD Trace analyzer
 */
class FChaosVDTraceProvider : public TraceServices::IProvider
{
public:
	
	static FName ProviderName;

	FChaosVDTraceProvider(TraceServices::IAnalysisSession& InSession);

	void CreateRecordingInstanceForSession(const FString& InSessionName);
	void DeleteRecordingInstanceForSession();
	void AddFrame(const int32 InSolverGUID, FChaosVDSolverFrameData&& FrameData);
	FChaosVDSolverFrameData* GetFrame(const int32 InSolverGUID, const int32 FrameNumber) const;
	FChaosVDSolverFrameData* GetLastFrame(const int32 InSolverGUID) const;

	TSharedPtr<FChaosVDRecording> GetRecordingForSession() const;

private:
	TraceServices::IAnalysisSession& Session;

	TSharedPtr<FChaosVDRecording> InternalRecording;
};
