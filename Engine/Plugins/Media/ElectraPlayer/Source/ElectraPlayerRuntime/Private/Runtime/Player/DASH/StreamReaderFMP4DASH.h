// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Player/PlayerStreamReader.h"
#include "Player/AdaptiveStreamingPlayerResourceRequest.h"
#include "Player/DASH/PlaylistReaderDASH_Internal.h"
#include "Player/DASH/PlayerEventDASH.h"
#include "Demuxer/ParserISO14496-12.h"
#include "HTTP/HTTPManager.h"
#include "Player/DRM/DRMManager.h"

namespace Electra
{

class FStreamSegmentRequestFMP4DASH : public IStreamSegment
{
public:
	FStreamSegmentRequestFMP4DASH();
	virtual ~FStreamSegmentRequestFMP4DASH();

	virtual void SetPlaybackSequenceID(uint32 PlaybackSequenceID) override;
	virtual uint32 GetPlaybackSequenceID() const override;

	virtual void SetExecutionDelay(const FTimeValue& ExecutionDelay) override;

	virtual EStreamType GetType() const override;

	virtual void GetDependentStreams(TArray<FDependentStreams>& OutDependentStreams) const override;

	virtual void GetEndedStreams(TArray<TSharedPtrTS<IStreamSegment>>& OutAlreadyEndedStreams) override;

	//! Returns the first PTS value as indicated by the media timeline. This should correspond to the actual absolute PTS of the sample.
	virtual FTimeValue GetFirstPTS() const override;

	virtual int32 GetQualityIndex() const override;
	virtual int32 GetBitrate() const override;

	virtual void GetDownloadStats(Metrics::FSegmentDownloadStats& OutStats) const override;

	EStreamType												StreamType = EStreamType::Unsupported;				//!< Type of stream (video, audio, etc.)
	TSharedPtrTS<IPlaybackAssetRepresentation>				Representation;										//!< The representation this request belongs to.
	TSharedPtrTS<IPlaybackAssetAdaptationSet>				AdaptationSet;										//!< The adaptation set the representation belongs to.
	TSharedPtrTS<ITimelineMediaAsset>						Period;												//!< The period the adaptation set belongs to.
	FManifestDASHInternal::FSegmentInformation				Segment;											//!< Segment information (URLs and timing values)
	TArray<TSharedPtrTS<FStreamSegmentRequestFMP4DASH>>		DependentStreams;									//!< Streams this segment depends on. Currently only used to hold the set of requests for the initial playback start.
	bool													bIsEOSSegment = false;								//!< true if this is not an actual request but a stream-has-already-ended request.
	bool													bIsInitialStartRequest = false;						//!< true if this is the initial playback start request.
	FTimeValue												PeriodStart;										//!< Value to add to all DTS & PTS to map them into the Period timeline
	FTimeValue												AST = FTimeValue::GetZero();						//!< Value of AST to add to all time to generate wallclock time
	FTimeValue												AdditionalAdjustmentTime = FTimeValue::GetZero();	//!< Sum of any other time corrections
	bool													bInsertFillerData = false;							//!< true to insert empty access units into the buffer instead of reading actual data.

	// UTC wallclock times during which this segment can be fetched;
	FTimeValue												ASAST;
	FTimeValue												SAET;
	FTimeValue												DownloadDelayTime;

	// Encryption
	TSharedPtrTS<ElectraCDM::IMediaCDMClient>				DrmClient;
	FString													DrmMimeType;


	// Internal work variables
	TSharedPtrTS<FBufferSourceInfo>							SourceBufferInfo;
	FPlayerLoopState										PlayerLoopState;
	int32													NumOverallRetries = 0;								//!< Number of retries for this _segment_ across all possible quality levels and CDNs.
	uint32													CurrentPlaybackSequenceID = ~0U;					//!< Set by the player before adding the request to the stream reader.

	Metrics::FSegmentDownloadStats							DownloadStats;
	HTTP::FConnectionInfo									ConnectionInfo;
	FTimeValue												NextLargestExpectedTimestamp;						//!< Largest timestamp of all samples (plus its duration) across all tracks.

	bool													bWarnedAboutTimescale = false;
};



/**
 *
**/
class FStreamReaderFMP4DASH : public IStreamReader
{
public:
	FStreamReaderFMP4DASH();
	virtual ~FStreamReaderFMP4DASH();

	virtual UEMediaError Create(IPlayerSessionServices* PlayerSessionService, const CreateParam& InCreateParam) override;
	virtual void Close() override;

	//! Adds a request to read from a stream
	virtual EAddResult AddRequest(uint32 CurrentPlaybackSequenceID, TSharedPtrTS<IStreamSegment> Request) override;

	//! Cancels any ongoing requests of the given stream type. Silent cancellation will not notify OnFragmentClose() or OnFragmentReachedEOS(). 
	virtual void CancelRequest(EStreamType StreamType, bool bSilent) override;

	//! Cancels all pending requests.
	virtual void CancelRequests() override;

private:
	ELECTRA_IMPL_DEFAULT_ERROR_METHODS(DASHFMP4Reader);

	struct FStreamHandler : public FMediaThread, public IParserISO14496_12::IReader, public IParserISO14496_12::IBoxCallback
	{
		struct FReadBuffer
		{
			FReadBuffer()
			{
				Reset();
			}
			void Reset()
			{
				ReceiveBuffer.Reset();
				ParsePos = 0;
				MaxParsePos = TNumericLimits<int64>::Max();
			}
			TSharedPtrTS<IElectraHttpManager::FReceiveBuffer>	ReceiveBuffer;
			int64												ParsePos;
			int64												MaxParsePos;
		};

		static uint32											UniqueDownloadID;

		IStreamReader::CreateParam								Parameters;
		TSharedPtrTS<FStreamSegmentRequestFMP4DASH>				CurrentRequest;
		FMediaSemaphore											WorkSignal;
		FMediaEvent												IsIdleSignal;
		volatile bool											bTerminate = false;
		volatile bool											bRequestCanceled = false;
		volatile bool											bSilentCancellation = false;
		volatile bool											bHasErrored = false;
		bool													bAbortedByABR = false;
		bool													bAllowEarlyEmitting = false;
		bool													bFillRemainingDuration = false;

		IPlayerSessionServices*									PlayerSessionService = nullptr;
		FReadBuffer												ReadBuffer;
		FMediaEvent												DownloadCompleteSignal;
		TSharedPtrTS<IParserISO14496_12>						MP4Parser;
		int32													NumMOOFBoxesFound = 0;

		TMediaQueueDynamicNoLock<FAccessUnit *>					AccessUnitFIFO;
		FTimeValue 												DurationSuccessfullyRead;
		FTimeValue 												DurationSuccessfullyDelivered;

		TArray<TSharedPtrTS<DASH::FPlayerEvent>>				SegmentEventsFound;

		FMediaCriticalSection									MetricUpdateLock;
		int32													ProgressReportCount = 0;
		TSharedPtrTS<IAdaptiveStreamSelector>					StreamSelector;


		FStreamHandler();
		virtual ~FStreamHandler();
		void Cancel(bool bSilent);
		void SignalWork();
		void WorkerThread();
		void HandleRequest();

		FErrorDetail GetInitSegment(TSharedPtrTS<const IParserISO14496_12>& OutMP4InitSegment, const TSharedPtrTS<FStreamSegmentRequestFMP4DASH>& InRequest);
		void CheckForInbandDASHEvents();
		void HandleEventMessages();

		void LogMessage(IInfoLog::ELevel Level, const FString& Message);

		int32 HTTPProgressCallback(const IElectraHttpManager::FRequest* Request);
		void HTTPCompletionCallback(const IElectraHttpManager::FRequest* Request);
		void HTTPUpdateStats(const FTimeValue& CurrentTime, const IElectraHttpManager::FRequest* Request);

		bool HasErrored() const;

		// Methods from IParserISO14496_12::IReader
		virtual int64 ReadData(void* IntoBuffer, int64 NumBytesToRead) override;
		virtual bool HasReachedEOF() const override;
		virtual bool HasReadBeenAborted() const override;
		virtual int64 GetCurrentOffset() const override;
		// Methods from IParserISO14496_12::IBoxCallback
		virtual IParserISO14496_12::IBoxCallback::EParseContinuation OnFoundBox(IParserISO14496_12::FBoxType Box, int64 BoxSizeInBytes, int64 FileDataOffset, int64 BoxDataOffset) override;
		virtual IParserISO14496_12::IBoxCallback::EParseContinuation OnEndOfBox(IParserISO14496_12::FBoxType Box, int64 BoxSizeInBytes, int64 FileDataOffset, int64 BoxDataOffset) override;
	};

	// Currently set to use 2 handlers, one for video and one for audio. This could become a pool of n if we need to stream
	// multiple dependent segments, keeping a pool of available and active handlers to cycle between.
	FStreamHandler						StreamHandlers[2];		// 0 = video (MEDIAstreamType_Video), 1 = audio (MEDIAstreamType_Audio)
	IPlayerSessionServices*				PlayerSessionService = nullptr;
	bool								bIsStarted = false;
	FErrorDetail						ErrorDetail;
};


} // namespace Electra

