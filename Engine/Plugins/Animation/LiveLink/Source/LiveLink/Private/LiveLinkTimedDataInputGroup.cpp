// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkTimedDataInputGroup.h"

#include "ILiveLinkModule.h"
#include "ITimeManagementModule.h"
#include "LiveLinkClient.h"
#include "LiveLinkLog.h"
#include "LiveLinkSourceSettings.h"
#include "TimedDataInputCollection.h"

#if WITH_EDITOR
#include "Styling/SlateStyle.h"
#endif

#define LOCTEXT_NAMESPACE "LiveLinkTimedDataInput"


FLiveLinkTimedDataInputGroup::FLiveLinkTimedDataInputGroup(FLiveLinkClient* InClient, FGuid InSource)
	: LiveLinkClient(InClient)
	, Source(InSource)
{
	ITimeManagementModule::Get().GetTimedDataInputCollection().Add(this);
}

FLiveLinkTimedDataInputGroup::~FLiveLinkTimedDataInputGroup()
{
	ITimeManagementModule::Get().GetTimedDataInputCollection().Remove(this);
}

FText FLiveLinkTimedDataInputGroup::GetDisplayName() const
{
	return LiveLinkClient->GetSourceType(Source);
}

FText FLiveLinkTimedDataInputGroup::GetDescription() const
{
	return LiveLinkClient->GetSourceMachineName(Source);
}

#if WITH_EDITOR
const FSlateBrush* FLiveLinkTimedDataInputGroup::GetDisplayIcon() const
{
	return ILiveLinkModule::Get().GetStyle()->GetBrush("LiveLinkIcon");
}
#endif

void FLiveLinkTimedDataInputGroup::SetEvaluationType(ELiveLinkSourceMode SourceMode)
{
	if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Source))
	{
		if (Settings->Mode != SourceMode)
		{
			Settings->Mode = SourceMode;
		}
	}
}

void FLiveLinkTimedDataInputGroup::SetEvaluationOffset(ELiveLinkSourceMode SourceMode, double OffsetInSeconds)
{
	if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Source))
	{
		switch (SourceMode)
		{
		case ELiveLinkSourceMode::Latest:
			if (!FMath::IsNearlyEqual(Settings->BufferSettings.LatestOffset, (float)OffsetInSeconds))
			{
				Settings->BufferSettings.LatestOffset = (float)OffsetInSeconds;
			}
			break;
		case ELiveLinkSourceMode::EngineTime:
			if (!FMath::IsNearlyEqual(Settings->BufferSettings.EngineTimeOffset, (float)OffsetInSeconds))
			{
				Settings->BufferSettings.EngineTimeOffset = (float)OffsetInSeconds;
			}
			break;
		case ELiveLinkSourceMode::Timecode:
			float OffsetInFrame = (float)ITimedDataInput::ConvertSecondOffsetInFrameOffset(OffsetInSeconds, Settings->BufferSettings.TimecodeFrameRate);
			if (!FMath::IsNearlyEqual(Settings->BufferSettings.TimecodeFrameOffset, OffsetInFrame))
			{
				Settings->BufferSettings.TimecodeFrameOffset = OffsetInFrame;
			}
			break;
		}
	}
}

void FLiveLinkTimedDataInputGroup::SetBufferMaxSize(int32 BufferSize)
{
	if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Source))
	{
		if (Settings->BufferSettings.MaxNumberOfFrameToBuffered != BufferSize)
		{
			Settings->BufferSettings.MaxNumberOfFrameToBuffered = BufferSize;
		}
	}
}

#undef LOCTEXT_NAMESPACE
