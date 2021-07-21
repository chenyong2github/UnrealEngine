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

struct REWINDDEBUGGER_API FDebugObjectInfo
{
	FDebugObjectInfo(uint64 Id, const FString& Name): ObjectId(Id), ObjectName(Name), bExpanded(true)
	{
	}

	uint64 ObjectId;
	FString ObjectName;
	bool bExpanded;

	TArray<TSharedPtr<FDebugObjectInfo>> Children;
};

class REWINDDEBUGGER_API IRewindDebugger
{
public:
	// get the time the debugger is scrubbed to, in seconds since the capture started (or the recording duration while the game is running)
	virtual double CurrentTraceTime() const = 0;

	// get the current analysis session
	virtual const TraceServices::IAnalysisSession* GetAnalysisSession() const = 0;

	// get insights id for the selected target actor
	virtual uint64 GetTargetActorId() const = 0;

	// get a list of all components of the selected target actor (with the actor as the first element in the list)
	virtual TArray<TSharedPtr<FDebugObjectInfo>>& GetDebugComponents() = 0;

	// get posiotion of the selected target actor (returns true if position is valid)
	virtual bool GetTargetActorPosition(FVector& OutPosition) const = 0;

	// get the world that the debugger is replaying in
	virtual UWorld* GetWorldToVisualize() const = 0;

	// returns true if recording is active
	virtual bool IsRecording() const = 0;

	// returns true if PIE is running and not paused
	virtual bool IsPIESimulating() const = 0;

	// returns the length of the current recording
	virtual double GetRecordingDuration() const = 0;
};
