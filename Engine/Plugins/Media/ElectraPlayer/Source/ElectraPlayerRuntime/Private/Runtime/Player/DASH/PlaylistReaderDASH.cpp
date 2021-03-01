// Copyright Epic Games, Inc. All Rights Reserved.

#include "PlayerCore.h"
#include "Player/Manifest.h"
#include "HTTP/HTTPManager.h"
#include "SynchronizedClock.h"
#include "PlaylistReaderDASH.h"
#include "PlaylistReaderDASH_Internal.h"
#include "ManifestBuilderDASH.h"
#include "ManifestDASH.h"
#include "Utilities/HashFunctions.h"
#include "Utilities/TimeUtilities.h"
#include "HAL/LowLevelMemTracker.h"
#include "ElectraPlayerPrivate.h"
#include "Player/AdaptiveStreamingPlayerResourceRequest.h"
#include "Player/PlayerEntityCache.h"

#define ERRCODE_DASH_PARSER_ERROR							1
#define ERRCODE_DASH_MPD_DOWNLOAD_FAILED					2
#define ERRCODE_DASH_MPD_PARSING_FAILED						3
#define ERRCODE_DASH_MPD_UNSUPPORTED_DOCUMENT_ENCODING		4
#define ERRCODE_DASH_MPD_REMOTE_ENTITY_FAILED				5



namespace Electra
{

const FString IPlaylistReaderDASH::OptionKeyMPDLoadConnectTimeout(TEXT("mpd_connection_timeout"));				//!< (FTimeValue) value specifying connection timeout fetching the MPD
const FString IPlaylistReaderDASH::OptionKeyMPDLoadNoDataTimeout(TEXT("mpd_nodata_timeout"));					//!< (FTimeValue) value specifying no-data timeout fetching the MPD
const FString IPlaylistReaderDASH::OptionKeyMPDReloadConnectTimeout(TEXT("mpd_update_connection_timeout"));		//!< (FTimeValue) value specifying connection timeout fetching the MPD repeatedly
const FString IPlaylistReaderDASH::OptionKeyMPDReloadNoDataTimeout(TEXT("mpd_update_connection_timeout"));		//!< (FTimeValue) value specifying no-data timeout fetching the MPD repeatedly


/**
 * This class is responsible for downloading a DASH MPD and parsing it.
 */
class FPlaylistReaderDASH : public TSharedFromThis<FPlaylistReaderDASH, ESPMode::ThreadSafe>, public IPlaylistReaderDASH, public FMediaThread
{
public:
	FPlaylistReaderDASH();
	void Initialize(IPlayerSessionServices* PlayerSessionServices);

	virtual ~FPlaylistReaderDASH();
	virtual void Close() override;

	/**
	 * Returns the type of playlist format.
	 * For this implementation it will be "dash".
	 *
	 * @return "dash" to indicate this is a DASH MPD.
	 */
	virtual const FString& GetPlaylistType() const override
	{
		static FString Type("dash");
		return Type;
	}

	/**
	 * Loads and parses the MPD.
	 *
	 * @param URL     URL of the MPD to load
	 * @param Preferences
	 *                User preferences for initial stream selection.
	 * @param Options Options for the reader and parser
	 */
	virtual void LoadAndParse(const FString& URL, const FStreamPreferences& Preferences, const FParamDict& Options) override;

	/**
	 * Returns the URL from which the MPD was loaded (or supposed to be loaded).
	 *
	 * @return The MPD URL
	 */
	virtual FString GetURL() const override;

	/**
	 * Returns an interface to the manifest created from the loaded MPD.
	 *
	 * @return A shared manifest interface pointer.
	 */
	virtual TSharedPtrTS<IManifest> GetManifest() override;

	/**
	 * Requests updating of a dynamic DASH MPD or an XLINK item.
	 *
	 * @param LoadRequest
	 */
	virtual void RequestMPDUpdate(const FMPDLoadRequestDASH& LoadRequest) override;

	virtual void AddElementLoadRequests(const TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& RemoteElementLoadRequests) override;

private:
	using FResourceLoadRequestPtr = TSharedPtrTS<FMPDLoadRequestDASH>;

	void OnHTTPResourceRequestComplete(TSharedPtrTS<FHTTPResourceRequest> InRequest);

	FErrorDetail GetXMLResponseString(FString& OutXMLString, FResourceLoadRequestPtr FromRequest);

	void StartWorkerThread();
	void StopWorkerThread();
	void WorkerThread(void);
	void ExecutePendingRequests(const FTimeValue& TimeNow);
	void HandleCompletedRequests(const FTimeValue& TimeNow);
	void HandleStaticRequestCompletions(const FTimeValue& TimeNow);
	void CheckForMPDUpdate(const FTimeValue& TimeNow);

	void PostError(const FErrorDetail& Error);
	void PostError(const FString& Message, uint16 Code, UEMediaError Error = UEMEDIA_ERROR_OK);
	void LogMessage(IInfoLog::ELevel Level, const FString& Message);
	FErrorDetail CreateErrorAndLog(const FString& Message, uint16 Code, UEMediaError Error = UEMEDIA_ERROR_OK);

	void SetupRequestTimeouts(FResourceLoadRequestPtr InRequest);
	int32 CheckForRetry(FResourceLoadRequestPtr InRequest, int32 Error);

	void EnqueueResourceRequest(FResourceLoadRequestPtr InRequest);
	void EnqueueResourceRetryRequest(FResourceLoadRequestPtr InRequest, const FTimeValue& AtUTCTime);
	void EnqueueInitialXLinkRequests();

	void ManifestDownloadCompleted(FResourceLoadRequestPtr Request, bool bSuccess);
	void InitialMPDXLinkElementDownloadCompleted(FResourceLoadRequestPtr Request, bool bSuccess);


	IPlayerSessionServices*									PlayerSessionServices;
	FStreamPreferences										StreamPreferences;
	FParamDict												Options;
	FString													MPDURL;
	FMediaSemaphore											WorkerThreadSignal;
	bool													bIsWorkerThreadStarted;
	volatile bool											bTerminateWorkerThread;

	FCriticalSection										RequestsLock;
	TArray<FResourceLoadRequestPtr>							PendingRequests;
	TArray<FResourceLoadRequestPtr>							ActiveRequests;
	TArray<FResourceLoadRequestPtr>							CompletedRequests;


	TSharedPtrTS<FManifestDASHInternal>						Manifest;
	FErrorDetail											LastErrorDetail;

	TSharedPtrTS<FManifestDASH>								PlayerManifest;
};


/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/

TSharedPtrTS<IPlaylistReader> IPlaylistReaderDASH::Create(IPlayerSessionServices* PlayerSessionServices)
{
	TSharedPtrTS<FPlaylistReaderDASH> PlaylistReader = MakeSharedTS<FPlaylistReaderDASH>();
	if (PlaylistReader)
	{
		PlaylistReader->Initialize(PlayerSessionServices);
	}
	return PlaylistReader;
}

/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/
/***************************************************************************************************************************************************/


FPlaylistReaderDASH::FPlaylistReaderDASH()
	: FMediaThread("ElectraPlayer::DASH MPD")
	, PlayerSessionServices(nullptr)
	, bIsWorkerThreadStarted(false)
	, bTerminateWorkerThread(false)
{
}

FPlaylistReaderDASH::~FPlaylistReaderDASH()
{
	Close();
}

FString FPlaylistReaderDASH::GetURL() const
{
	return MPDURL;
}

TSharedPtrTS<IManifest> FPlaylistReaderDASH::GetManifest()
{
	return PlayerManifest;
}

void FPlaylistReaderDASH::Initialize(IPlayerSessionServices* InPlayerSessionServices)
{
	PlayerSessionServices = InPlayerSessionServices;
}

void FPlaylistReaderDASH::Close()
{
	StopWorkerThread();
}

void FPlaylistReaderDASH::StartWorkerThread()
{
	check(!bIsWorkerThreadStarted);
	bTerminateWorkerThread = false;
	ThreadStart(Electra::MakeDelegate(this, &FPlaylistReaderDASH::WorkerThread));
	bIsWorkerThreadStarted = true;
}

void FPlaylistReaderDASH::StopWorkerThread()
{
	if (bIsWorkerThreadStarted)
	{
		bTerminateWorkerThread = true;
		WorkerThreadSignal.Release();
		ThreadWaitDone();
		ThreadReset();
		bIsWorkerThreadStarted = false;
	}
}

void FPlaylistReaderDASH::PostError(const FErrorDetail& Error)
{
	check(PlayerSessionServices);
	if (PlayerSessionServices)
	{
		PlayerSessionServices->PostError(Error);
	}
}

void FPlaylistReaderDASH::PostError(const FString& InMessage, uint16 InCode, UEMediaError InError)
{
	LastErrorDetail.Clear();
	LastErrorDetail.SetError(InError != UEMEDIA_ERROR_OK ? InError : UEMEDIA_ERROR_DETAIL);
	LastErrorDetail.SetFacility(Facility::EFacility::DASHMPDReader);
	LastErrorDetail.SetCode(InCode);
	LastErrorDetail.SetMessage(InMessage);
	PostError(LastErrorDetail);
}

FErrorDetail FPlaylistReaderDASH::CreateErrorAndLog(const FString& InMessage, uint16 InCode, UEMediaError InError)
{
	FErrorDetail err;
	err.SetError(InError != UEMEDIA_ERROR_OK ? InError : UEMEDIA_ERROR_DETAIL);
	err.SetFacility(Facility::EFacility::DASHMPDReader);
	err.SetCode(InCode);
	err.SetMessage(InMessage);
	check(PlayerSessionServices);
	if (PlayerSessionServices)
	{
		PlayerSessionServices->PostLog(Facility::EFacility::DASHMPDReader, IInfoLog::ELevel::Error, err.GetPrintable());
	}
	return err;
}


void FPlaylistReaderDASH::LogMessage(IInfoLog::ELevel Level, const FString& Message)
{
	if (PlayerSessionServices)
	{
		PlayerSessionServices->PostLog(Facility::EFacility::DASHMPDReader, Level, Message);
	}
}

void FPlaylistReaderDASH::LoadAndParse(const FString& URL, const FStreamPreferences& Preferences, const FParamDict& InOptions)
{
	StreamPreferences = Preferences;
	Options 		  = InOptions;
	MPDURL = URL;
	StartWorkerThread();
}

void FPlaylistReaderDASH::RequestMPDUpdate(const FMPDLoadRequestDASH& LoadRequest)
{
}

void FPlaylistReaderDASH::CheckForMPDUpdate(const FTimeValue& TimeNow)
{
}


void FPlaylistReaderDASH::OnHTTPResourceRequestComplete(TSharedPtrTS<FHTTPResourceRequest> InRequest)
{
	FResourceLoadRequestPtr lr = StaticCastSharedPtr<FMPDLoadRequestDASH>(InRequest->GetObject());
	if (lr.IsValid())
	{
		FScopeLock lock(&RequestsLock);
		CompletedRequests.Emplace(lr);
		ActiveRequests.Remove(lr);
	}
	WorkerThreadSignal.Release();
}


void FPlaylistReaderDASH::EnqueueResourceRequest(FResourceLoadRequestPtr InRequest)
{
	FScopeLock lock(&RequestsLock);
	PendingRequests.Emplace(MoveTemp(InRequest));
	WorkerThreadSignal.Release();
}

void FPlaylistReaderDASH::EnqueueResourceRetryRequest(FResourceLoadRequestPtr InRequest, const FTimeValue& AtUTCTime)
{
	FScopeLock lock(&RequestsLock);
	InRequest->ExecuteAtUTC = AtUTCTime;
	++InRequest->Attempt;
	PendingRequests.Emplace(MoveTemp(InRequest));
	WorkerThreadSignal.Release();
}

void FPlaylistReaderDASH::EnqueueInitialXLinkRequests()
{
	check(Manifest.IsValid());
	TArray<TWeakPtrTS<FMPDLoadRequestDASH>> RemoteElementLoadRequests;
	Manifest->GetRemoteElementLoadRequests(RemoteElementLoadRequests);
	FScopeLock lock(&RequestsLock);
	for(int32 i=0; i<RemoteElementLoadRequests.Num(); ++i)
	{
		TSharedPtrTS<FMPDLoadRequestDASH> LReq = RemoteElementLoadRequests[i].Pin();
		if (LReq.IsValid())
		{
			LReq->OwningManifest = Manifest;
			LReq->CompleteCallback.BindThreadSafeSP(AsShared(), &FPlaylistReaderDASH::InitialMPDXLinkElementDownloadCompleted);
			PendingRequests.Emplace(MoveTemp(LReq));
		}
	}
	WorkerThreadSignal.Release();
}

void FPlaylistReaderDASH::AddElementLoadRequests(const TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& RemoteElementLoadRequests)
{
	if (RemoteElementLoadRequests.Num())
	{
		FScopeLock lock(&RequestsLock);
		for(int32 i=0; i<RemoteElementLoadRequests.Num(); ++i)
		{
			TSharedPtrTS<FMPDLoadRequestDASH> LReq = RemoteElementLoadRequests[i].Pin();
			if (LReq.IsValid())
			{
				LReq->OwningManifest = Manifest;
				PendingRequests.Emplace(MoveTemp(LReq));
			}
		}
		WorkerThreadSignal.Release();
	}
}



void FPlaylistReaderDASH::WorkerThread(void)
{
	LLM_SCOPE(ELLMTag::ElectraPlayer);
	CSV_SCOPED_TIMING_STAT(ElectraPlayer, MPDReaderDASH_Worker);

	// Setup the playlist load request for the master playlist.
	TSharedPtrTS<FMPDLoadRequestDASH> PlaylistLoadRequest = MakeSharedTS<FMPDLoadRequestDASH>();
	PlaylistLoadRequest->LoadType = FMPDLoadRequestDASH::ELoadType::MPD;
	PlaylistLoadRequest->URL = MPDURL;
	PlaylistLoadRequest->CompleteCallback.BindThreadSafeSP(AsShared(), &FPlaylistReaderDASH::ManifestDownloadCompleted);
	EnqueueResourceRequest(MoveTemp(PlaylistLoadRequest));

	while(!bTerminateWorkerThread)
	{
		WorkerThreadSignal.Obtain(1000 * 100);
		if (bTerminateWorkerThread)
		{
			break;
		}
		FTimeValue Now = PlayerSessionServices->GetSynchronizedUTCTime()->GetTime();

		HandleCompletedRequests(Now);

		ExecutePendingRequests(Now);

		// Check if the MPD must be updated.
		CheckForMPDUpdate(Now);
	}

	// Cleanup!
	RequestsLock.Lock();
	PendingRequests.Empty();
	for(int32 i=0; i<ActiveRequests.Num(); ++i)
	{
		if (ActiveRequests[i]->Request.IsValid())
		{
			ActiveRequests[i]->Request->Cancel();
		}
	}
	ActiveRequests.Empty();
	CompletedRequests.Empty();
	RequestsLock.Unlock();
}


void FPlaylistReaderDASH::ExecutePendingRequests(const FTimeValue& TimeNow)
{
	FScopeLock lock(&RequestsLock);

	for(int32 i=0; i<PendingRequests.Num(); ++i)
	{
		FResourceLoadRequestPtr Request = PendingRequests[i];
		if (!Request->ExecuteAtUTC.IsValid() || TimeNow >= Request->ExecuteAtUTC)
		{
			PendingRequests.RemoveAt(i);
			--i;

			// Check for reserved URIs
			if (Request->URL.Equals(TEXT("urn:mpeg:dash:resolve-to-zero:2013")))
			{
				CompletedRequests.Emplace(Request);
				WorkerThreadSignal.Release();
			}
			else
			{
				++Request->Attempt;
				ActiveRequests.Push(Request);

				Request->Request = MakeSharedTS<FHTTPResourceRequest>();
				Request->Request->URL(Request->URL).Range(Request->Range).Headers(Request->Headers).Object(Request);
				// Set up timeouts and also accepted encodings depending on the type of request.
				SetupRequestTimeouts(Request);
				Request->Request->AllowStaticQuery(IAdaptiveStreamingPlayerResourceRequest::EPlaybackResourceType::Playlist);

				Request->Request->Callback().BindThreadSafeSP(AsShared(), &FPlaylistReaderDASH::OnHTTPResourceRequestComplete);
				Request->Request->StartGet(PlayerSessionServices);
			}
		}
	}
}


void FPlaylistReaderDASH::SetupRequestTimeouts(FResourceLoadRequestPtr InRequest)
{
	switch(InRequest->LoadType)
	{
		case FMPDLoadRequestDASH::ELoadType::MPD:
		{
			InRequest->Request->ConnectionTimeout(Options.GetValue(IPlaylistReaderDASH::OptionKeyMPDLoadConnectTimeout).SafeGetTimeValue(FTimeValue().SetFromMilliseconds(1000 * 8)));
			InRequest->Request->NoDataTimeout(Options.GetValue(IPlaylistReaderDASH::OptionKeyMPDLoadNoDataTimeout).SafeGetTimeValue(FTimeValue().SetFromMilliseconds(1000 * 5)));
			break;
		}
		case FMPDLoadRequestDASH::ELoadType::MPDUpdate:
		{
			InRequest->Request->ConnectionTimeout(Options.GetValue(IPlaylistReaderDASH::OptionKeyMPDReloadConnectTimeout).SafeGetTimeValue(FTimeValue().SetFromMilliseconds(1000 * 2)));
			InRequest->Request->NoDataTimeout(Options.GetValue(IPlaylistReaderDASH::OptionKeyMPDReloadNoDataTimeout).SafeGetTimeValue(FTimeValue().SetFromMilliseconds(1000 * 2)));
			break;
		}
		case FMPDLoadRequestDASH::ELoadType::Segment:
		{
			InRequest->Request->ConnectionTimeout(FTimeValue(FTimeValue::MillisecondsToHNS(1000 * 5)));
			InRequest->Request->NoDataTimeout(FTimeValue(FTimeValue::MillisecondsToHNS(1000 * 2)));
			//InRequest->Request->AcceptEncoding(TEXT("identity"));
			break;
		}
		case FMPDLoadRequestDASH::ELoadType::XLink_Period:
		case FMPDLoadRequestDASH::ELoadType::XLink_AdaptationSet:
		case FMPDLoadRequestDASH::ELoadType::XLink_EventStream:
		case FMPDLoadRequestDASH::ELoadType::XLink_SegmentList:
		case FMPDLoadRequestDASH::ELoadType::XLink_URLQuery:
		case FMPDLoadRequestDASH::ELoadType::XLink_InitializationSet:
		case FMPDLoadRequestDASH::ELoadType::Callback:
		default:
		{
			break;
		}
	}
}

int32 FPlaylistReaderDASH::CheckForRetry(FResourceLoadRequestPtr InRequest, int32 Error)
{
	switch(InRequest->LoadType)
	{
		case FMPDLoadRequestDASH::ELoadType::MPD:
		{
			if (Error < 100 && InRequest->Attempt < 3)
			{
				return 500 * (1 << InRequest->Attempt);
			}
			else if (Error >= 502 && Error <= 504 && InRequest->Attempt < 2)
			{
				return 1000 * (1 << InRequest->Attempt);
			}
			return -1;
		}
		case FMPDLoadRequestDASH::ELoadType::MPDUpdate:
		{
			if (Error < 100 && InRequest->Attempt < 3)
			{
				return 500 * (1 << InRequest->Attempt);
			}
			else if (InRequest->Attempt < 2 &&
					(Error == 204 || Error == 205 || Error == 404 || Error == 408 || Error == 429 || Error == 502 || Error == 503 || Error == 504))
			{
				return 1000 * (1 << InRequest->Attempt);
			}
			return -1;
		}
		case FMPDLoadRequestDASH::ELoadType::Segment:
		{
			if (Error < 100 && InRequest->Attempt < 2)
			{
				return 250 * (1 << InRequest->Attempt);
			}
			else if (InRequest->Attempt < 2 &&
					(Error == 204 || Error == 205 || Error == 404 || Error == 408 || Error == 429 || Error == 502 || Error == 503 || Error == 504))
			{
				return 500 * (1 << InRequest->Attempt);
			}
			return -1;
		}

		case FMPDLoadRequestDASH::ELoadType::XLink_Period:
		case FMPDLoadRequestDASH::ELoadType::XLink_AdaptationSet:
		case FMPDLoadRequestDASH::ELoadType::XLink_EventStream:
		case FMPDLoadRequestDASH::ELoadType::XLink_SegmentList:
		case FMPDLoadRequestDASH::ELoadType::XLink_URLQuery:
		case FMPDLoadRequestDASH::ELoadType::XLink_InitializationSet:
		case FMPDLoadRequestDASH::ELoadType::Callback:
		default:
		{
			break;
		}
	}
	return -1;
}


void FPlaylistReaderDASH::HandleCompletedRequests(const FTimeValue& TimeNow)
{
	RequestsLock.Lock();
	TArray<FResourceLoadRequestPtr> cr(MoveTemp(CompletedRequests));
	RequestsLock.Unlock();
	for(int32 i=0; i<cr.Num(); ++i)
	{
		TSharedPtrTS<FHTTPResourceRequest> req = cr[i]->Request;
		// Special URI requests have no actual download request. Only notify the callback of the completion.
		if (!req.IsValid())
		{
			cr[i]->CompleteCallback.ExecuteIfBound(cr[i], true);
			continue;
		}
		// Ignore canceled requests.
		if (req->GetWasCanceled())
		{
			continue;
		}
		// Did the request succeed?
		if (req->GetError() == 0)
		{
			// Set the response headers for this entity with the header cache.
			switch(cr[i]->GetLoadType())
			{
				case FMPDLoadRequestDASH::ELoadType::MPD:
				case FMPDLoadRequestDASH::ELoadType::MPDUpdate:
				{
					PlayerSessionServices->GetEntityCache()->SetRecentResponseHeaders(IPlayerEntityCache::EEntityType::Document, cr[i]->URL, req->GetConnectionInfo()->ResponseHeaders);
					break;
				}
				case FMPDLoadRequestDASH::ELoadType::XLink_Period:
				case FMPDLoadRequestDASH::ELoadType::XLink_AdaptationSet:
				case FMPDLoadRequestDASH::ELoadType::XLink_EventStream:
				case FMPDLoadRequestDASH::ELoadType::XLink_SegmentList:
				case FMPDLoadRequestDASH::ELoadType::XLink_URLQuery:
				{
					PlayerSessionServices->GetEntityCache()->SetRecentResponseHeaders(IPlayerEntityCache::EEntityType::XLink, cr[i]->URL, req->GetConnectionInfo()->ResponseHeaders);
					break;
				}
				case FMPDLoadRequestDASH::ELoadType::Callback:
				{
					PlayerSessionServices->GetEntityCache()->SetRecentResponseHeaders(IPlayerEntityCache::EEntityType::Callback, cr[i]->URL, req->GetConnectionInfo()->ResponseHeaders);
					break;
				}
				case FMPDLoadRequestDASH::ELoadType::Segment:
				{
					PlayerSessionServices->GetEntityCache()->SetRecentResponseHeaders(IPlayerEntityCache::EEntityType::Segment, cr[i]->URL, req->GetConnectionInfo()->ResponseHeaders);
					break;
				}
				default:
				{
					break;
				}
			}

			cr[i]->CompleteCallback.ExecuteIfBound(cr[i], true);
		}
		else
		{
			// Handle possible retries.
			int32 retryInMillisec = CheckForRetry(cr[i], req->GetError());
			if (retryInMillisec >= 0)
			{
				// Try again
				LogMessage(IInfoLog::ELevel::Warning, FString::Printf(TEXT("Failed to download %s (%s) from %s, retrying..."), cr[i]->GetRequestTypeName(), *cr[i]->URL, *req->GetErrorString()));
				EnqueueResourceRetryRequest(cr[i], TimeNow + FTimeValue().SetFromMilliseconds(retryInMillisec));
			}
			else
			{
				// Give up. Notify caller of failure.
				LogMessage(IInfoLog::ELevel::Error, FString::Printf(TEXT("Failed to download %s (%s) from %s, giving up."), cr[i]->GetRequestTypeName(), *cr[i]->URL, *req->GetErrorString()));
				cr[i]->CompleteCallback.ExecuteIfBound(cr[i], false);
			}
		}
	}
}

FErrorDetail FPlaylistReaderDASH::GetXMLResponseString(FString& OutXMLString, FResourceLoadRequestPtr FromRequest)
{
	if (FromRequest.IsValid() && FromRequest->Request.IsValid())
	{
		TSharedPtrTS<IElectraHttpManager::FReceiveBuffer> ResponseBuffer = FromRequest->Request->GetResponseBuffer();
		int32 NumResponseBytes = ResponseBuffer->Buffer.Num();
		const uint8* ResponseBytes = (const uint8*)ResponseBuffer->Buffer.GetLinearReadData();
		// Check for potential BOMs
		if (NumResponseBytes > 3 && ResponseBytes[0] == 0xEF && ResponseBytes[1] == 0xBB && ResponseBytes[2] == 0xBF)
		{
			// UTF-8 BOM
			NumResponseBytes -= 3;
			ResponseBytes += 3;
		}
		else if (NumResponseBytes >= 2 && ResponseBytes[0] == 0xFE && ResponseBytes[1] == 0xFF)
		{
			// UTF-16 BE BOM
			return CreateErrorAndLog(FString::Printf(TEXT("Document has unsupported UTF-16 BE BOM!")), ERRCODE_DASH_MPD_UNSUPPORTED_DOCUMENT_ENCODING);
		}
		else if (NumResponseBytes >= 2 && ResponseBytes[0] == 0xFF && ResponseBytes[1] == 0xFE)
		{
			// UTF-16 LE BOM
			return CreateErrorAndLog(FString::Printf(TEXT("Document has unsupported UTF-16 LE BOM!")), ERRCODE_DASH_MPD_UNSUPPORTED_DOCUMENT_ENCODING);
		}
		else if (NumResponseBytes >= 4 && ResponseBytes[0] == 0x00 && ResponseBytes[1] == 0x00 && ResponseBytes[2] == 0xFE && ResponseBytes[3] == 0xFF)
		{
			// UTF-32 BE BOM
			return CreateErrorAndLog(FString::Printf(TEXT("Document has unsupported UTF-32 BE BOM!")), ERRCODE_DASH_MPD_UNSUPPORTED_DOCUMENT_ENCODING);
		}
		else if (NumResponseBytes >= 4 && ResponseBytes[0] == 0xFF && ResponseBytes[1] == 0xFE && ResponseBytes[2] == 0x00 && ResponseBytes[3] == 0x00)
		{
			// UTF-32 LE BOM
			return CreateErrorAndLog(FString::Printf(TEXT("Document has unsupported UTF-32 LE BOM!")), ERRCODE_DASH_MPD_UNSUPPORTED_DOCUMENT_ENCODING);
		}
		FString XML(NumResponseBytes, (TCHAR*)FUTF8ToTCHAR((const ANSICHAR*)ResponseBytes, NumResponseBytes).Get());
		OutXMLString = MoveTemp(XML);
	}
	return FErrorDetail();
}


void FPlaylistReaderDASH::ManifestDownloadCompleted(FResourceLoadRequestPtr Request, bool bSuccess)
{
	if (bSuccess)
	{
		if (!Request->Request.IsValid() || !Request->Request->GetResponseBuffer().IsValid())
		{
			return;
		}
		
		// Get the Date header from the response and set our clock to this time.
		// Do this once only with the date from the master playlist.
		const HTTP::FConnectionInfo* ConnInfo = Request->GetConnectionInfo();
		if (ConnInfo)
		{
			for(int32 i=0; i<ConnInfo->ResponseHeaders.Num(); ++i)
			{
				if (ConnInfo->ResponseHeaders[i].Header == "Date")
				{
					// How old is the response already?
					FTimeValue ResponseAge = MEDIAutcTime::Current() - ConnInfo->RequestStartTime - FTimeValue().SetFromSeconds(ConnInfo->TimeUntilFirstByte);
					// Parse the header
					FTimeValue DateFromHeader;
					UEMediaError DateParseError = RFC7231::ParseDateTime(DateFromHeader, ConnInfo->ResponseHeaders[i].Value);
					if (DateParseError == UEMEDIA_ERROR_OK && DateFromHeader.IsValid())
					{
						PlayerSessionServices->GetSynchronizedUTCTime()->SetTime(DateFromHeader + ResponseAge);
					}
					break;
				}
			}
		}

		PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistDownloadMessage::Create(ConnInfo, Playlist::EListType::Master, Playlist::ELoadType::Initial));

		FString XML;
		LastErrorDetail = GetXMLResponseString(XML, Request);
		if (LastErrorDetail.IsOK())
		{
			IManifestBuilderDASH* Builder = IManifestBuilderDASH::Create(PlayerSessionServices);
			TSharedPtrTS<FManifestDASHInternal> NewManifest;
			LastErrorDetail = Builder->BuildFromMPD(NewManifest, XML.GetCharArray().GetData(), Request, StreamPreferences, Options);
			Manifest = NewManifest;
			PlayerManifest = FManifestDASH::Create(PlayerSessionServices, Options, AsShared(), Manifest);
			// Notify that the "master playlist" has been parsed, successfully or not. If we still need to resolve remote entities this is not an error!
			PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistLoadedMessage::Create(!LastErrorDetail.IsTryAgain() ? LastErrorDetail : FErrorDetail(), ConnInfo, Playlist::EListType::Master, Playlist::ELoadType::Initial));
			if (LastErrorDetail.IsOK())
			{
				PlayerManifest->UpdateTimeline();
				// Notify that the "variant playlists" are ready. There are no variants in DASH, but this is the trigger that the playlists are all set up and are good to go now.
				PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistLoadedMessage::Create(LastErrorDetail, ConnInfo, Playlist::EListType::Variant, Playlist::ELoadType::Initial));
			}
			// "try again" is returned when there are remote entities that need to be resolved first.
			else if (LastErrorDetail.IsTryAgain())
			{
				EnqueueInitialXLinkRequests();
			}
			else
			{
				PostError(LastErrorDetail);
			}
		}
		else
		{
			PostError(LastErrorDetail);
		}
	}
	else
	{
		PostError(FString::Printf(TEXT("Failed to download MPD \"%s\" (%s)"), *Request->URL, *Request->GetErrorDetail()), ERRCODE_DASH_MPD_DOWNLOAD_FAILED, UEMEDIA_ERROR_READ_ERROR);
	}
}

void FPlaylistReaderDASH::InitialMPDXLinkElementDownloadCompleted(FResourceLoadRequestPtr Request, bool bSuccess)
{
	// Is the manifest for which this request was made still of interest?
	TSharedPtrTS<FManifestDASHInternal> ForManifest = Request->OwningManifest.Pin();
	if (ForManifest.IsValid())
	{
		FString XML;
		LastErrorDetail = GetXMLResponseString(XML, Request);
		LastErrorDetail = ForManifest->ResolveInitialRemoteElementRequest(PlayerSessionServices, Request, MoveTemp(XML), bSuccess);

		// Now check if all pending requests have finished
		if (LastErrorDetail.IsOK())
		{
			LastErrorDetail = ForManifest->BuildAfterInitialRemoteElementDownload(PlayerSessionServices);
			if (LastErrorDetail.IsOK())
			{
				PlayerManifest->UpdateTimeline();
			}
			// Notify that the "variant playlists" are ready. There are no variants in DASH, but this is the trigger that the playlists are all set up and are good to go now.
			PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistLoadedMessage::Create(LastErrorDetail, nullptr, Playlist::EListType::Variant, Playlist::ELoadType::Initial));
		}
		else if (LastErrorDetail.IsTryAgain())
		{
			LastErrorDetail.Clear();
		}
		else
		{
			LastErrorDetail.SetMessage(FString::Printf(TEXT("Failed to process initial xlink:onLoad entities (%s)"), *LastErrorDetail.GetMessage()));
			PostError(LastErrorDetail);
		}
	}
}



} // namespace Electra


