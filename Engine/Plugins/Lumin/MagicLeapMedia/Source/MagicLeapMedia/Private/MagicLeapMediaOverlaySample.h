// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "IMediaOverlaySample.h"
#include "Misc/Timespan.h"

class FMagicLeapMediaOverlaySample : public IMediaOverlaySample
{
public:
	FMagicLeapMediaOverlaySample()
		: Duration(FTimespan::Zero())
		, Time(FTimespan::Zero())
	{ }

	void Intitialize(char* Data, const EMediaOverlaySampleType InSampleType, const FTimespan InDuration, const FTimespan InTime)
	{
		check(Data != nullptr);
		OverlaySampleType = InSampleType;
		Text = FText::FromString(FString(ANSI_TO_TCHAR(Data)));
		Duration = InDuration;
		Time = InTime;
	}

	virtual ~FMagicLeapMediaOverlaySample() {};

	/** IMediaOverlaySample interface **/
	virtual FTimespan GetDuration() const override
	{
		return Duration;
	}

	virtual TOptional<FVector2D> GetPosition() const override
	{
		return TOptional<FVector2D>();
	}

	virtual FText GetText() const override
	{
		return Text;
	}

	virtual FMediaTimeStamp GetTime() const override
	{
		return FMediaTimeStamp(Time);
	}

	virtual EMediaOverlaySampleType GetType() const override
	{
		return OverlaySampleType;
	}

private:
	EMediaOverlaySampleType OverlaySampleType;
	FText Text;
	FTimespan Duration;
	FTimespan Time;
};

