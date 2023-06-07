// Copyright Epic Games, Inc. All Rights Reserved.

#include "CaptureCardMediaSource.h"

int64 UCaptureCardMediaSource::GetMediaOption(const FName& Key, int64 DefaultValue) const
{
	if (Key == UE::CaptureCardMediaSource::InterlaceFieldOrder)
	{
		return static_cast<int64>(InterlaceFieldOrder);
	}

	if (Key == UE::CaptureCardMediaSource::SourceColorSpace)
	{
		return (int64) OverrideSourceColorSpace;
	}

	if (Key == UE::CaptureCardMediaSource::SourceEncoding)
	{
		return (int64) OverrideSourceEncoding;
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

bool UCaptureCardMediaSource::GetMediaOption(const FName& Key, bool bDefaultValue) const
{
	if (Key == UE::CaptureCardMediaSource::OverrideSourceColorSpace)
	{
		return bOverrideSourceColorSpace;
	}

	if (Key == UE::CaptureCardMediaSource::OverrideSourceEncoding)
	{
		return bOverrideSourceEncoding;
	}

	return UTimeSynchronizableMediaSource::GetMediaOption(Key, bDefaultValue);
}

bool UCaptureCardMediaSource::HasMediaOption(const FName& Key) const
{
	if (Key == UE::CaptureCardMediaSource::Deinterlacer
		|| Key == UE::CaptureCardMediaSource::InterlaceFieldOrder
		|| Key == UE::CaptureCardMediaSource::OverrideSourceEncoding
		|| Key == UE::CaptureCardMediaSource::OverrideSourceColorSpace
		|| Key == UE::CaptureCardMediaSource::SourceEncoding
		|| Key == UE::CaptureCardMediaSource::SourceColorSpace)
		{
			return true;
		}

	return UTimeSynchronizableMediaSource::HasMediaOption(Key);
}
