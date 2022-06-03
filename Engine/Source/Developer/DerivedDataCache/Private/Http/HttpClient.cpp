// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpClient.h"

#include "Misc/AsciiSet.h"
#include "Misc/ScopeRWLock.h"
#include "Tasks/Task.h"

#define CURL_NO_OLDIES
#if PLATFORM_MICROSOFT
#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#endif

#if defined(PLATFORM_CURL_INCLUDE)
#include PLATFORM_CURL_INCLUDE
#else
#include "curl/curl.h"
#endif // defined(PLATFORM_CURL_INCLUDE)

#if PLATFORM_MICROSOFT
#include "Microsoft/HideMicrosoftPlatformTypes.h"
#endif

namespace UE
{

namespace Http::Private
{

static constexpr uint32 WaitIntervalMs = 10;

struct FHttpSharedDataInternals
{
	CURLSH* CurlShare;
	CURLM* CurlMulti;
	TDepletableMpscQueue<CURL*> PendingRequestAdditions;
	FRWLock Locks[CURL_LOCK_DATA_LAST];
	bool WriteLocked[CURL_LOCK_DATA_LAST]{};
};

struct FHttpSharedDataStatics
{
	static void LockFn(CURL* Handle, curl_lock_data Data, curl_lock_access Access, void* User)
	{
		FHttpSharedDataInternals* SharedDataInternals = ((FHttpSharedData*)User)->Internals.Get();
		if (Access == CURL_LOCK_ACCESS_SHARED)
		{
			SharedDataInternals->Locks[Data].ReadLock();
		}
		else
		{
			SharedDataInternals->Locks[Data].WriteLock();
			SharedDataInternals->WriteLocked[Data] = true;
		}
	}

	static void UnlockFn(CURL* Handle, curl_lock_data Data, void* User)
	{
		FHttpSharedDataInternals* SharedDataInternals = ((FHttpSharedData*)User)->Internals.Get();
		if (!SharedDataInternals->WriteLocked[Data])
		{
			SharedDataInternals->Locks[Data].ReadUnlock();
		}
		else
		{
			SharedDataInternals->WriteLocked[Data] = false;
			SharedDataInternals->Locks[Data].WriteUnlock();
		}
	}
};

} // Http::Private

void FHttpAccessToken::SetToken(const FStringView Token)
{
	FWriteScopeLock WriteLock(Lock);
	const FAnsiStringView Prefix = ANSITEXTVIEW("Authorization: Bearer ");
	const int32 TokenLen = FPlatformString::ConvertedLength<ANSICHAR>(Token.GetData(), Token.Len());
	Header.Empty(Prefix.Len() + TokenLen);
	Header.Append(Prefix.GetData(), Prefix.Len());
	const int32 TokenIndex = Header.AddUninitialized(TokenLen);
	FPlatformString::Convert(Header.GetData() + TokenIndex, TokenLen, Token.GetData(), Token.Len());
	Serial.fetch_add(1, std::memory_order_relaxed);
}

FAnsiStringBuilderBase& operator<<(FAnsiStringBuilderBase& Builder, const FHttpAccessToken& Token)
{
	FReadScopeLock ReadLock(Token.Lock);
	return Builder.Append(Token.Header);
}

FHttpSharedData::FHttpSharedData()
	: PendingRequestEvent(EEventMode::AutoReset)
{
	Internals = MakePimpl<Http::Private::FHttpSharedDataInternals>();
	Internals->CurlShare = curl_share_init();
	curl_share_setopt(Internals->CurlShare, CURLSHOPT_USERDATA, this);
	curl_share_setopt(Internals->CurlShare, CURLSHOPT_LOCKFUNC, Http::Private::FHttpSharedDataStatics::LockFn);
	curl_share_setopt(Internals->CurlShare, CURLSHOPT_UNLOCKFUNC, Http::Private::FHttpSharedDataStatics::UnlockFn);
	curl_share_setopt(Internals->CurlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
	curl_share_setopt(Internals->CurlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
	Internals->CurlMulti = curl_multi_init();
	curl_multi_setopt(Internals->CurlMulti, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
}

FHttpSharedData::~FHttpSharedData()
{
	bAsyncThreadShutdownRequested.store(true, std::memory_order_relaxed);
	if (AsyncServiceThread.IsJoinable())
	{
		AsyncServiceThread.Join();
	}

	curl_multi_cleanup(Internals->CurlMulti);
	curl_share_cleanup(Internals->CurlShare);
}

void FHttpSharedData::AddRequest(void* Curl)
{
	if (Internals->PendingRequestAdditions.EnqueueAndReturnWasEmpty(static_cast<CURL*>(Curl)))
	{
		PendingRequestEvent.Trigger();
	}

	if (!bAsyncThreadStarting.load(std::memory_order_relaxed) && !bAsyncThreadStarting.exchange(true, std::memory_order_relaxed))
	{
		AsyncServiceThread = FThread(TEXT("HttpCacheStore"), [this] { ProcessAsyncRequests(); }, 64 * 1024, TPri_Normal);
	}
}

void* FHttpSharedData::GetCurlShare() const
{
	return Internals->CurlShare;
}

void FHttpSharedData::ProcessAsyncRequests()
{
	int ActiveTransfers = 0;

	auto ProcessPendingRequests = [this, &ActiveTransfers]
	{
		int CurrentActiveTransfers = -1;

		do
		{
			Internals->PendingRequestAdditions.Deplete([this, &ActiveTransfers](CURL* Curl)
			{
				curl_multi_add_handle(Internals->CurlMulti, Curl);
				++ActiveTransfers;
			});

			curl_multi_perform(Internals->CurlMulti, &CurrentActiveTransfers);

			if (CurrentActiveTransfers == 0 || ActiveTransfers != CurrentActiveTransfers)
			{
				for (;;)
				{
					int MsgsStillInQueue = 0; // may use that to impose some upper limit we may spend in that loop
					CURLMsg* Message = curl_multi_info_read(Internals->CurlMulti, &MsgsStillInQueue);

					if (!Message)
					{
						break;
					}

					// find out which requests have completed
					if (Message->msg == CURLMSG_DONE)
					{
						CURL* CompletedHandle = Message->easy_handle;
						curl_multi_remove_handle(Internals->CurlMulti, CompletedHandle);

						void* PrivateData = nullptr;
						curl_easy_getinfo(CompletedHandle, CURLINFO_PRIVATE, &PrivateData);
						FHttpRequest* CompletedRequest = (FHttpRequest*)PrivateData;

						if (CompletedRequest)
						{
							// It is important that the CompleteAsync call doesn't happen on this thread as it is possible it will block waiting
							// for a free HTTP request, and if that happens on this thread, we can deadlock as no HTTP requests will become 
							// available while this thread is blocked.
							Tasks::Launch(TEXT("FHttpRequest::CompleteAsync"), [CompletedRequest, Result = Message->data.result]() mutable
							{
								CompletedRequest->CompleteAsync(Result);
							});
						}
					}
				}
				ActiveTransfers = CurrentActiveTransfers;
			}

			if (CurrentActiveTransfers > 0)
			{
				curl_multi_wait(Internals->CurlMulti, nullptr, 0, 1, nullptr);
			}
		}
		while (CurrentActiveTransfers > 0);
	};

	do
	{
		ProcessPendingRequests();
		PendingRequestEvent.Wait(100);
	}
	while (!FHttpSharedData::bAsyncThreadShutdownRequested.load(std::memory_order_relaxed));

	// Process last requests before shutdown.  May want these to be aborted instead.
	ProcessPendingRequests();
}

FHttpRequestPool::FHttpRequestPool(
	const FStringView InServiceUrl,
	FStringView InEffectiveServiceUrl,
	const FHttpAccessToken* const InAuthorizationToken,
	FHttpSharedData* const InSharedData,
	const uint32 PoolSize,
	const uint32 InOverflowLimit)
	: ActiveOverflowRequests(0)
	, OverflowLimit(InOverflowLimit)
{
	InEffectiveServiceUrl = FAsciiSet::TrimSuffixWith(InEffectiveServiceUrl, "/");

	Pool.AddUninitialized(PoolSize);
	Requests.AddUninitialized(PoolSize);
	for (int32 Index = 0; Index < Pool.Num(); ++Index)
	{
		Pool[Index].Usage = 0u;
		Pool[Index].Request = new(&Requests[Index]) FHttpRequest(InServiceUrl, InEffectiveServiceUrl, InAuthorizationToken, InSharedData, /*bLogErrors*/ true);
	}

	InitData = MakeUnique<FInitData>(InServiceUrl, InEffectiveServiceUrl, InAuthorizationToken, InSharedData);
}

FHttpRequestPool::~FHttpRequestPool()
{
	check(ActiveOverflowRequests.load() == 0);
	for (const FEntry& Entry : Pool)
	{
		// No requests should be in use by now.
		check(Entry.Usage.load(std::memory_order_acquire) == 0u);
	}
}

FHttpRequest* FHttpRequestPool::GetFreeRequest(bool bUnboundedOverflow)
{
	for (FEntry& Entry : Pool)
	{
		if (!Entry.Usage.load(std::memory_order_relaxed))
		{
			uint8 Expected = 0u;
			if (Entry.Usage.compare_exchange_strong(Expected, 1u))
			{
				Entry.Request->Reset();
				return Entry.Request;
			}
		}
	}
	if (bUnboundedOverflow || (OverflowLimit > 0))
	{
		// The use of two operations here (load, then fetch_add) implies that we can exceed the overflow limit because the combined operation
		// is not atomic.  This is acceptable for our use case.  If we wanted to enforce the hard limit, we could use a loop instead.
		if (bUnboundedOverflow || (ActiveOverflowRequests.load(std::memory_order_relaxed) < OverflowLimit))
		{
			// Create an overflow request (outside of the pre-allocated range of requests)
			ActiveOverflowRequests.fetch_add(1, std::memory_order_relaxed);
			return new FHttpRequest(InitData->ServiceUrl, InitData->EffectiveServiceUrl, InitData->AccessToken, InitData->SharedData, true);
		}
	}
	return nullptr;
}

FHttpRequest* FHttpRequestPool::WaitForFreeRequest(bool bUnboundedOverflow)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(HttpDDC_WaitForConnection);

	FHttpRequest* Request = GetFreeRequest(bUnboundedOverflow);
	if (Request == nullptr)
	{
		// Make it a fair by allowing each thread to register itself in a FIFO
		// so that the first thread to start waiting is the first one to get a request.
		FWaiter* Waiter = new FWaiter(this);
		Waiter->AddRef(); // One ref for the thread that will dequeue
		Waiter->AddRef(); // One ref for us

		Waiters.enqueue(Waiter);

		while (!Waiter->Wait(Http::Private::WaitIntervalMs))
		{
			// While waiting, allow us to check if a race occurred and a request has been freed
			// between the time we checked for free requests and the time we queued ourself as a Waiter.
			if ((Request = GetFreeRequest(bUnboundedOverflow)) != nullptr)
			{
				// We abandon the FWaiter, it will be freed by the next dequeue
				// and if it has a request, it will be queued back to the pool.
				Waiter->Release();
				return Request;
			}
		}

		Request = Waiter->Request.exchange(nullptr);
		Request->Reset();
		Waiter->Release();
	}
	check(Request);
	return Request;
}

void FHttpRequestPool::ReleaseRequestToPool(FHttpRequest* Request)
{
	if ((Request < Requests.GetData()) || (Request >= (Requests.GetData() + Requests.Num())))
	{
		// For overflow requests (outside of the pre-allocated range of requests), just delete it immediately
		delete Request;
		ActiveOverflowRequests.fetch_sub(1, std::memory_order_relaxed);
		return;
	}

	for (FEntry& Entry : Pool)
	{
		if (Entry.Request == Request)
		{
			// If only 1 user is remaining, we can give it to a waiter
			// instead of releasing it back to the pool.
			if (Entry.Usage == 1u)
			{
				if (FWaiter* Waiter = Waiters.dequeue())
				{
					Waiter->Request = Request;
					Waiter->Trigger();
					Waiter->Release();
					return;
				}
			}
			
			Entry.Usage--;
			return;
		}
	}

	checkNoEntry();
}

void FHttpRequestPool::MakeRequestShared(FHttpRequest* Request, uint8 Users)
{
	// Overflow requests (outside of the pre-allocated range of requests), cannot be made shared
	check((Request >= Requests.GetData()) && (Request < (Requests.GetData() + Requests.Num())));

	check(Users != 0);
	for (FEntry& Entry : Pool)
	{
		if (Entry.Request == Request)
		{
			Entry.Usage = Users;
			return;
		}
	}

	checkNoEntry();
}

} // UE
