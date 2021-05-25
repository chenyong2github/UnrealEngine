// Copyright Epic Games, Inc. All Rights Reserved.

#include "Recorder/TakeRecorderParameters.h"
#include "Recorder/TakeRecorder.h"

FTakeRecorderUserParameters::FTakeRecorderUserParameters()
	: bMaximizeViewport(false)
	, CountdownSeconds(0.f)
	, EngineTimeDilation(1.f)
	, bStopAtPlaybackEnd(false)
	, bRemoveRedundantTracks(true)
	, ReduceKeysTolerance(KINDA_SMALL_NUMBER)
	, bSaveRecordedAssets(false)
	, bAutoLock(true)
	, bAutoSerialize(false)
{
	// Defaults for all user parameter structures
	// User defaults should be set in UTakeRecorderUserSettings
	// So as not to affect structs created with script
}

FTakeRecorderProjectParameters::FTakeRecorderProjectParameters()
	: bStartAtCurrentTimecode(true)
	, bRecordTimecode(false)
	, bRecordSourcesIntoSubSequences(false)
	, bRecordToPossessable(false)
	, bShowNotifications(true)
{}

FTakeRecorderParameters::FTakeRecorderParameters() 
	: TakeRecorderMode(ETakeRecorderMode::RecordNewSequence)
{}
