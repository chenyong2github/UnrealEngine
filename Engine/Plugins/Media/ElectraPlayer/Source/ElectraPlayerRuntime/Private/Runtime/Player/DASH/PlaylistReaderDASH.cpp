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
#include "Utilities/URLParser.h"
#include "HAL/LowLevelMemTracker.h"
#include "ElectraPlayerPrivate.h"
#include "Player/AdaptiveStreamingPlayerResourceRequest.h"
#include "Player/PlayerEntityCache.h"
#include "Player/DASH/OptionKeynamesDASH.h"

#include "Misc/DateTime.h"

#define ERRCODE_DASH_PARSER_ERROR							1
#define ERRCODE_DASH_MPD_DOWNLOAD_FAILED					2
#define ERRCODE_DASH_MPD_PARSING_FAILED						3
#define ERRCODE_DASH_MPD_UNSUPPORTED_DOCUMENT_ENCODING		4
#define ERRCODE_DASH_MPD_REMOTE_ENTITY_FAILED				5


//#define DO_NOT_PERFORM_CONDITIONAL_GET

namespace Electra
{

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
	virtual const FString& GetPlaylistType() const override
	{
		static FString Type("dash");
		return Type;
	}
	virtual void LoadAndParse(const FString& URL) override;
	virtual FString GetURL() const override;
	virtual TSharedPtrTS<IManifest> GetManifest() override;
	virtual void AddElementLoadRequests(const TArray<TWeakPtrTS<FMPDLoadRequestDASH>>& RemoteElementLoadRequests) override;
	virtual void RequestMPDUpdate(bool bForcedUpdate) override;
	virtual TSharedPtrTS<FManifestDASHInternal> GetCurrentMPD() override
	{
		return Manifest;
	}

private:
	using FResourceLoadRequestPtr = TSharedPtrTS<FMPDLoadRequestDASH>;

	void OnHTTPResourceRequestComplete(TSharedPtrTS<FHTTPResourceRequest> InRequest);

	FErrorDetail GetXMLResponseString(FString& OutXMLString, FResourceLoadRequestPtr FromRequest);

	void StartWorkerThread();
	void StopWorkerThread();
	void WorkerThread();
	void ExecutePendingRequests(const FTimeValue& TimeNow);
	void HandleCompletedRequests(const FTimeValue& TimeNow);
	void HandleStaticRequestCompletions(const FTimeValue& TimeNow);
	void CheckForMPDUpdate();

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
	void ManifestUpdateDownloadCompleted(FResourceLoadRequestPtr Request, bool bSuccess);
	void InitialMPDXLinkElementDownloadCompleted(FResourceLoadRequestPtr Request, bool bSuccess);


	IPlayerSessionServices*									PlayerSessionServices = nullptr;
	FString													MPDURL;
	FString													Fragment;
	FMediaSemaphore											WorkerThreadSignal;
	bool													bIsWorkerThreadStarted = false;
	volatile bool											bTerminateWorkerThread = false;

	FCriticalSection										RequestsLock;
	TArray<FResourceLoadRequestPtr>							PendingRequests;
	TArray<FResourceLoadRequestPtr>							ActiveRequests;
	TArray<FResourceLoadRequestPtr>							CompletedRequests;
	FTimeValue												MinTimeBetweenUpdates;
	FTimeValue												NextMPDUpdateTime;
	FTimeValue												MostRecentMPDUpdateTime;
	bool													bIsMPDUpdateInProgress = false;
	enum class EUpdateRequestType
	{
		None,
		Regular,
		Forced
	};
	EUpdateRequestType										UpdateRequested = EUpdateRequestType::None;

	TUniquePtr<IManifestBuilderDASH>						Builder;

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

void FPlaylistReaderDASH::LoadAndParse(const FString& URL)
{
	MPDURL = URL;
	StartWorkerThread();
}


void FPlaylistReaderDASH::CheckForMPDUpdate()
{
	RequestsLock.Lock();
	FTimeValue Next = NextMPDUpdateTime;
	bool bRequestNow = false;
	bool bIsForced = false;
	if (UpdateRequested == EUpdateRequestType::Forced)
	{
		bIsForced = true;
		Next.SetToZero();
	}
	else if (UpdateRequested == EUpdateRequestType::Regular)
	{
		if (!Next.IsValid())
		{
			Next.SetToZero();
		}
	}
	if (!bIsMPDUpdateInProgress && Next.IsValid())
	{
		// Get the time now and do not pass it in from the worker thread as the clock could have been
		// resynchronized to the server time in the meantime.
		FTimeValue Now = PlayerSessionServices->GetSynchronizedUTCTime()->GetTime();
		// Limit the frequency of the updates.
		bool bIsPossible = !MostRecentMPDUpdateTime.IsValid() || Now - MostRecentMPDUpdateTime > MinTimeBetweenUpdates;
		if (Next < Now && bIsPossible)
		{
			bRequestNow = true;
			NextMPDUpdateTime.SetToInvalid();
		}
	}
	UpdateRequested = EUpdateRequestType::None;
	RequestsLock.Unlock();
	if (bRequestNow)
	{
		TSharedPtrTS<const FDashMPD_MPDType> MPDRoot = Manifest.IsValid() ? Manifest->GetMPDRoot() : nullptr;
		if (MPDRoot.IsValid())
		{
			FString NewMPDLocation;
			// See if there are any <Location> elements on the current MPD that tell us from where to get the update.
			// As per A.11 no <BaseURL> elements affect the construction of the new document URL. At most, if it is
			// a relative URL it will be resolved against the current document URL.
			const TArray<TSharedPtrTS<FDashMPD_OtherType>>& Locations = MPDRoot->GetLocations();
			if (Locations.Num())
			{
				// There can be several new locations, but the elements have no DASH defined attributes to differentiate them.
				// They are merely elements containing a URL. In absence of any preference we just use the first one.
				NewMPDLocation = Locations[0]->GetData();
			}

			TArray<TSharedPtrTS<const FDashMPD_BaseURLType>> OutBaseURLs;
			FString URL, RequestHeader;
			FTimeValue UrlATO;
			DASHUrlHelpers::BuildAbsoluteElementURL(URL, UrlATO, MPDRoot->GetDocumentURL(), OutBaseURLs, NewMPDLocation);

			// The URL query might need to be changed. Look for the UrlQuery properties.
			TArray<TSharedPtrTS<FDashMPD_UrlQueryInfoType>> UrlQueries;
			DASHUrlHelpers::GetAllHierarchyUrlQueries(UrlQueries, MPDRoot, DASHUrlHelpers::EUrlQueryRequestType::Mpd, false);
			FErrorDetail Error = DASHUrlHelpers::ApplyUrlQueries(PlayerSessionServices, MPDRoot->GetDocumentURL(), URL, RequestHeader, UrlQueries);
			if (Error.IsOK())
			{
				FString ETag = MPDRoot->GetETag();

				TSharedPtrTS<FMPDLoadRequestDASH> PlaylistLoadRequest = MakeSharedTS<FMPDLoadRequestDASH>();
				PlaylistLoadRequest->LoadType = FMPDLoadRequestDASH::ELoadType::MPDUpdate;
				PlaylistLoadRequest->URL = URL;
				if (RequestHeader.Len())
				{
					PlaylistLoadRequest->Headers.Emplace(HTTP::FHTTPHeader({DASH::HTTPHeaderOptionName, RequestHeader}));
				}
#ifndef DO_NOT_PERFORM_CONDITIONAL_GET
				// As per the standard we should perform conditional GET requests.
				if (ETag.Len())
				{
					PlaylistLoadRequest->Headers.Emplace(HTTP::FHTTPHeader({TEXT("If-None-Match"), ETag}));
				}
#endif
				PlaylistLoadRequest->CompleteCallback.BindThreadSafeSP(AsShared(), &FPlaylistReaderDASH::ManifestUpdateDownloadCompleted);
				
				bIsMPDUpdateInProgress = true;
				EnqueueResourceRequest(MoveTemp(PlaylistLoadRequest));
			}
			else
			{
				// Failed to build the URL.
				PostError(Error);
			}
		}
	}
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

void FPlaylistReaderDASH::RequestMPDUpdate(bool bForcedUpdate)
{
	FScopeLock lock(&RequestsLock);
	if (bForcedUpdate)
	{
		UpdateRequested = EUpdateRequestType::Forced;
	}
	else if (UpdateRequested == EUpdateRequestType::None)
	{
		UpdateRequested = EUpdateRequestType::Regular;
	}
}


void FPlaylistReaderDASH::WorkerThread()
{
	LLM_SCOPE(ELLMTag::ElectraPlayer);
	CSV_SCOPED_TIMING_STAT(ElectraPlayer, MPDReaderDASH_Worker);

	// Get the minimum MPD update time limit.
	MinTimeBetweenUpdates = PlayerSessionServices->GetOptions().GetValue(DASH::OptionKey_MinTimeBetweenMPDUpdates).SafeGetTimeValue(FTimeValue().SetFromMilliseconds(1000));

	// Setup the playlist load request for the master playlist.
	TSharedPtrTS<FMPDLoadRequestDASH> PlaylistLoadRequest = MakeSharedTS<FMPDLoadRequestDASH>();
	PlaylistLoadRequest->LoadType = FMPDLoadRequestDASH::ELoadType::MPD;
	FURL_RFC3986 UrlParser;
	UrlParser.Parse(MPDURL);
	PlaylistLoadRequest->URL = UrlParser.Get(true, false);
	Fragment = UrlParser.GetFragment();
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
		CheckForMPDUpdate();
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
	Builder.Reset();
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
	const FParamDict& Options = PlayerSessionServices->GetOptions();
	switch(InRequest->LoadType)
	{
		case FMPDLoadRequestDASH::ELoadType::MPD:
		{
			InRequest->Request->ConnectionTimeout(Options.GetValue(DASH::OptionKeyMPDLoadConnectTimeout).SafeGetTimeValue(FTimeValue().SetFromMilliseconds(1000 * 8)));
			InRequest->Request->NoDataTimeout(Options.GetValue(DASH::OptionKeyMPDLoadNoDataTimeout).SafeGetTimeValue(FTimeValue().SetFromMilliseconds(1000 * 5)));
			break;
		}
		case FMPDLoadRequestDASH::ELoadType::MPDUpdate:
		{
			InRequest->Request->ConnectionTimeout(Options.GetValue(DASH::OptionKeyMPDReloadConnectTimeout).SafeGetTimeValue(FTimeValue().SetFromMilliseconds(1000 * 2)));
			InRequest->Request->NoDataTimeout(Options.GetValue(DASH::OptionKeyMPDReloadNoDataTimeout).SafeGetTimeValue(FTimeValue().SetFromMilliseconds(1000 * 2)));
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
		if (!ConnInfo)
		{
			return;
		}

		FString ETag;
		FTimeValue FetchTime;
		FString EffectiveURL = ConnInfo->EffectiveURL;
		for(int32 i=0; i<ConnInfo->ResponseHeaders.Num(); ++i)
		{
			if (ConnInfo->ResponseHeaders[i].Header == "Date")
			{
				// Parse the header
				FTimeValue DateFromHeader;
				UEMediaError DateParseError = RFC7231::ParseDateTime(DateFromHeader, ConnInfo->ResponseHeaders[i].Value);
				if (DateParseError == UEMEDIA_ERROR_OK && DateFromHeader.IsValid())
				{
					PlayerSessionServices->GetSynchronizedUTCTime()->SetTime(ConnInfo->RequestStartTime, DateFromHeader);
				}
				// The MPD 'FetchTime' is the time at which the request to get the MPD was made to the server.
				FetchTime = PlayerSessionServices->GetSynchronizedUTCTime()->MapToSyncTime(ConnInfo->RequestStartTime);
			}
			else if (ConnInfo->ResponseHeaders[i].Header == "ETag")
			{
				ETag = ConnInfo->ResponseHeaders[i].Value;
			}
		}
		MostRecentMPDUpdateTime = FetchTime;

		PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistDownloadMessage::Create(ConnInfo, Playlist::EListType::Master, Playlist::ELoadType::Initial));

		FString XML;
		LastErrorDetail = GetXMLResponseString(XML, Request);
		if (LastErrorDetail.IsOK())
		{
			Builder.Reset(IManifestBuilderDASH::Create(PlayerSessionServices));
			TSharedPtrTS<FManifestDASHInternal> NewManifest;
			LastErrorDetail = Builder->BuildFromMPD(NewManifest, XML.GetCharArray().GetData(), EffectiveURL, FetchTime, ETag);
			if (LastErrorDetail.IsOK() || LastErrorDetail.IsTryAgain())
			{
				if (NewManifest.IsValid())
				{
					// Parse the URL fragment into its components. For a DASH URL the fragment is constructed like a query string with & delimited key/value pairs.
					TArray<FURL_RFC3986::FQueryParam> URLFragmentComponents;
					FURL_RFC3986::GetQueryParams(URLFragmentComponents, Fragment, false);	// The fragment is already URL escaped, so no need to do it again.
					NewManifest->SetURLFragmentComponents(MoveTemp(URLFragmentComponents));
					// If the URL has a special fragment part to turn this presentation into an event, do so.
					// Note: This MAY require the client clock to be synced to the server IF the special keyword 'now' is used.
					NewManifest->TransformIntoEpicEvent();
				}
				Manifest = NewManifest;
				PlayerManifest = FManifestDASH::Create(PlayerSessionServices, Manifest);

				// Check if the MPD defines an @minimumUpdatePeriod
				FTimeValue mup = Manifest->GetMinimumUpdatePeriod();
				// If the update time is zero then updates happen just in time when segments are required or through
				// an inband event stream. Either way, we do not need to update periodically.
				if (mup.IsValid() && mup > FTimeValue::GetZero())
				{
					// Warn if MUP is really small
					if (mup.GetAsMilliseconds() < 1000)
					{
						LogMessage(IInfoLog::ELevel::Warning, FString::Printf(TEXT("MPD@minimumUpdatePeriod is set to a really small value of %lld msec. This could be a performance issue."), (long long int)mup.GetAsMilliseconds()));
					}
					RequestsLock.Lock();
					NextMPDUpdateTime = MostRecentMPDUpdateTime + mup;
					RequestsLock.Unlock();
				}

				// Notify that the "master playlist" has been parsed, successfully or not. If we still need to resolve remote entities this is not an error!
				PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistLoadedMessage::Create(!LastErrorDetail.IsTryAgain() ? LastErrorDetail : FErrorDetail(), ConnInfo, Playlist::EListType::Master, Playlist::ELoadType::Initial));
				if (LastErrorDetail.IsOK())
				{
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
			PostError(LastErrorDetail);
		}
	}
	else
	{
		PostError(FString::Printf(TEXT("Failed to download MPD \"%s\" (%s)"), *Request->URL, *Request->GetErrorDetail()), ERRCODE_DASH_MPD_DOWNLOAD_FAILED, UEMEDIA_ERROR_READ_ERROR);
	}
}


void FPlaylistReaderDASH::ManifestUpdateDownloadCompleted(FResourceLoadRequestPtr Request, bool bSuccess)
{
	bIsMPDUpdateInProgress = false;

	if (bSuccess)
	{
		if (!Request->Request.IsValid() || !Request->Request->GetResponseBuffer().IsValid())
		{
			return;
		}
		
		const HTTP::FConnectionInfo* ConnInfo = Request->GetConnectionInfo();
		if (!ConnInfo)
		{
			return;
		}
		FString ETag;
		FString EffectiveURL;
		FTimeValue FetchTime;
		EffectiveURL = ConnInfo->EffectiveURL;
		FetchTime = PlayerSessionServices->GetSynchronizedUTCTime()->MapToSyncTime(ConnInfo->RequestStartTime);
		for(int32 i=0; i<ConnInfo->ResponseHeaders.Num(); ++i)
		{
			if (ConnInfo->ResponseHeaders[i].Header == "ETag")
			{
				ETag = ConnInfo->ResponseHeaders[i].Value;
			}
		}
		MostRecentMPDUpdateTime = FetchTime;

		// If this was a "304 - Not modified" (RFC 7323) there is no new MPD at the moment.
		if (ConnInfo->StatusInfo.HTTPStatus == 304)
		{
			// It does however extend the validity of the MPD.
			Manifest->GetMPDRoot()->SetFetchTime(FetchTime);

			// The minimum update period still holds.
			FTimeValue mup = Manifest->GetMinimumUpdatePeriod();
			if (mup.IsValid() && mup > FTimeValue::GetZero())
			{
				RequestsLock.Lock();
				NextMPDUpdateTime = MostRecentMPDUpdateTime + mup;
				RequestsLock.Unlock();
			}
		}
		else
		{
			PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistDownloadMessage::Create(ConnInfo, Playlist::EListType::Master, Playlist::ELoadType::Update));

			FString XML;
			LastErrorDetail = GetXMLResponseString(XML, Request);
			if (LastErrorDetail.IsOK())
			{
				TSharedPtrTS<FManifestDASHInternal> NewManifest;
				LastErrorDetail = Builder->BuildFromMPD(NewManifest, XML.GetCharArray().GetData(), EffectiveURL, FetchTime, ETag);
				if (LastErrorDetail.IsOK() || LastErrorDetail.IsTryAgain())
				{
					// Copy over the initial document URL fragments.
					if (NewManifest.IsValid())
					{
						NewManifest->SetURLFragmentComponents(Manifest->GetURLFragmentComponents());
					}

					// Switch over tp the new manifest.
					Manifest = NewManifest;
					// Also update in the external manifest we handed out to the player.
					PlayerManifest->UpdateInternalManifest(Manifest);

					// Check if the new MPD also defines an @minimumUpdatePeriod
					FTimeValue mup = Manifest->GetMinimumUpdatePeriod();
					if (mup.IsValid() && mup > FTimeValue::GetZero())
					{
						if (mup.GetAsMilliseconds() < 1000)
						{
							LogMessage(IInfoLog::ELevel::Warning, FString::Printf(TEXT("MPD@minimumUpdatePeriod is set to a really small value of %lld msec. This could be a performance issue."), (long long int)mup.GetAsMilliseconds()));
						}
						RequestsLock.Lock();
						NextMPDUpdateTime = MostRecentMPDUpdateTime + mup;
						RequestsLock.Unlock();
					}

					PlayerSessionServices->SendMessageToPlayer(IPlaylistReader::PlaylistLoadedMessage::Create(!LastErrorDetail.IsTryAgain() ? LastErrorDetail : FErrorDetail(), ConnInfo, Playlist::EListType::Master, Playlist::ELoadType::Update));
					if (LastErrorDetail.IsOK())
					{
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
				PostError(LastErrorDetail);
			}
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
		LastErrorDetail = ForManifest->ResolveInitialRemoteElementRequest(Request, MoveTemp(XML), bSuccess);

		// Now check if all pending requests have finished
		if (LastErrorDetail.IsOK())
		{
			LastErrorDetail = ForManifest->BuildAfterInitialRemoteElementDownload();
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


