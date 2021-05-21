// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/AdaptiveStreamingPlayerABR.h"

#include "Player/Manifest.h"
#include "Player/PlaybackTimeline.h"
#include "StreamAccessUnitBuffer.h"
#include "Utilities/Utilities.h"



namespace Electra
{

	template <typename T>
	class TSimpleMovingAverage
	{
	public:
		//! History sample
		TSimpleMovingAverage(int32 maxNumSMASamples = kMaxSMASamples)
			: MaxSamples(maxNumSMASamples)
		{
			SimpleMovingAverage.Resize(MaxSamples);
			LastSample = T(0);
		}

		void SetMaxSMASampleCount(int32 maxSMASamples)
		{
			FMediaCriticalSection::ScopedLock lock(CriticalSection);
			MaxSamples = maxSMASamples;
			SimpleMovingAverage.Clear();
			SimpleMovingAverage.Resize(MaxSamples);
		}

		//! Clears everything
		void Clear()
		{
			FMediaCriticalSection::ScopedLock lock(CriticalSection);
			SimpleMovingAverage.Clear();
		}

		void InitializeTo(const T& v)
		{
			FMediaCriticalSection::ScopedLock lock(CriticalSection);
			Clear();
			AddSample(v);
		}

		//! Adds a sample to the mean.
		void AddSample(const T& value)
		{
			FMediaCriticalSection::ScopedLock lock(CriticalSection);

			LastSample = value;

			// Update SMA (http://en.wikipedia.org/wiki/Moving_average#Simple_moving_average)
			if (SimpleMovingAverage.Capacity())
			{
				if (SimpleMovingAverage.Num() >= MaxSamples)
					SimpleMovingAverage.Pop();
				check(!SimpleMovingAverage.IsFull());
				SimpleMovingAverage.Push(LastSample);
			}
		}

		//! Returns the sample value that was added last, if one exists
		T GetLastSample() const
		{
			FMediaCriticalSection::ScopedLock lock(CriticalSection);
			return LastSample;
		}

		//! Returns the simple moving average from all the collected history samples.
		T GetSMA() const
		{
			FMediaCriticalSection::ScopedLock lock(CriticalSection);
			if (SimpleMovingAverage.Num())
			{
				T sum = 0;
				for(SIZE_T i=0; i<SimpleMovingAverage.Num(); ++i)
				{
					sum += SimpleMovingAverage[i];
				}
				return T(sum / SimpleMovingAverage.Num());
			}
			return T(0);
		}

	private:
		enum
		{
			kMaxSMASamples = 5,		//!< Default amount of SMA samples
		};

		mutable FMediaCriticalSection	CriticalSection;
		TMediaQueueNoLock<T>			SimpleMovingAverage;	//!< Simple moving average
		T								LastSample;				//!< The last sample that was added.
		int32							MaxSamples;				//!< Limit number of SMA samples (0 < mMaxSMASamples <= kMaxSMASamples)
	};


	struct FSimulationResult
	{
		FSimulationResult()
			: SecondsAdded(0.0)
			, SecondsGained(0.0)
			, SmallestGain(0.0)
			, DownloadTime(0.0)
		{
		}
		double		SecondsAdded;
		double		SecondsGained;
		double		SmallestGain;
		double		DownloadTime;
	};




	class FAdaptiveStreamSelector : public IAdaptiveStreamSelector
	{
	public:
		FAdaptiveStreamSelector(IPlayerSessionServices* PlayerSessionServices, const IAdaptiveStreamSelector::FConfiguration& config);
		virtual ~FAdaptiveStreamSelector();
		virtual void SetPresentationType(EMediaPresentationType PresentationType) override;
		virtual void SetCanSwitchToAlternateStreams(bool bCanSwitch) override;
		virtual void SetCurrentPlaybackPeriod(EStreamType InStreamType, TSharedPtrTS<IManifest::IPlayPeriod> InCurrentPlayPeriod) override;
		virtual void SetBandwidth(int64 bitsPerSecond) override;
		virtual void SetForcedNextBandwidth(int64 bitsPerSecond) override;
		virtual void MarkStreamAsUnavailable(const FBlacklistedStream& BlacklistedStream) override;
		virtual void MarkStreamAsAvailable(const FBlacklistedStream& NoLongerBlacklistedStream) override;
		virtual int64 GetLastBandwidth() override;
		virtual int64 GetAverageBandwidth() override;
		virtual int64 GetAverageThroughput() override;
		virtual double GetAverageLatency() override;

		virtual void SetBandwidthCeiling(int32 HighestManifestBitrate) override;
		virtual void SetMaxVideoResolution(int32 MaxWidth, int32 MaxHeight) override;

		virtual ESegmentAction SelectSuitableStreams(FTimeValue& OutDelay, TSharedPtrTS<const IStreamSegment> CurrentSegment) override;

		virtual FABRDownloadProgressDecision ReportDownloadProgress(const Metrics::FSegmentDownloadStats& SegmentDownloadStats) override;
		virtual void ReportDownloadEnd(const Metrics::FSegmentDownloadStats& SegmentDownloadStats) override;
		virtual void ReportBufferingStart(Metrics::EBufferingReason BufferingReason) override;
		virtual void ReportBufferingEnd(Metrics::EBufferingReason BufferingReason) override;
		virtual void ReportOpenSource(const FString& URL) override {}
		virtual void ReportReceivedMasterPlaylist(const FString& EffectiveURL) override {}
		virtual void ReportReceivedPlaylists() override {}
		virtual void ReportTracksChanged() override {}
		virtual void ReportPlaylistDownload(const Metrics::FPlaylistDownloadStats& PlaylistDownloadStats) override {}
		virtual void ReportBandwidth(int64 EffectiveBps, int64 ThroughputBps, double LatencyInSeconds) override {}
		virtual void ReportBufferUtilization(const Metrics::FBufferStats& BufferStats) override {}
		virtual void ReportSegmentDownload(const Metrics::FSegmentDownloadStats& SegmentDownloadStats) override {}
		virtual void ReportLicenseKey(const Metrics::FLicenseKeyStats& LicenseKeyStats) override {}
		virtual void ReportDataAvailabilityChange(const Metrics::FDataAvailabilityChange& DataAvailability) override {}
		virtual void ReportVideoQualityChange(int32 NewBitrate, int32 PreviousBitrate, bool bIsDrasticDownswitch) override {}
		virtual void ReportPrerollStart() override {}
		virtual void ReportPrerollEnd() override {}
		virtual void ReportPlaybackStart() override {}
		virtual void ReportPlaybackPaused() override {}
		virtual void ReportPlaybackResumed() override {}
		virtual void ReportPlaybackEnded() override {}
		virtual void ReportJumpInPlayPosition(const FTimeValue& ToNewTime, const FTimeValue& FromTime, Metrics::ETimeJumpReason TimejumpReason) override {}
		virtual void ReportPlaybackStopped() override {}
		virtual void ReportError(const FString& ErrorReason) override {}
		virtual void ReportLogMessage(IInfoLog::ELevel InLogLevel, const FString& LogMessage, int64 PlayerWallclockMilliseconds) override {}
		virtual void ReportDroppedVideoFrame() override {}
		virtual void ReportDroppedAudioFrame() override {}

	private:
		struct FStreamInformation
		{
			struct FStreamHealth
			{
				FStreamHealth()
				{
					Reset();
				}
				void Reset()
				{
					//PreviousAttempts.Reset();
					BecomesAvailableAgainAtUTC.SetToZero();
					LastDownloadStats = {};
				}
				//TSharedPtrTS<HTTP::FRetryInfo>				PreviousAttempts;
				FTimeValue										BecomesAvailableAgainAtUTC;
				Metrics::FSegmentDownloadStats					LastDownloadStats;
			};

			FString											AdaptationSetUniqueID;
			FString											RepresentationUniqueID;

			FStreamHealth									Health;
			// Set for convenience.
			FStreamCodecInformation::FResolution			Resolution;
			int32											Bitrate;
		};

		void LogMessage(IInfoLog::ELevel Level, const FString& Message)
		{
			if (PlayerSessionServices)
			{
				PlayerSessionServices->PostLog(Facility::EFacility::ABR, Level, Message);
			}
		}


		TSharedPtrTS<FStreamInformation> GetStreamInformation(const Metrics::FSegmentDownloadStats& FromDownloadStats);

		void SimulateDownload(FSimulationResult& sim, const TArray<IManifest::IPlayPeriod::FSegmentInformation>& NextSegments, double secondsToSimulate, int64 atBPS);


		IPlayerSessionServices*								PlayerSessionServices;
		FConfiguration										Config;

		FMediaCriticalSection								AccessMutex;
		EMediaPresentationType								PresentationType;
		TSharedPtrTS<IManifest::IPlayPeriod>				CurrentPlayPeriodVideo;
		TSharedPtrTS<IManifest::IPlayPeriod>				CurrentPlayPeriodAudio;
		bool												bPlayerIsBuffering;
		bool												bCanSwitchToAlternateStreams;

		TArray<TSharedPtrTS<FStreamInformation>>			StreamInformationVideo;
		TArray<TSharedPtrTS<FStreamInformation>>			StreamInformationAudio;
		FString												CurrentVideoAdaptationSetID;
		FString												CurrentAudioAdaptationSetID;

		TArray<FBlacklistedStream>							BlacklistedExternally;

		int32												BandwidthCeiling;
		int32												ForcedInitialBandwidth;
		FStreamCodecInformation::FResolution				MaxStreamResolution;

		TSimpleMovingAverage<double>						AverageBandwidth;
		TSimpleMovingAverage<double>						AverageLatency;
		TSimpleMovingAverage<int64>							AverageThroughput;
	};



	IAdaptiveStreamSelector::FConfiguration::FRateScale::FRateScale()
		// Change no defaults without making adjustments to the values in Configuration::Configuration() below!
		: LastToAvg(0.0f)				// prefer last rate
		, Scale(1.0f)					// 100%
		, MaxClampBPS(50000000)		// limit to 50 Mbps
	{
	}

	double IAdaptiveStreamSelector::FConfiguration::FRateScale::Apply(const double lastBps, const double averageBps) const
	{
		return Utils::Max(Utils::Min((lastBps + LastToAvg * (averageBps - lastBps)) * Scale, double(MaxClampBPS)), 0.0);
	}


	IAdaptiveStreamSelector::FConfiguration::FConfiguration()
	{
		SimulationLongtermRateScale.LastToAvg = 1.0f; 			// For simulation use the average, not last bandwidth.
		ForwardSimulationDurationMSec = 1000 * 20;		// 20 seconds
		VideoBufferLowWatermarkMSec = 1000 * 3;
		VideoBufferHighWatermarkMSec = 1000 * 8;
		MaxBandwidthHistorySize = 3;				// track download speed of the last 5 fragments.
		DiscardSmallBandwidthSamplesLess = 64 << 10;
		FudgeSmallBandwidthSamplesLess = 128 << 10;
		FudgeSmallBandwidthSamplesFactor = 1000;
	}

















	//-----------------------------------------------------------------------------
	/**
	 * Create an instance of this class
	 *
	 * @param PlayerSessionServices
	 * @param config
	 *
	 * @return
	 */
	TSharedPtrTS<IAdaptiveStreamSelector> IAdaptiveStreamSelector::Create(IPlayerSessionServices* PlayerSessionServices, const IAdaptiveStreamSelector::FConfiguration& config)
	{
		return TSharedPtrTS<IAdaptiveStreamSelector>(new FAdaptiveStreamSelector(PlayerSessionServices, config));
	}

	//-----------------------------------------------------------------------------
	/**
	 * CTOR
	 *
	 * @param config
	 */
	FAdaptiveStreamSelector::FAdaptiveStreamSelector(IPlayerSessionServices* InPlayerSessionServices, const IAdaptiveStreamSelector::FConfiguration& config)
		: PlayerSessionServices(InPlayerSessionServices)
		, Config(config)
		, bPlayerIsBuffering(true)
		, BandwidthCeiling(0x7fffffff)
		, ForcedInitialBandwidth(0)
	{
		AverageBandwidth.SetMaxSMASampleCount(Config.MaxBandwidthHistorySize);
		AverageLatency.SetMaxSMASampleCount(Config.MaxBandwidthHistorySize);
		AverageThroughput.SetMaxSMASampleCount(Config.MaxBandwidthHistorySize);
		MaxStreamResolution.Width = MaxStreamResolution.Height = 8192;
		PresentationType = EMediaPresentationType::Realtime;
		bCanSwitchToAlternateStreams = true;
	}

	//-----------------------------------------------------------------------------
	/**
	 * DTOR
	 */
	FAdaptiveStreamSelector::~FAdaptiveStreamSelector()
	{
	}



	//-----------------------------------------------------------------------------
	/**
	 * Sets the type of presentation.
	 *
	 * @param InAssetType
	 */
	void FAdaptiveStreamSelector::SetPresentationType(IAdaptiveStreamSelector::EMediaPresentationType InAssetType)
	{
		PresentationType = InAssetType;
	}


	//-----------------------------------------------------------------------------
	/**
	 * Sets whether or not a segment download failure can potentially be resolved by switching to a different stream. Defaults to yes.
	 *
	 * @param bCanSwitch
	 */
	void FAdaptiveStreamSelector::SetCanSwitchToAlternateStreams(bool bCanSwitch)
	{
		bCanSwitchToAlternateStreams = bCanSwitch;
	}


	//-----------------------------------------------------------------------------
	/**
	 * Sets the playback period from which to select streams now.
	 *
	 * @param InStreamType
	 * @param CurrentPlayPeriod
	 */
	void FAdaptiveStreamSelector::SetCurrentPlaybackPeriod(EStreamType InStreamType, TSharedPtrTS<IManifest::IPlayPeriod> InCurrentPlayPeriod)
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		if (InStreamType == EStreamType::Video)
		{
			CurrentPlayPeriodVideo = InCurrentPlayPeriod;
			if (CurrentPlayPeriodVideo.IsValid())
			{
				TSharedPtrTS<ITimelineMediaAsset> Asset = CurrentPlayPeriodVideo->GetMediaAsset();
				if (Asset.IsValid())
				{
					CurrentVideoAdaptationSetID = InCurrentPlayPeriod->GetSelectedAdaptationSetID(InStreamType);
					StreamInformationVideo.Empty();
					// Get video stream information
					if (!CurrentVideoAdaptationSetID.IsEmpty())
					{
						for(int32 nA=0, maxA=Asset->GetNumberOfAdaptationSets(InStreamType); nA<maxA; ++nA)
						{
							TSharedPtrTS<IPlaybackAssetAdaptationSet> Adapt = Asset->GetAdaptationSetByTypeAndIndex(InStreamType, nA);
							if (CurrentVideoAdaptationSetID.Equals(Adapt->GetUniqueIdentifier()))
							{
								for(int32 i=0, iMax=Adapt->GetNumberOfRepresentations(); i<iMax; ++i)
								{
									TSharedPtrTS<IPlaybackAssetRepresentation> Repr = Adapt->GetRepresentationByIndex(i);
									TSharedPtrTS<FStreamInformation> si = MakeSharedTS<FStreamInformation>();
									si->AdaptationSetUniqueID = Adapt->GetUniqueIdentifier();
									si->RepresentationUniqueID = Repr->GetUniqueIdentifier();
									si->Bitrate = Repr->GetBitrate();
									if (Repr->GetCodecInformation().IsVideoCodec())
									{
										si->Resolution = Repr->GetCodecInformation().GetResolution();
									}
									StreamInformationVideo.Push(si);
								}
								// Sort the representations by ascending bitrate
								StreamInformationVideo.Sort([](const TSharedPtrTS<FStreamInformation>& a, const TSharedPtrTS<FStreamInformation>& b)
								{
									return a->Bitrate < b->Bitrate;
								});
									
								break;
							}
						}
					}
				}
			}
			else
			{
				CurrentVideoAdaptationSetID.Empty();
				StreamInformationVideo.Empty();
			}
		}
		else if (InStreamType == EStreamType::Audio)
		{
			CurrentPlayPeriodAudio = InCurrentPlayPeriod;
			if (CurrentPlayPeriodAudio.IsValid())
			{
				TSharedPtrTS<ITimelineMediaAsset> Asset = CurrentPlayPeriodAudio->GetMediaAsset();
				if (Asset.IsValid())
				{
					CurrentAudioAdaptationSetID = InCurrentPlayPeriod->GetSelectedAdaptationSetID(InStreamType);
					StreamInformationAudio.Empty();
					// Get audio stream information
					if (!CurrentAudioAdaptationSetID.IsEmpty())
					{
						for(int32 nA=0, maxA=Asset->GetNumberOfAdaptationSets(InStreamType); nA<maxA; ++nA)
						{
							TSharedPtrTS<IPlaybackAssetAdaptationSet> Adapt = Asset->GetAdaptationSetByTypeAndIndex(InStreamType, nA);
							if (CurrentAudioAdaptationSetID.Equals(Adapt->GetUniqueIdentifier()))
							{
								for(int32 i=0, iMax=Adapt->GetNumberOfRepresentations(); i<iMax; ++i)
								{
									TSharedPtrTS<IPlaybackAssetRepresentation> Repr = Adapt->GetRepresentationByIndex(i);
									TSharedPtrTS<FStreamInformation> si = MakeSharedTS<FStreamInformation>();
									si->AdaptationSetUniqueID = Adapt->GetUniqueIdentifier();
									si->RepresentationUniqueID = Repr->GetUniqueIdentifier();
									si->Bitrate = Repr->GetBitrate();
									StreamInformationAudio.Push(si);
								}
								// Sort the representations by ascending bitrate
								StreamInformationAudio.Sort([](const TSharedPtrTS<FStreamInformation>& a, const TSharedPtrTS<FStreamInformation>& b)
								{
									return a->Bitrate > b->Bitrate;
								});

								break;
							}
						}
					}
				}
			}
			else
			{
				CurrentAudioAdaptationSetID.Empty();
				StreamInformationAudio.Empty();
			}
		}
	}


	//-----------------------------------------------------------------------------
	/**
	 * Sets the initial bandwidth, either from guessing, past history or a current measurement.
	 *
	 * @param bitPerSecond
	 */
	void FAdaptiveStreamSelector::SetBandwidth(int64 bitsPerSecond)
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		AverageBandwidth.InitializeTo((double)bitsPerSecond);
		AverageLatency.InitializeTo((double)0.1);
		AverageThroughput.InitializeTo(bitsPerSecond);
	}


	//-----------------------------------------------------------------------------
	/**
	 * Sets a forced bitrate for the next segment fetch only.
	 *
	 * @param bitsPerSecond
	 */
	void FAdaptiveStreamSelector::SetForcedNextBandwidth(int64 bitsPerSecond)
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		ForcedInitialBandwidth = (int32)bitsPerSecond;
	}


	//-----------------------------------------------------------------------------
	/**
	 * Called to (temporarily) mark a stream as unavailable.
	 * This is used in formats like HLS when a dedicated stream playlist is in error
	 * and thus segment information for this stream is not available.
	 *
	 * @param BlacklistedStream
	 */
	void FAdaptiveStreamSelector::MarkStreamAsUnavailable(const FBlacklistedStream& BlacklistedStream)
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		BlacklistedExternally.Push(BlacklistedStream);
	}

	//-----------------------------------------------------------------------------
	/**
	 * Marks a previously set to unavailable stream as being available again.
	 * Usually when the stream specific playlist (in HLS) has become available again.
	 *
	 * @param NoLongerBlacklistedStream
	 */
	void FAdaptiveStreamSelector::MarkStreamAsAvailable(const FBlacklistedStream& NoLongerBlacklistedStream)
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		BlacklistedExternally.Remove(NoLongerBlacklistedStream);
	}


	//-----------------------------------------------------------------------------
	/**
	 * Returns the last measured bandwidth sample in bps.
	 *
	 * @return
	 */
	int64 FAdaptiveStreamSelector::GetLastBandwidth()
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		double lastBandwidth = AverageBandwidth.GetLastSample();
		return (int64)lastBandwidth;
	}


	//-----------------------------------------------------------------------------
	/**
	 * Returns the average measured bandwidth sample in bps.
	 *
	 * @return
	 */
	int64 FAdaptiveStreamSelector::GetAverageBandwidth()
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		const double averageBandwidth = AverageBandwidth.GetSMA();
		return (int64)averageBandwidth;
	}

	//-----------------------------------------------------------------------------
	/**
	 * Returns the average measured throughput in bps.
	 *
	 * @return
	 */
	int64 FAdaptiveStreamSelector::GetAverageThroughput()
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		return AverageThroughput.GetSMA();
	}

	//-----------------------------------------------------------------------------
	/**
	 * Returns the average measured latency in seconds.
	 *
	 * @return
	 */
	double FAdaptiveStreamSelector::GetAverageLatency()
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		return AverageLatency.GetSMA();
	}



	//-----------------------------------------------------------------------------
	/**
	 * Sets a highest bandwidth limit for a stream type. Call with 0 to disable.
	 *
	 * @param HighestManifestBitrate
	 */
	void FAdaptiveStreamSelector::SetBandwidthCeiling(int32 HighestManifestBitrate)
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		BandwidthCeiling = HighestManifestBitrate > 0 ? HighestManifestBitrate : 0x7fffffff;
	}


	//-----------------------------------------------------------------------------
	/**
	 * Limits video resolution.
	 *
	 * @param MaxWidth
	 * @param MaxHeight
	 */
	void FAdaptiveStreamSelector::SetMaxVideoResolution(int32 MaxWidth, int32 MaxHeight)
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		MaxStreamResolution.Width = MaxWidth;
		MaxStreamResolution.Height = MaxHeight;
	}



	FABRDownloadProgressDecision FAdaptiveStreamSelector::ReportDownloadProgress(const Metrics::FSegmentDownloadStats& SegmentDownloadStats)
	{
		FABRDownloadProgressDecision Decision;
		uint32 Flags = SegmentDownloadStats.ABRState.ProgressDecision.Flags;

		FAccessUnitBufferInfo CurrentBufferStats;
		PlayerSessionServices->GetStreamBufferStats(CurrentBufferStats, SegmentDownloadStats.StreamType);
		const double secondsInBuffer = CurrentBufferStats.PlayableDuration.GetAsSeconds();
		// Estimate how long we will take to complete the download.
		double EstimatedTotalDownloadTime = SegmentDownloadStats.Duration;
		//double currentPercentage = 0.0;
		// Can we base this off the byte sizes?
		if (SegmentDownloadStats.NumBytesDownloaded > 0 && SegmentDownloadStats.ByteSize > 0)
		{
			EstimatedTotalDownloadTime = (SegmentDownloadStats.TimeToDownload / SegmentDownloadStats.NumBytesDownloaded) * SegmentDownloadStats.ByteSize;
			//currentPercentage = SegmentDownloadStats.NumBytesDownloaded / SegmentDownloadStats.ByteSize;
		}
		// Otherwise, can we base it off the durations?
		else if (SegmentDownloadStats.DurationDownloaded > 0.0 && SegmentDownloadStats.Duration > 0.0)
		{
			EstimatedTotalDownloadTime = (SegmentDownloadStats.TimeToDownload / SegmentDownloadStats.DurationDownloaded) * SegmentDownloadStats.Duration;
			//currentPercentage = SegmentDownloadStats.DurationDownloaded / SegmentDownloadStats.Duration;
		}
		double EstimatedDownloadTimeRemaining = EstimatedTotalDownloadTime - SegmentDownloadStats.TimeToDownload;

		if ((SegmentDownloadStats.ABRState.ProgressDecision.Flags & FABRDownloadProgressDecision::EDecisionFlags::eABR_AbortDownload) == 0)
		{
			// Did we take longer to download than the segment duration?
			double MaxSegmentDownloadDuration = SegmentDownloadStats.Duration;
			// If there is a forced initial bitrate to start playback with then we allow for the download duration to take longer than normally
			// as to not abort the first download unless it's download time is really getting problematic.
			if (ForcedInitialBandwidth && SegmentDownloadStats.StreamType == EStreamType::Video)
			{
				MaxSegmentDownloadDuration *= 2.0;
			}
			if (SegmentDownloadStats.TimeToDownload >= MaxSegmentDownloadDuration)
			{
				// If the estimate to complete download is longer than what is currently in the buffer abort the download and hope
				// switching to a lower quality will help.
		//		if (EstimatedDownloadTimeRemaining >= secondsInBuffer)
		// Be more aggressive and demand the buffer must have the total duration! This is to avoid late switching that may be too late to try to download a whole new segment
				if (EstimatedTotalDownloadTime >= secondsInBuffer)
				{
					Flags = FABRDownloadProgressDecision::EDecisionFlags::eABR_AbortDownload;
					// If content was already emitted into the buffer we cannot switch. Insert dummy filler data instead.
					if (SegmentDownloadStats.DurationDelivered > 0.0)
					{
						Flags |= FABRDownloadProgressDecision::EDecisionFlags::eABR_InsertFillerData;
					}

					Decision.Reason = FString::Printf(TEXT("Abort download of %s %s segment @ %d bps for time %.3f with %.3fs in buffer and %.3fs of %.3fs fetched after %.3fs. Estimated total download time %.3fs")
						, GetStreamTypeName(SegmentDownloadStats.StreamType)
						, GetSegmentTypeString(SegmentDownloadStats.SegmentType)
						, SegmentDownloadStats.Bitrate
						, SegmentDownloadStats.PresentationTime
						, secondsInBuffer
						, SegmentDownloadStats.DurationDownloaded
						, MaxSegmentDownloadDuration
						, SegmentDownloadStats.TimeToDownload
						, EstimatedTotalDownloadTime);
					LogMessage(IInfoLog::ELevel::Info, Decision.Reason);
				}
			}
		}

		// If the buffer is low allow the data being streamed in to already be emitted to the buffer.
		// This will however mean that in case of remaining problems with this segment we cannot switch to another quality level.
		if ((SegmentDownloadStats.ABRState.ProgressDecision.Flags & FABRDownloadProgressDecision::EDecisionFlags::eABR_EmitPartialData) == 0)
		{
			if (secondsInBuffer < 1.0 && SegmentDownloadStats.DurationDelivered == 0.0)
			{
				Flags |= FABRDownloadProgressDecision::EDecisionFlags::eABR_EmitPartialData;
				/*
							LogMessage(IInfoLog::ELevel::Info, StringHelpers::SPrintf("Emitting partial download of %s %s segment @ %d bps for time %.3f with %.3fs in buffer and %.3fs of %.3fs fetched after %.3fs. Estimated total download time %.3fs"
								  , GetStreamTypeName(SegmentDownloadStats.StreamType)
								  , GetSegmentTypeString(SegmentDownloadStats.SegmentType)
								  , SegmentDownloadStats.Bitrate
								  , SegmentDownloadStats.PresentationTime
								  , secondsInBuffer
								  , SegmentDownloadStats.DurationDownloaded
								  , SegmentDownloadStats.Duration
								  , SegmentDownloadStats.TimeToDownload
								  , EstimatedTotalDownloadTime));
				*/
			}
		}

		Decision.Flags = (FABRDownloadProgressDecision::EDecisionFlags) Flags;
		return Decision;
	}

	void FAdaptiveStreamSelector::ReportDownloadEnd(const Metrics::FSegmentDownloadStats& SegmentDownloadStats)
	{
		if (SegmentDownloadStats.SegmentType == Metrics::ESegmentType::Media &&
			SegmentDownloadStats.StreamType == EStreamType::Video &&
			(SegmentDownloadStats.bWasSuccessful || SegmentDownloadStats.NumBytesDownloaded))	// any number of bytes received counts!
		{
			// At the end of the segment download, successful or otherwise, we clear the forced bitrate and let nature take its course.
			ForcedInitialBandwidth = 0;
			// Add bandwidth sample if it can be calculated.
			if (SegmentDownloadStats.NumBytesDownloaded && SegmentDownloadStats.TimeToDownload > 0.0)
			{
				FMediaCriticalSection::ScopedLock lock(AccessMutex);
				double v = SegmentDownloadStats.NumBytesDownloaded * 8 / SegmentDownloadStats.TimeToDownload;
				AverageBandwidth.AddSample(v);
				AverageLatency.AddSample(SegmentDownloadStats.TimeToFirstByte);
				AverageThroughput.AddSample(SegmentDownloadStats.ThroughputBps > 0 ? SegmentDownloadStats.ThroughputBps : (int64)v);
			}
		}
	}

	void FAdaptiveStreamSelector::ReportBufferingStart(Metrics::EBufferingReason BufferingReason)
	{
		/*
			Initial,
			Seeking,
			Rebuffering,
		*/
		bPlayerIsBuffering = true;

		if (BufferingReason == Metrics::EBufferingReason::Rebuffering)
		{
			double Scale = 0.6;
			double avgCurrent = AverageBandwidth.GetLastSample();
			AverageBandwidth.InitializeTo(avgCurrent * Scale);
			AverageThroughput.InitializeTo((int64)(AverageThroughput.GetLastSample() * Scale));
			LogMessage(IInfoLog::ELevel::Info, FString::Printf(TEXT("Rebuffering cuts bandwidth by %.2f from %d to %d"), Scale, (int)avgCurrent, (int)(avgCurrent * Scale)));
		}
	}

	void FAdaptiveStreamSelector::ReportBufferingEnd(Metrics::EBufferingReason BufferingReason)
	{
		bPlayerIsBuffering = false;
	}

	TSharedPtrTS<FAdaptiveStreamSelector::FStreamInformation> FAdaptiveStreamSelector::GetStreamInformation(const Metrics::FSegmentDownloadStats& FromDownloadStats)
	{
		FMediaCriticalSection::ScopedLock lock(AccessMutex);
		const TArray<TSharedPtrTS<FStreamInformation>>* StreamInfos = nullptr;
		if (FromDownloadStats.StreamType == EStreamType::Video)
		{
			StreamInfos = &StreamInformationVideo;
		}
		else if (FromDownloadStats.StreamType == EStreamType::Audio)
		{
			StreamInfos = &StreamInformationAudio;
		}
		if (StreamInfos)
		{
			for(int32 i=0, iMax=StreamInfos->Num(); i<iMax; ++i)
			{
				if (FromDownloadStats.AdaptationSetID == (*StreamInfos)[i]->AdaptationSetUniqueID &&
					FromDownloadStats.RepresentationID == (*StreamInfos)[i]->RepresentationUniqueID &&
					//FromDownloadStats.CDN              == (*StreamInfos)[i]->CDN &&
					1)
				{
					return (*StreamInfos)[i];
				}
			}
		}
		return nullptr;
	}


	//-----------------------------------------------------------------------------
	/**
	 * Simulates download of the next fragments at the current download rate.
	 *
	 * @param sim
	 * @param NextSegments
	 * @param secondsToSimulate
	 * @param atBPS
	 */
	void FAdaptiveStreamSelector::SimulateDownload(FSimulationResult& sim, const TArray<IManifest::IPlayPeriod::FSegmentInformation>& NextSegments, double secondsToSimulate, int64 atBPS)
	{
		sim.SecondsAdded = 0.0;
		sim.SecondsGained = 0.0;
		sim.SmallestGain = 0.0;
		sim.DownloadTime = 0.0;

		bool bFirst = true;
		int32 infoIdx = 0;
		check(NextSegments.Num() > 0);
		double s = 8.0 / atBPS;
		while (secondsToSimulate > 0.0)
		{
			double Dur = NextSegments[infoIdx].Duration.GetAsSeconds();

			double timeToDownload = NextSegments[infoIdx].ByteSize * s;

			secondsToSimulate -= Dur;
			sim.SecondsAdded += Dur;
			sim.DownloadTime += timeToDownload;
			sim.SecondsGained += Dur - timeToDownload;

			if (sim.SecondsGained < sim.SmallestGain || bFirst)
			{
				sim.SmallestGain = sim.SecondsGained;
			}
			bFirst = false;

			// Advance to the next info, if present. Otherwise repeat the last one ad nauseum.
			if (infoIdx + 1 < NextSegments.Num())
			{
				++infoIdx;
			}
		}
	}


	//-----------------------------------------------------------------------------
	/**
	 * Returns the index of a feasible video or multiplex stream.
	 *
	 * @param OutDelay
	 * @param CurrentSegment
	 *
	 * @return
	 */
	IAdaptiveStreamSelector::ESegmentAction FAdaptiveStreamSelector::SelectSuitableStreams(FTimeValue& OutDelay, TSharedPtrTS<const IStreamSegment> CurrentSegment)
	{
		OutDelay.SetToZero();

		FTimeValue TimeNow = PlayerSessionServices->GetSynchronizedUTCTime()->GetTime();
		// Make all streams available again if their blacklist time has expired
		AccessMutex.Lock();
		for(int32 i=0, iMax=StreamInformationVideo.Num(); i<iMax; ++i)
		{
			if (StreamInformationVideo[i]->Health.BecomesAvailableAgainAtUTC <= TimeNow)
			{
				StreamInformationVideo[i]->Health.BecomesAvailableAgainAtUTC.SetToZero();
			}
		}
		for(int32 i=0, iMax=StreamInformationAudio.Num(); i<iMax; ++i)
		{
			if (StreamInformationAudio[i]->Health.BecomesAvailableAgainAtUTC <= TimeNow)
			{
				StreamInformationAudio[i]->Health.BecomesAvailableAgainAtUTC.SetToZero();
			}
		}
		AccessMutex.Unlock();


		EStreamType StreamType = CurrentSegment.IsValid() ? CurrentSegment->GetType() : StreamInformationVideo.Num() ? EStreamType::Video : EStreamType::Audio;
		
		TSharedPtrTS<IManifest::IPlayPeriod> CurrentPlayPeriod;
		if (StreamType == EStreamType::Video)
		{
			CurrentPlayPeriod = CurrentPlayPeriodVideo;
		}
		else if (StreamType == EStreamType::Audio)
		{
			CurrentPlayPeriod = CurrentPlayPeriodAudio;
		}
		if (!CurrentPlayPeriod.IsValid())
		{
			return ESegmentAction::FetchNext;
		}

		// Was the previous segment download in error?
		bool bRetryIfPossible = false;
		bool bSkipWithFiller = false;
		if (CurrentSegment.IsValid())
		{
			Metrics::FSegmentDownloadStats Stats;
			CurrentSegment->GetDownloadStats(Stats);
			// Try to get the stream information for the current downloaded segment. We may not find it on a period transition where the
			// segment is the last of the previous period.
			TSharedPtrTS<FAdaptiveStreamSelector::FStreamInformation> CurrentStreamInfo = GetStreamInformation(Stats);
			if (CurrentStreamInfo.IsValid())
			{
				if (!Stats.bWasSuccessful)
				{
					// Was not successful. Figure out what to do now.
					// In the case where the segment had an availability window set the stream reader was waiting for
					// to be entered and we got a 404 back, the server did not manage to publish the new segment in time.
					// We try this again after a slight delay.
					if (Stats.AvailibilityDelay && Utils::AbsoluteValue(Stats.AvailibilityDelay) < 0.5)
					{
						//LogMessage(IInfoLog::ELevel::Warning, FString::Printf(TEXT("Segment not available at announced time. Trying again.")));
						CurrentPlayPeriod->IncreaseSegmentFetchDelay(FTimeValue(FTimeValue::MillisecondsToHNS(100)));
						OutDelay.SetFromMilliseconds(500);
						return ESegmentAction::Retry;
					}

					// Too many failures already? It's unlikely that there is a way that would magically fix itself now.
					if (Stats.RetryNumber >= 3)
					{
						LogMessage(IInfoLog::ELevel::Error, FString::Printf(TEXT("Exceeded permissable number of retries (%d). Failing now."), Stats.RetryNumber));
						return ESegmentAction::Fail;
					}

					// If we cannot switch to another stream we need to retry this one.
					if (!bCanSwitchToAlternateStreams)
					{
						return ESegmentAction::Retry;
					}

					// Is this an init segment?
					if (Stats.SegmentType == Metrics::ESegmentType::Init)
					{
						bRetryIfPossible = true;
						// Did the init segment fail to be parsed? If so this stream is dead for good.
						if (Stats.bParseFailure)
						{
							CurrentStreamInfo->Health.BecomesAvailableAgainAtUTC.SetToInvalid();
						}
						// Did we abort that stream in ReportDownloadProgress() ?
						else if (Stats.bWasAborted)
						{
							// Since the download was aborted for the lack of time we should not be trying anything of a same or better bitrate.
							double bps = 8.0 * Stats.NumBytesDownloaded / Stats.TimeToDownload;
							double fallbackBps = Stats.Bitrate / 2;
							AverageBandwidth.InitializeTo(Utils::Min(fallbackBps / 2.0, bps));
						}
						// Any other case of failure
						else
						{
							// Take this stream offline for a brief moment.
							if (CurrentStreamInfo->Health.BecomesAvailableAgainAtUTC == FTimeValue::GetZero())
							{
								CurrentStreamInfo->Health.BecomesAvailableAgainAtUTC = TimeNow + FTimeValue().SetFromMilliseconds(1000);
							}
						}
					}
					// A media segment failure
					else
					{
						bRetryIfPossible = true;
						bSkipWithFiller = true;

						// Did we abort that stream in ReportDownloadProgress() ?
						if (Stats.bWasAborted)
						{
							// Since the download was aborted for the lack of time we should not be trying anything of a same or better bitrate.
							if (Stats.StreamType == EStreamType::Video)
							{
								double bps = 8.0 * Stats.NumBytesDownloaded / Stats.TimeToDownload;
								double fallbackBps = Stats.Bitrate / 2;
								AverageBandwidth.InitializeTo(Utils::Min(fallbackBps / 2.0, bps));
							}
						}
						else
						{
							// Take a video stream offline for a brief moment. Audio is usually the only one with no alternative to switch to, so don't do that!
							if (Stats.StreamType == EStreamType::Video)
							{
								if (CurrentStreamInfo->Health.BecomesAvailableAgainAtUTC == FTimeValue::GetZero())
								{
									CurrentStreamInfo->Health.BecomesAvailableAgainAtUTC = TimeNow + FTimeValue().SetFromMilliseconds(1000);
								}
							}
							else
							{
								// For VoD we retry the audio segment up to a certain number of times while for Live we skip over it
								// since retrying is a luxury we do not have in this case.
								if (PresentationType == IAdaptiveStreamSelector::EMediaPresentationType::OnDemand)
								{
									bRetryIfPossible = true;
								}
								else
								{
									bRetryIfPossible = false;
								}
							}
						}

						// If any content has already been put out into the buffers we cannot retry the segment on another quality level.
						if (Stats.DurationDelivered > 0.0)
						{
							bRetryIfPossible = false;
							bSkipWithFiller = false;
						}
					}
				}
				// Update stats
				CurrentStreamInfo->Health.LastDownloadStats = Stats;
			}
		}

		// Anything besides video or audio?
		// NOTE: For the love of God do not remove this line without re-instating the else if() below. See comments there!
		if (StreamType != EStreamType::Video && StreamType != EStreamType::Audio)
		{
			return ESegmentAction::FetchNext;
		}


		TArray<TSharedPtrTS<FStreamInformation>>			PossibleRepresentations;
		FAccessUnitBufferInfo								CurrentBufferStats;

		PlayerSessionServices->GetStreamBufferStats(CurrentBufferStats, StreamType);

		// For video streams do a download simulation
		if (StreamType == EStreamType::Video)
		{
			const double secondsInVideoBuffer = CurrentBufferStats.PlayableDuration.GetAsSeconds();
			// Buffer watermark levels
			const double videoBufferLowWatermark = double(Config.VideoBufferLowWatermarkMSec) / 1000.0;
			const double videoBufferHighWatermark = double(Config.VideoBufferHighWatermarkMSec) / 1000.0;

			// Get average and last bitrate.
			const int64 averageBandwidth = Utils::Max(GetAverageBandwidth(), (int64)100000);
			int64 lastBandwidth = GetLastBandwidth();
			if (lastBandwidth <= 0)
			{
				lastBandwidth = averageBandwidth;
			}
			lastBandwidth = Utils::Max(lastBandwidth, (int64)100000);

			// Setup simulation and last bandwidth such that the current audio bitrate is accounted for and clamped to at least 100 Kbps.
			const int64 simulationBandwidthLongterm = Utils::Max((int64)Config.SimulationLongtermRateScale.Apply(lastBandwidth, averageBandwidth), (int64)100000);
			// How long to simulate download for.
			const double simulationDuration = double(Config.ForwardSimulationDurationMSec) / 1000.0;

			// Bandwidth to simulate download of just the next fragment with.
			const int64 simulationBandwidthNextFragment = Utils::Max((int64)Config.SimulationNextFragmentRateScale.Apply(lastBandwidth, averageBandwidth), (int64)100000);

			// For each video stream simulate how the buffer will behave if we were to use it.
			// The lowest quality is always available regardless of bandwidth or resolution or other constraints.
			if (StreamInformationVideo.Num())
			{
				PossibleRepresentations.Push(StreamInformationVideo[0]);
			}
			for(int32 nStr=1; nStr<StreamInformationVideo.Num(); ++nStr)
			{
				// Check if bitrate and resolution are acceptable
				if (StreamInformationVideo[nStr]->Bitrate <= BandwidthCeiling &&
					!StreamInformationVideo[nStr]->Resolution.ExceedsLimit(MaxStreamResolution))
				{
					FSimulationResult sim;

					// Get information on the next segments.
					TArray<IManifest::IPlayPeriod::FSegmentInformation> NextSegments;
					FTimeValue AverageSegmentDuration;
					CurrentPlayPeriod->GetSegmentInformation(NextSegments, AverageSegmentDuration, CurrentSegment, FTimeValue().SetFromSeconds(simulationDuration), StreamInformationVideo[nStr]->AdaptationSetUniqueID, StreamInformationVideo[nStr]->RepresentationUniqueID);

					// Long term prediction: Simulate download (at a user-scaled bandwidth factor)
					SimulateDownload(sim, NextSegments, simulationDuration, simulationBandwidthLongterm);

					// Mid term prediction: Estimate how much we will have in the video buffer when downloading just the next fragment.
					double secondsInBufferAfterNext;
					if (NextSegments.Num())
					{
						secondsInBufferAfterNext = secondsInVideoBuffer + NextSegments[0].Duration.GetAsSeconds() - (NextSegments[0].ByteSize * (8.0 / simulationBandwidthNextFragment));
					}
					else
					{
						secondsInBufferAfterNext = secondsInVideoBuffer;
					}

					// A stream is feasible if it:
					//    - gives at least some data when the current buffer level is lower than the low-threshold
					//      OR
					//    - even with the smallest gain in data the buffer level AFTER the NEXT fragment will be higher than the low-threshold
					// AND
					//    - the simulated gain plus what is in the buffer now will be higher than the high-threshold.
					bool bCond1 = sim.SmallestGain > 0.0 && secondsInVideoBuffer < videoBufferLowWatermark;
					bool bCond2 = sim.SmallestGain + secondsInBufferAfterNext > videoBufferLowWatermark;
					bool bCond3 = sim.SecondsGained + secondsInVideoBuffer > videoBufferHighWatermark;
					bool bIsFeasible = (bCond1 || bCond2) && bCond3;

					// If there is an enforced initial bitrate then any stream that is within that bitrate is feasible by default.
					if (ForcedInitialBandwidth && StreamInformationVideo[nStr]->Bitrate <= ForcedInitialBandwidth)
					{
						bIsFeasible = true;
					}

					if (bIsFeasible)
					{
						PossibleRepresentations.Push(StreamInformationVideo[nStr]);
					}
				}
			}
		}
		// For audio we do no download simulation or anything.
	// NOTE: removed this else if check because of static code analyzer complaining this would always be true, which it is not.
	//       it is only true as long as the above check that if this is anything but video or audio we already return.
	//       otherwise, with that if() above removed *NOT* having this check here will be a major bug!
		//else if (StreamType == EStreamType::Audio)
		else
		{
			for(int32 nStr=0; nStr<StreamInformationAudio.Num(); ++nStr)
			{
				PossibleRepresentations.Push(StreamInformationAudio[nStr]);
			}
		}

		// At this point we have a list of representations that we could potentially use.
		if (PossibleRepresentations.Num())
		{
			// Remove those that have been externally blacklisted. This may include the lowest quality stream!
			FMediaCriticalSection::ScopedLock lock(AccessMutex);
			if (BlacklistedExternally.Num())
			{
				TArray<TSharedPtrTS<FStreamInformation>> RemainingCandidates;
				for(int32 j=0, jMax=PossibleRepresentations.Num(); j<jMax; ++j)
				{
					bool bStillGood = true;
					for(int32 i=0, iMax=BlacklistedExternally.Num(); i<iMax; ++i)
					{
						if (PossibleRepresentations[j]->AdaptationSetUniqueID == BlacklistedExternally[i].AdaptationSetUniqueID &&
							PossibleRepresentations[j]->RepresentationUniqueID == BlacklistedExternally[i].RepresentationUniqueID)
						{
							bStillGood = false;
							break;
						}
					}
					if (bStillGood)
					{
						RemainingCandidates.Push(PossibleRepresentations[j]);
					}
				}
				Swap(PossibleRepresentations, RemainingCandidates);
			}

			// Filter out streams that are currently not healthy
			//if (1)
			{
				TArray<TSharedPtrTS<FStreamInformation>> RemainingCandidates;
				for(int32 j=0, jMax=PossibleRepresentations.Num(); j<jMax; ++j)
				{
					bool bStillGood = PossibleRepresentations[j]->Health.BecomesAvailableAgainAtUTC <= TimeNow;
					if (bStillGood)
					{
						RemainingCandidates.Push(PossibleRepresentations[j]);
					}
				}
				Swap(PossibleRepresentations, RemainingCandidates);
			}

			// Still something left to use?
			if (PossibleRepresentations.Num())
			{
				const TSharedPtrTS<FStreamInformation>& Candidate = PossibleRepresentations.Top();
				CurrentPlayPeriod->SelectStream(Candidate->AdaptationSetUniqueID, Candidate->RepresentationUniqueID);

				if (bRetryIfPossible)
				{
					return ESegmentAction::Retry;
				}
				else if (bSkipWithFiller)
				{
					return ESegmentAction::Fill;
				}
				else
				{
					return ESegmentAction::FetchNext;
				}
			}
			else
			{
				// TODO: Are there representations we could lift the temporary blacklist from?
				if (bSkipWithFiller)
				{
					return ESegmentAction::Fill;
				}
			}
		}
		return ESegmentAction::Fail;
	}


} // namespace Electra

