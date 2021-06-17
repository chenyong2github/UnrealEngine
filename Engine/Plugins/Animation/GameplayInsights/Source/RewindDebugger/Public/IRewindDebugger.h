// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"

// IRewindDebugger
//
// Public interface to rewind debugger

namespace TraceServices
{
	class IAnalysisSession;
}

class IRewindDebugger
{
public:
	// get the time the debugger is scrubbed to, in seconds since the capture started (or the recording duration while the game is running)
	virtual double CurrentTraceTime() const = 0;

	// get the current analysis session
	virtual const TraceServices::IAnalysisSession* GetAnalysisSession() const = 0;

	// get insights id for the selected target actor
	virtual uint64 GetTargetActorId() const = 0;

	// get posiotion of the selected target actor (returns true if position is valid)
	virtual bool GetTargetActorPosition(FVector& OutPosition) const = 0;

	// get the world that the debugger is replaying in
	virtual UWorld* GetWorldToVisualize() const = 0;
};
