// Copyright Epic Games, Inc. All Rights Reserved.

#include "LiveLinkTimedDataInput.h"

#include "HAL/IConsoleManager.h"
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


namespace LiveLinkTimedDataInput
{
	TAutoConsoleVariable<int32> CVarLiveLinkMinBufferSize(
		TEXT("LiveLink.TimedDataInput.MinBufferSize"),
		5,
		TEXT("The min size the timed data input is allowed to set the buffer size."),
		ECVF_Default
	);

	TAutoConsoleVariable<int32> CVarLiveLinkMaxBufferSize(
		TEXT("LiveLink.TimedDataInput.MaxBufferSize"),
		200,
		TEXT("The max size the timed data input is allowed to set the buffer size."),
		ECVF_Default
	);


	ETimedDataInputEvaluationType ToTimedDataInputEvaluationType(ELiveLinkSourceMode SourceMode)
	{
		switch (SourceMode)
		{
		case ELiveLinkSourceMode::EngineTime:
			return ETimedDataInputEvaluationType::PlatformTime;
		case ELiveLinkSourceMode::Timecode:
			return ETimedDataInputEvaluationType::Timecode;
		case ELiveLinkSourceMode::Latest:
		default:
			return ETimedDataInputEvaluationType::None;
		}
		return ETimedDataInputEvaluationType::None;
	}

	ELiveLinkSourceMode ToLiveLinkSourceMode(ETimedDataInputEvaluationType EvaluationType)
	{
		switch (EvaluationType)
		{
		case ETimedDataInputEvaluationType::PlatformTime:
			return ELiveLinkSourceMode::EngineTime;
		case ETimedDataInputEvaluationType::Timecode:
			return ELiveLinkSourceMode::Timecode;
		case ETimedDataInputEvaluationType::None:
		default:
			return ELiveLinkSourceMode::Latest;
		}
		return ELiveLinkSourceMode::Latest;
	}
}


FLiveLinkTimedDataInput::FLiveLinkTimedDataInput(FLiveLinkClient* InClient, FGuid InSource)
	: LiveLinkClient(InClient)
	, Source(InSource)
{
	ITimeManagementModule::Get().GetTimedDataInputCollection().Add(this);
}

FLiveLinkTimedDataInput::~FLiveLinkTimedDataInput()
{
	ITimeManagementModule::Get().GetTimedDataInputCollection().Remove(this);
}

FText FLiveLinkTimedDataInput::GetDisplayName() const
{
	return LiveLinkClient->GetSourceType(Source);
}

TArray<ITimedDataInputChannel*> FLiveLinkTimedDataInput::GetChannels() const
{
	return Channels;
}

ETimedDataInputEvaluationType FLiveLinkTimedDataInput::GetEvaluationType() const
{
	if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Source))
	{
		return LiveLinkTimedDataInput::ToTimedDataInputEvaluationType(Settings->Mode);
	}
	return ETimedDataInputEvaluationType::None;
}

void FLiveLinkTimedDataInput::SetEvaluationType(ETimedDataInputEvaluationType NewSourceMode)
{
	if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Source))
	{
		ELiveLinkSourceMode SourceMode = LiveLinkTimedDataInput::ToLiveLinkSourceMode(NewSourceMode);
		if (Settings->Mode != SourceMode)
		{
			Settings->Mode = SourceMode;
		}
	}
}

double FLiveLinkTimedDataInput::GetEvaluationOffsetInSeconds() const
{
	if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Source))
	{
		switch (Settings->Mode)
		{
		case ELiveLinkSourceMode::EngineTime:
			return Settings->BufferSettings.EngineTimeOffset;
		case ELiveLinkSourceMode::Timecode:
			return ITimedDataInput::ConvertFrameOffsetInSecondOffset(Settings->BufferSettings.TimecodeFrameOffset, Settings->BufferSettings.TimecodeFrameRate);
		case ELiveLinkSourceMode::Latest:
		default:
			return Settings->BufferSettings.LatestOffset;
		}
	}
	return 0.f;
}

void FLiveLinkTimedDataInput::SetEvaluationOffsetInSeconds(double OffsetInSeconds)
{
	if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Source))
	{
		switch (Settings->Mode)
		{
		case ELiveLinkSourceMode::Latest:
			Settings->BufferSettings.LatestOffset = (float)OffsetInSeconds;
			break;
		case ELiveLinkSourceMode::EngineTime:
			Settings->BufferSettings.EngineTimeOffset = (float)OffsetInSeconds;
			break;
		case ELiveLinkSourceMode::Timecode:
			float OffsetInFrame = (float)ITimedDataInput::ConvertSecondOffsetInFrameOffset(OffsetInSeconds, Settings->BufferSettings.TimecodeFrameRate);
			Settings->BufferSettings.TimecodeFrameOffset = OffsetInFrame;
			break;
		}
	}
}

FFrameRate FLiveLinkTimedDataInput::GetFrameRate() const
{
	if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Source))
	{
		if (Settings->Mode == ELiveLinkSourceMode::Timecode)
		{
			return Settings->BufferSettings.TimecodeFrameRate;
		}
	}
	return ITimedDataInput::UnknownFrameRate;
}

int32 FLiveLinkTimedDataInput::GetDataBufferSize() const
{
	if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Source))
	{
		return Settings->BufferSettings.MaxNumberOfFrameToBuffered;
	}
	return 0;
}

void FLiveLinkTimedDataInput::SetDataBufferSize(int32 BufferSize)
{
	int32 NewSize = FMath::Clamp(BufferSize, LiveLinkTimedDataInput::CVarLiveLinkMinBufferSize.GetValueOnGameThread(), LiveLinkTimedDataInput::CVarLiveLinkMaxBufferSize.GetValueOnGameThread());
	if (ULiveLinkSourceSettings* Settings = LiveLinkClient->GetSourceSettings(Source))
	{
		Settings->BufferSettings.MaxNumberOfFrameToBuffered = NewSize;
	}
}

#if WITH_EDITOR
const FSlateBrush* FLiveLinkTimedDataInput::GetDisplayIcon() const
{
	return ILiveLinkModule::Get().GetStyle()->GetBrush("LiveLinkIcon");
}
#endif

#undef LOCTEXT_NAMESPACE
