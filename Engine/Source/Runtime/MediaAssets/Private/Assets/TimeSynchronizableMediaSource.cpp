// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimeSynchronizableMediaSource.h"



UTimeSynchronizableMediaSource::UTimeSynchronizableMediaSource()
	: bUseTimeSynchronization(false)
	, FrameDelay(0)
	, TimeDelay(0.0)
{ }


/*
* IMediaOptions interface
*/

bool UTimeSynchronizableMediaSource::GetMediaOption(const FName& Key, bool DefaultValue) const
{
	if (Key == TimeSynchronizableMedia::UseTimeSynchronizatioOption)
	{
		return bUseTimeSynchronization;
	}

	return Super::GetMediaOption(Key, DefaultValue);
}

int64 UTimeSynchronizableMediaSource::GetMediaOption(const FName& Key, int64 DefaultValue) const
{
	if (Key == TimeSynchronizableMedia::FrameDelay)
	{
		return FrameDelay;
	}

	return Super::GetMediaOption(Key, DefaultValue);
}

double UTimeSynchronizableMediaSource::GetMediaOption(const FName& Key, double DefaultValue) const
{
	if (Key == TimeSynchronizableMedia::TimeDelay)
	{
		return TimeDelay;
	}

	return Super::GetMediaOption(Key, DefaultValue);
}

FString UTimeSynchronizableMediaSource::GetMediaOption(const FName& Key, const FString& DefaultValue) const
{
	return Super::GetMediaOption(Key, DefaultValue);
}

bool UTimeSynchronizableMediaSource::HasMediaOption(const FName& Key) const
{
	if (Key == TimeSynchronizableMedia::UseTimeSynchronizatioOption
		|| Key == TimeSynchronizableMedia::FrameDelay
		|| Key == TimeSynchronizableMedia::TimeDelay)
	{
		return true;
	}

	return Super::HasMediaOption(Key);
}

