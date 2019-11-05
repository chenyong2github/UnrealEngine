// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Player.h"
#include "StreamerConnection.h"
#include "Codecs/VideoDecoder.h"
#include "HUDStats.h"

#include "IMediaEventSink.h"
#include "IMediaOptions.h"
#include "Misc/Optional.h"
#include "MediaSamples.h"
#include "HAL/PlatformTime.h"

DEFINE_LOG_CATEGORY(PixelPlayer);

FPlayer::FPlayer(IMediaEventSink& InEventSink)
	: EventSink(InEventSink)
	, MediaSamples(MakeUnique<FMediaSamples>())
{

}

FPlayer::~FPlayer()
{
	Close();
}

bool FPlayer::Open(const FString& InUrl, const IMediaOptions* Options)
{
	UE_LOG(PixelPlayer, Log, TEXT("%p Open %s"), this, *InUrl);

	Close();

	FString Scheme;
	FString Location;
	if (!InUrl.Split(TEXT("://"), &Scheme, &Location, ESearchCase::CaseSensitive))
	{
		UE_LOG(PixelPlayer, Error, TEXT("Invalid URL, cannot parse the scheme: %s"), *InUrl);
		return false;
	}

	if (Scheme != TEXT("webrtc"))
	{
		UE_LOG(PixelPlayer, Error, TEXT("Invalid URL scheme (%s), only `webrtc` protocol is supported"), *Scheme);
		return false;
	}

	Url = InUrl;
		
	FString SignallingServerAddress = InUrl.Replace(TEXT("webrtc://"), TEXT("ws://"), ESearchCase::IgnoreCase);
	StreamerConnection = MakeUnique<FStreamerConnection>(
		SignallingServerAddress, 
		/* OnDisconnection */ [this]() { Close(); },
		/* OnAudioSample */ [this](const TSharedRef<IMediaAudioSample, ESPMode::ThreadSafe>& Sample) { OnAudioSample(Sample); },
		/* OnVideoFrame */ [this](const TSharedRef<IMediaTextureSample, ESPMode::ThreadSafe>& Sample) { OnVideoFrame(Sample); }
	);

	State = EMediaState::Preparing;

	return true;
}

bool FPlayer::Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* /*Options*/)
{
	unimplemented();
	return false;
}

void FPlayer::Close()
{
	if (GetState() == EMediaState::Closed)
	{
		return;
	}

	UE_LOG(PixelPlayer, Log, TEXT("%p Close"), this);

	StreamerConnection.Reset();

	MediaSamples->FlushSamples();

	State = EMediaState::Closed;

	DeferredEvents.Enqueue(EMediaEvent::MediaClosed);
}

void FPlayer::TickInput(FTimespan DeltaTime, FTimespan Timecode)
{
	//if (State == EMediaState::Closed)
	//{
	//	return;
	//}

	//UE_LOG(PixelPlayer, VeryVerbose, TEXT("TickInput: DeltaTime %d, Timecode %d"), DeltaTime.GetTicks(), Timecode.GetTicks());

	// forward session events
	EMediaEvent Event;
	while (DeferredEvents.Dequeue(Event))
	{
		EventSink.ReceiveMediaEvent(Event);
	}
}

bool FPlayer::CreateDXManagerAndDevice()
{
	return FVideoDecoder::CreateDXManagerAndDevice();
}

bool FPlayer::DestroyDXManagerAndDevice()
{
	return FVideoDecoder::DestroyDXManagerAndDevice();
}

FName FPlayer::GetPlayerName() const
{
	static FName PlayerName(TEXT("PixelStreamingPlayer"));
	return PlayerName;
}

IMediaSamples& FPlayer::GetSamples()
{
	return *MediaSamples;
}

IMediaTracks& FPlayer::GetTracks()
{
	return MediaTracks;
}

// IMediaControl impl

bool FPlayer::CanControl(EMediaControl Control) const
{
	EMediaState CurrentState = GetState();
	if (Control == EMediaControl::Pause)
	{
		return CurrentState == EMediaState::Playing;
	}

	if (Control == EMediaControl::Resume)
	{
		return CurrentState == EMediaState::Paused || CurrentState == EMediaState::Stopped;
	}

	return false;
}

float FPlayer::GetRate() const
{
	return 1.0f; // always normal (real-time) playback rate
}

EMediaState FPlayer::GetState() const
{
	return State;
}

EMediaStatus FPlayer::GetStatus() const
{
	return Status;
}

TRangeSet<float> FPlayer::GetSupportedRates(EMediaRateThinning Thinning) const
{
	TRangeSet<float> Res;
	Res.Add(TRange<float>{1.0f}); // only normal (real-time) playback rate
	Res.Add(TRange<float>{0.0f}); // and pause
	return Res;
}

FTimespan FPlayer::GetTime() const
{
	return FTimespan::FromSeconds(FPlatformTime::Seconds());
}

FTimespan FPlayer::GetDuration() const
{
	return FTimespan::MaxValue();
}

bool FPlayer::SetRate(float Rate)
{
	if (Rate == 1.0f)
	{
		return Play();
	}
	else if (Rate == 0.0f)
	{
		return Pause();
	}

	UE_LOG(PixelPlayer, Error, TEXT("Unsupported rate: %f"), Rate);
	return false; // only normal (real-time) playback rate is supported
}

void FPlayer::OnAudioSample(const TSharedRef<IMediaAudioSample, ESPMode::ThreadSafe>& Sample)
{
	MediaSamples->AddAudio(Sample);
}

void FPlayer::OnVideoFrame(const TSharedRef<IMediaTextureSample, ESPMode::ThreadSafe>& Sample)
{
	int64 RtcNowUs = rtc::TimeMicros();
	FTimespan CaptureTs = Sample->GetDuration();
	double LatencyMs = (FTimespan::FromMicroseconds(RtcNowUs) - CaptureTs).GetTotalMilliseconds();
	// >= 1min latency doesn't make sense but can happen if stats are enabled during streaming, as player side tries to get timestamp
	// from the frame that doesn't have it yet
	const double Minute = 60 * 1000;
	if (LatencyMs > 0 && LatencyMs < Minute)
	{
		FHUDStats::Get().EndToEndLatencyMs.Update(LatencyMs);
	}

	UE_LOG(PixelPlayer, Verbose, TEXT("(%d) Sending frame for rendering: ts %lld, capture ts %lld, latency %.0f"), RtcTimeMs(), Sample->GetTime().GetTicks(), Sample->GetDuration().GetTicks(), LatencyMs);
	MediaSamples->AddVideo(Sample);
}
