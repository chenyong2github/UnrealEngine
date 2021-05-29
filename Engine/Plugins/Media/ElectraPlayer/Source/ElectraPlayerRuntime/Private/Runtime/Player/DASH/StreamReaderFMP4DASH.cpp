// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "HTTP/HTTPManager.h"
#include "StreamReaderFMP4DASH.h"
#include "Demuxer/ParserISO14496-12.h"
#include "Demuxer/ParserISO14496-12_Utils.h"
#include "StreamAccessUnitBuffer.h"
#include "HAL/LowLevelMemTracker.h"
#include "ElectraPlayerPrivate.h"
#include "Player/AdaptiveStreamingPlayerABR.h"
#include "Player/PlayerEntityCache.h"
#include "Player/DASH/PlaylistReaderDASH.h"
#include "Player/DASH/OptionKeynamesDASH.h"
#include "Player/DASH/PlayerEventDASH.h"
#include "Player/DASH/PlayerEventDASH_Internal.h"
#include "Player/DRM/DRMManager.h"
#include "Utilities/Utilities.h"

#define INTERNAL_ERROR_INIT_SEGMENT_DOWNLOAD_ERROR					1
#define INTERNAL_ERROR_INIT_SEGMENT_PARSE_ERROR						2


DECLARE_CYCLE_STAT(TEXT("FStreamReaderFMP4DASH::HandleRequest"), STAT_ElectraPlayer_DASH_StreamReader, STATGROUP_ElectraPlayer);

namespace Electra
{

FStreamSegmentRequestFMP4DASH::FStreamSegmentRequestFMP4DASH()
{
}

FStreamSegmentRequestFMP4DASH::~FStreamSegmentRequestFMP4DASH()
{
}

void FStreamSegmentRequestFMP4DASH::SetPlaybackSequenceID(uint32 PlaybackSequenceID)
{
	CurrentPlaybackSequenceID = PlaybackSequenceID;
}

uint32 FStreamSegmentRequestFMP4DASH::GetPlaybackSequenceID() const
{
	return CurrentPlaybackSequenceID;
}

void FStreamSegmentRequestFMP4DASH::SetExecutionDelay(const FTimeValue& ExecutionDelay)
{
	DownloadDelayTime = ExecutionDelay;
}


EStreamType FStreamSegmentRequestFMP4DASH::GetType() const
{
	return StreamType;
}

void FStreamSegmentRequestFMP4DASH::GetDependentStreams(TArray<FDependentStreams>& OutDependentStreams) const
{
	OutDependentStreams.Empty();
	for(int32 i=0; i<DependentStreams.Num(); ++i)
	{
		FDependentStreams& depStr = OutDependentStreams.AddDefaulted_GetRef();
		depStr.StreamType = DependentStreams[i]->GetType();
	}
}

void FStreamSegmentRequestFMP4DASH::GetEndedStreams(TArray<TSharedPtrTS<IStreamSegment>>& OutAlreadyEndedStreams)
{
	OutAlreadyEndedStreams.Empty();
	if (bIsEOSSegment)
	{
		OutAlreadyEndedStreams.Push(SharedThis(this));
	}
	for(int32 i=0; i<DependentStreams.Num(); ++i)
	{
		if (DependentStreams[i]->bIsEOSSegment)
		{
			OutAlreadyEndedStreams.Push(DependentStreams[i]);
		}
	}
}

FTimeValue FStreamSegmentRequestFMP4DASH::GetFirstPTS() const
{
	return AST + AdditionalAdjustmentTime + PeriodStart + FTimeValue(Segment.Time - Segment.PTO, Segment.Timescale);
}

int32 FStreamSegmentRequestFMP4DASH::GetQualityIndex() const
{
	return Representation->GetQualityIndex();
}

int32 FStreamSegmentRequestFMP4DASH::GetBitrate() const
{
	return Representation->GetBitrate();
}

void FStreamSegmentRequestFMP4DASH::GetDownloadStats(Metrics::FSegmentDownloadStats& OutStats) const
{
	OutStats = DownloadStats;
}

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

FStreamReaderFMP4DASH::FStreamReaderFMP4DASH()
{
}


FStreamReaderFMP4DASH::~FStreamReaderFMP4DASH()
{
	Close();
}

UEMediaError FStreamReaderFMP4DASH::Create(IPlayerSessionServices* InPlayerSessionService, const IStreamReader::CreateParam& InCreateParam)
{
	check(InPlayerSessionService);
	PlayerSessionService = InPlayerSessionService;

	if (!InCreateParam.MemoryProvider || !InCreateParam.EventListener)
	{
		return UEMEDIA_ERROR_BAD_ARGUMENTS;
	}

	bIsStarted = true;
	for(int32 i=0; i<FMEDIA_STATIC_ARRAY_COUNT(StreamHandlers); ++i)
	{
		StreamHandlers[i].PlayerSessionService = PlayerSessionService;
		StreamHandlers[i].Parameters		   = InCreateParam;
		StreamHandlers[i].bTerminate		   = false;
		StreamHandlers[i].bRequestCanceled     = false;
		StreamHandlers[i].bSilentCancellation  = false;
		StreamHandlers[i].bHasErrored   	   = false;
		StreamHandlers[i].IsIdleSignal.Signal();

		StreamHandlers[i].ThreadSetName(i==0?"ElectraPlayer::fmp4 Video":"ElectraPlayer::fmp4 Audio");
		StreamHandlers[i].ThreadStart(Electra::MakeDelegate(&StreamHandlers[i], &FStreamHandler::WorkerThread));
	}
	return UEMEDIA_ERROR_OK;
}

void FStreamReaderFMP4DASH::Close()
{
	if (bIsStarted)
	{
		bIsStarted = false;
		// Signal the worker threads to end.
		for(int32 i=0; i<FMEDIA_STATIC_ARRAY_COUNT(StreamHandlers); ++i)
		{
			StreamHandlers[i].bTerminate = true;
			StreamHandlers[i].Cancel(true);
			StreamHandlers[i].SignalWork();
		}
		// Wait until they finished.
		for(int32 i=0; i<FMEDIA_STATIC_ARRAY_COUNT(StreamHandlers); ++i)
		{
			StreamHandlers[i].ThreadWaitDone();
			StreamHandlers[i].ThreadReset();
		}
	}
}

IStreamReader::EAddResult FStreamReaderFMP4DASH::AddRequest(uint32 CurrentPlaybackSequenceID, TSharedPtrTS<IStreamSegment> InRequest)
{
	TSharedPtrTS<FStreamSegmentRequestFMP4DASH> Request = StaticCastSharedPtr<FStreamSegmentRequestFMP4DASH>(InRequest);

	if (Request->bIsInitialStartRequest)
	{
		for(int32 i=0; i<Request->DependentStreams.Num(); ++i)
		{
			EAddResult Result = AddRequest(CurrentPlaybackSequenceID, Request->DependentStreams[i]);
			if (Result != IStreamReader::EAddResult::Added)
			{
				return Result;
			}
		}
	}
	else
	{
		// Video and audio only for now.
		if (Request->GetType() != EStreamType::Video && Request->GetType() != EStreamType::Audio)
		{
			ErrorDetail.SetMessage(FString::Printf(TEXT("Request is not for video or audio")));
			return IStreamReader::EAddResult::Error;
		}
	
		// Get the handler for the main request.
		// TODO: We could have a pool of handlers or create a handler on demand if necessary.
		//       Alternatively think about adding a local queue but be wary about request cancellation.
		FStreamHandler* Handler = nullptr;
		switch(Request->GetType())
		{
			case EStreamType::Video:
				Handler = &StreamHandlers[0];
				break;
			case EStreamType::Audio:
				Handler = &StreamHandlers[1];
				break;
			default:
				break;
		}
		if (!Handler)
		{
			ErrorDetail.SetMessage(FString::Printf(TEXT("No handler for stream type")));
			return IStreamReader::EAddResult::Error;
		}
		// Is the handler busy?
		bool bIsIdle = Handler->IsIdleSignal.WaitTimeout(1000 * 1000);
		if (!bIsIdle)
		{
			ErrorDetail.SetMessage(FString::Printf(TEXT("The handler for this stream type is busy!?")));
			return IStreamReader::EAddResult::Error;
		}

		Request->SetPlaybackSequenceID(CurrentPlaybackSequenceID);
		Handler->bRequestCanceled = false;
		Handler->bSilentCancellation = false;
		Handler->CurrentRequest = Request;
		Handler->SignalWork();
	}
	return IStreamReader::EAddResult::Added;
}

void FStreamReaderFMP4DASH::CancelRequest(EStreamType StreamType, bool bSilent)
{
	if (StreamType == EStreamType::Video)
	{
		StreamHandlers[0].Cancel(bSilent);
	}
	else if (StreamType == EStreamType::Audio)
	{
		StreamHandlers[1].Cancel(bSilent);
	}
}

void FStreamReaderFMP4DASH::CancelRequests()
{
	for(int32 i=0; i<FMEDIA_STATIC_ARRAY_COUNT(StreamHandlers); ++i)
	{
		StreamHandlers[i].Cancel(false);
	}
}





uint32 FStreamReaderFMP4DASH::FStreamHandler::UniqueDownloadID = 1;

FStreamReaderFMP4DASH::FStreamHandler::FStreamHandler()
{ }

FStreamReaderFMP4DASH::FStreamHandler::~FStreamHandler()
{
	// NOTE: The thread will have been terminated by the enclosing FStreamReaderFMP4DASH's Close() method!
}

void FStreamReaderFMP4DASH::FStreamHandler::Cancel(bool bSilent)
{
	bSilentCancellation = bSilent;
	bRequestCanceled = true;
}

void FStreamReaderFMP4DASH::FStreamHandler::SignalWork()
{
	WorkSignal.Release();
}

void FStreamReaderFMP4DASH::FStreamHandler::WorkerThread()
{
	LLM_SCOPE(ELLMTag::ElectraPlayer);

	StreamSelector = PlayerSessionService->GetStreamSelector();
	check(StreamSelector.IsValid());
	while(!bTerminate)
	{
		WorkSignal.Obtain();
		if (!bTerminate)
		{
			if (CurrentRequest.IsValid())
			{
				IsIdleSignal.Reset();
				if (!bRequestCanceled)
				{
					HandleRequest();
				}
				else
				{
					CurrentRequest.Reset();
				}
				IsIdleSignal.Signal();
			}
			bRequestCanceled = false;
			bSilentCancellation = false;
		}
	}
	StreamSelector.Reset();
}


void FStreamReaderFMP4DASH::FStreamHandler::LogMessage(IInfoLog::ELevel Level, const FString& Message)
{
	PlayerSessionService->PostLog(Facility::EFacility::DASHFMP4Reader, Level, Message);
}


int32 FStreamReaderFMP4DASH::FStreamHandler::HTTPProgressCallback(const IElectraHttpManager::FRequest* Request)
{
	HTTPUpdateStats(MEDIAutcTime::Current(), Request);
	++ProgressReportCount;

	// Aborted?
	return HasReadBeenAborted() ? 1 : 0;
}

void FStreamReaderFMP4DASH::FStreamHandler::HTTPCompletionCallback(const IElectraHttpManager::FRequest* Request)
{
	HTTPUpdateStats(FTimeValue::GetInvalid(), Request);

	bHasErrored = Request->ConnectionInfo.StatusInfo.ErrorDetail.IsError();
	DownloadCompleteSignal.Signal();
}

void FStreamReaderFMP4DASH::FStreamHandler::HTTPUpdateStats(const FTimeValue& CurrentTime, const IElectraHttpManager::FRequest* Request)
{
	TSharedPtrTS<FStreamSegmentRequestFMP4DASH> SegmentRequest = CurrentRequest;
	if (SegmentRequest.IsValid())
	{
		FMediaCriticalSection::ScopedLock lock(MetricUpdateLock);
		SegmentRequest->ConnectionInfo = Request->ConnectionInfo;
		// Update the current download stats which we report periodically to the ABR.
		Metrics::FSegmentDownloadStats& ds = SegmentRequest->DownloadStats;
		if (Request->ConnectionInfo.EffectiveURL.Len())
		{
			ds.URL 			  = Request->ConnectionInfo.EffectiveURL;
		}
		ds.HTTPStatusCode     = Request->ConnectionInfo.StatusInfo.HTTPStatus;
		ds.TimeToFirstByte    = Request->ConnectionInfo.TimeUntilFirstByte;
		ds.TimeToDownload     = ((CurrentTime.IsValid() ? CurrentTime : Request->ConnectionInfo.RequestEndTime) - Request->ConnectionInfo.RequestStartTime).GetAsSeconds();
		ds.ByteSize 		  = Request->ConnectionInfo.ContentLength;
		ds.NumBytesDownloaded = Request->ConnectionInfo.BytesReadSoFar;
	}
}





FErrorDetail FStreamReaderFMP4DASH::FStreamHandler::GetInitSegment(TSharedPtrTS<const IParserISO14496_12>& OutMP4InitSegment, const TSharedPtrTS<FStreamSegmentRequestFMP4DASH>& Request)
{
	// Is an init segment required?
	if (Request->Segment.InitializationURL.URL.IsEmpty())
	{
		// No init segment required. We're done.
		return FErrorDetail();
	}

	// Check if we already have this cached.
	IPlayerEntityCache::FCacheItem CachedItem;
	if (PlayerSessionService->GetEntityCache()->GetCachedEntity(CachedItem, Request->Segment.InitializationURL.URL, Request->Segment.InitializationURL.Range))
	{
		// Already cached. Use it.
		OutMP4InitSegment = CachedItem.Parsed14496_12Data;
		return FErrorDetail();
	}

	// Get the manifest reader. We need it to handle the init segment download.
	TSharedPtrTS<IPlaylistReader> ManifestReader = PlayerSessionService->GetManifestReader();
	if (!ManifestReader.IsValid())
	{
		return PostError(PlayerSessionService, TEXT("Entity loader disappeared"), 0);
	}

	// Create a download finished structure we can wait upon for completion.
	struct FLoadResult
	{
		FMediaEvent Event;
		bool bSuccess = false;
	};
	TSharedPtrTS<FLoadResult> LoadResult = MakeSharedTS<FLoadResult>();

	// Create the download request.
	TSharedPtrTS<FMPDLoadRequestDASH> LoadReq = MakeSharedTS<FMPDLoadRequestDASH>();
	LoadReq->LoadType = FMPDLoadRequestDASH::ELoadType::Segment;
	LoadReq->URL = Request->Segment.InitializationURL.URL;
	LoadReq->Range = Request->Segment.InitializationURL.Range;
	if (Request->Segment.InitializationURL.CustomHeader.Len())
	{
		LoadReq->Headers.Emplace(HTTP::FHTTPHeader({DASH::HTTPHeaderOptionName, Request->Segment.InitializationURL.CustomHeader}));
	}
	LoadReq->PlayerSessionServices = PlayerSessionService;
	LoadReq->CompleteCallback.BindLambda([LoadResult](TSharedPtrTS<FMPDLoadRequestDASH> LoadRequest, bool bSuccess)
	{
		LoadResult->bSuccess = bSuccess;
		LoadResult->Event.Signal();
	});
	// Issue the download request.
	check(ManifestReader->GetPlaylistType().Equals(TEXT("dash")));
	IPlaylistReaderDASH* Reader = static_cast<IPlaylistReaderDASH*>(ManifestReader.Get());
	Reader->AddElementLoadRequests(TArray<TWeakPtrTS<FMPDLoadRequestDASH>>({LoadReq}));

	// Wait for completion or abort
	while(!HasReadBeenAborted())
	{
		if (LoadResult->Event.WaitTimeout(1000 * 100))
		{
			break;
		}
	}
	if (HasReadBeenAborted())
	{
		return FErrorDetail();
	}
	
	// Set up download stats to be sent to the stream selector.
	const HTTP::FConnectionInfo* ci = LoadReq->GetConnectionInfo();
	Metrics::FSegmentDownloadStats ds;
	ds.StatsID = FMediaInterlockedIncrement(UniqueDownloadID);
	ds.StreamType = Request->GetType();
	ds.SegmentType = Metrics::ESegmentType::Init;
	ds.URL = Request->Segment.InitializationURL.URL;
	ds.Range = Request->Segment.InitializationURL.Range;
	ds.CDN = Request->Segment.InitializationURL.CDN;
	ds.bWasSuccessful = LoadResult->bSuccess;
	ds.HTTPStatusCode = ci->StatusInfo.HTTPStatus;
	ds.TimeToFirstByte = ci->TimeUntilFirstByte;
	ds.TimeToDownload = (ci->RequestEndTime - ci->RequestStartTime).GetAsSeconds();
	ds.ByteSize = ci->ContentLength;
	ds.NumBytesDownloaded = ci->BytesReadSoFar;
	ds.ThroughputBps = ci->Throughput.GetThroughput() ? ci->Throughput.GetThroughput() : (ds.TimeToDownload > 0.0 ? 8 * ds.NumBytesDownloaded / ds.TimeToDownload : 0);
	ds.MediaAssetID = Request->Period.IsValid() ? Request->Period->GetUniqueIdentifier() : "";
	ds.AdaptationSetID = Request->AdaptationSet.IsValid() ? Request->AdaptationSet->GetUniqueIdentifier() : "";
	ds.RepresentationID = Request->Representation.IsValid() ? Request->Representation->GetUniqueIdentifier() : "";
	ds.Bitrate = Request->GetBitrate();

	if (!LoadResult->bSuccess)
	{
		Request->ConnectionInfo = *ci;
		StreamSelector->ReportDownloadEnd(ds);
		return CreateError(FString::Printf(TEXT("Init segment download error: %s"),	*ci->StatusInfo.ErrorDetail.GetMessage()), INTERNAL_ERROR_INIT_SEGMENT_DOWNLOAD_ERROR);
	}

	SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_DASH_StreamReader);
	CSV_SCOPED_TIMING_STAT(ElectraPlayer, DASH_StreamReader);

	FMP4StaticDataReader StaticDataReader;
	StaticDataReader.SetParseData(LoadReq->Request->GetResponseBuffer());
	TSharedPtrTS<IParserISO14496_12> Init = IParserISO14496_12::CreateParser();
	UEMediaError parseError = Init->ParseHeader(&StaticDataReader, this, PlayerSessionService, nullptr);
	if (parseError == UEMEDIA_ERROR_OK || parseError == UEMEDIA_ERROR_END_OF_STREAM)
	{
		// Parse the tracks of the init segment. We do this mainly to get to the CSD we might need should we have to insert filler data later.
		parseError = Init->PrepareTracks(TSharedPtrTS<const IParserISO14496_12>());
		if (parseError == UEMEDIA_ERROR_OK)
		{
			// Add this to the entity cache in case it needs to be retrieved again.
			IPlayerEntityCache::FCacheItem CacheItem;
			CacheItem.URL = LoadReq->URL;
			CacheItem.Range = LoadReq->Range;
			CacheItem.Parsed14496_12Data = Init;
			PlayerSessionService->GetEntityCache()->CacheEntity(CacheItem);
			OutMP4InitSegment = Init;

			StreamSelector->ReportDownloadEnd(ds);
			return FErrorDetail();
		}
		else
		{
			ds.bParseFailure = true;
			Request->ConnectionInfo = *ci;
			StreamSelector->ReportDownloadEnd(ds);
			return CreateError(FString::Printf(TEXT("Track preparation of init segment \"%s\" failed"), *LoadReq->URL), INTERNAL_ERROR_INIT_SEGMENT_PARSE_ERROR);
		}
	}
	else
	{
		ds.bParseFailure = true;
		StreamSelector->ReportDownloadEnd(ds);
		return CreateError(FString::Printf(TEXT("Parse error of init segment \"%s\""), *LoadReq->URL), INTERNAL_ERROR_INIT_SEGMENT_PARSE_ERROR);
	}
}

void FStreamReaderFMP4DASH::FStreamHandler::CheckForInbandDASHEvents()
{
	SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_DASH_StreamReader);
	CSV_SCOPED_TIMING_STAT(ElectraPlayer, DASH_StreamReader);
	bool bHasInbandEvent = false;
	if (!CurrentRequest->bIsEOSSegment)
	{
		for(int32 i=0; i<CurrentRequest->Segment.InbandEventStreams.Num(); ++i)
		{
			const FManifestDASHInternal::FSegmentInformation::FInbandEventStream& ibs = CurrentRequest->Segment.InbandEventStreams[i];
			if (ibs.SchemeIdUri.Equals(DASH::Schemes::ManifestEvents::Scheme_urn_mpeg_dash_event_2012))
			{
				bHasInbandEvent = true;
				break;
			}
		}
	}
	TSharedPtrTS<IPlaylistReader> ManifestReader = PlayerSessionService->GetManifestReader();
	IPlaylistReaderDASH* Reader = static_cast<IPlaylistReaderDASH*>(ManifestReader.Get());
	if (Reader)
	{
		Reader->SetStreamInbandEventUsage(CurrentRequest->GetType(), bHasInbandEvent);
	}
}

void FStreamReaderFMP4DASH::FStreamHandler::HandleEventMessages()
{
	SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_DASH_StreamReader);
	CSV_SCOPED_TIMING_STAT(ElectraPlayer, DASH_StreamReader);
	// Are there 'emsg' boxes we need to handle?
	if (MP4Parser->GetNumberOfEventMessages())
	{
		FTimeValue AbsPeriodStart = CurrentRequest->PeriodStart + CurrentRequest->AST + CurrentRequest->AdditionalAdjustmentTime + CurrentRequest->PlayerLoopState.LoopBasetime;
		// We may need the EPT from the 'sidx' if there is one.
		const IParserISO14496_12::ISegmentIndex* Sidx = MP4Parser->GetSegmentIndexByIndex(0);
		for(int32 nEmsg=0, nEmsgs=MP4Parser->GetNumberOfEventMessages(); nEmsg<nEmsgs; ++nEmsg)
		{
			const IParserISO14496_12::IEventMessage* Emsg = MP4Parser->GetEventMessageByIndex(nEmsg);
			// This event must be described by an <InbandEventStream> in order to be processed.
			for(int32 i=0; i<CurrentRequest->Segment.InbandEventStreams.Num(); ++i)
			{
				const FManifestDASHInternal::FSegmentInformation::FInbandEventStream& ibs = CurrentRequest->Segment.InbandEventStreams[i];
				if (ibs.SchemeIdUri.Equals(Emsg->GetSchemeIdUri()) &&
					(ibs.Value.IsEmpty() || ibs.Value.Equals(Emsg->GetValue())))
				{
					TSharedPtrTS<DASH::FPlayerEvent> NewEvent = MakeSharedTS<DASH::FPlayerEvent>();
					NewEvent->SetOrigin(IAdaptiveStreamingPlayerAEMSEvent::EOrigin::InbandEventStream);
					NewEvent->SetSchemeIdUri(Emsg->GetSchemeIdUri());
					NewEvent->SetValue(Emsg->GetValue());
					NewEvent->SetID(LexToString(Emsg->GetID()));
					uint32 Timescale = Emsg->GetTimescale();
					uint32 Duration = Emsg->GetEventDuration();
					FTimeValue PTS;
					if (Emsg->GetVersion() == 0)
					{
						// Version 0 uses a presentation time delta relative to the EPT of the SIDX, if it exists, or if not
						// to the PTS of the first AU, which should be the same as the segment media start time.
						FTimeValue PTD((int64)Emsg->GetPresentationTimeDelta(), Timescale);
						FTimeValue EPT;
						if (Sidx)
						{
							EPT.SetFromND((int64)Sidx->GetEarliestPresentationTime(), Sidx->GetTimescale());
						}
						else
						{
							EPT.SetFromND((int64)CurrentRequest->Segment.Time, CurrentRequest->Segment.Timescale);
						}
						FTimeValue PTO(CurrentRequest->Segment.PTO, CurrentRequest->Segment.Timescale);
						PTS = AbsPeriodStart - PTO + EPT + PTD;
					}
					else if (Emsg->GetVersion() == 1)
					{
						FTimeValue EventTime(Emsg->GetPresentationTime(), Timescale);
						FTimeValue PTO(FTimeValue::GetZero());
						PTS = AbsPeriodStart - PTO + EventTime;
					}
					NewEvent->SetPresentationTime(PTS);
					if (Duration != 0xffffffff)
					{
						NewEvent->SetDuration(FTimeValue((int64)Duration, Timescale));
					}
					NewEvent->SetMessageData(Emsg->GetMessageData());
					NewEvent->SetPeriodID(CurrentRequest->Period->GetUniqueIdentifier());
					// Add the event to the handler.
					if (PTS.IsValid())
					{
						/*
							Check that we have no seen this event in this segment already. This is for the case where the 'emsg' appears
							inbetween multiple 'moof' boxes. As per ISO/IEC 23009-1:2019 Section 5.10.3.3.1 General:
								A Media Segment if based on the ISO BMFF container may contain one or more event message ('emsg') boxes. If present, any 'emsg' box shall be placed as follows:
								- It may be placed before the first 'moof' box of the segment.
								- It may be placed in between any 'mdat' and 'moof' box. In this case, an equivalent 'emsg' with the same id value shall be present before the first 'moof' box of any Segment.
						*/
						int32 EventIdx = SegmentEventsFound.IndexOfByPredicate([&NewEvent](const TSharedPtrTS<DASH::FPlayerEvent>& This) {
							return NewEvent->GetSchemeIdUri().Equals(This->GetSchemeIdUri()) &&
								   NewEvent->GetID().Equals(This->GetID()) &&
								   (NewEvent->GetValue().IsEmpty() || NewEvent->GetValue().Equals(This->GetValue()));
						});
						if (EventIdx == INDEX_NONE)
						{
							PlayerSessionService->GetAEMSEventHandler()->AddEvent(NewEvent, CurrentRequest->Period->GetUniqueIdentifier(), IAdaptiveStreamingPlayerAEMSHandler::EEventAddMode::AddIfNotExists);
							SegmentEventsFound.Emplace(MoveTemp(NewEvent));
						}
					}
					break;
				}
			}
		}
	}
}

void FStreamReaderFMP4DASH::FStreamHandler::HandleRequest()
{
	// Get the request into a local shared pointer to hold on to it.
	TSharedPtrTS<FStreamSegmentRequestFMP4DASH> Request = CurrentRequest;
	TSharedPtr<const FPlayerLoopState, ESPMode::ThreadSafe> PlayerLoopState = MakeShared<const FPlayerLoopState, ESPMode::ThreadSafe>(Request->PlayerLoopState);
	TSharedPtrTS<FAccessUnit::CodecData> CSD(new FAccessUnit::CodecData);
	FTimeValue SegmentDuration = FTimeValue().SetFromND(Request->Segment.Duration, Request->Segment.Timescale);
	FString RequestURL = Request->Segment.MediaURL.URL;
	TSharedPtrTS<const IParserISO14496_12> MP4InitSegment;
	FErrorDetail InitSegmentError;
	UEMediaError Error;
	bool bIsEmptyFillerSegment = Request->bInsertFillerData;
	bool bIsLastSegment = Request->Segment.bIsLastInPeriod;

	Metrics::FSegmentDownloadStats& ds = CurrentRequest->DownloadStats;
	ds.StatsID = FMediaInterlockedIncrement(UniqueDownloadID);
	ds.FailureReason.Empty();
	ds.bWasSuccessful      = true;
	ds.bWasAborted  	   = false;
	ds.bDidTimeout  	   = false;
	ds.HTTPStatusCode      = 0;
	ds.StreamType   	   = Request->GetType();
	ds.SegmentType  	   = Metrics::ESegmentType::Media;
	ds.PresentationTime    = Request->GetFirstPTS().GetAsSeconds();
	ds.Bitrate  		   = Request->GetBitrate();
	ds.Duration 		   = SegmentDuration.GetAsSeconds();
	ds.DurationDownloaded  = 0.0;
	ds.DurationDelivered   = 0.0;
	ds.TimeToFirstByte     = 0.0;
	ds.TimeToDownload      = 0.0;
	ds.ByteSize 		   = -1;
	ds.NumBytesDownloaded  = 0;
	ds.ThroughputBps	   = 0;
	ds.bInsertedFillerData = false;

	ds.MediaAssetID 	= Request->Period.IsValid() ? Request->Period->GetUniqueIdentifier() : "";
	ds.AdaptationSetID  = Request->AdaptationSet.IsValid() ? Request->AdaptationSet->GetUniqueIdentifier() : "";
	ds.RepresentationID = Request->Representation.IsValid() ? Request->Representation->GetUniqueIdentifier() : "";
	ds.URL  			= Request->Segment.MediaURL.URL;
	ds.Range  			= Request->Segment.MediaURL.Range;
	ds.CDN  			= Request->Segment.MediaURL.CDN;
	ds.RetryNumber  	= Request->NumOverallRetries;
	ds.ABRState.Reset();

	// Clear out the list of events found the last time.
	SegmentEventsFound.Empty();
	CheckForInbandDASHEvents();
	/*
		Actually handle the request only if this is not an EOS segment.
		A segment that is already at EOS is not meant to be loaded as it does not exist and there
		would not be another segment following it either. They are meant to indicate to the player
		that a stream has ended and will not be delivering any more data.
		
		NOTE:
		  We had to handle this request up to detecting the use of inband event streams in order to
		  signal that this stream has now ended and will not be receiving any further inband events!
	*/
	if (Request->bIsEOSSegment)
	{
		CurrentRequest.Reset();
		return;
	}

	// Does this request have an availability window?
	if (Request->ASAST.IsValid())
	{
		FTimeValue Now = PlayerSessionService->GetSynchronizedUTCTime()->GetTime();
		FTimeValue When = Request->ASAST;
		if (Request->DownloadDelayTime.IsValid())
		{
			When += Request->DownloadDelayTime;
		}
		// Availability start time not reached yet?
		while(Now < When)
		{
			if (HasReadBeenAborted())
			{
				CurrentRequest.Reset();
				return;
			}
			const int64 SleepIntervalMS = 100;
			FMediaRunnable::SleepMilliseconds((uint32) Utils::Max(Utils::Min((When - Now).GetAsMilliseconds(), SleepIntervalMS), (int64)0));
			Now = PlayerSessionService->GetSynchronizedUTCTime()->GetTime();
		}

		// Already expired?
		if (Now > Request->SAET)
		{
			// TODO:
		}
	}

	// Clear internal work variables.
	bHasErrored 		   = false;
	bAbortedByABR   	   = false;
	bAllowEarlyEmitting    = false;
	bFillRemainingDuration = false;
	DurationSuccessfullyRead.SetToZero();
	DurationSuccessfullyDelivered.SetToZero();
	AccessUnitFIFO.Clear();

	FTimeValue NextExpectedDTS;
	FTimeValue LastKnownAUDuration;
	FTimeValue TimeOffset = Request->PeriodStart + Request->AST + Request->AdditionalAdjustmentTime + Request->PlayerLoopState.LoopBasetime;


	// Get the init segment if there is one. Either gets it from the entity cache or requests it now and adds it to the cache.
	InitSegmentError = GetInitSegment(MP4InitSegment, Request);
	// Get the CSD from the init segment in case there is a failure with the segment later.
	if (MP4InitSegment.IsValid())
	{
		if (MP4InitSegment->GetNumberOfTracks() == 1)
		{
			const IParserISO14496_12::ITrack* Track = MP4InitSegment->GetTrackByIndex(0);
			if (Track)
			{
				CSD->CodecSpecificData = Track->GetCodecSpecificData();
				CSD->RawCSD = Track->GetCodecSpecificDataRAW();
				CSD->ParsedInfo = Track->GetCodecInformation();
			}
		}
	}

	Parameters.EventListener->OnFragmentOpen(CurrentRequest);

	if (InitSegmentError.IsOK() && !HasReadBeenAborted())
	{
		if (!bIsEmptyFillerSegment)
		{
			ReadBuffer.Reset();
			ReadBuffer.ReceiveBuffer = MakeSharedTS<IElectraHttpManager::FReceiveBuffer>();

			// Start downloading the segment.
			TSharedPtrTS<IElectraHttpManager::FProgressListener>	ProgressListener(new IElectraHttpManager::FProgressListener);
			ProgressListener->ProgressDelegate   = Electra::MakeDelegate(this, &FStreamHandler::HTTPProgressCallback);
			ProgressListener->CompletionDelegate = Electra::MakeDelegate(this, &FStreamHandler::HTTPCompletionCallback);
			TSharedPtrTS<IElectraHttpManager::FRequest> HTTP(new IElectraHttpManager::FRequest);
			HTTP->Parameters.URL   = RequestURL;
			HTTP->ReceiveBuffer    = ReadBuffer.ReceiveBuffer;
			HTTP->ProgressListener = ProgressListener;
			HTTP->Parameters.Range.Set(Request->Segment.MediaURL.Range);
			HTTP->Parameters.AcceptEncoding.Set(TEXT("identity"));
			if (Request->Segment.MediaURL.CustomHeader.Len())
			{
				HTTP->Parameters.RequestHeaders.Emplace(HTTP::FHTTPHeader({DASH::HTTPHeaderOptionName, Request->Segment.MediaURL.CustomHeader}));
			}

			ProgressReportCount = 0;
			DownloadCompleteSignal.Reset();
			PlayerSessionService->GetHTTPManager()->AddRequest(HTTP, false);

			MP4Parser = IParserISO14496_12::CreateParser();
			NumMOOFBoxesFound = 0;

			int64 LastSuccessfulFilePos = 0;
			uint32 TrackTimescale = 0;
			bool bDone = false;
			bool bIsFirstAU = true;
			bool bReadPastLastPTS = false;

			// Encrypted?
			TSharedPtr<ElectraCDM::IMediaCDMDecrypter, ESPMode::ThreadSafe> Decrypter;
			if (Request->DrmClient.IsValid())
			{
				if (Request->DrmClient->CreateDecrypter(Decrypter, Request->DrmMimeType) != ElectraCDM::ECDMError::Success)
				{
					bDone = true;
					bHasErrored = true;
					ds.FailureReason = FString::Printf(TEXT("Failed to create decrypter for segment. %s"), *Request->DrmClient->GetLastErrorMessage());
					LogMessage(IInfoLog::ELevel::Error, ds.FailureReason);
				}
			}

			while(!bDone && !HasErrored() && !HasReadBeenAborted())
			{
				UEMediaError parseError = MP4Parser->ParseHeader(this, this, PlayerSessionService, MP4InitSegment.Get());
				if (parseError == UEMEDIA_ERROR_OK)
				{
					{
						SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_DASH_StreamReader);
						CSV_SCOPED_TIMING_STAT(ElectraPlayer, DASH_StreamReader);
						Request->Segment.bSawLMSG = MP4Parser->HasBrand(IParserISO14496_12::BrandType_lmsg);
						parseError = MP4Parser->PrepareTracks(MP4InitSegment);
					}
					if (parseError == UEMEDIA_ERROR_OK)
					{
						// For the time being we only want to have a single track in the movie segments.
						if (MP4Parser->GetNumberOfTracks() == 1)
						{
							const IParserISO14496_12::ITrack* Track = MP4Parser->GetTrackByIndex(0);
							check(Track);
							if (Track)
							{
								HandleEventMessages();

								CSD->CodecSpecificData = Track->GetCodecSpecificData();
								CSD->RawCSD = Track->GetCodecSpecificDataRAW();
								CSD->ParsedInfo = Track->GetCodecInformation();

								IParserISO14496_12::ITrackIterator* TrackIterator = Track->CreateIterator();
								TrackTimescale = TrackIterator->GetTimescale();

								int64 MediaLocalFirstAUTime = Request->Segment.MediaLocalFirstAUTime;
								int64 MediaLocalLastAUTime = Request->Segment.MediaLocalLastAUTime;

								if (TrackTimescale != Request->Segment.Timescale)
								{
									// Need to rescale the AU times from the MPD timescale to the media timescale.
									MediaLocalFirstAUTime = FTimeFraction(Request->Segment.MediaLocalFirstAUTime, Request->Segment.Timescale).GetAsTimebase(TrackTimescale);
									MediaLocalLastAUTime = FTimeFraction(Request->Segment.MediaLocalLastAUTime, Request->Segment.Timescale).GetAsTimebase(TrackTimescale);

									if (!Request->bWarnedAboutTimescale)
									{
										Request->bWarnedAboutTimescale = true;
										LogMessage(IInfoLog::ELevel::Warning, FString::Printf(TEXT("Track timescale %u differs from timescale of %u in MPD or segment index. This may cause playback problems!"), TrackTimescale, Request->Segment.Timescale));
									}
								}

								// If the track uses encryption we update the DRM system with the PSSH boxes that are currently in use.
								if (Decrypter.IsValid())
								{
									TArray<TArray<uint8>> PSSHBoxes;
									Track->GetPSSHBoxes(PSSHBoxes, true, true);
									Decrypter->UpdateInitDataFromMultiplePSSH(PSSHBoxes);
								}

								for(Error = TrackIterator->StartAtFirst(false); Error == UEMEDIA_ERROR_OK; Error = TrackIterator->Next())
								{
									// Get the DTS and PTS. Those are 0-based in a fragment and offset by the base media decode time of the fragment.
									int64 AUDTS = TrackIterator->GetDTS();
									int64 AUPTS = TrackIterator->GetPTS();
									int64 AUDuration = TrackIterator->GetDuration();

									// Create access unit
									FAccessUnit *AccessUnit = FAccessUnit::Create(Parameters.MemoryProvider);
									AccessUnit->ESType = Request->GetType();
									AccessUnit->AUSize = (uint32) TrackIterator->GetSampleSize();
									AccessUnit->AUData = AccessUnit->AllocatePayloadBuffer(AccessUnit->AUSize);
									AccessUnit->bIsFirstInSequence = bIsFirstAU;
									AccessUnit->bIsSyncSample = TrackIterator->IsSyncSample();
									AccessUnit->bIsDummyData = false;
									AccessUnit->AUCodecData = CSD;
									AccessUnit->BufferSourceInfo = Request->SourceBufferInfo;
									AccessUnit->PlayerLoopState = PlayerLoopState;

									AccessUnit->DropState = FAccessUnit::EDropState::None;
									// Is this AU entirely before the time we want?
									int64 Overlap = AUPTS + AUDuration - MediaLocalFirstAUTime;
									if (Overlap <= 0)
									{
										AccessUnit->DropState |= FAccessUnit::EDropState::PtsTooEarly;
										AccessUnit->DropState |= FAccessUnit::EDropState::DtsTooEarly;
									}
									// Partial overlap?
									else if (AUPTS < MediaLocalFirstAUTime)
									{
										// Use it, but remember the amount of overlap.
										AccessUnit->OverlapAdjust.SetFromND(Overlap, TrackTimescale);
									}
									// Entirely past the time allowed?
									if (bIsLastSegment)
									{
										if (AUPTS >= MediaLocalLastAUTime)
										{
											AccessUnit->DropState |= FAccessUnit::EDropState::PtsTooLate;
											AccessUnit->DropState |= FAccessUnit::EDropState::DtsTooLate;
										}
										else if (AUPTS + AUDuration >= MediaLocalLastAUTime)
										{
											// Note that we cannot stop reading here. For one we need to continue in case there are additional tracks here
											// that have not yet finished and we also need to make it all the way to the end in case there are additional
											// boxes that we have to read (like 'emsg' or 'lmsg').
											AccessUnit->OverlapAdjust.SetFromND(AUPTS + AUDuration - MediaLocalLastAUTime, TrackTimescale);
										}
									}
									FTimeValue Duration(TrackIterator->GetDuration(), TrackTimescale);
									AccessUnit->Duration = Duration;

									// Offset the AU's DTS and PTS to the time mapping of the segment.
									AccessUnit->DTS.SetFromND(AUDTS, TrackTimescale);
									AccessUnit->DTS += TimeOffset;
									AccessUnit->PTS.SetFromND(AUPTS, TrackTimescale);
									AccessUnit->PTS += TimeOffset;

									ElectraCDM::FMediaCDMSampleInfo SampleEncryptionInfo;
									bool bIsSampleEncrypted = TrackIterator->GetEncryptionInfo(SampleEncryptionInfo);

									// There should not be any gaps!
									int64 NumBytesToSkip = TrackIterator->GetSampleFileOffset() - GetCurrentOffset();
									if (NumBytesToSkip < 0)
									{
										// Current read position is already farther than where the data is supposed to be.
										FAccessUnit::Release(AccessUnit);
										AccessUnit = nullptr;
										ds.FailureReason = FString::Printf(TEXT("Read position already at %lld but data starts at %lld in segment \"%s\""), (long long int)GetCurrentOffset(), (long long int)TrackIterator->GetSampleFileOffset(), *RequestURL);
										LogMessage(IInfoLog::ELevel::Error, ds.FailureReason);
										bHasErrored = true;
										bDone = true;
										break;
									}
									else if (NumBytesToSkip > 0)
									{
										int64 NumSkipped = ReadData(nullptr, NumBytesToSkip);
										if (NumSkipped != NumBytesToSkip)
										{
											FAccessUnit::Release(AccessUnit);
											AccessUnit = nullptr;
											ds.FailureReason = FString::Printf(TEXT("Failed to skip over %lld bytes in segment \"%s\""), (long long int)NumBytesToSkip, *RequestURL);
											LogMessage(IInfoLog::ELevel::Error, ds.FailureReason);
											bHasErrored = true;
											bDone = true;
											break;
										}
									}
									int64 NumRead = ReadData(AccessUnit->AUData, AccessUnit->AUSize);
									if (NumRead == AccessUnit->AUSize)
									{
										DurationSuccessfullyRead += Duration;
										NextExpectedDTS = AccessUnit->DTS + Duration;
										LastKnownAUDuration = Duration;
										LastSuccessfulFilePos = GetCurrentOffset();

										// If we need to decrypt we have to wait for the decrypter to become ready.
										if (bIsSampleEncrypted && Decrypter.IsValid())
										{
											while(!bTerminate && !HasReadBeenAborted() && 
												  (Decrypter->GetState() == ElectraCDM::ECDMState::WaitingForKey || Decrypter->GetState() == ElectraCDM::ECDMState::Idle))
											{
												FMediaRunnable::SleepMilliseconds(100);
											}
											ElectraCDM::ECDMError DecryptResult = ElectraCDM::ECDMError::Failure;
											if (Decrypter->GetState() == ElectraCDM::ECDMState::Ready)
											{
												SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_DASH_StreamReader);
												CSV_SCOPED_TIMING_STAT(ElectraPlayer, DASH_StreamReader);
												DecryptResult = Decrypter->DecryptInPlace((uint8*) AccessUnit->AUData, (int32) AccessUnit->AUSize, SampleEncryptionInfo);
											}
											if (DecryptResult != ElectraCDM::ECDMError::Success)
											{
												FAccessUnit::Release(AccessUnit);
												AccessUnit = nullptr;
												ds.FailureReason = FString::Printf(TEXT("Failed to decrypt segment \"%s\" with error %d (%s)"), *RequestURL, (int32)DecryptResult, *Decrypter->GetLastErrorMessage());
												LogMessage(IInfoLog::ELevel::Error, ds.FailureReason);
												bHasErrored = true;
												bDone = true;
												break;
											}
										}
									}
									else
									{
										// Did not get the number of bytes we needed. Either because of a read error or because we got aborted.
										FAccessUnit::Release(AccessUnit);
										AccessUnit = nullptr;
										bDone = true;
										break;
									}

									// Check if the AU is outside the time range we are allowed to read.
									// The last one (the one that is already outside the range, actually) is tagged as such and sent into the buffer.
									// The respective decoder has to handle this flag if necessary and/or drop the AU.
									if (AccessUnit)
									{
										// Already sent the last one?
										if (bReadPastLastPTS)
										{
											// Yes. Release this AU and do not forward it. Continue reading however.
											FAccessUnit::Release(AccessUnit);
											AccessUnit = nullptr;
										}
										else if ((AccessUnit->DropState & (FAccessUnit::EDropState::PtsTooLate | FAccessUnit::EDropState::DtsTooLate)) == (FAccessUnit::EDropState::PtsTooLate | FAccessUnit::EDropState::DtsTooLate))
										{
											// Tag the last one and send it off, but stop doing so for the remainder of the segment.
											// Note: we continue reading this segment all the way to the end on purpose in case there are further 'emsg' boxes.
											AccessUnit->bIsLastInPeriod = true;
											bReadPastLastPTS = true;
										}
									}

									if (AccessUnit)
									{
										AccessUnitFIFO.Push(AccessUnit);
										// Clear the pointer since we must not touch this AU again after we handed it off.
										AccessUnit = nullptr;
									}

									// Shall we pass on any AUs we already read?
									if (bAllowEarlyEmitting)
									{
										SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_DASH_StreamReader);
										CSV_SCOPED_TIMING_STAT(ElectraPlayer, DASH_StreamReader);
										while(AccessUnitFIFO.Num() && !HasReadBeenAborted())
										{
											FAccessUnit* pNext = AccessUnitFIFO.FrontRef();
											if (Parameters.EventListener->OnFragmentAccessUnitReceived(pNext))
											{
												DurationSuccessfullyDelivered += pNext->Duration;
												AccessUnitFIFO.Pop();
											}
											else
											{
												break;
											}
										}
									}

									bIsFirstAU = false;
								}
								delete TrackIterator;

								if (Error != UEMEDIA_ERROR_OK && Error != UEMEDIA_ERROR_END_OF_STREAM)
								{
									// error iterating
									ds.FailureReason = FString::Printf(TEXT("Failed to iterate over segment \"%s\""), *RequestURL);
									LogMessage(IInfoLog::ELevel::Error, ds.FailureReason);
									bHasErrored = true;
								}

								// Check if we are done or if there is additional data that needs parsing, like more moof boxes.
								if (HasReadBeenAborted() || HasReachedEOF())
								{
									bDone = true;
								}
							}
							else
							{
								// can't really happen. would indicate an internal screw up
								ds.FailureReason = FString::Printf(TEXT("Segment \"%s\" has no track"), *RequestURL);
								LogMessage(IInfoLog::ELevel::Error, ds.FailureReason);
								bHasErrored = true;
							}
						}
						else
						{
							// more than 1 track
							ds.FailureReason = FString::Printf(TEXT("Segment \"%s\" has more than one track"), *RequestURL);
							LogMessage(IInfoLog::ELevel::Error, ds.FailureReason);
							bHasErrored = true;
						}
					}
					else
					{
						// error preparing track for iterating
						ds.FailureReason = FString::Printf(TEXT("Failed to prepare segment \"%s\" for iterating"), *RequestURL);
						LogMessage(IInfoLog::ELevel::Error, ds.FailureReason);
						bHasErrored = true;
					}
				}
				else if (parseError == UEMEDIA_ERROR_END_OF_STREAM)
				{
					bDone = true;
				}
				else
				{
					// failed to parse the segment (in general)
					if (!HasReadBeenAborted() && !HasErrored())
					{
						ds.FailureReason = FString::Printf(TEXT("Failed to parse segment \"%s\""), *RequestURL);
						LogMessage(IInfoLog::ELevel::Error, ds.FailureReason);
						bHasErrored = true;
					}
				}
			}
			ProgressListener.Reset();
			// Note: It is only safe to access the connection info when the HTTP request has completed or the request been removed.
			PlayerSessionService->GetHTTPManager()->RemoveRequest(HTTP, false);
			CurrentRequest->ConnectionInfo = HTTP->ConnectionInfo;
			HTTP.Reset();
		}
	}
	else
	{
		// Init segment error.
		if (!HasReadBeenAborted())
		{
			CurrentRequest->ConnectionInfo.StatusInfo.ErrorDetail = InitSegmentError;
			bHasErrored = true;
		}
	}

	// Do we need to fill remaining duration with dummy data?
	if (bIsEmptyFillerSegment || bFillRemainingDuration)
	{
		// Get the supposed segment duration.
		FTimeValue SegmentDurationToGo = SegmentDuration;
		FTimeValue DiscardBefore = TimeOffset + FTimeValue(Request->Segment.MediaLocalFirstAUTime, Request->Segment.Timescale);
		FTimeValue DiscardAfter = TimeOffset + FTimeValue(Request->Segment.MediaLocalLastAUTime, Request->Segment.Timescale);

		// Did we get anything so far?
		FTimeValue DefaultDuration;
		if (NextExpectedDTS.IsValid())
		{
			check(DurationSuccessfullyRead.IsValid());
			check(LastKnownAUDuration.IsValid());
			SegmentDurationToGo -= DurationSuccessfullyRead;
			DefaultDuration = LastKnownAUDuration;
		}
		else
		{
			// No. We need to start with the segment time.
			NextExpectedDTS = Request->GetFirstPTS();
			switch(Request->GetType())
			{
				case EStreamType::Video:
				{
					DefaultDuration.SetFromND(1, 60);
					break;
				}
				case EStreamType::Audio:
				{
					int64 n = 1024;
					uint32 d = 48000;
					if (CSD.IsValid() && CSD->ParsedInfo.GetSamplingRate())
					{
						d = (uint32) CSD->ParsedInfo.GetSamplingRate();
						switch(CSD->ParsedInfo.GetCodec())
						{
							case FStreamCodecInformation::ECodec::AAC:
							{
								n = CSD->ParsedInfo.GetExtras().GetValue("samples_per_block").SafeGetInt64(1024);
								break;
							}
						}
					}
					DefaultDuration.SetFromND(n, d);
					break;
				}
				default:
				{
					DefaultDuration.SetFromND(1, 10);
					break;
				}
			}
		}

		ds.bInsertedFillerData = SegmentDurationToGo > FTimeValue::GetZero();
		while(SegmentDurationToGo > FTimeValue::GetZero())
		{
			FAccessUnit *AccessUnit = FAccessUnit::Create(Parameters.MemoryProvider);
			if (!AccessUnit)
			{
				break;
			}
			if (DefaultDuration > SegmentDurationToGo)
			{
				DefaultDuration = SegmentDurationToGo;
			}

			AccessUnit->ESType = Request->GetType();
			AccessUnit->BufferSourceInfo = Request->SourceBufferInfo;
			AccessUnit->PlayerLoopState = PlayerLoopState;
			AccessUnit->Duration = DefaultDuration;
			AccessUnit->AUSize = 0;
			AccessUnit->AUData = nullptr;
			AccessUnit->bIsDummyData = true;
			if (CSD.IsValid() && CSD->CodecSpecificData.Num())
			{
				AccessUnit->AUCodecData = CSD;
			}

			// Calculate the drop on the fragment local NextExpectedDTS/PTS.
			AccessUnit->DropState = FAccessUnit::EDropState::None;
			if (NextExpectedDTS < DiscardBefore)
			{
				AccessUnit->DropState |= FAccessUnit::EDropState::DtsTooEarly;
				AccessUnit->DropState |= FAccessUnit::EDropState::PtsTooEarly;
			}
			else if (NextExpectedDTS + DefaultDuration > DiscardAfter)
			{
				AccessUnit->DropState |= FAccessUnit::EDropState::DtsTooEarly;
				AccessUnit->DropState |= FAccessUnit::EDropState::PtsTooEarly;
			}
			AccessUnit->DTS = NextExpectedDTS;
			AccessUnit->PTS = NextExpectedDTS;

			NextExpectedDTS += DefaultDuration;
			SegmentDurationToGo -= DefaultDuration;
			AccessUnitFIFO.Push(AccessUnit);
		}
	}

	// Pass the AUs in our FIFO to the player.
	bool bFillWithDummyData = bIsEmptyFillerSegment || bFillRemainingDuration;
	while(AccessUnitFIFO.Num() && !bTerminate && (!HasReadBeenAborted() || bFillWithDummyData))
	{
		FAccessUnit* pNext = AccessUnitFIFO.FrontRef();
		while(!bTerminate && (!HasReadBeenAborted() || bFillWithDummyData))
		{
			if (Parameters.EventListener->OnFragmentAccessUnitReceived(pNext))
			{
				DurationSuccessfullyDelivered += pNext->Duration;
				AccessUnitFIFO.Pop();
				break;
			}
			else
			{
				FMediaRunnable::SleepMilliseconds(100);
			}
		}
	}
	// Anything not handed over after an abort we delete
	while(AccessUnitFIFO.Num())
	{
		FAccessUnit::Release(AccessUnitFIFO.Pop());
	}

	// If we did not set a specific failure message yet set the one from the downloader.
	if (ds.FailureReason.Len() == 0)
	{
		ds.FailureReason = CurrentRequest->ConnectionInfo.StatusInfo.ErrorDetail.GetMessage();
	}
	// If the ABR aborted this takes precedence in the failure message. Overwrite it.
	if (bAbortedByABR)
	{
		ds.FailureReason = ds.ABRState.ProgressDecision.Reason;
	}
	// Set up remaining download stat fields.
	ds.bWasAborted  	  = bAbortedByABR;
	ds.bWasSuccessful     = !bHasErrored && !bAbortedByABR;
	ds.URL  			  = CurrentRequest->ConnectionInfo.EffectiveURL;
	ds.HTTPStatusCode     = CurrentRequest->ConnectionInfo.StatusInfo.HTTPStatus;
	ds.DurationDownloaded = DurationSuccessfullyRead.GetAsSeconds();
	ds.DurationDelivered  = DurationSuccessfullyDelivered.GetAsSeconds();
	ds.TimeToFirstByte    = CurrentRequest->ConnectionInfo.TimeUntilFirstByte;
	ds.TimeToDownload     = (CurrentRequest->ConnectionInfo.RequestEndTime - CurrentRequest->ConnectionInfo.RequestStartTime).GetAsSeconds();
	ds.ByteSize 		  = CurrentRequest->ConnectionInfo.ContentLength;
	ds.NumBytesDownloaded = CurrentRequest->ConnectionInfo.BytesReadSoFar;
	ds.ThroughputBps	  = CurrentRequest->ConnectionInfo.Throughput.GetThroughput();
	if (ds.ThroughputBps == 0)
	{
		ds.ThroughputBps = ds.TimeToDownload > 0.0 ? 8 * ds.NumBytesDownloaded / ds.TimeToDownload : 0;
	}

	// Was this request for a segment that might potentially be missing and it did?
	if (Request->Segment.bMayBeMissing && (ds.HTTPStatusCode == 404 || ds.HTTPStatusCode == 416))
	{
		// This is not an actual error then. Pretend all was well.
		ds.bWasSuccessful = true;
		ds.HTTPStatusCode = 200;
		ds.bIsMissingSegment = true;
		ds.FailureReason.Empty();
		CurrentRequest->DownloadStats.bWasSuccessful = true;
		CurrentRequest->DownloadStats.HTTPStatusCode = 200;
		CurrentRequest->DownloadStats.bIsMissingSegment = true;
		CurrentRequest->DownloadStats.FailureReason.Empty();
		CurrentRequest->ConnectionInfo.StatusInfo.Empty();
		// Take note of the missing segment in the segment info as well so the search for the next segment
		// can return quicker.
		CurrentRequest->Segment.bIsMissing = true;
	}

	// If we had to wait for the segment to become available and we got a 404 back we might have been trying to fetch
	// the segment before the server made it available.
	if (Request->ASAST.IsValid() && (ds.HTTPStatusCode == 404 || ds.HTTPStatusCode == 416))
	{
		FTimeValue Now = PlayerSessionService->GetSynchronizedUTCTime()->GetTime();
		if ((ds.AvailibilityDelay = (Request->ASAST - Now).GetAsSeconds()) == 0.0)
		{
			// In the extremely unlikely event this comes out to zero exactly set a small value so the ABR knows there was a delay.
			ds.AvailibilityDelay = -0.01;
		}
	}

	// If we failed to get the segment and there is an inband DASH event stream which triggers MPD events and
	// we did not get such an event in the 'emsg' boxes, then we err on the safe side and assume this segment
	// would have carried an MPD update event and fire an artificial event.
	if (!ds.bWasSuccessful && CurrentRequest->Segment.InbandEventStreams.FindByPredicate([](const FManifestDASHInternal::FSegmentInformation::FInbandEventStream& This){ return This.SchemeIdUri.Equals(DASH::Schemes::ManifestEvents::Scheme_urn_mpeg_dash_event_2012); }))
	{
		if (SegmentEventsFound.IndexOfByPredicate([](const TSharedPtrTS<DASH::FPlayerEvent>& This){return This->GetSchemeIdUri().Equals(DASH::Schemes::ManifestEvents::Scheme_urn_mpeg_dash_event_2012);}) == INDEX_NONE)
		{
			SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_DASH_StreamReader);
			CSV_SCOPED_TIMING_STAT(ElectraPlayer, DASH_StreamReader);
			TSharedPtrTS<DASH::FPlayerEvent> NewEvent = MakeSharedTS<DASH::FPlayerEvent>();
			NewEvent->SetOrigin(IAdaptiveStreamingPlayerAEMSEvent::EOrigin::InbandEventStream);
			NewEvent->SetSchemeIdUri(DASH::Schemes::ManifestEvents::Scheme_urn_mpeg_dash_event_2012);
			NewEvent->SetValue(TEXT("1"));
			NewEvent->SetID("$missed$");
			FTimeValue EPT((int64)CurrentRequest->Segment.Time, CurrentRequest->Segment.Timescale);
			FTimeValue PTO(CurrentRequest->Segment.PTO, CurrentRequest->Segment.Timescale);
			NewEvent->SetPresentationTime(TimeOffset - PTO + EPT);
			NewEvent->SetPeriodID(CurrentRequest->Period->GetUniqueIdentifier());
			PlayerSessionService->GetAEMSEventHandler()->AddEvent(NewEvent, CurrentRequest->Period->GetUniqueIdentifier(), IAdaptiveStreamingPlayerAEMSHandler::EEventAddMode::AddIfNotExists);
		}
	}

	if (!bSilentCancellation)
	{
		StreamSelector->ReportDownloadEnd(ds);
	}

	// Remember the next expected timestamp.
	CurrentRequest->NextLargestExpectedTimestamp = NextExpectedDTS;

	// Clean out everything before reporting OnFragmentClose().
	TSharedPtrTS<FStreamSegmentRequestFMP4DASH> FinishedRequest = CurrentRequest;
	CurrentRequest.Reset();
	ReadBuffer.Reset();
	MP4Parser.Reset();
	SegmentEventsFound.Empty();

	if (!bSilentCancellation)
	{
		Parameters.EventListener->OnFragmentClose(FinishedRequest);
	}
}


bool FStreamReaderFMP4DASH::FStreamHandler::HasErrored() const
{
	return bHasErrored;
}




/**
 * Read n bytes of data into the provided buffer.
 *
 * Reading must return the number of bytes asked to get, if necessary by blocking.
 * If a read error prevents reading the number of bytes -1 must be returned.
 *
 * @param IntoBuffer Buffer into which to store the data bytes. If nullptr is passed the data must be skipped over.
 * @param NumBytesToRead The number of bytes to read. Must not read more bytes and no less than requested.
 * @return The number of bytes read or -1 on a read error.
 */
int64 FStreamReaderFMP4DASH::FStreamHandler::ReadData(void* IntoBuffer, int64 NumBytesToRead)
{
	FPODRingbuffer& SourceBuffer = ReadBuffer.ReceiveBuffer->Buffer;
	// Make sure the buffer will have the amount of data we need.
	while(1)
	{
		// Check if a HTTP reader progress event fired in the meantime.
		if (ProgressReportCount)
		{
			ProgressReportCount = 0;
			if (CurrentRequest.IsValid())
			{
				MetricUpdateLock.Lock();
				Metrics::FSegmentDownloadStats currentDownloadStats = CurrentRequest->DownloadStats;
				currentDownloadStats.DurationDelivered = DurationSuccessfullyDelivered.GetAsSeconds();
				currentDownloadStats.DurationDownloaded = DurationSuccessfullyRead.GetAsSeconds();
				MetricUpdateLock.Unlock();

				Metrics::FSegmentDownloadStats& ds = CurrentRequest->DownloadStats;
				FABRDownloadProgressDecision StreamSelectorDecision = StreamSelector->ReportDownloadProgress(currentDownloadStats);
				ds.ABRState.ProgressDecision = StreamSelectorDecision;
				if ((StreamSelectorDecision.Flags & FABRDownloadProgressDecision::EDecisionFlags::eABR_EmitPartialData) != 0)
				{
					SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_DASH_StreamReader);
					CSV_SCOPED_TIMING_STAT(ElectraPlayer, DASH_StreamReader);
					bAllowEarlyEmitting = true;
					// Deliver all enqueued AUs right now. Unless the request also gets aborted we could be stuck
					// in here for a while longer.
					while(AccessUnitFIFO.Num())
					{
						FAccessUnit* pNext = AccessUnitFIFO.FrontRef();
						if (Parameters.EventListener->OnFragmentAccessUnitReceived(pNext))
						{
							DurationSuccessfullyDelivered += pNext->Duration;
							AccessUnitFIFO.Pop();
						}
						else
						{
							break;
						}
					}
				}
				if ((StreamSelectorDecision.Flags & FABRDownloadProgressDecision::EDecisionFlags::eABR_InsertFillerData) != 0)
				{
					bFillRemainingDuration = true;
				}
				if ((StreamSelectorDecision.Flags & FABRDownloadProgressDecision::EDecisionFlags::eABR_AbortDownload) != 0)
				{
					// When aborted and early emitting did place something into the buffers we need to fill
					// the remainder no matter what.
					if (DurationSuccessfullyDelivered > FTimeValue::GetZero())
					{
						bFillRemainingDuration = true;
					}
					bAbortedByABR = true;
					return -1;
				}
			}
		}

		if (!SourceBuffer.WaitUntilSizeAvailable(ReadBuffer.ParsePos + NumBytesToRead, 1000 * 100))
		{
			if (HasErrored() || HasReadBeenAborted() || SourceBuffer.WasAborted())
			{
				return -1;
			}
		}
		else
		{
			SCOPE_CYCLE_COUNTER(STAT_ElectraPlayer_DASH_StreamReader);
			CSV_SCOPED_TIMING_STAT(ElectraPlayer, DASH_StreamReader);
			SourceBuffer.Lock();
			if (SourceBuffer.Num() >= ReadBuffer.ParsePos + NumBytesToRead)
			{
				if (IntoBuffer)
				{
					FMemory::Memcpy(IntoBuffer, SourceBuffer.GetLinearReadData() + ReadBuffer.ParsePos, NumBytesToRead);
				}
				SourceBuffer.Unlock();
				ReadBuffer.ParsePos += NumBytesToRead;
				return NumBytesToRead;
			}
			else
			{
				// Return 0 at EOF and -1 on error.
				SourceBuffer.Unlock();
				return HasErrored() ? -1 : 0;
			}
		}
	}
	return -1;
}

/**
 * Checks if the data source has reached the End Of File (EOF) and cannot provide any additional data.
 *
 * @return If EOF has been reached returns true, otherwise false.
 */
bool FStreamReaderFMP4DASH::FStreamHandler::HasReachedEOF() const
{
	const FPODRingbuffer& SourceBuffer = ReadBuffer.ReceiveBuffer->Buffer;
	return !HasErrored() && SourceBuffer.GetEOD() && (ReadBuffer.ParsePos >= SourceBuffer.Num() || ReadBuffer.ParsePos >= ReadBuffer.MaxParsePos);
}

/**
 * Checks if reading of the file and therefor parsing has been aborted.
 *
 * @return true if reading/parsing has been aborted, false otherwise.
 */
bool FStreamReaderFMP4DASH::FStreamHandler::HasReadBeenAborted() const
{
	return bTerminate || bRequestCanceled || bAbortedByABR;
}

/**
 * Returns the current read offset.
 *
 * The first read offset is not necessarily zero. It could be anywhere inside the source.
 *
 * @return The current byte offset in the source.
 */
int64 FStreamReaderFMP4DASH::FStreamHandler::GetCurrentOffset() const
{
	return ReadBuffer.ParsePos;
}


IParserISO14496_12::IBoxCallback::EParseContinuation FStreamReaderFMP4DASH::FStreamHandler::OnFoundBox(IParserISO14496_12::FBoxType Box, int64 BoxSizeInBytes, int64 FileDataOffset, int64 BoxDataOffset)
{
	// Check which box is being parsed next.
	switch(Box)
	{
		case IParserISO14496_12::BoxType_moov:
		case IParserISO14496_12::BoxType_sidx:
		{
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Continue;
		}
		case IParserISO14496_12::BoxType_moof:
		{
			++NumMOOFBoxesFound;
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Continue;
		}
		case IParserISO14496_12::BoxType_mdat:
		{
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Stop;
		}
		default:
		{
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Continue;
		}
	}
}

IParserISO14496_12::IBoxCallback::EParseContinuation FStreamReaderFMP4DASH::FStreamHandler::OnEndOfBox(IParserISO14496_12::FBoxType Box, int64 BoxSizeInBytes, int64 FileDataOffset, int64 BoxDataOffset)
{
	return IParserISO14496_12::IBoxCallback::EParseContinuation::Continue;
}



} // namespace Electra

