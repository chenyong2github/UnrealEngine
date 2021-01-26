// Copyright Epic Games, Inc. All Rights Reserved.

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
#include "Player/AdaptiveStreamingPlayerResourceRequest.h"
#include "Player/PlayerStreamReader.h"
#include "Player/mp4/ManifestMP4.h"


#define ERRCODE_MP4_INVALID_FILE							1



namespace Electra
{

/**
 * This class is responsible for downloading the mp4 non-mdat boxes and parsing them.
 */
class FPlaylistReaderMP4 : public IPlaylistReaderMP4, public IParserISO14496_12::IReader, public IParserISO14496_12::IBoxCallback, public FMediaThread
{
public:
	FPlaylistReaderMP4();
	void Initialize(IPlayerSessionServices* PlayerSessionServices);

	virtual ~FPlaylistReaderMP4();

	/**
	 * Returns the type of playlist format.
	 * For this implementation it will be "mp4".
	 *
	 * @return "mp4" to indicate this is an mp4 file.
	 */
	virtual const FString& GetPlaylistType() const override
	{
		static FString Type("mp4");
		return Type;
	}

	/**
	 * Loads and parses the playlist.
	 *
	 * @param URL     URL of the playlist to load
	 * @param Preferences
	 *                User preferences for initial stream selection.
	 * @param Options Options for the reader and parser
	 */
	virtual void LoadAndParse(const FString& URL, const FStreamPreferences& Preferences, const FParamDict& Options) override;

	/**
	 * Returns the URL from which the playlist was loaded (or supposed to be loaded).
	 *
	 * @return The playlist URL
	 */
	virtual FString GetURL() const override;

	/**
	 * Returns an interface to the manifest created from the loaded mp4 playlists.
	 *
	 * @return A shared manifest interface pointer.
	 */
	virtual TSharedPtrTS<IManifest> GetManifest() override;

private:
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
			bHasBeenAborted = false;
		}
		void Abort()
		{
			TSharedPtrTS<IElectraHttpManager::FReceiveBuffer>	CurrentBuffer = ReceiveBuffer;
			if (CurrentBuffer.IsValid())
			{
				CurrentBuffer->Buffer.Abort();
			}
			bHasBeenAborted = true;
		}
		TSharedPtrTS<IElectraHttpManager::FReceiveBuffer>		ReceiveBuffer;
		int64												ParsePos;
		bool												bHasBeenAborted;
	};

	// Methods from IParserISO14496_12::IReader
	virtual int64 ReadData(void* IntoBuffer, int64 NumBytesToRead) override;
	virtual bool HasReachedEOF() const override;
	virtual bool HasReadBeenAborted() const override;
	virtual int64 GetCurrentOffset() const override;
	// Methods from IParserISO14496_12::IBoxCallback
	virtual IParserISO14496_12::IBoxCallback::EParseContinuation OnFoundBox(IParserISO14496_12::FBoxType Box, int64 BoxSizeInBytes, int64 FileDataOffset, int64 BoxDataOffset) override;

	void Close();
	void StartWorkerThread();
	void StopWorkerThread();
	void WorkerThread(void);

	void PostError(const FString& Message, uint16 Code, UEMediaError Error = UEMEDIA_ERROR_OK);
	void LogMessage(IInfoLog::ELevel Level, const FString& Message);

	FErrorDetail CreateErrorAndLog(const FString& Message, uint16 Code, UEMediaError Error = UEMEDIA_ERROR_OK);

	int32 HTTPProgressCallback(const IElectraHttpManager::FRequest* Request);
	void HTTPCompletionCallback(const IElectraHttpManager::FRequest* Request);

	bool HasErrored() const
	{
		return bHasErrored;
	}

	IPlayerSessionServices*									PlayerSessionServices;
	FStreamPreferences										StreamPreferences;
	FParamDict												Options;
	FString													MasterPlaylistURL;
	FMediaEvent												WorkerThreadQuitSignal;
	bool													bIsWorkerThreadStarted;

	TSharedPtrTS<IElectraHttpManager::FProgressListener>		ProgressListener;
	FReadBuffer												ReadBuffer;
	bool													bAbort;
	bool													bHasErrored;

	TSharedPtrTS<IParserISO14496_12>						MP4Parser;
	bool													bFoundBoxFTYP;
	bool													bFoundBoxMOOV;
	bool													bFoundBoxSIDX;
	bool													bFoundBoxMOOF;
	bool													bFoundBoxMDAT;

	TSharedPtrTS<FManifestMP4Internal>						Manifest;
	FErrorDetail											LastErrorDetail;
};


/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

IPlaylistReader* IPlaylistReaderMP4::Create(IPlayerSessionServices* PlayerSessionServices)
{
	FPlaylistReaderMP4* PlaylistReader = new FPlaylistReaderMP4;
	if (PlaylistReader)
	{
		PlaylistReader->Initialize(PlayerSessionServices);
	}
	return PlaylistReader;
}

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/


FPlaylistReaderMP4::FPlaylistReaderMP4()
	: FMediaThread("ElectraPlayer::MP4 Playlist")
	, PlayerSessionServices(nullptr)
	, bIsWorkerThreadStarted(false)
	, bAbort(false)
	, bHasErrored(false)
	, bFoundBoxFTYP(false)
	, bFoundBoxMOOV(false)
	, bFoundBoxSIDX(false)
	, bFoundBoxMOOF(false)
	, bFoundBoxMDAT(false)
{
}

FPlaylistReaderMP4::~FPlaylistReaderMP4()
{
	Close();
}

FString FPlaylistReaderMP4::GetURL() const
{
	return MasterPlaylistURL;
}

TSharedPtrTS<IManifest> FPlaylistReaderMP4::GetManifest()
{
	return Manifest;
}

void FPlaylistReaderMP4::Initialize(IPlayerSessionServices* InPlayerSessionServices)
{
	PlayerSessionServices = InPlayerSessionServices;
	ProgressListener = MakeSharedTS<IElectraHttpManager::FProgressListener>();
	ProgressListener->CompletionDelegate = Electra::MakeDelegate(this, &FPlaylistReaderMP4::HTTPCompletionCallback);
	ProgressListener->ProgressDelegate   = Electra::MakeDelegate(this, &FPlaylistReaderMP4::HTTPProgressCallback);
}

void FPlaylistReaderMP4::Close()
{
	ReadBuffer.Abort();
	bAbort = true;

	StopWorkerThread();
}

void FPlaylistReaderMP4::StartWorkerThread()
{
	check(!bIsWorkerThreadStarted);
	ThreadStart(Electra::MakeDelegate(this, &FPlaylistReaderMP4::WorkerThread));
	bIsWorkerThreadStarted = true;
}

void FPlaylistReaderMP4::StopWorkerThread()
{
	if (bIsWorkerThreadStarted)
	{
		WorkerThreadQuitSignal.Signal();
		ThreadWaitDone();
		ThreadReset();
		bIsWorkerThreadStarted = false;
	}
}

void FPlaylistReaderMP4::PostError(const FString& InMessage, uint16 InCode, UEMediaError InError)
{
	LastErrorDetail.Clear();
	LastErrorDetail.SetError(InError != UEMEDIA_ERROR_OK ? InError : UEMEDIA_ERROR_DETAIL);
	LastErrorDetail.SetFacility(Facility::EFacility::MP4PlaylistReader);
	LastErrorDetail.SetCode(InCode);
	LastErrorDetail.SetMessage(InMessage);
	check(PlayerSessionServices);
	if (PlayerSessionServices)
	{
		PlayerSessionServices->PostError(LastErrorDetail);
	}
}

FErrorDetail FPlaylistReaderMP4::CreateErrorAndLog(const FString& InMessage, uint16 InCode, UEMediaError InError)
{
	FErrorDetail err;
	err.SetError(InError != UEMEDIA_ERROR_OK ? InError : UEMEDIA_ERROR_DETAIL);
	err.SetFacility(Facility::EFacility::MP4PlaylistReader);
	err.SetCode(InCode);
	err.SetMessage(InMessage);
	check(PlayerSessionServices);
	if (PlayerSessionServices)
	{
		PlayerSessionServices->PostLog(Facility::EFacility::MP4PlaylistReader, IInfoLog::ELevel::Error, err.GetPrintable());
	}
	return err;
}

void FPlaylistReaderMP4::LogMessage(IInfoLog::ELevel Level, const FString& Message)
{
	if (PlayerSessionServices)
	{
		PlayerSessionServices->PostLog(Facility::EFacility::MP4PlaylistReader, Level, Message);
	}
}

void FPlaylistReaderMP4::LoadAndParse(const FString& URL, const FStreamPreferences& Preferences, const FParamDict& InOptions)
{
	StreamPreferences = Preferences;
	Options 		  = InOptions;
	MasterPlaylistURL = URL;
	StartWorkerThread();
}

int32 FPlaylistReaderMP4::HTTPProgressCallback(const IElectraHttpManager::FRequest* Request)
{
	// Aborted?
	return bAbort ? 1 : 0;
}

void FPlaylistReaderMP4::HTTPCompletionCallback(const IElectraHttpManager::FRequest* InRequest)
{
	bHasErrored = InRequest->ConnectionInfo.StatusInfo.ErrorDetail.IsError();
}


void FPlaylistReaderMP4::WorkerThread(void)
{
	LLM_SCOPE(ELLMTag::ElectraPlayer);
	CSV_SCOPED_TIMING_STAT(ElectraPlayer, PlaylistReaderMP4_Worker);

	ReadBuffer.Reset();
	ReadBuffer.ReceiveBuffer = MakeSharedTS<IElectraHttpManager::FReceiveBuffer>();
	ReadBuffer.ReceiveBuffer->Buffer.Reserve(1 << 20);
	ReadBuffer.ReceiveBuffer->bEnableRingbuffer = true;

	TSharedPtrTS<IElectraHttpManager::FRequest> HTTP(new IElectraHttpManager::FRequest);
	HTTP->Parameters.URL = MasterPlaylistURL;
	// Perform small reads of 256K ranges only. We just want to fetch the boxes up to the first 'mdat' and those should be fairly small.
	HTTP->Parameters.SubRangeRequestSize = 256 << 10;
	HTTP->ReceiveBuffer = ReadBuffer.ReceiveBuffer;
	HTTP->ProgressListener = ProgressListener;
	PlayerSessionServices->GetHTTPManager()->AddRequest(HTTP);

	// Create the parser we need for parsing the "moov" box containing all the track information.
	MP4Parser = IParserISO14496_12::CreateParser();

	UEMediaError parseError = MP4Parser->ParseHeader(this, this, Options, PlayerSessionServices);

	ProgressListener.Reset();
	PlayerSessionServices->GetHTTPManager()->RemoveRequest(HTTP);

	if (parseError != UEMEDIA_ERROR_ABORTED)
	{
		const HTTP::FConnectionInfo* ConnInfo = &HTTP->ConnectionInfo;

		// Notify the download of the "master playlist". This indicates the download only, not the parsing thereof.
		PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistDownloadMessage::Create(ConnInfo, Playlist::EListType::Master, Playlist::ELoadType::Initial));
		// Notify that the "master playlist" has been parsed, successfully or not.
		PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistLoadedMessage::Create(LastErrorDetail, ConnInfo, Playlist::EListType::Master, Playlist::ELoadType::Initial));

		if (parseError == UEMEDIA_ERROR_OK)
		{
			// See that we have parsed all the boxes we need.
			if (bFoundBoxFTYP && bFoundBoxMOOV)
			{
				// Prepare the tracks in the stream that are of a supported codec.
				parseError = MP4Parser->PrepareTracks(TSharedPtrTS<const IParserISO14496_12>());
				if (parseError == UEMEDIA_ERROR_OK)
				{
					Manifest = MakeSharedTS<FManifestMP4Internal>(PlayerSessionServices);
					FErrorDetail err = Manifest->Build(MP4Parser, MasterPlaylistURL, *ConnInfo);

					// Notify that the "variant playlists" are ready. There are no variants in an mp4, but this is the trigger that the playlists are all set up and are good to go now.
					PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistLoadedMessage::Create(LastErrorDetail, ConnInfo, Playlist::EListType::Variant, Playlist::ELoadType::Initial));
				}
				else
				{
					PostError(FString::Printf(TEXT("Failed to parse tracks in mp4 \"%s\" with error %u"), *ConnInfo->EffectiveURL, parseError), ERRCODE_MP4_INVALID_FILE, UEMEDIA_ERROR_FORMAT_ERROR);
				}
			}
			else
			{
				// No moov box usually means this is not a fast-start file.
				PostError(FString::Printf(TEXT("No moov box found before mdat in \"%s\". Make sure the video is fast-startable."), *ConnInfo->EffectiveURL), ERRCODE_MP4_INVALID_FILE, UEMEDIA_ERROR_FORMAT_ERROR);
			}
		}
		else
		{
			PostError(FString::Printf(TEXT("Failed to parse mp4 \"%s\" with error %u"), *ConnInfo->EffectiveURL, parseError), ERRCODE_MP4_INVALID_FILE, UEMEDIA_ERROR_FORMAT_ERROR);
		}
	}

	HTTP.Reset();
	ReadBuffer.Reset();

	// This thread's work is done. We only wait for termination now.
	WorkerThreadQuitSignal.Wait();
}






/**
 * Read n bytes of data into the provided buffer.
 *
 * Reading must return the number of bytes asked to get, if necessary by blocking.
 * If a read error prevents reading the number of bytes -1 must be returned.
 *
 * @param ToBuffer Buffer into which to store the data bytes. If nullptr is passed the data must be skipped over.
 * @param NumBytes The number of bytes to read. Must not read more bytes and no less than requested.
 * @return The number of bytes read or -1 on a read error.
 */
int64 FPlaylistReaderMP4::ReadData(void* ToBuffer, int64 NumBytes)
{
	FPODRingbuffer& SourceBuffer = ReadBuffer.ReceiveBuffer->Buffer;

	uint8* OutputBuffer = (uint8*)ToBuffer;
	// Do we have enough data in the ringbuffer to satisfy the read?
	if (SourceBuffer.Num() >= NumBytes)
	{
		// Yes. Get the data and return.
		int32 NumGot = SourceBuffer.PopData(OutputBuffer, NumBytes);
		check(NumGot == NumBytes);
		ReadBuffer.ParsePos += NumBytes;
		return NumBytes;
	}
	else
	{
		// Do not have enough data yet or we want to read more than the ringbuffer can hold.
		int32 NumBytesToGo = NumBytes;
		while(NumBytesToGo > 0)
		{
			if (HasErrored() || SourceBuffer.WasAborted() || bAbort)
			{
				return -1;
			}
			// EOD?
			if (SourceBuffer.IsEndOfData())
			{
				return 0;
			}

			// Get whatever amount of data is currently available to free up the buffer for receiving more data.
			int32 NumGot = SourceBuffer.PopData(OutputBuffer, NumBytesToGo);
			if ((NumBytesToGo -= NumGot) > 0)
			{
				if (OutputBuffer)
				{
					OutputBuffer += NumGot;
				}
				// Wait for data to arrive in the ringbuffer.
				int32 WaitForBytes = NumBytesToGo > SourceBuffer.Capacity() ? SourceBuffer.Capacity() : NumBytesToGo;
				SourceBuffer.WaitUntilSizeAvailable(WaitForBytes, 1000 * 100);
			}
		}
		ReadBuffer.ParsePos += NumBytes;
		return NumBytes;
	}
}

/**
 * Checks if the data source has reached the End Of File (EOF) and cannot provide any additional data.
 *
 * @return If EOF has been reached returns true, otherwise false.
 */
bool FPlaylistReaderMP4::HasReachedEOF() const
{
	FPODRingbuffer& SourceBuffer = ReadBuffer.ReceiveBuffer->Buffer;
	return SourceBuffer.IsEndOfData();
}

/**
 * Checks if reading of the file and therefor parsing has been aborted.
 *
 * @return true if reading/parsing has been aborted, false otherwise.
 */
bool FPlaylistReaderMP4::HasReadBeenAborted() const
{
	return bAbort || ReadBuffer.bHasBeenAborted;
}

/**
 * Returns the current read offset.
 *
 * The first read offset is not necessarily zero. It could be anywhere inside the source.
 *
 * @return The current byte offset in the source.
 */
int64 FPlaylistReaderMP4::GetCurrentOffset() const
{
	return ReadBuffer.ParsePos;
}


IParserISO14496_12::IBoxCallback::EParseContinuation FPlaylistReaderMP4::OnFoundBox(IParserISO14496_12::FBoxType Box, int64 BoxSizeInBytes, int64 FileDataOffset, int64 BoxDataOffset)
{
	// We require the very first box to be an 'ftyp' box.
	if (FileDataOffset == 0 && Box != IParserISO14496_12::BoxType_ftyp)
	{
		PostError("Invalid mp4 file: first box is not 'ftyp'", ERRCODE_MP4_INVALID_FILE, UEMEDIA_ERROR_FORMAT_ERROR);
		return IParserISO14496_12::IBoxCallback::EParseContinuation::Stop;
	}

	// Check which box is being parsed next.
	switch(Box)
	{
		case IParserISO14496_12::BoxType_ftyp:
		{
			bFoundBoxFTYP = true;
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Continue;
		}
		case IParserISO14496_12::BoxType_moov:
		{
			bFoundBoxMOOV = true;
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Continue;
		}
		case IParserISO14496_12::BoxType_sidx:
		{
			bFoundBoxSIDX = true;
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Continue;
		}
		case IParserISO14496_12::BoxType_moof:
		{
			bFoundBoxMOOF = true;
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Stop;
		}
		case IParserISO14496_12::BoxType_mdat:
		{
			bFoundBoxMDAT = true;
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Stop;
		}
		default:
		{
			return IParserISO14496_12::IBoxCallback::EParseContinuation::Continue;
		}
	}
}


} // namespace Electra


