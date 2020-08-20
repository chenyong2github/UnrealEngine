// Copyright Epic Games, Inc. All Rights Reserved.

#include "StageMessages.h"

#include "Misc/App.h"
#include "StageMonitoringSettings.h"
#include "VPSettings.h"


namespace StageProviderMessageUtils
{
	static const FQualifiedFrameTime InvalidTime = FQualifiedFrameTime(FFrameTime(FFrameNumber(-1)), FFrameRate(-1, -1));
	static FString CachedComputerName = FPlatformProcess::ComputerName();
}

FStageProviderMessage::FStageProviderMessage()
{
	//Common setup of timecode for all provider messages
	TOptional<FQualifiedFrameTime> CurrentFrameTime = FApp::GetCurrentFrameTime();
	if (CurrentFrameTime.IsSet())
	{
		FrameTime = CurrentFrameTime.GetValue();
	}
	else
	{
		FrameTime = StageProviderMessageUtils::InvalidTime;
	}
}

FString FCriticalStateProviderMessage::ToString() const
{
	switch (State)
	{
		case EStageCriticalStateEvent::Enter:
		{
			return FString::Printf(TEXT("%s: Entered critical state"), *SourceName.ToString());
		}
		case EStageCriticalStateEvent::Exit:
		default:
		{
			return FString::Printf(TEXT("%s: Exited critical state"), *SourceName.ToString());
		}
	}
}

void FStageInstanceDescriptor::FillWithInstanceData()
{
	//A machine could spawn multiple UE instances. Need to be able to differentiate them. ProcessId is there for that reason
	MachineName = StageProviderMessageUtils::CachedComputerName;
	ProcessId = FPlatformProcess::GetCurrentProcessId();

	RolesStringified = GetDefault<UVPSettings>()->GetRoles().ToStringSimple();

	FriendlyName = GetDefault<UStageMonitoringSettings>()->CommandLineFriendlyName;
	if (FriendlyName == NAME_None)
	{
		const FString TempName = FString::Printf(TEXT("%s:%d"), *MachineName, ProcessId);
		FriendlyName = *TempName;
	}

	SessionId = GetDefault<UStageMonitoringSettings>()->GetStageSessionId();
}

FStageProviderDiscoveryMessage::FStageProviderDiscoveryMessage()
{
	Descriptor.FillWithInstanceData();
}

FStageProviderDiscoveryResponseMessage::FStageProviderDiscoveryResponseMessage()
{
	Descriptor.FillWithInstanceData();
}
