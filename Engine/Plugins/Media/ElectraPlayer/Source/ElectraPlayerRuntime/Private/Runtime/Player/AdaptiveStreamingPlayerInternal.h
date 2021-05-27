// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Player/AdaptiveStreamingPlayer.h"
#include "Player/Manifest.h"
#include "Player/AdaptiveStreamingPlayerInternalConfig.h"

#include "HTTP/HTTPManager.h"
#include "Player/PlayerSessionServices.h"
#include "SynchronizedClock.h"

#include "Demuxer/ParserISO14496-12.h"

#include "Player/AdaptiveStreamingPlayerResourceRequest.h"
#include "Player/PlayerStreamReader.h"
#include "Player/PlaylistReader.h"
#include "Player/PlayerStreamFilter.h"
#include "Player/PlayerEntityCache.h"

#include "ElectraCDM.h"

#include "InfoLog.h"

#define INTERR_ALL_STREAMS_HAVE_FAILED			1
#define INTERR_UNSUPPORTED_FORMAT				2
#define INTERR_COULD_NOT_LOCATE_START_SEGMENT	3
#define INTERR_COULD_NOT_LOCATE_START_PERIOD	4
#define INTERR_FRAGMENT_NOT_AVAILABLE			0x101
#define INTERR_FRAGMENT_READER_REQUEST			0x102
#define INTERR_CREATE_FRAGMENT_READER			0x103
#define INTERR_REBUFFER_SHALL_THROW_ERROR		0x200


namespace Electra
{
class FAdaptiveStreamingPlayer;


class FMediaRenderClock : public IMediaRenderClock
{
public:
	FMediaRenderClock()
		: bIsPaused(true)
	{
	}

	virtual ~FMediaRenderClock()
	{
	}

	virtual void SetCurrentTime(ERendererType ForRenderer, const FTimeValue& CurrentRenderTime) override
	{
		FClock* Clk = GetClock(ForRenderer);
		if (Clk)
		{
			FTimeValue Now(MEDIAutcTime::Current());
			FMediaCriticalSection::ScopedLock lock(Lock);
			Clk->RenderTime = CurrentRenderTime;
			Clk->LastSystemBaseTime = Now;
			Clk->RunningTimeOffset.SetToZero();
		}
	}

	virtual FTimeValue GetInterpolatedRenderTime(ERendererType FromRenderer) override
	{
		FClock* Clk = GetClock(FromRenderer);
		if (Clk)
		{
			FTimeValue Now(MEDIAutcTime::Current());
			FMediaCriticalSection::ScopedLock lock(Lock);
			FTimeValue diff = !bIsPaused ? Now - Clk->LastSystemBaseTime : FTimeValue::GetZero();
			return Clk->RenderTime + Clk->RunningTimeOffset + diff;
		}
		return FTimeValue();
	}

	void Start()
	{
		FTimeValue Now(MEDIAutcTime::Current());
		FMediaCriticalSection::ScopedLock lock(Lock);
		if (bIsPaused)
		{
			for(int32 i = 0; i < FMEDIA_STATIC_ARRAY_COUNT(Clock); ++i)
			{
				Clock[i].LastSystemBaseTime = Now;
			}
			bIsPaused = false;
		}
	}

	void Stop()
	{
		FTimeValue Now(MEDIAutcTime::Current());
		FMediaCriticalSection::ScopedLock lock(Lock);
		if (!bIsPaused)
		{
			bIsPaused = true;
			for(int32 i=0; i<FMEDIA_STATIC_ARRAY_COUNT(Clock); ++i)
			{
				FTimeValue diff = Now - Clock[i].LastSystemBaseTime;
				Clock[i].RunningTimeOffset += diff;
			}
		}
	}

	bool IsRunning() const
	{
		return !bIsPaused;
	}

private:
	struct FClock
	{
		FClock()
		{
			LastSystemBaseTime = MEDIAutcTime::Current();
			RunningTimeOffset.SetToZero();
		}
		FTimeValue	RenderTime;
		FTimeValue	LastSystemBaseTime;
		FTimeValue	RunningTimeOffset;
	};

	FClock* GetClock(ERendererType ForRenderer)
	{
		FClock* Clk = nullptr;
		switch(ForRenderer)
		{
			case IMediaRenderClock::ERendererType::Video:
				Clk = &Clock[0];
				break;
			case IMediaRenderClock::ERendererType::Audio:
				Clk = &Clock[1];
				break;
			case IMediaRenderClock::ERendererType::Subtitles:
				Clk = &Clock[2];
				break;
			default:
				Clk = nullptr;
				break;
		}
		return Clk;
	}

	FMediaCriticalSection		Lock;
	FClock			Clock[3];
	bool			bIsPaused;
};



/**
 * Interchange structure between the player worker thread and the public API to
 * avoid mutex locks all over the place.
 */
struct FPlaybackState
{
	FPlaybackState()
	{
		Reset();
	}
	void Reset()
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		SeekableRange.Reset();
		Duration.SetToInvalid();
		CurrentPlayPosition.SetToInvalid();
		LoopState.Reset();
		bHaveMetadata = false;
		bHasEnded     = false;
		bIsSeeking    = false;
		bIsBuffering  = false;
		bIsPlaying    = false;
		bIsPaused     = false;
	}

	mutable FMediaCriticalSection			Lock;
	FTimeRange								SeekableRange;
	FTimeRange								TimelineRange;
	FTimeValue								Duration;
	FTimeValue								CurrentPlayPosition;
	FPlayerLoopState						LoopState;
	bool									bHaveMetadata;
	bool									bHasEnded;
	bool									bIsSeeking;
	bool									bIsBuffering;
	bool									bIsPlaying;
	bool									bIsPaused;
	TArray<FTrackMetadata>					VideoTracks;
	TArray<FTrackMetadata>					AudioTracks;
	TArray<FTimespan>						SeekablePositions;


	void SetSeekableRange(const FTimeRange& TimeRange)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		SeekableRange = TimeRange;
	}
	void GetSeekableRange(FTimeRange& OutRange) const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		OutRange = SeekableRange;
	}

	void SetSeekablePositions(const TArray<FTimespan>& InSeekablePositions)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		SeekablePositions = InSeekablePositions;
	}
	void GetSeekablePositions(TArray<FTimespan>& OutSeekablePositions) const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		OutSeekablePositions = SeekablePositions;
	}

	void SetTimelineRange(const FTimeRange& TimeRange)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		TimelineRange = TimeRange;
	}
	void GetTimelineRange(FTimeRange& OutRange) const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		OutRange = TimelineRange;
	}

	void SetDuration(const FTimeValue& InDuration)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		Duration = InDuration;
	}
	FTimeValue GetDuration() const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		return Duration;
	}

	void SetPlayPosition(const FTimeValue& InPosition)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		CurrentPlayPosition = InPosition;
	}
	FTimeValue GetPlayPosition() const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		return CurrentPlayPosition;
	}

	void SetHaveMetadata(bool bInHaveMetadata)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		bHaveMetadata = bInHaveMetadata;
	}
	bool GetHaveMetadata() const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		return bHaveMetadata;
	}

	void SetHasEnded(bool bInHasEnded)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		bHasEnded = bInHasEnded;
	}
	bool GetHasEnded() const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		return bHasEnded;
	}

	void SetIsSeeking(bool bInIsSeeking)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		bIsSeeking = bInIsSeeking;
	}
	bool GetIsSeeking() const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		return bIsSeeking;
	}

	void SetIsBuffering(bool bInIsBuffering)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		bIsBuffering = bInIsBuffering;
	}
	bool GetIsBuffering() const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		return bIsBuffering;
	}

	void SetIsPlaying(bool bInIsPlaying)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		bIsPlaying = bInIsPlaying;
	}
	bool GetIsPlaying() const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		return bIsPlaying;
	}

	void SetIsPaused(bool bInIsPaused)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		bIsPaused = bInIsPaused;
	}
	bool GetIsPaused() const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		return bIsPaused;
	}

	void SetPausedAndPlaying(bool bInIsPaused, bool bInIsPlaying)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		bIsPaused  = bInIsPaused;
		bIsPlaying = bInIsPlaying;
	}

	bool SetTrackMetadata(const TArray<FTrackMetadata> &InVideoTracks, const TArray<FTrackMetadata>& InAudioTracks)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);

		auto ChangedTrackMetadata = [](const TArray<FTrackMetadata> &These, const TArray<FTrackMetadata>& Other) -> bool
		{
			if (These.Num() == Other.Num())
			{
				for(int32 i=0; i<These.Num(); ++i)
				{
					if (!These[i].Equals(Other[i]))
					{
						return true;
					}
				}
				return false;
			}
			return true;
		};

		bool bChanged = ChangedTrackMetadata(VideoTracks, InVideoTracks) || ChangedTrackMetadata(AudioTracks, InAudioTracks);
		VideoTracks = InVideoTracks;
		AudioTracks = InAudioTracks;
		return bChanged;
	}
	void GetTrackMetadata(TArray<FTrackMetadata> &OutVideoTracks, TArray<FTrackMetadata>& OutAudioTracks) const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		OutVideoTracks = VideoTracks;
		OutAudioTracks = AudioTracks;
	}

	void SetLoopState(const FPlayerLoopState& InLoopState)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		LoopState = InLoopState;
	}
	void GetLoopState(FPlayerLoopState& OutLoopState) const
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		OutLoopState = LoopState;
	}
	void SetLoopStateEnable(bool bEnable)
	{
		FMediaCriticalSection::ScopedLock lock(Lock);
		LoopState.bLoopEnabled = bEnable;
	}

};






struct FMetricEvent
{
	enum class EType
	{
		OpenSource,
		ReceivedMasterPlaylist,
		ReceivedPlaylists,
		TracksChanged,
		BufferingStart,
		BufferingEnd,
		Bandwidth,
		BufferUtilization,
		PlaylistDownload,
		SegmentDownload,
		DataAvailabilityChange,
		VideoQualityChange,
		PrerollStart,
		PrerollEnd,
		PlaybackStart,
		PlaybackPaused,
		PlaybackResumed,
		PlaybackEnded,
		PlaybackJumped,
		PlaybackStopped,
		LicenseKey,
		Errored,
		LogMessage,
	};

	struct FParam
	{
		struct FBandwidth
		{
			int64	EffectiveBps;
			int64	ThroughputBps;
			double	Latency;
		};
		struct FQualityChange
		{
			int32	NewBitrate;
			int32	PrevBitrate;
			bool	bIsDrastic;
		};
		struct FLogMessage
		{
			FString				Message;
			IInfoLog::ELevel	Level;
			int64				AtMillis;
		};
		struct FTimeJumped
		{
			FTimeValue					ToNewTime;
			FTimeValue					FromTime;
			Metrics::ETimeJumpReason	Reason;

		};
		FString								URL;
		Metrics::EBufferingReason			BufferingReason;
		Metrics::FBufferStats				BufferStats;
		Metrics::FPlaylistDownloadStats		PlaylistStats;
		Metrics::FSegmentDownloadStats		SegmentStats;
		Metrics::FLicenseKeyStats			LicenseKeyStats;
		Metrics::FDataAvailabilityChange	DataAvailability;
		FBandwidth							Bandwidth;
		FQualityChange						QualityChange;
		FTimeJumped							TimeJump;
		FErrorDetail						ErrorDetail;
		FLogMessage							LogMessage;
	};

	EType			Type;
	FParam			Param;
	TWeakPtr<FAdaptiveStreamingPlayer, ESPMode::ThreadSafe>	Player;
	FMediaEvent	*EventSignal = nullptr;

	static TSharedPtrTS<FMetricEvent> ReportOpenSource(const FString& URL)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::OpenSource;
		Evt->Param.URL = URL;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportReceivedMasterPlaylist(const FString& EffectiveURL)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::ReceivedMasterPlaylist;
		Evt->Param.URL = EffectiveURL;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportReceivedPlaylists()
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::ReceivedPlaylists;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportTracksChanged()
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::TracksChanged;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportBufferingStart(Metrics::EBufferingReason BufferingReason)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::BufferingStart;
		Evt->Param.BufferingReason = BufferingReason;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportBufferingEnd(Metrics::EBufferingReason BufferingReason)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::BufferingEnd;
		Evt->Param.BufferingReason = BufferingReason;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportBandwidth(int64 EffectiveBps, int64 ThroughputBps, double LatencyInSeconds)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::Bandwidth;
		Evt->Param.Bandwidth.EffectiveBps = EffectiveBps;
		Evt->Param.Bandwidth.ThroughputBps = ThroughputBps;
		Evt->Param.Bandwidth.Latency = LatencyInSeconds;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportBufferUtilization(const Metrics::FBufferStats& BufferStats)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::BufferUtilization;
		Evt->Param.BufferStats = BufferStats;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportPlaylistDownload(const Metrics::FPlaylistDownloadStats& PlaylistDownloadStats)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::PlaylistDownload;
		Evt->Param.PlaylistStats = PlaylistDownloadStats;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportSegmentDownload(const Metrics::FSegmentDownloadStats& SegmentDownloadStats)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::SegmentDownload;
		Evt->Param.SegmentStats = SegmentDownloadStats;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportLicenseKey(const Metrics::FLicenseKeyStats& LicenseKeyStats)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::LicenseKey;
		Evt->Param.LicenseKeyStats = LicenseKeyStats;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportDataAvailabilityChange(const Metrics::FDataAvailabilityChange& DataAvailability)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::DataAvailabilityChange;
		Evt->Param.DataAvailability = DataAvailability;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportVideoQualityChange(int32 NewBitrate, int32 PreviousBitrate, bool bIsDrasticDownswitch)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::VideoQualityChange;
		Evt->Param.QualityChange.NewBitrate = NewBitrate;
		Evt->Param.QualityChange.PrevBitrate = PreviousBitrate;
		Evt->Param.QualityChange.bIsDrastic = bIsDrasticDownswitch;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportPrerollStart()
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::PrerollStart;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportPrerollEnd()
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::PrerollEnd;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportPlaybackStart()
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::PlaybackStart;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportPlaybackPaused()
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::PlaybackPaused;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportPlaybackResumed()
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::PlaybackResumed;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportPlaybackEnded()
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::PlaybackEnded;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportJumpInPlayPosition(const FTimeValue& ToNewTime, const FTimeValue& FromTime, Metrics::ETimeJumpReason TimejumpReason)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::PlaybackJumped;
		Evt->Param.TimeJump.ToNewTime = ToNewTime;
		Evt->Param.TimeJump.FromTime = FromTime;
		Evt->Param.TimeJump.Reason = TimejumpReason;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportPlaybackStopped()
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::PlaybackStopped;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportError(const FErrorDetail& ErrorDetail)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::Errored;
		Evt->Param.ErrorDetail = ErrorDetail;
		return Evt;
	}
	static TSharedPtrTS<FMetricEvent> ReportLogMessage(IInfoLog::ELevel inLogLevel, const FString& LogMessage, int64 PlayerWallclockMilliseconds)
	{
		TSharedPtrTS<FMetricEvent> Evt = MakeSharedTS<FMetricEvent>();
		Evt->Type = EType::LogMessage;
		Evt->Param.LogMessage.Level = inLogLevel;
		Evt->Param.LogMessage.Message = LogMessage;
		Evt->Param.LogMessage.AtMillis = PlayerWallclockMilliseconds;
		return Evt;
	}

};



class FAdaptiveStreamingPlayerEventHandler
{
public:
	static TSharedPtr<FAdaptiveStreamingPlayerEventHandler, ESPMode::ThreadSafe> Create();

	void DispatchEvent(TSharedPtrTS<FMetricEvent> InEvent);
	void DispatchEventAndWait(TSharedPtrTS<FMetricEvent> InEvent);

	virtual ~FAdaptiveStreamingPlayerEventHandler();
private:
	void StartWorkerThread();
	void StopWorkerThread();
	void WorkerThreadFN();

	FMediaThread													WorkerThread;
	TMediaMessageQueueDynamicNoTimeout<TSharedPtrTS<FMetricEvent>>		EventQueue;
	static TWeakPtr<FAdaptiveStreamingPlayerEventHandler, ESPMode::ThreadSafe>	SingletonSelf;
	static FCriticalSection														SingletonLock;
};



class FAdaptiveStreamingPlayer : public IAdaptiveStreamingPlayer
							   , public IPlayerSessionServices
							   , public IStreamReader::StreamReaderEventListener
							   , public IAccessUnitMemoryProvider
							   , public IPlayerStreamFilter
							   , public TSharedFromThis<FAdaptiveStreamingPlayer, ESPMode::ThreadSafe>
{
public:
	FAdaptiveStreamingPlayer(const IAdaptiveStreamingPlayer::FCreateParam& InCreateParameters);
	virtual ~FAdaptiveStreamingPlayer();

	virtual void SetStaticResourceProviderCallback(const TSharedPtr<IAdaptiveStreamingPlayerResourceProvider, ESPMode::ThreadSafe>& InStaticResourceProvider) override;
	virtual void SetVideoDecoderResourceDelegate(const TSharedPtr<IVideoDecoderResourceDelegate, ESPMode::ThreadSafe>& ResourceDelegate) override;

	virtual void AddMetricsReceiver(IAdaptiveStreamingPlayerMetrics* InMetricsReceiver) override;
	virtual void RemoveMetricsReceiver(IAdaptiveStreamingPlayerMetrics* InMetricsReceiver) override;

	// Metrics receiver accessor for event dispatcher thread.
	void LockMetricsReceivers()
	{
		MetricListenerCriticalSection.Lock();
	}
	void UnlockMetricsReceivers()
	{
		MetricListenerCriticalSection.Unlock();
	}
	const TArray<IAdaptiveStreamingPlayerMetrics*, TInlineAllocator<4>>& GetMetricsReceivers()
	{
		return MetricListeners;
	}


	virtual void AddAEMSReceiver(TWeakPtrTS<IAdaptiveStreamingPlayerAEMSReceiver> InReceiver, FString InForSchemeIdUri, FString InForValue, IAdaptiveStreamingPlayerAEMSReceiver::EDispatchMode InDispatchMode) override;
	virtual void RemoveAEMSReceiver(TWeakPtrTS<IAdaptiveStreamingPlayerAEMSReceiver> InReceiver, FString InForSchemeIdUri, FString InForValue, IAdaptiveStreamingPlayerAEMSReceiver::EDispatchMode InDispatchMode) override;

	virtual void Initialize(const FParamDict& Options) override;

	virtual void SetInitialStreamAttributes(EStreamType StreamType, const FStreamSelectionAttributes& InitialSelection) override;
	virtual void LoadManifest(const FString& manifestURL) override;

	virtual void SeekTo(const FSeekParam& NewPosition) override;
	virtual void Pause() override;
	virtual void Resume() override;
	virtual void Stop() override;
	virtual void SetLooping(const FLoopParam& InLoopParams) override;

	virtual FErrorDetail GetError() const override;

	virtual bool HaveMetadata() const override;
	virtual FTimeValue GetDuration() const override;
	virtual FTimeValue GetPlayPosition() const override;
	virtual void GetTimelineRange(FTimeRange& OutRange) const override;
	virtual void GetSeekableRange(FTimeRange& OutRange) const override;
	virtual void GetSeekablePositions(TArray<FTimespan>& OutPositions) const override;
	virtual bool HasEnded() const override;
	virtual bool IsSeeking() const override;
	virtual bool IsBuffering() const override;
	virtual bool IsPlaying() const override;
	virtual bool IsPaused() const override;

	virtual void GetLoopState(FPlayerLoopState& OutLoopState) const override;
	virtual void GetTrackMetadata(TArray<FTrackMetadata>& OutTrackMetadata, EStreamType StreamType) const override;
	//virtual void GetSelectedTrackMetadata(TOptional<FTrackMetadata>& OutSelectedTrackMetadata, EStreamType StreamType) const override;
	virtual void GetSelectedTrackAttributes(FStreamSelectionAttributes& OutAttributes, EStreamType StreamType) const override;

	virtual void SetBitrateCeiling(int32 highestSelectableBitrate) override;
	virtual void SetMaxResolution(int32 MaxWidth, int32 MaxHeight) override;

	//virtual void SelectTrackByMetadata(EStreamType StreamType, const FTrackMetadata& StreamMetadata) override;
	virtual void SelectTrackByAttributes(EStreamType StreamType, const FStreamSelectionAttributes& Attributes) override;
	virtual void DeselectTrack(EStreamType StreamType) override;
	virtual bool IsTrackDeselected(EStreamType StreamType) override;

#if PLATFORM_ANDROID
	virtual void Android_UpdateSurface(const TSharedPtr<IOptionPointerValueContainer>& Surface) override;
#endif

	void DebugPrint(void* pPlayer, void (*debugDrawPrintf)(void* pPlayer, const char *pFmt, ...));
	static void DebugHandle(void* pPlayer, void (*debugDrawPrintf)(void* pPlayer, const char *pFmt, ...));

private:
	// Methods from IPlayerSessionServices
	virtual void PostError(const FErrorDetail& Error) override;
	virtual void PostLog(Facility::EFacility FromFacility, IInfoLog::ELevel LogLevel, const FString& Message) override;
	virtual void SendMessageToPlayer(TSharedPtrTS<IPlayerMessage> PlayerMessage) override;
	virtual void GetExternalGuid(FGuid& OutExternalGuid) override;
	virtual ISynchronizedUTCTime* GetSynchronizedUTCTime() override;
	virtual TSharedPtr<IAdaptiveStreamingPlayerResourceProvider, ESPMode::ThreadSafe> GetStaticResourceProvider() override;
	virtual IElectraHttpManager* GetHTTPManager() override;
	virtual TSharedPtrTS<IAdaptiveStreamSelector> GetStreamSelector() override;
	virtual void GetStreamBufferStats(FAccessUnitBufferInfo& OutBufferStats, EStreamType ForStream) override;
	virtual IPlayerStreamFilter* GetStreamFilter() override;
	virtual	TSharedPtrTS<IPlaylistReader> GetManifestReader() override;
	virtual TSharedPtrTS<IPlayerEntityCache> GetEntityCache() override;
	virtual IAdaptiveStreamingPlayerAEMSHandler* GetAEMSEventHandler() override;
	virtual FParamDict& GetOptions() override;
	virtual TSharedPtrTS<FDRMManager> GetDRMManager() override;

	// Methods from IPlayerStreamFilter
	virtual bool CanDecodeStream(const FStreamCodecInformation& InStreamCodecInfo) const override;


	enum class EPlayerState
	{
		eState_Idle,
		eState_ParsingManifest,
		eState_PreparingStreams,
		eState_Ready,
		eState_Buffering,					//!< Initial buffering at start or after a seek (an expected buffering)
		eState_Playing,						//!<
		eState_Paused,						//!<
		eState_Rebuffering,					//!< Rebuffering due to buffer underrun. Temporary state only.
		eState_Seeking,						//!< Seeking. Temporary state only.
		eState_Error,
	};
	static const char* GetPlayerStateName(EPlayerState s)
	{
		switch(s)
		{
			case EPlayerState::eState_Idle:				return("Idle");
			case EPlayerState::eState_ParsingManifest:	return("Parsing manifest");
			case EPlayerState::eState_PreparingStreams:	return("Preparing streams");
			case EPlayerState::eState_Ready:			return("Ready");
			case EPlayerState::eState_Buffering:		return("Buffering");
			case EPlayerState::eState_Playing:			return("Playing");
			case EPlayerState::eState_Paused:			return("Paused");
			case EPlayerState::eState_Rebuffering:		return("Rebuffering");
			case EPlayerState::eState_Seeking:			return("Seeking");
			case EPlayerState::eState_Error:			return("Error");
			default:									return("undefined");
		}
	}

	enum class EDecoderState
	{
		eDecoder_Paused,
		eDecoder_Running,
	};
	static const char* GetDecoderStateName(EDecoderState s)
	{
		switch(s)
		{
			case EDecoderState::eDecoder_Paused:	return("Paused");
			case EDecoderState::eDecoder_Running:	return("Running");
			default:								return("undefined");
		}
	}

	enum class EPipelineState
	{
		ePipeline_Stopped,
		ePipeline_Prerolling,
		ePipeline_Running,
	};
	static const char* GetPipelineStateName(EPipelineState s)
	{
		switch(s)
		{
			case EPipelineState::ePipeline_Stopped:		return("Stopped");
			case EPipelineState::ePipeline_Prerolling:	return("Prerolling");
			case EPipelineState::ePipeline_Running:		return("Running");
			default:									return("undefined");
		}
	}

	enum class EStreamState
	{
		eStream_Running,
		eStream_ReachedEnd,
	};
	static const char* GetStreamStateName(EStreamState s)
	{
		switch(s)
		{
			case EStreamState::eStream_Running:		return("Running");
			case EStreamState::eStream_ReachedEnd:	return("Reached end");
			default:								return("undefined");
		}
	}

	struct FVideoRenderer
	{
		void Close()
		{
			Renderer.Reset();
		}
		void Flush(bool bHoldCurrentFrame)
		{
			if (Renderer.IsValid())
			{
				FParamDict options;
				options.Set("hold_current_frame", FVariantValue(bHoldCurrentFrame));
				Renderer->Flush(options);
			}
		}
		TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe>	Renderer;
	};

	struct FAudioRenderer
	{
		void Close()
		{
			Renderer.Reset();
		}
		void Flush()
		{
			if (Renderer.IsValid())
			{
				FParamDict options;
				Renderer->Flush(options);
			}
		}
		TSharedPtr<IMediaRenderer, ESPMode::ThreadSafe>	Renderer;
	};

	struct FVideoDecoder : public IAccessUnitBufferListener, public IDecoderOutputBufferListener
		{
		FVideoDecoder()
			: Parent(nullptr)
			, Decoder(nullptr)
		{
		}
		void Close()
		{
			if (Decoder)
			{
				Decoder->SetAUInputBufferListener(nullptr);
				Decoder->SetReadyBufferListener(nullptr);
			}
			delete Decoder;
			Decoder = nullptr;
		}
		void Flush()
		{
			if (Decoder)
			{
				Decoder->AUdataFlushEverything();
			}
		}
		virtual void DecoderInputNeeded(const IAccessUnitBufferListener::FBufferStats& currentInputBufferStats)
		{
			if (Parent)
			{
				Parent->VideoDecoderInputNeeded(currentInputBufferStats);
			}
		}

		virtual void DecoderOutputReady(const IDecoderOutputBufferListener::FDecodeReadyStats& currentReadyStats)
		{
			if (Parent)
			{
				Parent->VideoDecoderOutputReady(currentReadyStats);
			}
		}

		FAdaptiveStreamingPlayer*		Parent;
		IVideoDecoderH264*				Decoder;
		};

	struct FAudioDecoder : public IAccessUnitBufferListener, public IDecoderOutputBufferListener
		{
		FAudioDecoder()
			: Parent(nullptr)
			, Decoder(nullptr)
		{
		}
		void Close()
		{
			if (Decoder)
			{
				Decoder->SetAUInputBufferListener(nullptr);
				Decoder->SetReadyBufferListener(nullptr);
			}
			delete Decoder;
			Decoder = nullptr;
		}
		void Flush()
		{
			if (Decoder)
			{
				Decoder->AUdataFlushEverything();
			}
		}
		virtual void DecoderInputNeeded(const IAccessUnitBufferListener::FBufferStats& currentInputBufferStats)
		{
			if (Parent)
			{
				Parent->AudioDecoderInputNeeded(currentInputBufferStats);
			}
		}

		virtual void DecoderOutputReady(const IDecoderOutputBufferListener::FDecodeReadyStats& currentReadyStats)
		{
			if (Parent)
			{
				Parent->AudioDecoderOutputReady(currentReadyStats);
			}
		}

		FAdaptiveStreamingPlayer*		Parent;
		IAudioDecoderAAC*				Decoder;
		};


	class FWorkerThread
	{
	public:
		FWorkerThread()
			: bStarted(false)
		{
		}

		struct FMessage
		{
			enum class EType
			{
				// Commands
				LoadManifest,
				Quit,
				Pause,
				Resume,
				Seek,
				Loop,
				Close,
				ChangeBitrate,
				LimitResolution,
				InitialStreamAttributes,
				SelectTrackByMetadata,
				SelectTrackByAttributes,
				DeselectTrack,
				// Player session message
				PlayerSession,
				// Fragment reader messages
				FragmentOpen,
				FragmentClose,
				// Decoder buffer messages
				BufferUnderrun,
			};

			struct FData
			{
				struct FLoadManifest
				{
					FString											URL;
					FString											MimeType;
				};
				struct FStreamReader
				{
					FStreamReader()
						: AUType(EStreamType::Video), AUSize(0)
					{
					}
					TSharedPtrTS<IStreamSegment>					Request;
					EStreamType										AUType;
					SIZE_T											AUSize;
				};
				struct FEvent
				{
					FEvent()
						: Event(nullptr)
					{
					}
					FMediaEvent*									Event;
				};
				struct FStartPlay
				{
					FSeekParam										Position;
				};
				struct FLoop
				{
					FLoop()
						: Signal(nullptr)
					{
					}
					FLoopParam										Loop;
					FMediaEvent*									Signal;
				};
				struct FBitrate
				{
					int32											Value;
				};
				struct FSession
				{
					TSharedPtrTS<IPlayerMessage>					PlayerMessage;
				};
				struct FResolution
				{
					FResolution() : Width(0), Height(0) { }
					int32											Width;
					int32											Height;
				};
				struct FInitialStreamSelect
				{
					EStreamType										StreamType;
					FStreamSelectionAttributes						InitialSelection;
				};
				struct FMetadataTrackSelection
				{
					EStreamType										StreamType;
					FTrackMetadata									TrackMetadata;
					FStreamSelectionAttributes						TrackAttributes;
				};

				FLoadManifest				ManifestToLoad;
				FStreamReader				StreamReader;
				FEvent						MediaEvent;
				FStartPlay					StartPlay;
				FLoop						Looping;
				FBitrate					Bitrate;
				FSession					Session;
				FResolution					Resolution;
				FInitialStreamSelect		InitialStreamAttribute;
				FMetadataTrackSelection		TrackSelection;
			};
			EType					Type;
			FData   				Data;
		};

		void SendMessage(FMessage::EType type)
		{
			FMessage Msg;
			Msg.Type = type;
			WorkMessages.SendMessage(Msg);
		}
		void SendMessage(FMessage::EType type, TSharedPtrTS<IStreamSegment> pRequest)
		{
			FMessage Msg;
			Msg.Type = type;
			Msg.Data.StreamReader.Request = pRequest;
			WorkMessages.SendMessage(Msg);
		}
		void SendMessage(FMessage::EType type, FMediaEvent* pEventSignal)
		{
			FMessage Msg;
			Msg.Type = type;
			Msg.Data.MediaEvent.Event = pEventSignal;
			WorkMessages.SendMessage(Msg);
		}
		void SendLoadManifestMessage(const FString& URL, const FString& MimeType)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::LoadManifest;
			Msg.Data.ManifestToLoad.URL = URL;
			Msg.Data.ManifestToLoad.MimeType = MimeType;
			WorkMessages.SendMessage(Msg);
		}
		void SendPauseMessage()
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::Pause;
			WorkMessages.SendMessage(Msg);
		}
		void SendResumeMessage()
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::Resume;
			WorkMessages.SendMessage(Msg);
		}
		void SendSeekMessage(const FSeekParam& NewPosition)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::Seek;
			Msg.Data.StartPlay.Position = NewPosition;
			WorkMessages.SendMessage(Msg);
		}
		void SendLoopMessage(const FLoopParam& InLoopParams)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::Loop;
			Msg.Data.Looping.Loop = InLoopParams;
			WorkMessages.SendMessage(Msg);
		}
		void SendCloseMessage(FMediaEvent* pEventSignal)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::Close;
			Msg.Data.MediaEvent.Event = pEventSignal;
			WorkMessages.SendMessage(Msg);
		}
		void SendBitrateMessage(EStreamType type, int32 value, int32 which)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::ChangeBitrate;
			Msg.Data.Bitrate.Value = value;
			WorkMessages.SendMessage(Msg);
		}
		void SendPlayerSessionMessage(const TSharedPtrTS<IPlayerMessage>& message)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::PlayerSession;
			Msg.Data.Session.PlayerMessage = message;
			WorkMessages.SendMessage(Msg);
		}
		void SendResolutionMessage(int32 Width, int32 Height)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::LimitResolution;
			Msg.Data.Resolution.Width = Width;
			Msg.Data.Resolution.Height = Height;
			WorkMessages.SendMessage(Msg);
		}
		void SendInitialStreamAttributeMessage(EStreamType StreamType, const FStreamSelectionAttributes& InitialSelection)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::InitialStreamAttributes;
			Msg.Data.InitialStreamAttribute.StreamType = StreamType;
			Msg.Data.InitialStreamAttribute.InitialSelection = InitialSelection;
			WorkMessages.SendMessage(Msg);
		}
		void SendTrackSelectByMetadataMessage(EStreamType StreamType, const FTrackMetadata& TrackMetadata)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::SelectTrackByMetadata;
			Msg.Data.TrackSelection.StreamType = StreamType;
			Msg.Data.TrackSelection.TrackMetadata = TrackMetadata;
			WorkMessages.SendMessage(Msg);
		}
		void SendTrackSelectByAttributeMessage(EStreamType StreamType, const FStreamSelectionAttributes& TrackAttributes)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::SelectTrackByAttributes;
			Msg.Data.TrackSelection.StreamType = StreamType;
			Msg.Data.TrackSelection.TrackAttributes = TrackAttributes;
			WorkMessages.SendMessage(Msg);
		}
		void SendTrackDeselectMessage(EStreamType StreamType)
		{
			FMessage Msg;
			Msg.Type = FMessage::EType::DeselectTrack;
			Msg.Data.TrackSelection.StreamType = StreamType;
			WorkMessages.SendMessage(Msg);
		}

		FMediaThread									MediaThread;
		TMediaMessageQueueDynamicWithTimeout<FMessage>	WorkMessages;
		bool											bStarted;
	};

	struct FPendingStartRequest
	{
		FPlayStartPosition										StartAt;
		IManifest::ESearchType									SearchType;
		FTimeValue												RetryAtTime;
		bool													bIsPlayStart = false;
		bool													bForLooping = false;
		TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>	FinishedRequests;
	};

	struct FBufferStats
	{
		struct FStallMonitor
		{
			int64 DurationMillisec;
			int64 PreviousCheckTime;
			bool bPreviousState;
			FStallMonitor()
			{
				Clear();
			}
			void Clear()
			{
				DurationMillisec = 0;
				PreviousCheckTime = 0;
				bPreviousState = false;
			}
			void Update(int64 tNowMillisec, bool bInCurrentStallState)
			{
				if (bInCurrentStallState)
				{
					if (!bPreviousState)
					{
						PreviousCheckTime = tNowMillisec;
						DurationMillisec = 0;
					}
					else
					{
						DurationMillisec = tNowMillisec - PreviousCheckTime;
					}
				}
				else
				{
					DurationMillisec = 0;
				}
				bPreviousState = bInCurrentStallState;
			}
			int64 GetStalledDurationMillisec() const
			{
				return DurationMillisec;
			}
		};

		void Clear()
		{
			StreamBuffer.Clear();
			DecoderInputBuffer.Clear();
			DecoderOutputBuffer.Clear();
			DecoderOutputStalledMonitor.Clear();
		}
		void UpdateStalledDuration(int64 tNowMillisec)
		{
			DecoderOutputStalledMonitor.Update(tNowMillisec, DecoderOutputBuffer.bOutputStalled);
		}
		int64 GetStalledDurationMillisec() const
		{
			return DecoderOutputStalledMonitor.GetStalledDurationMillisec();
		}
		FAccessUnitBufferInfo								StreamBuffer;
		IAccessUnitBufferListener::FBufferStats				DecoderInputBuffer;
		IDecoderOutputBufferListener::FDecodeReadyStats		DecoderOutputBuffer;
		FStallMonitor										DecoderOutputStalledMonitor;
	};

	struct FPrerollVars
	{
		FPrerollVars()
		{
			Clear();
			bIsVeryFirstStart = true;
		}
		void Clear()
		{
			StartTime  	  = -1;
			bHaveEnoughVideo = false;
			bHaveEnoughAudio = false;
			bHaveEnoughText  = false;
		}
		int64					StartTime;
		bool					bHaveEnoughVideo;
		bool					bHaveEnoughAudio;
		bool					bHaveEnoughText;
		bool					bIsVeryFirstStart;
	};

	struct FPostrollVars
	{
		struct FRenderState
		{
			FRenderState()
			{
				Clear();
			}
			void Clear()
			{
				LastCheckTime   = -1;
				LastBufferCount = 0;
			}
			int64	LastCheckTime;
			int32	LastBufferCount;
		};

		FPostrollVars()
		{
			Clear();
		}

		void Clear()
		{
			Video.Clear();
			Audio.Clear();
		}

		FRenderState		Video;
		FRenderState		Audio;
	};

	struct FStreamBitrateInfo
	{
		FStreamBitrateInfo()
		{
			Clear();
		}
		void Clear()
		{
			Bitrate 	 = 0;
			QualityLevel = 0;
		}
		int32			Bitrate;
		int32			QualityLevel;
	};

	struct FPendingSegmentRequest
	{
		TSharedPtrTS<IStreamSegment>				Request;
		FTimeValue									AtTime;
		TSharedPtrTS<IManifest::IPlayPeriod>		Period;					//!< Set if transitioning between periods. This is the new period that needs to be readied.
		bool										bStartOver = false;
		EStreamType									StreamType = EStreamType::Unsupported;
		FPlayStartPosition							StartoverPosition;
	};

	struct FUpcomingPeriod
	{
		TSharedPtrTS<ITimelineMediaAsset>		Period;
		FString									ID;
		FTimeRange								TimeRange;
	};


	void InternalLoadManifest(const FString& URL, const FString& MimeType);
	void InternalCancelLoadManifest();
	bool SelectManifest();
	void UpdateManifest();
	void OnManifestGetMimeTypeComplete(TSharedPtrTS<FHTTPResourceRequest> InRequest);

	void VideoDecoderInputNeeded(const IAccessUnitBufferListener::FBufferStats& currentInputBufferStats);
	void VideoDecoderOutputReady(const IDecoderOutputBufferListener::FDecodeReadyStats& currentReadyStats);

	void AudioDecoderInputNeeded(const IAccessUnitBufferListener::FBufferStats& currentInputBufferStats);
	void AudioDecoderOutputReady(const IDecoderOutputBufferListener::FDecodeReadyStats& currentReadyStats);



	void WorkerThreadFN();
	void StartWorkerThread();
	void StopWorkerThread();

	int32 CreateRenderers();
	void DestroyRenderers();

	int32 CreateInitialDecoder(EStreamType type);
	void DestroyDecoders();
	bool FindMatchingStreamInfo(FStreamCodecInformation& OutStreamInfo, int32 MaxWidth, int32 MaxHeight);
	void UpdateStreamResolutionLimit();
	void AddUpcomingPeriod(TSharedPtrTS<IManifest::IPlayPeriod> InUpcomingPeriod);

	// AU memory / generic stream reader memory
	void* AUAllocate(IAccessUnitMemoryProvider::EDataType type, SIZE_T size, SIZE_T alignment) override;
	void AUDeallocate(IAccessUnitMemoryProvider::EDataType type, void *pAddr) override;

	// Stream reader events
	void OnFragmentOpen(TSharedPtrTS<IStreamSegment> pRequest) override;
	bool OnFragmentAccessUnitReceived(FAccessUnit* pAccessUnit) override;
	void OnFragmentReachedEOS(EStreamType InStreamType, TSharedPtr<const FBufferSourceInfo, ESPMode::ThreadSafe> InStreamSourceInfo) override;
	void OnFragmentClose(TSharedPtrTS<IStreamSegment> pRequest) override;

	void InternalHandlePendingStartRequest(const FTimeValue& CurrentTime);
	void InternalHandlePendingFirstSegmentRequest(const FTimeValue& CurrentTime);
	void InternalHandleCompletedSegmentRequests(const FTimeValue& CurrentTime);
	void InternalHandleSegmentTrackChanges(const FTimeValue& CurrentTime);

	void UpdateDiagnostics();
	void HandleNewBufferedData();
	void HandleNewOutputData();
	void HandleSessionMessage(TSharedPtrTS<IPlayerMessage> SessionMessage);
	void HandlePlayStateChanges();
	void HandlePendingMediaSegmentRequests();
	void HandleDeselectedBuffers();
	void HandleDecoderChanges();
	void HandleMetadataChanges();
	void HandleAEMSEvents();

	void CheckForStreamEnd();

	void CheckForErrors();

	void FeedDecoder(EStreamType Type, FMultiTrackAccessUnitBuffer& FromMultistreamBuffer, IAccessUnitBufferInterface* Decoder);

	bool InternalStartAt(const FSeekParam& NewPosition);
	void InternalPause();
	void InternalResume();
	void InternalRebuffer();
	void InternalStop(bool bHoldCurrentFrame);
	void InternalClose();
	void InternalSetLoop(const FLoopParam& LoopParam);
	void InternalSetPlaybackEnded();

	void DispatchEvent(TSharedPtrTS<FMetricEvent> Event);
	void DispatchEventAndWait(TSharedPtrTS<FMetricEvent> Event);
	void DispatchBufferingEvent(bool bBegin, EPlayerState Reason);
	void DispatchSegmentDownloadedEvent(TSharedPtrTS<IStreamSegment> Request);
	void DispatchBufferUtilizationEvent(EStreamType BufferType);

	void UpdateDataAvailabilityState(Metrics::FDataAvailabilityChange& DataAvailabilityState, Metrics::FDataAvailabilityChange::EAvailability NewAvailability);

	void StartRendering();
	void StopRendering();

	FTimeValue GetCurrentPlayTime();


	//
	// Member variables
	//
	FGuid																ExternalPlayerGUID;

	TWeakPtr<IAdaptiveStreamingPlayerResourceProvider, ESPMode::ThreadSafe> StaticResourceProvider;
	TWeakPtr<IVideoDecoderResourceDelegate, ESPMode::ThreadSafe>		VideoDecoderResourceDelegate;

	FWorkerThread															WorkerThread;
	TSharedPtr<FAdaptiveStreamingPlayerEventHandler, ESPMode::ThreadSafe>	EventDispatcher;

	FParamDict															PlayerOptions;
	FPlaybackState														PlaybackState;
	ISynchronizedUTCTime*												SynchronizedUTCTime;
	IAdaptiveStreamingPlayerAEMSHandler*								AEMSEventHandler;

	TSharedPtrTS<FMediaRenderClock>										RenderClock;

	TSharedPtrTS<IElectraHttpManager>									HttpManager;
	TSharedPtrTS<IPlayerEntityCache>									EntityCache;

	TSharedPtrTS<FDRMManager>											DrmManager;

	TMediaQueueDynamic<TSharedPtrTS<FErrorDetail>>						ErrorQueue;

	FString																ManifestURL;
	EMediaFormatType													ManifestType;
	TSharedPtrTS<IManifest>												Manifest;
	TSharedPtrTS<IPlaylistReader>										ManifestReader;
	TSharedPtrTS<FHTTPResourceRequest>									ManifestMimeTypeRequest;

	IStreamReader*														StreamReaderHandler;
	TMediaOptionalValue<bool> 											bHaveVideoReader;
	TMediaOptionalValue<bool> 											bHaveAudioReader;
	TMediaOptionalValue<bool> 											bHaveTextReader;


	FMultiTrackAccessUnitBuffer											MultiStreamBufferVid;
	FMultiTrackAccessUnitBuffer											MultiStreamBufferAud;
	FMultiTrackAccessUnitBuffer											MultiStreamBufferTxt;

	Metrics::FDataAvailabilityChange									DataAvailabilityStateVid;
	Metrics::FDataAvailabilityChange									DataAvailabilityStateAud;
	Metrics::FDataAvailabilityChange									DataAvailabilityStateTxt;

	FStreamSelectionAttributes											StreamSelectionAttributesVid;
	FStreamSelectionAttributes											StreamSelectionAttributesAud;
	FStreamSelectionAttributes											StreamSelectionAttributesTxt;

	FStreamSelectionAttributes											SelectedStreamAttributesVid;
	FStreamSelectionAttributes											SelectedStreamAttributesAud;
	FStreamSelectionAttributes											SelectedStreamAttributesTxt;

	TSharedPtrTS<FStreamSelectionAttributes>							PendingTrackSelectionVid;
	TSharedPtrTS<FStreamSelectionAttributes>							PendingTrackSelectionAud;
	TSharedPtrTS<FStreamSelectionAttributes>							PendingTrackSelectionTxt;

	EPlayerState														CurrentState;
	EPipelineState														PipelineState;
	EDecoderState														DecoderState;
	EStreamState														StreamState;

	FPrerollVars														PrerollVars;
	FPostrollVars														PostrollVars;
	EPlayerState														LastBufferingState;
	FSeekParam															StartAtTime;
	double																PlaybackRate;
	FTimeValue															RebufferDetectedAtPlayPos;
	bool																bRebufferPending;
	bool																bIsPlayStart;
	bool																bIsClosing;

	TSharedPtrTS<IAdaptiveStreamSelector>								StreamSelector;
	int32																BitrateCeiling;
	int32																VideoResolutionLimitWidth;
	int32																VideoResolutionLimitHeight;

	FStreamBitrateInfo													CurrentVideoStreamBitrate;

	bool																bShouldBePaused;
	bool																bShouldBePlaying;
	bool																bSeekPending;

	FPlayerLoopState													CurrentLoopState;
	FLoopParam															CurrentLoopParam;
	TMediaQueueDynamicNoLock<FPlayerLoopState>							NextLoopStates;

	TArray<FUpcomingPeriod>												UpcomingPeriods;
	TSharedPtrTS<IManifest::IPlayPeriod>								InitialPlayPeriod;
	TSharedPtrTS<IManifest::IPlayPeriod>								CurrentPlayPeriodVideo;
	TSharedPtrTS<IManifest::IPlayPeriod>								CurrentPlayPeriodAudio;
	TSharedPtrTS<IManifest::IPlayPeriod>								CurrentPlayPeriodText;
	TSharedPtrTS<FPendingStartRequest>									PendingStartRequest;
	TSharedPtrTS<IStreamSegment>										PendingFirstSegmentRequest;
	TQueue<FPendingSegmentRequest>										NextPendingSegmentRequests;
	TMultiMap<EStreamType, TSharedPtrTS<IStreamSegment>>				CompletedSegmentRequests;
	bool																bFirstSegmentRequestIsForLooping;

	uint32																CurrentPlaybackSequenceID[4]; // 0=video, 1=audio, 2=subtitiles, 3=UNSUPPORTED

	FVideoRenderer														VideoRender;
	FAudioRenderer														AudioRender;
	FVideoDecoder														VideoDecoder;
	FAudioDecoder														AudioDecoder;

	FMediaCriticalSection												MetricListenerCriticalSection;
	TArray<IAdaptiveStreamingPlayerMetrics*, TInlineAllocator<4>>		MetricListeners;

	FMediaCriticalSection												DiagnosticsCriticalSection;
	FBufferStats														VideoBufferStats;
	FBufferStats														AudioBufferStats;
	FBufferStats														TextBufferStats;
	FErrorDetail														LastErrorDetail;

	AdaptiveStreamingPlayerConfig::FConfiguration						PlayerConfig;

	static FAdaptiveStreamingPlayer* 									PointerToLatestPlayer;
};

} // namespace Electra

