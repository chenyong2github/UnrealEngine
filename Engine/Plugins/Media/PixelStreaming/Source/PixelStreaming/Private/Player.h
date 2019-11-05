// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PixelStreamingPrivate.h"
#include "MediaTracks.h"

#include "IMediaCache.h"
#include "IMediaPlayer.h"
#include "IMediaView.h"
#include "IMediaControls.h"
#include "IMediaEventSink.h"

#include "Containers/UnrealString.h"
#include "Containers/Queue.h"
#include "Templates/SharedPointer.h"
#include "Templates/UniquePtr.h"
#include "Templates/Atomic.h"

class IMediaOptions;
class FMediaSamples;
class FArchive;
class IMediaAudioSample;
class IMediaTextureSample;

class FThread;

class FStreamerConnection;

// A media player for PixelStreaming.
// This class is a bridge between UE4 Media Framework and underlying implementation of WebRTC player
class FPlayer
	: public IMediaPlayer
	, private IMediaCache
	, private IMediaView
	, private IMediaControls
{
public:
	explicit FPlayer(IMediaEventSink& InEventSink);
	virtual ~FPlayer();

public:
	static bool CreateDXManagerAndDevice();
	static bool DestroyDXManagerAndDevice();

	// IMediaPlayer impl

	void Close() override;

	IMediaCache& GetCache() override
	{
		return *this;
	}

	IMediaControls& GetControls() override
	{
		return *this;
	}

	FString GetInfo() const override
	{
		return GetUrl();
	}
	
	FName GetPlayerName() const override;

	IMediaSamples& GetSamples() override;
			
	FString GetStats() const override
	{
		return FString{};
	}

	IMediaTracks& GetTracks() override;

	FString GetUrl() const override
	{
		return Url;
	}

	IMediaView& GetView() override
	{
		return *this;
	}

	bool Open(const FString& Url, const IMediaOptions* Options) override;
	bool Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* Options) override;

	void TickInput(FTimespan DeltaTime, FTimespan Timecode) override;

private: 	// IMediaControls impl
	bool CanControl(EMediaControl Control) const override;

	FTimespan GetDuration() const override;

	bool IsLooping() const override
	{
		return false;
	}

	bool Seek(const FTimespan& Time) override
	{
		return false;
	}

	bool SetLooping(bool bLooping) override
	{
		return bLooping == false;
	}

	EMediaState GetState() const override;
	EMediaStatus GetStatus() const override;
	TRangeSet<float> GetSupportedRates(EMediaRateThinning Thinning) const override;
	FTimespan GetTime() const override;
	float GetRate() const override;
	bool SetRate(float Rate) override;

private:
	void OnAudioSample(const TSharedRef<IMediaAudioSample, ESPMode::ThreadSafe>& Sample);
	void OnVideoFrame(const TSharedRef<IMediaTextureSample, ESPMode::ThreadSafe>& Sample);

private:
	/** The media event handler. */
	IMediaEventSink& EventSink;
	TQueue<EMediaEvent> DeferredEvents;

	TUniquePtr<FMediaSamples> MediaSamples;
	TAtomic<EMediaState> State{ EMediaState::Closed };
	TAtomic<EMediaStatus> Status{ EMediaStatus::None };

	FMediaTracks MediaTracks;

	FString Url;

	TUniquePtr<FStreamerConnection> StreamerConnection;

	double SmoothedEndToEndLatencyMs = 0;
};
