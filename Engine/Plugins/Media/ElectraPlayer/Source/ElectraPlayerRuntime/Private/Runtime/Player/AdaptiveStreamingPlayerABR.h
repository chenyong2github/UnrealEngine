// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "PlayerTime.h"
#include "StreamTypes.h"
#include "HTTP/HTTPManager.h"
#include "Player/Manifest.h"
#include "Player/PlayerSessionServices.h"
#include "Player/AdaptiveStreamingPlayerABR_State.h"
#include "Player/AdaptiveStreamingPlayerMetrics.h"

namespace Electra
{


class IAdaptiveStreamSelector : public IAdaptiveStreamingPlayerMetrics
{
public:
	struct FConfiguration
	{
		FConfiguration();
		struct FRateScale
		{
			FRateScale();
			double Apply(const double lastKbps, const double averageKbps) const;
			float		LastToAvg;			//!< [last, average] bandwidth scaler such that Rate = last + mLastToAvg * (average - last)
			float		Scale;				//!< Scale factor to apply next
			uint32		MaxClampBPS;		//!< Maximum value to clamp result to (in bits per second)
		};
		FRateScale		SimulationLongtermRateScale;
		FRateScale		SimulationNextFragmentRateScale;
		FRateScale		FinalRateAdjust;
		uint32			ForwardSimulationDurationMSec;			//!< Forward simulation duration
		uint32			VideoBufferLowWatermarkMSec;
		uint32			VideoBufferHighWatermarkMSec;
		uint32			MaxBandwidthHistorySize;				//!< Number of bandwidth history samples to give the estimated average bandwidth.
		uint32			DiscardSmallBandwidthSamplesLess;
		uint32			FudgeSmallBandwidthSamplesLess;
		uint32			FudgeSmallBandwidthSamplesFactor;
	};

	//! Create an instance of this class
	static TSharedPtrTS<IAdaptiveStreamSelector> Create(IPlayerSessionServices* PlayerSessionServices, const FConfiguration& config);
	virtual ~IAdaptiveStreamSelector() = default;

	enum class EMediaPresentationType
	{
		OnDemand,
		Live,
		Realtime
	};
	virtual void SetPresentationType(EMediaPresentationType AssetType) = 0;

	//! Sets whether or not a segment download failure can potentially be resolved by switching to a different stream. Defaults to yes.
	virtual void SetCanSwitchToAlternateStreams(bool bCanSwitch) = 0;

	//! Sets the manifest from which to select streams.
	virtual void SetCurrentPlaybackPeriod(EStreamType InStreamType, TSharedPtrTS<IManifest::IPlayPeriod> CurrentPlayPeriod) = 0;

	//! Sets the initial bandwidth, either from guessing, past history or a current measurement.
	virtual void SetBandwidth(int64 bitsPerSecond) = 0;

	//! Sets a forced bitrate for the next segment fetch only.
	virtual void SetForcedNextBandwidth(int64 bitsPerSecond) = 0;

	struct FBlacklistedStream
	{
		FString		AssetUniqueID;
		FString		AdaptationSetUniqueID;
		FString		RepresentationUniqueID;
		FString		CDN;
		bool operator == (const FBlacklistedStream& rhs) const
		{
			return AssetUniqueID == rhs.AssetUniqueID &&
				   AdaptationSetUniqueID == rhs.AdaptationSetUniqueID &&
				   RepresentationUniqueID == rhs.RepresentationUniqueID &&
				   CDN == rhs.CDN;
		}
	};
	virtual void MarkStreamAsUnavailable(const FBlacklistedStream& BlacklistedStream) = 0;
	virtual void MarkStreamAsAvailable(const FBlacklistedStream& NoLongerBlacklistedStream) = 0;


	virtual FABRDownloadProgressDecision ReportDownloadProgress(const Metrics::FSegmentDownloadStats& SegmentDownloadStats) = 0;
	virtual void ReportDownloadEnd(const Metrics::FSegmentDownloadStats& SegmentDownloadStats) = 0;


	//! Returns the last measured bandwidth sample in bps.
	virtual int64 GetLastBandwidth() = 0;

	//! Returns the average measured bandwidth in bps.
	virtual int64 GetAverageBandwidth() = 0;

	//! Returns the average measured throughput in bps.
	virtual int64 GetAverageThroughput() = 0;

	//! Returns the average measured latency in seconds.
	virtual double GetAverageLatency() = 0;

	//! Sets a highest bandwidth limit. Call with 0 to disable.
	virtual void SetBandwidthCeiling(int32 HighestManifestBitrate) = 0;

	//! Limits video resolution.
	virtual void SetMaxVideoResolution(int32 MaxWidth, int32 MaxHeight) = 0;

	enum class ESegmentAction
	{
		FetchNext,				//!< Fetch the next segment normally.
		Retry,					//!< Retry the same segment. Another quality or CDN may have been picked out already.
		Fill,					//!< Fill the segment's duration worth with dummy data.
		Fail					//!< Abort playback
	};

	//! Selects a feasible stream from the stream set to fetch the next fragment from.
	virtual ESegmentAction SelectSuitableStreams(FTimeValue& OutDelay, TSharedPtrTS<const IStreamSegment> CurrentSegment) = 0;


	virtual void ReportOpenSource(const FString& URL) = 0;
	virtual void ReportReceivedMasterPlaylist(const FString& EffectiveURL) = 0;
	virtual void ReportReceivedPlaylists() = 0;
	virtual void ReportTracksChanged() = 0;
	virtual void ReportPlaylistDownload(const Metrics::FPlaylistDownloadStats& PlaylistDownloadStats) = 0;
	virtual void ReportBufferingStart(Metrics::EBufferingReason BufferingReason) = 0;
	virtual void ReportBufferingEnd(Metrics::EBufferingReason BufferingReason) = 0;
	virtual void ReportBandwidth(int64 EffectiveBps, int64 ThroughputBps, double LatencyInSeconds) = 0;
	virtual void ReportBufferUtilization(const Metrics::FBufferStats& BufferStats) = 0;
	virtual void ReportSegmentDownload(const Metrics::FSegmentDownloadStats& SegmentDownloadStats) = 0;
	virtual void ReportLicenseKey(const Metrics::FLicenseKeyStats& LicenseKeyStats) = 0;
	virtual void ReportDataAvailabilityChange(const Metrics::FDataAvailabilityChange& DataAvailability) = 0;
	virtual void ReportVideoQualityChange(int32 NewBitrate, int32 PreviousBitrate, bool bIsDrasticDownswitch) = 0;
	virtual void ReportPrerollStart() = 0;
	virtual void ReportPrerollEnd() = 0;
	virtual void ReportPlaybackStart() = 0;
	virtual void ReportPlaybackPaused() = 0;
	virtual void ReportPlaybackResumed() = 0;
	virtual void ReportPlaybackEnded() = 0;
	virtual void ReportJumpInPlayPosition(const FTimeValue& ToNewTime, const FTimeValue& FromTime, Metrics::ETimeJumpReason TimejumpReason) = 0;
	virtual void ReportPlaybackStopped() = 0;
	virtual void ReportError(const FString& ErrorReason) = 0;
	virtual void ReportLogMessage(IInfoLog::ELevel LogLevel, const FString& LogMessage, int64 PlayerWallclockMilliseconds) = 0;
	virtual void ReportDroppedVideoFrame() = 0;
	virtual void ReportDroppedAudioFrame() = 0;
};


} // namespace Electra


