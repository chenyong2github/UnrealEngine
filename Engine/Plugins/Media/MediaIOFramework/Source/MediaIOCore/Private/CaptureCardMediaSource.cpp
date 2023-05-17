// Copyright Epic Games, Inc. All Rights Reserved.

#include "CaptureCardMediaSource.h"

int64 UCaptureCardMediaSource::GetMediaOption(const FName& Key, int64 DefaultValue) const
{
	if (Key == UE::CaptureCardMediaSource::InterlaceFieldOrder)
	{
		return static_cast<int64>(InterlaceFieldOrder);
	}

	return UTimeSynchronizableMediaSource::GetMediaOption(Key, DefaultValue);
}

FString UCaptureCardMediaSource::GetMediaOption(const FName& Key, const FString& DefaultValue) const
{
	if (Key == UE::CaptureCardMediaSource::Deinterlacer)
	{
		if (Deinterlacer)
		{
			return Deinterlacer->GetPathName();
		}
	}

	return UTimeSynchronizableMediaSource::GetMediaOption(Key, DefaultValue);
}

bool UCaptureCardMediaSource::HasMediaOption(const FName& Key) const
{
	if (Key == UE::CaptureCardMediaSource::Deinterlacer
		|| Key == UE::CaptureCardMediaSource::InterlaceFieldOrder)
		{
			return true;
		}

	return UTimeSynchronizableMediaSource::HasMediaOption(Key);
}