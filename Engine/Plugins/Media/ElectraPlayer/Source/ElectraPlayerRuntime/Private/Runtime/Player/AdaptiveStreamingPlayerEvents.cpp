// Copyright Epic Games, Inc. All Rights Reserved.

#include "Player/AdaptiveStreamingPlayerInternal.h"

namespace Electra
{


//-----------------------------------------------------------------------------
/**
 * Dispatches a 'buffering' event.
 *
 * @param bBegin
 * @param reason
 */
void FAdaptiveStreamingPlayer::DispatchBufferingEvent(bool bBegin, FAdaptiveStreamingPlayer::EPlayerState Reason)
{
	if (bBegin)
	{
		DispatchEvent(FMetricEvent::ReportBufferingStart(Reason == EPlayerState::eState_Buffering ? Metrics::EBufferingReason::Initial :
														 Reason == EPlayerState::eState_Rebuffering ? Metrics::EBufferingReason::Rebuffering :
														 Reason == EPlayerState::eState_Seeking ? Metrics::EBufferingReason::Seeking : Metrics::EBufferingReason::Rebuffering));
	}
	else
	{
		DispatchEvent(FMetricEvent::ReportBufferingEnd(Reason == EPlayerState::eState_Buffering ? Metrics::EBufferingReason::Initial :
													   Reason == EPlayerState::eState_Rebuffering ? Metrics::EBufferingReason::Rebuffering :
													   Reason == EPlayerState::eState_Seeking ? Metrics::EBufferingReason::Seeking : Metrics::EBufferingReason::Rebuffering));
	}
}

//-----------------------------------------------------------------------------
/**
 * Dispatches a 'fragment download' event.
 *
 * @param Request
 */
void FAdaptiveStreamingPlayer::DispatchSegmentDownloadedEvent(TSharedPtrTS<IStreamSegment> Request)
{
	if (Request.IsValid())
	{
		Metrics::FSegmentDownloadStats stats;
		Request->GetDownloadStats(stats);
		DispatchEvent(FMetricEvent::ReportSegmentDownload(stats));
	}
}


void FAdaptiveStreamingPlayer::DispatchBufferUtilizationEvent(EStreamType BufferType)
{
	Metrics::FBufferStats stats;
	stats.BufferType = BufferType;

	FAccessUnitBufferInfo bufStats;

	if (BufferType == EStreamType::Video)
	{
		MultiStreamBufferVid.GetStats(bufStats);
	}
	else if (BufferType == EStreamType::Audio)
	{
		MultiStreamBufferAud.GetStats(bufStats);
	}

	stats.MaxDurationInSeconds = bufStats.MaxDuration.GetAsSeconds();
	stats.DurationInUse 	   = bufStats.PushedDuration.GetAsSeconds();
	stats.MaxByteCapacity      = bufStats.MaxDataSize;
	stats.BytesInUse		   = bufStats.CurrentMemInUse;
	DispatchEvent(FMetricEvent::ReportBufferUtilization(stats));
}



} // namespace Electra

