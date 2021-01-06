// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once


#include "Containers/UnrealString.h"
#include "Containers/Queue.h"
#include "Misc/Guid.h"
#include "Templates/SharedPointer.h"
#include "IAnalyticsProviderET.h"

#include "IElectraPlayerInterface.h"

#include "Player/AdaptiveStreamingPlayer.h"

class FVideoDecoderOutput;
using FVideoDecoderOutputPtr = TSharedPtr<FVideoDecoderOutput, ESPMode::ThreadSafe>;
class IAudioDecoderOutput;
using IAudioDecoderOutputPtr = TSharedPtr<IAudioDecoderOutput, ESPMode::ThreadSafe>;

namespace Electra
{
class IVideoDecoderResourceDelegate;
}

class FElectraRendererVideo;
class FElectraRendererAudio;

using namespace Electra;

DECLARE_MULTICAST_DELEGATE_TwoParams(FElectraPlayerSendAnalyticMetricsDelegate, const TSharedPtr<IAnalyticsProviderET>& /*AnalyticsProvider*/, const FGuid& /*PlayerGuid*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FElectraPlayerSendAnalyticMetricsPerMinuteDelegate, const TSharedPtr<IAnalyticsProviderET>& /*AnalyticsProvider*/);
DECLARE_MULTICAST_DELEGATE_TwoParams(FElectraPlayerReportVideoStreamingErrorDelegate, const FGuid& /*PlayerGuid*/, const FString& /*LastError*/);
DECLARE_MULTICAST_DELEGATE_FourParams(FElectraPlayerReportSubtitlesMetricsDelegate, const FGuid& /*PlayerGuid*/, const FString& /*URL*/, double /*ResponseTime*/, const FString& /*LastError*/);

class FElectraPlayer
	: public IElectraPlayerInterface
	, public IAdaptiveStreamingPlayerMetrics
{
public:
	FElectraPlayer(const TSharedPtr<IElectraPlayerAdapterDelegate, ESPMode::ThreadSafe>& AdapterDelegate,
			  FElectraPlayerSendAnalyticMetricsDelegate& InSendAnalyticMetricsDelegate,
			  FElectraPlayerSendAnalyticMetricsPerMinuteDelegate& InSendAnalyticMetricsPerMinuteDelegate,
			  FElectraPlayerReportVideoStreamingErrorDelegate& InReportVideoStreamingErrorDelegate,
			  FElectraPlayerReportSubtitlesMetricsDelegate& InReportSubtitlesFileMetricsDelegate);
	~FElectraPlayer();

	void OnVideoDecoded(const FVideoDecoderOutputPtr& DecoderOutput, bool bDoNotRender);
	void OnVideoFlush();
	void OnAudioDecoded(const IAudioDecoderOutputPtr& DecoderOutput);
	void OnAudioFlush();

	void OnVideoRenderingStarted();
	void OnVideoRenderingStopped();
	void OnAudioRenderingStarted();
	void OnAudioRenderingStopped();

	void SendAnalyticMetrics(const TSharedPtr<IAnalyticsProviderET>& AnalyticsProvider, const FGuid& InPlayerGuid);
	void SendAnalyticMetricsPerMinute(const TSharedPtr<IAnalyticsProviderET>& AnalyticsProvider);
	void SendPendingAnalyticMetrics(const TSharedPtr<IAnalyticsProviderET>& AnalyticsProvider);
	void ReportVideoStreamingError(const FGuid& InPlayerGuid, const FString& LastError);
	void ReportSubtitlesMetrics(const FGuid& InPlayerGuid, const FString& URL, double ResponseTime, const FString& LastError);

	void DropOldFramesFromPresentationQueue();

	bool CanPresentVideoFrames(uint64 NumFrames);
	bool CanPresentAudioFrames(uint64 NumFrames);

	FString GetUrl() const
	{
		return MediaUrl;
	}

	void SetGuid(const FGuid& Guid)
	{
		PlayerGuid = Guid;
	}

	void SetAsyncResourceReleaseNotification(IAsyncResourceReleaseNotifyContainer* AsyncResourceReleaseNotification) override;

	// -------- PlayerAdapter (Plugin/Native) API

	bool OpenInternal(const FString& Url, const FParamDict & PlayerOptions, const FPlaystartOptions & InPlaystartOptions) override;
	void CloseInternal(bool bKillAfterClose) override;

	void Tick(FTimespan DeltaTime, FTimespan Timecode) override;

	bool IsKillAfterCloseAllowed() const override { return bAllowKillAfterCloseEvent;  }

	EPlayerState GetState() const override;
	EPlayerStatus GetStatus() const override;

	bool IsLooping() const override;
	bool SetLooping(bool bLooping) override;

	FTimespan GetTime() const override;
	FTimespan GetDuration() const override;

	bool IsLive() const override;
	FTimespan GetSeekableDuration() const override;

	float GetRate() const override;
	bool SetRate(float Rate) override;

	bool Seek(const FTimespan& Time) override;

	bool GetAudioTrackFormat(int32 TrackIndex, int32 FormatIndex, FAudioTrackFormat& OutFormat) const override;
	bool GetVideoTrackFormat(int32 TrackIndex, int32 FormatIndex, FVideoTrackFormat& OutFormat) const override;

	int32 GetNumTracks(EPlayerTrackType TrackType) const override;
	int32 GetNumTrackFormats(EPlayerTrackType TrackType, int32 TrackIndex) const override;
	int32 GetSelectedTrack(EPlayerTrackType TrackType) const override;
	FText GetTrackDisplayName(EPlayerTrackType TrackType, int32 TrackIndex) const override;
	int32 GetTrackFormat(EPlayerTrackType TrackType, int32 TrackIndex) const override;
	FString GetTrackLanguage(EPlayerTrackType TrackType, int32 TrackIndex) const override;
	FString GetTrackName(EPlayerTrackType TrackType, int32 TrackIndex) const override;
	bool SelectTrack(EPlayerTrackType TrackType, int32 TrackIndex) override;

	void NotifyOfOptionChange() override;

private:
	void CalculateTargetSeekTime(FTimespan& OutTargetTime, const FTimespan& InTime);

	bool PresentVideoFrame(const FVideoDecoderOutputPtr& InVideoFrame);
	bool PresentAudioFrame(const IAudioDecoderOutputPtr& DecoderOutput);
	
	void PlatformNotifyOfOptionChange();

	// Methods from IAdaptiveStreamingPlayerMetrics
	virtual void ReportOpenSource(const FString& URL) override;
	virtual void ReportReceivedMasterPlaylist(const FString& EffectiveURL) override;
	virtual void ReportReceivedPlaylists() override;
	virtual void ReportPlaylistDownload(const Metrics::FPlaylistDownloadStats& PlaylistDownloadStats) override;
	virtual void ReportBufferingStart(Metrics::EBufferingReason BufferingReason) override;
	virtual void ReportBufferingEnd(Metrics::EBufferingReason BufferingReason) override;
	virtual void ReportBandwidth(int64 EffectiveBps, int64 ThroughputBps, double LatencyInSeconds) override;
	virtual void ReportBufferUtilization(const Metrics::FBufferStats& BufferStats) override;
	virtual void ReportSegmentDownload(const Metrics::FSegmentDownloadStats& SegmentDownloadStats) override;
	virtual void ReportLicenseKey(const Metrics::FLicenseKeyStats& LicenseKeyStats) override;
	virtual void ReportDataAvailabilityChange(const Metrics::FDataAvailabilityChange& DataAvailability) override;
	virtual void ReportVideoQualityChange(int32 NewBitrate, int32 PreviousBitrate, bool bIsDrasticDownswitch) override;
	virtual void ReportPrerollStart() override;
	virtual void ReportPrerollEnd() override;
	virtual void ReportPlaybackStart() override;
	virtual void ReportPlaybackPaused() override;
	virtual void ReportPlaybackResumed() override;
	virtual void ReportPlaybackEnded() override;
	virtual void ReportJumpInPlayPosition(const FTimeValue& ToNewTime, const FTimeValue& FromTime, Metrics::ETimeJumpReason TimejumpReason) override;
	virtual void ReportPlaybackStopped() override;
	virtual void ReportError(const FString& ErrorReason) override;
	virtual void ReportLogMessage(IInfoLog::ELevel InLogLevel, const FString& InLogMessage, int64 InPlayerWallclockMilliseconds) override;
	virtual void ReportDroppedVideoFrame() override;
	virtual void ReportDroppedAudioFrame() override;
	
	void LogPresentationFramesQueues(FTimespan DeltaTime);

	void MediaStateOnPreparingFinished();
	bool MediaStateOnPlay();
	bool MediaStateOnPause();
	void MediaStateOnEndReached();
	void MediaStateOnSeekFinished();
	void MediaStateOnError();

	TSharedPtr<FStreamMetadata, ESPMode::ThreadSafe> GetTrackStreamMetadata(EPlayerTrackType TrackType, int32 TrackIndex) const;

	// Delegate to talk back to adapter host
	TWeakPtr<IElectraPlayerAdapterDelegate, ESPMode::ThreadSafe>	AdapterDelegate;

	// Contains number of audio tracks available to expose it later.
	int32											NumTracksAudio;
	int32											NumTracksVideo;
	int32											SelectedQuality;
	int32											SelectedVideoTrackIndex;
	int32											SelectedAudioTrackIndex;

	FIntPoint										LastPresentedFrameDimension;


	/** Media player Guid */
	FGuid											PlayerGuid;
	/** Metric delegates */
	FElectraPlayerSendAnalyticMetricsDelegate&			SendAnalyticMetricsDelegate;
	FElectraPlayerSendAnalyticMetricsPerMinuteDelegate&	SendAnalyticMetricsPerMinuteDelegate;
	FElectraPlayerReportVideoStreamingErrorDelegate&		ReportVideoStreamingErrorDelegate;
	FElectraPlayerReportSubtitlesMetricsDelegate&		ReportSubtitlesMetricsDelegate;

	/** Option interface **/
	FPlaystartOptions								PlaystartOptions;

	/**  */
	TAtomic<EPlayerState>							State;
	TAtomic<EPlayerStatus>							Status;
	bool											bWasClosedOnError;

	bool											bAllowKillAfterCloseEvent;

	/** Queued events */
	TQueue<IElectraPlayerAdapterDelegate::EPlayerEvent>	DeferredEvents;

	/** The URL of the currently opened media. */
	FString											MediaUrl;

	class FInternalPlayerImpl
	{
	public:
		/** The media player itself **/
		TSharedPtr<IAdaptiveStreamingPlayer, ESPMode::ThreadSafe>	AdaptivePlayer;

		/** Renderers to use **/
		TSharedPtr<FElectraRendererVideo, ESPMode::ThreadSafe>		RendererVideo;
		TSharedPtr<FElectraRendererAudio, ESPMode::ThreadSafe>		RendererAudio;

		/** */
		static void DoCloseAsync(TSharedPtr<FInternalPlayerImpl, ESPMode::ThreadSafe> && Player, TSharedPtr<IAsyncResourceReleaseNotifyContainer, ESPMode::ThreadSafe> AsyncDestructNotification);
	};

	TSharedPtr<FInternalPlayerImpl, ESPMode::ThreadSafe>			CurrentPlayer;

	TSharedPtr<IAsyncResourceReleaseNotifyContainer, ESPMode::ThreadSafe> AsyncResourceReleaseNotification;

	class FAdaptiveStreamingPlayerResourceProvider : public IAdaptiveStreamingPlayerResourceProvider
	{
	public:
		FAdaptiveStreamingPlayerResourceProvider(const TWeakPtr<IElectraPlayerAdapterDelegate, ESPMode::ThreadSafe> & AdapterDelegate);
		virtual ~FAdaptiveStreamingPlayerResourceProvider() = default;
		
		virtual void ProvideStaticPlaybackDataForURL(TSharedPtr<IAdaptiveStreamingPlayerResourceRequest, ESPMode::ThreadSafe> InOutRequest) override;

		void ProcessPendingStaticResourceRequests();
		void ClearPendingRequests();

	private:
		/** Requests for static resource fetches we want to perform on the main thread **/
		TQueue<TSharedPtr<IAdaptiveStreamingPlayerResourceRequest, ESPMode::ThreadSafe>, EQueueMode::Mpsc> PendingStaticResourceRequests;

		// Player adapter delegate
		TWeakPtr<IElectraPlayerAdapterDelegate, ESPMode::ThreadSafe>	AdapterDelegate;
	};

	TSharedPtr<FAdaptiveStreamingPlayerResourceProvider, ESPMode::ThreadSafe> StaticResourceProvider;
	TSharedPtr<IVideoDecoderResourceDelegate, ESPMode::ThreadSafe> VideoDecoderResourceDelegate;


	class FAverageValue
	{
	public:
		FAverageValue()
		: Samples(nullptr)
		, NumSamples(0)
		, MaxSamples(0)
		{
		}
		~FAverageValue()
		{
			delete[] Samples;
		}
		void SetNumSamples(int32 InMaxSamples)
		{
			check(InMaxSamples > 0);
			delete[] Samples;
			NumSamples = 0;
			MaxSamples = InMaxSamples;
			Samples = new double[MaxSamples];
		}
		void AddValue(double Value)
		{
			Samples[NumSamples % MaxSamples] = Value;
			++NumSamples;
		}
		void Reset()
		{
			NumSamples = 0;
		}
		double GetAverage() const
		{
			double Avg = 0.0;
			if (NumSamples > 0)
			{
				double Sum = 0.0;
				int32 Last = NumSamples <= MaxSamples ? NumSamples : MaxSamples;
				for (int32 i = 0; i < Last; ++i)
				{
					Sum += Samples[i];
				}
				Avg = Sum / Last;
			}
			return Avg;
		}
	private:
		double*	Samples;
		int32	NumSamples;
		int32	MaxSamples;
	};

	struct FDroppedFrameStats
	{
		enum class EFrameType
		{
			Undefined,
			VideoFrame,
			AudioFrame
		};
		FDroppedFrameStats()
		{
			FrameType = EFrameType::Undefined;
			Reset();
		}
		void SetFrameType(EFrameType InFrameType)
		{
			FrameType = InFrameType;
		}
		void Reset()
		{
			NumTotalDropped = 0;
			WorstDeltaTime = FTimespan::Zero();
			PlayerTimeAtLastReport = FTimespan::MinValue();
			SystemTimeAtLastReport = 0.0;
			LogWarningAfterSeconds = 10.0;
		}
		void SetLogWarningInterval(double InSecondsBetweenWarnings)
		{
			LogWarningAfterSeconds = InSecondsBetweenWarnings;
		}
		void AddNewDrop(const FTimespan& InFrameTime, const FTimespan& InPlayerTime, void* InPtrElectraPlayer, void* InPtrCurrentPlayer);

		EFrameType	FrameType;
		uint32		NumTotalDropped;
		FTimespan	WorstDeltaTime;
		FTimespan	PlayerTimeAtLastReport;
		double		SystemTimeAtLastReport;
		double		LogWarningAfterSeconds;
	};

	struct FStatistics
	{
		struct FBandwidth
		{
			FBandwidth()
			{
				Bandwidth.SetNumSamples(3);
				Latency.SetNumSamples(3);
				Reset();
			}
			void Reset()
			{
				Bandwidth.Reset();
				Latency.Reset();
			}
			void AddSample(double InBytesPerSecond, double InLatency)
			{
				Bandwidth.AddValue(InBytesPerSecond);
				Latency.AddValue(InLatency);
			}
			double GetAverageBandwidth() const
			{
				return Bandwidth.GetAverage();
			}
			double GetAverageLatency() const
			{
				return Latency.GetAverage();
			}
			FAverageValue	Bandwidth;
			FAverageValue	Latency;
		};
		FStatistics()
		{
			DroppedVideoFrames.SetFrameType(FDroppedFrameStats::EFrameType::VideoFrame);
			DroppedAudioFrames.SetFrameType(FDroppedFrameStats::EFrameType::AudioFrame);
			Reset();
		}
		void Reset()
		{
			InitialURL.Empty();
			CurrentlyActivePlaylistURL.Empty();
			LastError.Empty();
			LastState = "Empty";
			TimeAtOpen  					= -1.0;
			TimeToLoadMasterPlaylist		= -1.0;
			TimeToLoadStreamPlaylists   	= -1.0;
			InitialBufferingDuration		= -1.0;
			InitialStreamBitrate			= 0;
			TimeAtPrerollBegin  			= -1.0;
			TimeForInitialPreroll   		= -1.0;
			NumTimesRebuffered  			= 0;
			NumTimesForwarded   			= 0;
			NumTimesRewound 				= 0;
			NumTimesLooped  				= 0;
			TimeAtBufferingBegin			= 0.0;
			TotalRebufferingDuration		= 0.0;
			LongestRebufferingDuration  	= 0.0;
			PlayPosAtStart  				= -1.0;
			PlayPosAtEnd					= -1.0;
			NumQualityUpswitches			= 0;
			NumQualityDownswitches  		= 0;
			NumQualityDrasticDownswitches   = 0;
			NumVideoDatabytesStreamed   	= 0;
			NumAudioDatabytesStreamed   	= 0;
			NumSegmentDownloadsAborted  	= 0;
			CurrentlyActiveResolutionWidth  = 0;
			CurrentlyActiveResolutionHeight = 0;
			VideoSegmentBitratesStreamed.Empty();
			InitialBufferingBandwidth.Reset();
			bIsInitiallyDownloading 		= false;
			bDidPlaybackEnd 				= false;
			SubtitlesURL.Empty();
			SubtitlesResponseTime			= 0.0;
			SubtitlesLastError.Empty();
			DroppedVideoFrames.Reset();
			DroppedAudioFrames.Reset();
			MediaTimelineAtStart.Reset();
			MediaTimelineAtEnd.Reset();
			MediaDuration = 0.0;
		}

		FString					InitialURL;
		FString					CurrentlyActivePlaylistURL;
		FString					LastError;
		FString					LastState;		// "Empty", "Opening", "Preparing", "Buffering", "Idle", "Ready", "Playing", "Paused", "Seeking", "Rebuffering", "Ended"
		double					TimeAtOpen;
		double					TimeToLoadMasterPlaylist;
		double					TimeToLoadStreamPlaylists;
		double					InitialBufferingDuration;
		int32					InitialStreamBitrate;
		double					TimeAtPrerollBegin;
		double					TimeForInitialPreroll;
		int32					NumTimesRebuffered;
		int32					NumTimesForwarded;
		int32					NumTimesRewound;
		int32					NumTimesLooped;
		double					TimeAtBufferingBegin;
		double					TotalRebufferingDuration;
		double					LongestRebufferingDuration;
		double					PlayPosAtStart;
		double					PlayPosAtEnd;
		int32					NumQualityUpswitches;
		int32					NumQualityDownswitches;
		int32					NumQualityDrasticDownswitches;
		int64					NumVideoDatabytesStreamed;
		int64					NumAudioDatabytesStreamed;
		int32					NumSegmentDownloadsAborted;
		int32					CurrentlyActiveResolutionWidth;
		int32					CurrentlyActiveResolutionHeight;
		TMap<int32, uint32>		VideoSegmentBitratesStreamed;		// key=video stream bitrate, value=number of segments loaded at this rate
		FBandwidth				InitialBufferingBandwidth;
		bool					bIsInitiallyDownloading;
		bool					bDidPlaybackEnd;
		FString					SubtitlesURL;
		double					SubtitlesResponseTime;
		FString					SubtitlesLastError;
		FDroppedFrameStats		DroppedVideoFrames;
		FDroppedFrameStats		DroppedAudioFrames;
		FTimeRange				MediaTimelineAtStart;
		FTimeRange				MediaTimelineAtEnd;
		double					MediaDuration;
	};

	struct FAnalyticsEvent
	{
		FString EventName;
		TArray<FAnalyticsEventAttribute> ParamArray;
	};

	void UpdatePlayEndStatistics();
	void LogStatistics();
	void AddCommonAnalyticsAttributes(TArray<FAnalyticsEventAttribute>& InOutParamArray);
	TSharedPtr<FAnalyticsEvent> CreateAnalyticsEvent(FString InEventName);
	void EnqueueAnalyticsEvent(TSharedPtr<FAnalyticsEvent> InAnalyticEvent);

	FCriticalSection						StatisticsLock;
	FStatistics								Statistics;

	TQueue<TSharedPtr<FAnalyticsEvent>>		QueuedAnalyticEvents;
	int32									NumQueuedAnalyticEvents = 0;
	FString									AnalyticsOSVersion;
	FString									AnalyticsGPUType;

	/** Unique player instance GUID sent with each analytics event. This allows finding all events of a particular playback session. **/
	FString									AnalyticsInstanceGuid;
	/** Sequential analytics event number. Helps sorting events. **/
	uint32									AnalyticsInstanceEventCount;
};

ENUM_CLASS_FLAGS(FElectraPlayer::EPlayerStatus);

