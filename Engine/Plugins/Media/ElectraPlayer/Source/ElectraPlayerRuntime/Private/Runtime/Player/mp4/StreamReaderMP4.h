// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PlayerCore.h"
#include "Player/Manifest.h"
#include "HTTP/HTTPManager.h"
#include "Demuxer/ParserISO14496-12.h"
#include "SynchronizedClock.h"
#include "PlaylistReaderMP4.h"
#include "Utilities/HashFunctions.h"
#include "Utilities/TimeUtilities.h"
#include "HAL/LowLevelMemTracker.h"
#include "ElectraPlayerPrivate.h"
#include "Player/PlayerStreamReader.h"
#include "StreamAccessUnitBuffer.h"




namespace Electra
{


class FStreamSegmentRequestMP4 : public IStreamSegment
{
public:
	FStreamSegmentRequestMP4();
	virtual ~FStreamSegmentRequestMP4();

	virtual void SetPlaybackSequenceID(uint32 InPlaybackSequenceID) override;
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



	TSharedPtrTS<ITimelineMediaAsset>					MediaAsset;					//!< The entire mp4 asset
	TSharedPtrTS<IParserISO14496_12::ITrackIterator>	PrimaryTrackIterator;
	TArray<FDependentStreams>							DependentStreams;

	FTimeValue											FirstPTS;					//!< The PTS of the first sample
	FTimeValue											SegmentDuration;
	EStreamType											PrimaryStreamType;
	int64												FileStartOffset;			//!< Where to start in the file
	int64												FileEndOffset;				//!< Where to end in the file (for HTTP range GET requests)
	int64												SegmentInternalSize;		//!< Size of the segment as defined by internal structures.
	uint32												PlaybackSequenceID;
	int32												Bitrate;
	bool												bStartingOnMOOF;			//!< when using the 'sidx' a segment is expected to start on a 'moof', otherwise inside an 'mdat'.
	bool												bIsContinuationSegment;		//!< true if this segment continues where the previous left off and no sync samples should be expected.
	bool												bIsFirstSegment;			//!< true if this segment is the first to start with or the first after a seek.
	bool												bIsLastSegment;				//!< true if this segment is the last.

	bool												bAllTracksAtEOS;

	FPlayerLoopState									PlayerLoopState;
	int32												NumOverallRetries;			//!< Number of retries

	Metrics::FSegmentDownloadStats						DownloadStats;
	HTTP::FConnectionInfo								ConnectionInfo;
	FTimeValue											NextLargestExpectedTimestamp;	//!< Largest timestamp of all samples (plus its duration) across all tracks.
	int64												CurrentIteratorBytePos;
};


/**
 * This class implements an interface to read from an mp4 file.
 */
class FStreamReaderMP4 : public IStreamReader, public FMediaThread
{
public:
	FStreamReaderMP4();
	virtual ~FStreamReaderMP4();
	virtual UEMediaError Create(IPlayerSessionServices* PlayerSessionService, const CreateParam &createParam) override;
	virtual void Close() override;
	virtual EAddResult AddRequest(uint32 CurrentPlaybackSequenceID, TSharedPtrTS<IStreamSegment> Request) override;
	virtual void CancelRequest(EStreamType StreamType, bool bSilent) override;
	virtual void CancelRequests() override;

private:
	void WorkerThread();
	void LogMessage(IInfoLog::ELevel Level, const FString& Message);
	int32 HTTPProgressCallback(const IElectraHttpManager::FRequest* InRequest);
	void HTTPCompletionCallback(const IElectraHttpManager::FRequest* InRequest);
	void HTTPUpdateStats(const FTimeValue& CurrentTime, const IElectraHttpManager::FRequest* Request);
	void HandleRequest();

	bool HasBeenAborted() const;
	bool HasErrored() const;

	struct FReadBuffer
	{
		FReadBuffer()
		{
			Reset();
		}
		void Reset()
		{
			ReceiveBuffer.Reset();
			CurrentPos = 0;
			bAbort = false;
			bHasErrored = false;
		}
		void SetCurrentPos(int64 InPos)
		{
			CurrentPos = InPos;
		}
		int64 GetCurrentPos()
		{
			return CurrentPos;
		}
		void Abort()
		{
			bAbort = true;
			TSharedPtrTS<IElectraHttpManager::FReceiveBuffer>	Buffer = ReceiveBuffer;
			if (Buffer.IsValid())
			{
				Buffer->Buffer.Abort();
			}
		}
		void SetHasErrored()
		{
			bHasErrored = true;
		}
		int32 ReadTo(void* ToBuffer, int32 NumBytes);
		TSharedPtrTS<IElectraHttpManager::FReceiveBuffer>	ReceiveBuffer;
		int64												CurrentPos;
		bool												bAbort;
		bool												bHasErrored;
	};

	struct FSelectedTrackData
	{
		FSelectedTrackData()
			: StreamType(EStreamType::Video), bGotKeyframe(false), bIsSelectedTrack(false), bIsFirstInSequence(true)
		{
			DurationSuccessfullyRead.SetToZero();
			DurationSuccessfullyDelivered.SetToZero();
		}
		TSharedPtrTS<FAccessUnit::CodecData>	CSD;
		TSharedPtrTS<FBufferSourceInfo>			BufferSourceInfo;
		EStreamType								StreamType;
		bool									bGotKeyframe;
		bool									bIsSelectedTrack;
		bool									bIsFirstInSequence;
		FTimeValue								DurationSuccessfullyRead;
		FTimeValue								DurationSuccessfullyDelivered;
	};

	CreateParam									Parameters;
	IPlayerSessionServices*						PlayerSessionServices;
	bool										bIsStarted;
	bool										bTerminate;
	bool										bRequestCanceled;
	bool										bHasErrored;
	FErrorDetail								ErrorDetail;

	TSharedPtrTS<FStreamSegmentRequestMP4>		CurrentRequest;
	FMediaEvent									WorkSignal;
	FReadBuffer									ReadBuffer;
	FMediaCriticalSection						MetricUpdateLock;

	TMap<uint32, FSelectedTrackData>			ActiveTrackMap;

	static uint32								UniqueDownloadID;
};



} // namespace Electra


