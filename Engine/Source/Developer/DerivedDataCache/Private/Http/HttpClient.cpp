// Copyright Epic Games, Inc. All Rights Reserved.

#include "HttpClient.h"
#include "Tasks/Task.h"

#define UE_HTTPDDC_BACKEND_WAIT_INTERVAL 0.01f
#define UE_HTTPDDC_BACKEND_WAIT_INTERVAL_MS ((uint32)(UE_HTTPDDC_BACKEND_WAIT_INTERVAL*1000))

namespace UE
{

FString FHttpAccessToken::GetHeader()
{
	Lock.ReadLock();
	FString Header = FString::Printf(TEXT("Authorization: Bearer %s"), *Token);
	Lock.ReadUnlock();
	return Header;
}

void FHttpAccessToken::SetHeader(const TCHAR* InToken)
{
	Lock.WriteLock();
	Token = InToken;
	Serial++;
	Lock.WriteUnlock();
}

uint32 FHttpAccessToken::GetSerial() const
{
	return Serial;
}

FHttpSharedData::FHttpSharedData()
: PendingRequestEvent(EEventMode::AutoReset)
{
	CurlShare = curl_share_init();
	curl_share_setopt(CurlShare, CURLSHOPT_USERDATA, this);
	curl_share_setopt(CurlShare, CURLSHOPT_LOCKFUNC, LockFn);
	curl_share_setopt(CurlShare, CURLSHOPT_UNLOCKFUNC, UnlockFn);
	curl_share_setopt(CurlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
	curl_share_setopt(CurlShare, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
	CurlMulti = curl_multi_init();
	curl_multi_setopt(CurlMulti, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);
}

FHttpSharedData::~FHttpSharedData()
{
	bAsyncThreadShutdownRequested.store(true, std::memory_order_relaxed);
	if (AsyncServiceThread.IsJoinable())
	{
		AsyncServiceThread.Join();
	}

	curl_multi_cleanup(CurlMulti);
	curl_share_cleanup(CurlShare);
}

void FHttpSharedData::AddRequest(CURL* Curl)
{
	if (PendingRequestAdditions.EnqueueAndReturnWasEmpty(Curl))
	{
		PendingRequestEvent.Trigger();
	}

	if (!bAsyncThreadStarting.load(std::memory_order_relaxed) && !bAsyncThreadStarting.exchange(true, std::memory_order_relaxed))
	{
		AsyncServiceThread = FThread(TEXT("HttpCacheStore"), [this] { ProcessAsyncRequests(); }, 64 * 1024, TPri_Normal);
	}
}

void FHttpSharedData::LockFn(CURL* Handle, curl_lock_data Data, curl_lock_access Access, void* User)
{
	FHttpSharedData* SharedData = (FHttpSharedData*)User;
	if (Access == CURL_LOCK_ACCESS_SHARED)
	{
		SharedData->Locks[Data].ReadLock();
	}
	else
	{
		SharedData->Locks[Data].WriteLock();
		SharedData->WriteLocked[Data] = true;
	}
}

void FHttpSharedData::UnlockFn(CURL* Handle, curl_lock_data Data, void* User)
{
	FHttpSharedData* SharedData = (FHttpSharedData*)User;
	if (!SharedData->WriteLocked[Data])
	{
		SharedData->Locks[Data].ReadUnlock();
	}
	else
	{
		SharedData->WriteLocked[Data] = false;
		SharedData->Locks[Data].WriteUnlock();
	}
}

void FHttpSharedData::ProcessAsyncRequests()
{
	int ActiveTransfers = 0;

	auto ProcessPendingRequests = [this, &ActiveTransfers]()
	{
		int CurrentActiveTransfers = -1;

		do
		{
			PendingRequestAdditions.Deplete([this, &ActiveTransfers](CURL* Curl)
				{
					curl_multi_add_handle(CurlMulti, Curl);
					++ActiveTransfers;
				});

			curl_multi_perform(CurlMulti, &CurrentActiveTransfers);

			if (CurrentActiveTransfers == 0 || ActiveTransfers != CurrentActiveTransfers)
			{
				for (;;)
				{
					int MsgsStillInQueue = 0;	// may use that to impose some upper limit we may spend in that loop
					CURLMsg* Message = curl_multi_info_read(CurlMulti, &MsgsStillInQueue);

					if (!Message)
					{
						break;
					}

					// find out which requests have completed
					if (Message->msg == CURLMSG_DONE)
					{
						CURL* CompletedHandle = Message->easy_handle;
						curl_multi_remove_handle(CurlMulti, CompletedHandle);

						void* PrivateData = nullptr;
						curl_easy_getinfo(CompletedHandle, CURLINFO_PRIVATE, &PrivateData);
						FHttpRequest* CompletedRequest = (FHttpRequest*)PrivateData;

						if (CompletedRequest)
						{
							// It is important that the CompleteAsync call doesn't happen on this thread as it is possible it will block waiting
							// for a free HTTP request, and if that happens on this thread, we can deadlock as no HTTP requests will become 
							// available while this thread is blocked.
							UE::Tasks::Launch(TEXT("FHttpRequest::CompleteAsync"), [CompletedRequest, Result = Message->data.result]() mutable
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
				curl_multi_wait(CurlMulti, nullptr, 0, 1, nullptr);
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

FHttpRequestPool::FHttpRequestPool(const TCHAR* InServiceUrl, const TCHAR* InEffectiveServiceUrl, FHttpAccessToken* InAuthorizationToken, FHttpSharedData* InSharedData, uint32 PoolSize, uint32 InOverflowLimit)
: ActiveOverflowRequests(0)
, OverflowLimit(InOverflowLimit)
{
	Pool.AddUninitialized(PoolSize);
	Requests.AddUninitialized(PoolSize);
	for (uint8 i = 0; i < Pool.Num(); ++i)
	{
		Pool[i].Usage = 0u;
		Pool[i].Request = new(&Requests[i]) FHttpRequest(InServiceUrl, InEffectiveServiceUrl, InAuthorizationToken, InSharedData, true);
	}

	InitData = MakeUnique<FInitData>(InServiceUrl, InEffectiveServiceUrl, InAuthorizationToken, InSharedData);
}

FHttpRequestPool::~FHttpRequestPool()
{
	check(ActiveOverflowRequests.load() == 0);
	for (uint8 i = 0; i < Pool.Num(); ++i)
	{
		// No requests should be in use by now.
		check(Pool[i].Usage.load(std::memory_order_acquire) == 0u);
	}
}

FHttpRequest* FHttpRequestPool::GetFreeRequest(bool bUnboundedOverflow)
{
	for (uint8 i = 0; i < Pool.Num(); ++i)
	{
		if (!Pool[i].Usage.load(std::memory_order_relaxed))
		{
			uint8 Expected = 0u;
			if (Pool[i].Usage.compare_exchange_strong(Expected, 1u))
			{
				Pool[i].Request->Reset();
				return Pool[i].Request;
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
			return new FHttpRequest(*InitData->ServiceUrl, *InitData->EffectiveServiceUrl, InitData->AccessToken, InitData->SharedData, true);
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

		while (!Waiter->Wait(UE_HTTPDDC_BACKEND_WAIT_INTERVAL_MS))
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

	for (uint8 i = 0; i < Pool.Num(); ++i)
	{
		if (Pool[i].Request == Request)
		{
			// If only 1 user is remaining, we can give it to a waiter
			// instead of releasing it back to the pool.
			if (Pool[i].Usage == 1u)
			{
				if (FWaiter* Waiter = Waiters.dequeue())
				{
					Waiter->Request = Request;
					Waiter->Trigger();
					Waiter->Release();
					return;
				}
			}
			
			Pool[i].Usage--;
			return;
		}
	}
	check(false);
}

void FHttpRequestPool::MakeRequestShared(FHttpRequest* Request, uint8 Users)
{
	if ((Request < Requests.GetData()) || (Request >= (Requests.GetData() + Requests.Num())))
	{
		// Overflow requests (outside of the pre-allocated range of requests), cannot be made shared
		check(false);
	}

	check(Users != 0);
	for (uint8 i = 0; i < Pool.Num(); ++i)
	{
		if (Pool[i].Request == Request)
		{
			Pool[i].Usage = Users;
			return;
		}
	}
	check(false);
}

} // UE
