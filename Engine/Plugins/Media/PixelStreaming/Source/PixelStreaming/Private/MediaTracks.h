// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IMediaTracks.h"
#include "Misc/AssertionMacros.h"

// just a video track of a single predefined format
class FMediaTracks : public IMediaTracks
{
public:
	bool GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const override
	{
		return false; // audio is not supported
	}

	int32 GetNumTracks(EMediaTrackType TrackType) const override
	{
		return 1; // just video
	}

	int32 GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const override
	{
		return 1;
	}

	int32 GetSelectedTrack(EMediaTrackType TrackType) const override
	{
		return 0;
	}

	FText GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const override
	{
		static FText VideoTrackDisplayName = FText::FromString(TEXT("Video"));
		return VideoTrackDisplayName;
	}

	int32 GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const override
	{
		return 0;
	}

	FString GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const override
	{
		return FString{};
	}

	FString GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const override
	{
		if (TrackType == EMediaTrackType::Video && TrackIndex == 0)
		{
			static FString TrackName = TEXT("Video");
			return TrackName;
		}
		else
		{
			static FString InvalidTrack = TEXT("Invalid track request");
			return InvalidTrack;
		}
	}

	bool GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const override
	{
		if (TrackIndex == 0 && FormatIndex == 0)
		{
			OutFormat.Dim = FIntPoint{-1, -1};
			OutFormat.FrameRate = -1;
			OutFormat.FrameRates = TRange<float>();
			OutFormat.TypeName = "Video";
			return true;
		}
		else
		{
			return false;
		}
	}

	bool SelectTrack(EMediaTrackType TrackType, int32 TrackIndex) override
	{
		return TrackType == EMediaTrackType::Video && TrackIndex == 0;
	}

	bool SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex) override
	{
		return TrackType == EMediaTrackType::Video && TrackIndex == 0 && FormatIndex == 0;
	}
};
