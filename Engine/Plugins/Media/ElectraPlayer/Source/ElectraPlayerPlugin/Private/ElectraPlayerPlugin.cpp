// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/Optional.h"
#include "IMediaEventSink.h"
#include "IMediaOptions.h"
#include "MediaSamples.h"
#include "MediaPlayerOptions.h"

#include "ElectraPlayerPlugin.h"
#include "IElectraPlayerRuntimeModule.h"
#include "IElectraPlayerPluginModule.h"

using namespace Electra;

//-----------------------------------------------------------------------------

FElectraPlayerPlugin::FElectraPlayerPlugin()
	: OptionInterface(nullptr)
	, EventSink(nullptr)
{
	// Make sure a few assumptions are correct...
	static_assert((int32)EMediaEvent::MediaBuffering == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::MediaBuffering, "check alignment of both enums");
	static_assert((int32)EMediaEvent::MediaClosed == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::MediaClosed, "check alignment of both enums");
	static_assert((int32)EMediaEvent::MediaConnecting == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::MediaConnecting, "check alignment of both enums");
	static_assert((int32)EMediaEvent::MediaOpened == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::MediaOpened, "check alignment of both enums");
	static_assert((int32)EMediaEvent::MediaOpenFailed == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::MediaOpenFailed, "check alignment of both enums");
	static_assert((int32)EMediaEvent::PlaybackEndReached == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::PlaybackEndReached, "check alignment of both enums");
	static_assert((int32)EMediaEvent::PlaybackResumed == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::PlaybackResumed, "check alignment of both enums");
	static_assert((int32)EMediaEvent::PlaybackSuspended == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::PlaybackSuspended, "check alignment of both enums");
	static_assert((int32)EMediaEvent::SeekCompleted == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::SeekCompleted, "check alignment of both enums");
	static_assert((int32)EMediaEvent::TracksChanged == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::TracksChanged, "check alignment of both enums");
	static_assert((int32)EMediaEvent::Internal_PurgeVideoSamplesHint == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::Internal_PurgeVideoSamplesHint, "check alignment of both enums");
	static_assert((int32)EMediaEvent::Internal_ResetForDiscontinuity == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::Internal_ResetForDiscontinuity, "check alignment of both enums");
	static_assert((int32)EMediaEvent::Internal_RenderClockStart == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::Internal_RenderClockStart, "check alignment of both enums");
	static_assert((int32)EMediaEvent::Internal_RenderClockStop == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::Internal_RenderClockStop, "check alignment of both enums");
	static_assert((int32)EMediaEvent::Internal_VideoSamplesAvailable == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::Internal_VideoSamplesAvailable, "check alignment of both enums");
	static_assert((int32)EMediaEvent::Internal_VideoSamplesUnavailable == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::Internal_VideoSamplesUnavailable, "check alignment of both enums");
	static_assert((int32)EMediaEvent::Internal_AudioSamplesAvailable == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::Internal_AudioSamplesAvailable, "check alignment of both enums");
	static_assert((int32)EMediaEvent::Internal_AudioSamplesUnavailable == (int32)IElectraPlayerAdapterDelegate::EPlayerEvent::Internal_AudioSamplesUnavailable, "check alignment of both enums");

	static_assert((int32)EMediaState::Closed == (int32)IElectraPlayerInterface::EPlayerState::Closed, "check alignment of both enums");
	static_assert((int32)EMediaState::Error == (int32)IElectraPlayerInterface::EPlayerState::Error, "check alignment of both enums");
	static_assert((int32)EMediaState::Paused == (int32)IElectraPlayerInterface::EPlayerState::Paused, "check alignment of both enums");
	static_assert((int32)EMediaState::Playing == (int32)IElectraPlayerInterface::EPlayerState::Playing, "check alignment of both enums");
	static_assert((int32)EMediaState::Preparing == (int32)IElectraPlayerInterface::EPlayerState::Preparing, "check alignment of both enums");
	static_assert((int32)EMediaState::Stopped == (int32)IElectraPlayerInterface::EPlayerState::Stopped, "check alignment of both enums");

	static_assert((int32)EMediaStatus::None == (int32)IElectraPlayerInterface::EPlayerStatus::None, "check alignment of both enums");
	static_assert((int32)EMediaStatus::Buffering == (int32)IElectraPlayerInterface::EPlayerStatus::Buffering, "check alignment of both enums");
	static_assert((int32)EMediaStatus::Connecting == (int32)IElectraPlayerInterface::EPlayerStatus::Connecting, "check alignment of both enums");

	static_assert((int32)EMediaTrackType::Audio == (int32)IElectraPlayerInterface::EPlayerTrackType::Audio, "check alignment of both enums");
	static_assert((int32)EMediaTrackType::Video == (int32)IElectraPlayerInterface::EPlayerTrackType::Video, "check alignment of both enums");
}

bool FElectraPlayerPlugin::Initialize(IMediaEventSink& InEventSink,
	FElectraPlayerSendAnalyticMetricsDelegate& InSendAnalyticMetricsDelegate,
	FElectraPlayerSendAnalyticMetricsPerMinuteDelegate& InSendAnalyticMetricsPerMinuteDelegate,
	FElectraPlayerReportVideoStreamingErrorDelegate& InReportVideoStreamingErrorDelegate,
	FElectraPlayerReportSubtitlesMetricsDelegate& InReportSubtitlesFileMetricsDelegate)
{
	EventSink = &InEventSink;

	MediaSamples.Reset(new FMediaSamples);

	PlayerResourceDelegate.Reset(PlatformCreatePlayerResourceDelegate());

	PlayerDelegate = MakeShareable(new FPlayerAdapterDelegate(AsShared()));
	Player = MakeShareable(FElectraPlayerRuntimeFactory::CreatePlayer(PlayerDelegate, InSendAnalyticMetricsDelegate, InSendAnalyticMetricsPerMinuteDelegate, InReportVideoStreamingErrorDelegate, InReportSubtitlesFileMetricsDelegate));

	return true;
}

FElectraPlayerPlugin::~FElectraPlayerPlugin()
{
}

//-----------------------------------------------------------------------------

Electra::FVariantValue FElectraPlayerPlugin::FPlayerAdapterDelegate::QueryOptions(EOptionType Type, const Electra::FVariantValue & Param)
{
	if (TSharedPtr<FElectraPlayerPlugin, ESPMode::ThreadSafe> PinnedHost = Host.Pin())
	{
		switch (Type)
		{
		case	EOptionType::MaxVerticalStreamResolution:
			if (PinnedHost->OptionInterface)
			{
				return FVariantValue((int64)PinnedHost->OptionInterface->GetMediaOption(TEXT("MaxResolutionForMediaStreaming"), (int64)0));
			}
			break;

		case	EOptionType::MaxBandwidthForStreaming:
			if (PinnedHost->OptionInterface)
			{
				return FVariantValue((int64)PinnedHost->OptionInterface->GetMediaOption(TEXT("ElectraMaxStreamingBandwidth"), (int64)0));
			}
			break;

		case EOptionType::PlayListData:
			if (PinnedHost->OptionInterface)
			{
				const FName PlaylistOptionKey = TEXT("ElectraGetPlaylistData");
				if (PinnedHost->OptionInterface->HasMediaOption(PlaylistOptionKey))
				{
					check(Param.IsType(FVariantValue::EDataType::TypeFString));
					return FVariantValue(PinnedHost->OptionInterface->GetMediaOption(PlaylistOptionKey, Param.GetFString()));
				}
			}
			break;

		case EOptionType::LicenseKeyData:
			if (PinnedHost->OptionInterface)
			{
				const FName LicenseKeyDataOptionKey = TEXT("ElectraGetLicenseKeyData");
				if (PinnedHost->OptionInterface->HasMediaOption(LicenseKeyDataOptionKey))
				{
					check(Param.IsType(FVariantValue::EDataType::TypeFString));
					return FVariantValue(PinnedHost->OptionInterface->GetMediaOption(LicenseKeyDataOptionKey, Param.GetFString()));
				}
			}
			break;

		case EOptionType::PlaystartPosFromSeekPositions:
			if (PinnedHost->OptionInterface)
			{
				const FName PlaystartOptionKey = TEXT("ElectraGetPlaystartPosFromSeekPositions");
				if (PinnedHost->OptionInterface->HasMediaOption(PlaystartOptionKey))
				{
					check(Param.IsType(FVariantValue::EDataType::TypeSharedPointer));

					TSharedPtr<TArray<FTimespan>, ESPMode::ThreadSafe> PosArray = Param.GetSharedPointer<TArray<FTimespan>>();
					if (PosArray.IsValid())
					{
						TSharedPtr<FElectraSeekablePositions, ESPMode::ThreadSafe> Res = StaticCastSharedPtr<FElectraSeekablePositions, IMediaOptions::FDataContainer, ESPMode::ThreadSafe>(PinnedHost->OptionInterface->GetMediaOption(PlaystartOptionKey, MakeShared<FElectraSeekablePositions, ESPMode::ThreadSafe>(*PosArray)));
						if (Res.IsValid() && Res->Data.Num())
						{
							return FVariantValue(int64(Res->Data[0].GetTicks())); // return HNS
						}
					}
					return FVariantValue();
				}
			}
			break;

		default:
			break;
		}
	}
	return FVariantValue();
}


void FElectraPlayerPlugin::FPlayerAdapterDelegate::SendMediaEvent(EPlayerEvent Event)
{
	if (TSharedPtr<FElectraPlayerPlugin, ESPMode::ThreadSafe> PinnedHost = Host.Pin())
	{
		if (PinnedHost->EventSink)
		{
			PinnedHost->EventSink->ReceiveMediaEvent((EMediaEvent)Event);
		}
	}
}


void FElectraPlayerPlugin::FPlayerAdapterDelegate::OnVideoFlush()
{
	if (TSharedPtr<FElectraPlayerPlugin, ESPMode::ThreadSafe> PinnedHost = Host.Pin())
	{
		TRange<FTimespan> AllTime(FTimespan::MinValue(), FTimespan::MaxValue());
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> FlushSample;
		while (PinnedHost->GetSamples().FetchVideo(AllTime, FlushSample))
			;
	}
}


void FElectraPlayerPlugin::FPlayerAdapterDelegate::OnAudioFlush()
{
	if (TSharedPtr<FElectraPlayerPlugin, ESPMode::ThreadSafe> PinnedHost = Host.Pin())
	{
		TRange<FTimespan> AllTime(FTimespan::MinValue(), FTimespan::MaxValue());
		TSharedPtr<IMediaAudioSample, ESPMode::ThreadSafe> FlushSample;
		while (PinnedHost->GetSamples().FetchAudio(AllTime, FlushSample))
			;
	}
}


void FElectraPlayerPlugin::FPlayerAdapterDelegate::PresentVideoFrame(const FVideoDecoderOutputPtr & InVideoFrame)
{
	if (TSharedPtr<FElectraPlayerPlugin, ESPMode::ThreadSafe> PinnedHost = Host.Pin())
	{
		FElectraTextureSampleRef TextureSample = PinnedHost->OutputTexturePool.AcquireShared();
		TextureSample->Initialize(InVideoFrame.Get());
		PinnedHost->MediaSamples->AddVideo(TextureSample);
	}
}


void FElectraPlayerPlugin::FPlayerAdapterDelegate::PresentAudioFrame(const IAudioDecoderOutputPtr& InAudioFrame)
{
	if (TSharedPtr<FElectraPlayerPlugin, ESPMode::ThreadSafe> PinnedHost = Host.Pin())
	{
		TSharedRef<FElectraPlayerAudioSample, ESPMode::ThreadSafe> AudioSample = PinnedHost->OutputAudioPool.AcquireShared();
		AudioSample->Initialize(InAudioFrame);
		PinnedHost->MediaSamples->AddAudio(AudioSample);
	}
}


bool FElectraPlayerPlugin::FPlayerAdapterDelegate::CanReceiveVideoSamples(int32 NumFrames)
{
	if (TSharedPtr<FElectraPlayerPlugin, ESPMode::ThreadSafe> PinnedHost = Host.Pin())
	{
		return PinnedHost->MediaSamples->CanReceiveVideoSamples(NumFrames);
	}
	return false;
}


bool FElectraPlayerPlugin::FPlayerAdapterDelegate::CanReceiveAudioSamples(int32 NumFrames)
{
	if (TSharedPtr<FElectraPlayerPlugin, ESPMode::ThreadSafe> PinnedHost = Host.Pin())
	{
		return PinnedHost->MediaSamples->CanReceiveAudioSamples(NumFrames);
	}
	return false;
}

void FElectraPlayerPlugin::FPlayerAdapterDelegate::PrepareForDecoderShutdown()
{
	if (TSharedPtr<FElectraPlayerPlugin, ESPMode::ThreadSafe> PinnedHost = Host.Pin())
	{
		PinnedHost->OutputTexturePool.PrepareForDecoderShutdown();
	}
}


FString FElectraPlayerPlugin::FPlayerAdapterDelegate::GetVideoAdapterName() const
{
	return GRHIAdapterName;
}


IElectraPlayerResourceDelegate* FElectraPlayerPlugin::FPlayerAdapterDelegate::GetResourceDelegate() const
{
	if (TSharedPtr<FElectraPlayerPlugin, ESPMode::ThreadSafe> PinnedHost = Host.Pin())
	{
		return PinnedHost->PlayerResourceDelegate.Get();
	}
	return nullptr;
}

//-----------------------------------------------------------------------------

// IMediaPlayer interface

FGuid FElectraPlayerPlugin::GetPlayerPluginGUID() const
{
	static FGuid PlayerPluginGUID(0x94ee3f80, 0x8e604292, 0xb4d24dd5, 0xfdade1c2);
	return PlayerPluginGUID;
}

FString FElectraPlayerPlugin::GetInfo() const
{
	return TEXT("No information available");
}

IMediaSamples& FElectraPlayerPlugin::GetSamples()
{
	return *MediaSamples;
}

FString FElectraPlayerPlugin::GetStats() const
{
	return TEXT("ElectraPlayer: GetStats: <empty>?");
}

IMediaTracks& FElectraPlayerPlugin::GetTracks()
{
	return *this;
}

bool FElectraPlayerPlugin::Open(const FString& Url, const IMediaOptions* Options)
{
	return Open(Url, Options, nullptr);
}

bool FElectraPlayerPlugin::Open(const FString& Url, const IMediaOptions* Options, const FMediaPlayerOptions* InPlayerOptions)
{
	// Remember the option interface to poll for changes during playback.
	OptionInterface = Options;

	IElectraPlayerInterface::FPlaystartOptions LocalPlaystartOptions;

	// Get playstart options from passed options, if they exist.
	if (InPlayerOptions)
	{
		LocalPlaystartOptions.TimeOffset = InPlayerOptions->SeekTime;
		LocalPlaystartOptions.AudioTrackIndex = InPlayerOptions->Tracks.Audio;
	}

	// Set up options to initialize the internal player with.
	Electra::FParamDict PlayerOptions;
	//	PlayerOptions.Set("initial_bitrate", Electra::FVariantValue((int64)2000000));

		// Check for one-time initialization options that can't be changed during playback.
		/*
		int64 MinStreamFPS = Options->GetMediaOption(TEXT("MinStreamFPSForMediaStreaming"), (int64)-1);
		if (MinStreamFPS > 0)
		{
			PlayerOptions.Set("min_stream_fps", Electra::FVariantValue(MinStreamFPS));
			UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p][%p] IMediaPlayer::Open: Limiting min stream fps to %d"), this, AdaptivePlayer.Get(), (int32)MinStreamFPS);
		}

		int64 MaxStreamFPS = Options->GetMediaOption(TEXT("MaxStreamFPSForMediaStreaming"), (int64)-1);
		if (MaxStreamFPS > 0)
		{
			PlayerOptions.Set("max_stream_fps", Electra::FVariantValue(MaxStreamFPS));
			UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p][%p] IMediaPlayer::Open: Limiting max stream fps to %d"), this, AdaptivePlayer.Get(), (int32)MaxStreamFPS);
		}
		*/
	int64 MaxVerticalHeight = Options->GetMediaOption(TEXT("MaxElectraVerticalResolution"), (int64)-1);
	if (MaxVerticalHeight > 0)
	{
		PlayerOptions.Set("max_resoY", Electra::FVariantValue(MaxVerticalHeight));
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaPlayer::Open: Limiting vertical resolution to %d for all streams"), this, (int32)MaxVerticalHeight);
	}
	int64 MaxVerticalHeightAt60 = Options->GetMediaOption(TEXT("MaxElectraVerticalResolutionOf60fpsVideos"), (int64)-1);
	if (MaxVerticalHeightAt60 > 0)
	{
		PlayerOptions.Set("max_resoY_above_30fps", Electra::FVariantValue(MaxVerticalHeightAt60));
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaPlayer::Open: Limiting vertical resolution to %d for streams >30fps"), this, (int32)MaxVerticalHeightAt60);
	}

	int64 MaxVerticalHeightForWindowsSoftware = Options->GetMediaOption(TEXT("MaxElectraVerticalResolutionOfWindowsSWD"), (int64)-1);
	if (MaxVerticalHeightForWindowsSoftware > 0)
	{
		PlayerOptions.Set("max_resoY_windows_software", Electra::FVariantValue(MaxVerticalHeightForWindowsSoftware));
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaPlayer::Open: Limiting vertical resolution to %d for Windows software decoding"), this, (int32)MaxVerticalHeightForWindowsSoftware);
	}

	double LiveEdgeDistanceForNormalPresentation = Options->GetMediaOption(TEXT("ElectraLivePresentationOffset"), (double)-1.0);
	if (LiveEdgeDistanceForNormalPresentation > 0.0)
	{
		PlayerOptions.Set("seekable_range_live_end_offset", Electra::FVariantValue(Electra::FTimeValue().SetFromSeconds(LiveEdgeDistanceForNormalPresentation)));
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaPlayer::Open: Setting distance to live edge for normal presentations to %.3f seconds"), this, LiveEdgeDistanceForNormalPresentation);
	}
	double LiveEdgeDistanceForAudioOnlyPresentation = Options->GetMediaOption(TEXT("ElectraLiveAudioPresentationOffset"), (double)-1.0);
	if (LiveEdgeDistanceForAudioOnlyPresentation > 0.0)
	{
		PlayerOptions.Set("seekable_range_live_end_offset_audioonly", Electra::FVariantValue(Electra::FTimeValue().SetFromSeconds(LiveEdgeDistanceForAudioOnlyPresentation)));
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaPlayer::Open: Setting distance to live edge for audio-only presentation to %.3f seconds"), this, LiveEdgeDistanceForAudioOnlyPresentation);
	}
	bool bUseConservativeLiveEdgeDistance = Options->GetMediaOption(TEXT("ElectraLiveUseConservativePresentationOffset"), (bool)false);
	if (bUseConservativeLiveEdgeDistance)
	{
		PlayerOptions.Set("seekable_range_live_end_offset_conservative", Electra::FVariantValue(bUseConservativeLiveEdgeDistance));
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaPlayer::Open: Using conservative live edge for distance calculation"), this);
	}
	bool bDontAllowInsecureForMediaSegments = Options->GetMediaOption(TEXT("ElectraDontAllowInsecureForEncryptedMediaSegments"), (bool)false);
	if (bDontAllowInsecureForMediaSegments)
	{
		PlayerOptions.Set("dont_use_insecure_for_media_segments", Electra::FVariantValue(bDontAllowInsecureForMediaSegments));
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaPlayer::Open: Do not allow downgrade to http for encrypted media segments"), this);
	}
	bool bDontAllowInsecureForInitSegments = Options->GetMediaOption(TEXT("ElectraDontAllowInsecureForInitSegments"), (bool)false);
	if (bDontAllowInsecureForInitSegments)
	{
		PlayerOptions.Set("dont_use_insecure_for_init_segments", Electra::FVariantValue(bDontAllowInsecureForInitSegments));
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaPlayer::Open: Do not allow downgrade to http for init segments"), this);
	}
	bool bThrowErrorWhenRebuffering = Options->GetMediaOption(TEXT("ElectraThrowErrorWhenRebuffering"), (bool)false);
	if (bThrowErrorWhenRebuffering)
	{
		PlayerOptions.Set("throw_error_when_rebuffering", Electra::FVariantValue(bThrowErrorWhenRebuffering));
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaPlayer::Open: Throw playback error when rebuffering"), this);
	}

	// Check for options that can be changed during playback and apply them at startup already.
	// If a media source supports the MaxResolutionForMediaStreaming option then we can override the max resolution.
	int64 DefaultValue = 0;
	int64 MaxVerticalStreamResolution = Options->GetMediaOption(TEXT("MaxResolutionForMediaStreaming"), DefaultValue);
	if (MaxVerticalStreamResolution != 0)
	{
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaPlayer::Open: Limiting max resolution to %d"), this, (int32)MaxVerticalStreamResolution);
		LocalPlaystartOptions.MaxVerticalStreamResolution = (int32)MaxVerticalStreamResolution;
	}

	int64 MaxBandwidthForStreaming = Options->GetMediaOption(TEXT("ElectraMaxStreamingBandwidth"), (int64)0);
	if (MaxBandwidthForStreaming > 0)
	{
		UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] Limiting max streaming bandwidth to %d bps"), this, (int32)MaxBandwidthForStreaming);
		LocalPlaystartOptions.MaxBandwidthForStreaming = (int32)MaxBandwidthForStreaming;
	}

	return Player->OpenInternal(Url, PlayerOptions, LocalPlaystartOptions);
}

//-----------------------------------------------------------------------------
/**
 *
 */
bool FElectraPlayerPlugin::Open(const TSharedRef<FArchive, ESPMode::ThreadSafe>& Archive, const FString& OriginalUrl, const IMediaOptions* /*Options*/)
{
	// we support playback only from an external file, not from a "resource" (e.g. a packaged asset)
	UE_LOG(LogElectraPlayerPlugin, Error, TEXT("[%p] IMediaPlayer::Archive"), this);
	unimplemented();
	return false;
}


//-----------------------------------------------------------------------------
/**
*	Internal Close / Shutdown player
*/
void FElectraPlayerPlugin::Close()
{
	Player->CloseInternal(true);
}

//-----------------------------------------------------------------------------
/**
 *	Tick the player from the game thread
 */
void FElectraPlayerPlugin::TickInput(FTimespan DeltaTime, FTimespan Timecode)
{
	OutputTexturePool.Tick();
	Player->Tick(DeltaTime, Timecode);
}

//-----------------------------------------------------------------------------
/**
	Get special feature flags states
*/
bool FElectraPlayerPlugin::GetPlayerFeatureFlag(EFeatureFlag flag) const
{
	switch(flag)
	{
		case EFeatureFlag::AllowShutdownOnClose:
			return Player->IsKillAfterCloseAllowed();
		case EFeatureFlag::UsePlaybackTimingV2:
			return true;
		default:
			break;
	}
	return IMediaPlayer::GetPlayerFeatureFlag(flag);
}

//////////////////////////////////////////////////////////////////////////
// IMediaControl impl

//-----------------------------------------------------------------------------
/**
 *	Currently, we cannot do anything.. well, at least we can play!
 */
bool FElectraPlayerPlugin::CanControl(EMediaControl Control) const
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

	if (Control == EMediaControl::Seek)
	{
		return CurrentState == EMediaState::Playing || CurrentState == EMediaState::Paused || CurrentState == EMediaState::Stopped ;//|| CurrentState == Preparing;
	}

	return false;
}

//-----------------------------------------------------------------------------
/**
 *	Rate is only real-time
 */
float FElectraPlayerPlugin::GetRate() const
{
	return Player->GetRate();
}

//-----------------------------------------------------------------------------
/**
 *	Expose player state
 */
EMediaState FElectraPlayerPlugin::GetState() const
{
	return (EMediaState)Player->GetState();
}

//-----------------------------------------------------------------------------
/**
 *	Expose player status
 */
EMediaStatus FElectraPlayerPlugin::GetStatus() const
{
	return (EMediaStatus)Player->GetStatus();
}


bool FElectraPlayerPlugin::IsLooping() const
{
	return Player->IsLooping();
}


bool FElectraPlayerPlugin::SetLooping(bool bLooping)
{
	return Player->SetLooping(bLooping);
}

//-----------------------------------------------------------------------------
/**
 *	Only return real-time playback for the moment..
 */
TRangeSet<float> FElectraPlayerPlugin::GetSupportedRates(EMediaRateThinning Thinning) const
{
	TRangeSet<float> Res;
	Res.Add(TRange<float>{1.0f}); // only normal (real-time) playback rate
	Res.Add(TRange<float>{0.0f}); // and pause
	return Res;
}


FTimespan FElectraPlayerPlugin::GetTime() const
{
	return Player->GetTime();
}


FTimespan FElectraPlayerPlugin::GetDuration() const
{
	return Player->GetDuration();
}


bool FElectraPlayerPlugin::SetRate(float Rate)
{
	UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaControls::SetRate(%f)"), this, Rate);
	CSV_EVENT(ElectraPlayer, TEXT("Setting Rate"));

	return Player->SetRate(Rate);
}


bool FElectraPlayerPlugin::Seek(const FTimespan& Time)
{
	UE_LOG(LogElectraPlayerPlugin, Log, TEXT("[%p] IMediaControls::Seek() to %s"), this, *Time.ToString(TEXT("%h:%m:%s.%f")));
	CSV_EVENT(ElectraPlayer, TEXT("Seeking"));

	return Player->Seek(Time);
}


bool FElectraPlayerPlugin::GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaAudioTrackFormat& OutFormat) const
{
	IElectraPlayerInterface::FAudioTrackFormat Format;
	if (!Player->GetAudioTrackFormat(TrackIndex, FormatIndex, Format))
	{
		return false;
	}
	OutFormat.BitsPerSample = Format.BitsPerSample;
	OutFormat.NumChannels = Format.NumChannels;
	OutFormat.SampleRate = Format.SampleRate;
	OutFormat.TypeName = Format.TypeName;
	return true;
}


bool FElectraPlayerPlugin::GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FMediaVideoTrackFormat& OutFormat) const
{
	IElectraPlayerInterface::FVideoTrackFormat Format;
	if (!Player->GetVideoTrackFormat(TrackIndex, FormatIndex, Format))
	{
		return false;
	}
	OutFormat.Dim = Format.Dim;
	OutFormat.FrameRate = Format.FrameRate;
	OutFormat.FrameRates = Format.FrameRates;
	OutFormat.TypeName = Format.TypeName;
	return true;
}


int32 FElectraPlayerPlugin::GetNumTracks(EMediaTrackType TrackType) const
{
	return Player->GetNumTracks((IElectraPlayerInterface::EPlayerTrackType)TrackType);
}


int32 FElectraPlayerPlugin::GetNumTrackFormats(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return Player->GetNumTrackFormats((IElectraPlayerInterface::EPlayerTrackType)TrackType, TrackIndex);
}


int32 FElectraPlayerPlugin::GetSelectedTrack(EMediaTrackType TrackType) const
{
	return Player->GetSelectedTrack((IElectraPlayerInterface::EPlayerTrackType)TrackType);
}


FText FElectraPlayerPlugin::GetTrackDisplayName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return Player->GetTrackDisplayName((IElectraPlayerInterface::EPlayerTrackType)TrackType, TrackIndex);
}


int32 FElectraPlayerPlugin::GetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return Player->GetTrackFormat((IElectraPlayerInterface::EPlayerTrackType)TrackType, TrackIndex);
}


FString FElectraPlayerPlugin::GetTrackLanguage(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return Player->GetTrackLanguage((IElectraPlayerInterface::EPlayerTrackType)TrackType, TrackIndex);
}


FString FElectraPlayerPlugin::GetTrackName(EMediaTrackType TrackType, int32 TrackIndex) const
{
	return Player->GetTrackName((IElectraPlayerInterface::EPlayerTrackType)TrackType, TrackIndex);
}


bool FElectraPlayerPlugin::SelectTrack(EMediaTrackType TrackType, int32 TrackIndex)
{
	return Player->SelectTrack((IElectraPlayerInterface::EPlayerTrackType)TrackType, TrackIndex);
}


bool FElectraPlayerPlugin::SetTrackFormat(EMediaTrackType TrackType, int32 TrackIndex, int32 FormatIndex)
{
	return false;
}


bool FElectraPlayerPlugin::SetVideoTrackFrameRate(int32 TrackIndex, int32 FormatIndex, float FrameRate)
{
	return false;
}


void FElectraPlayerPlugin::SetLastAudioRenderedSampleTime(FTimespan SampleTime)
{
	// No-op.
}


