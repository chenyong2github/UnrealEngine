// Copyright Epic Games, Inc. All Rights Reserved.

#include "OnDemandIoDispatcherBackend.h"
#include "Containers/StringView.h"
#include "EncryptionKeyManager.h"
#include "HAL/Event.h"
#include "HAL/Platform.h"
#include "HAL/PlatformTime.h"
#include "HAL/PreprocessorHelpers.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "IO/IoAllocators.h"
#include "IO/IoCache.h"
#include "IO/IoDispatcher.h"
#include "IO/IoOffsetLength.h"
#include "IO/IoStore.h"
#include "IO/IoStoreOnDemand.h"
#include "IO/IoChunkEncoding.h"
#include "Misc/ScopeLock.h"
#include "Misc/ScopeRWLock.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "Serialization/CompactBinary.h"
#include "Serialization/CompactBinarySerialization.h"
#include "Tasks/Task.h"
#include "Tasks/Pipe.h"
#include <atomic>

#define WITH_HTTP_CLIENT 1
#if WITH_HTTP_CLIENT
#include "CoreHttp/Client.h"
#endif

namespace UE::IO::Private
{

using namespace UE::Tasks;

///////////////////////////////////////////////////////////////////////////////
TRACE_DECLARE_INT_COUNTER(TotalIoRequestCount, TEXT("OnDemandIoBackend/IoRequestCount"));
TRACE_DECLARE_INT_COUNTER(TotalIoRequestFailCount, TEXT("OnDemandIoBackend/IoRequestFailCount"));
TRACE_DECLARE_INT_COUNTER(TotalChunkRequestCount, TEXT("OnDemandIoBackend/ChunkRequestCount"));
TRACE_DECLARE_INT_COUNTER(CacheHitCount, TEXT("OnDemandIoBackend/Cache/HitCount"));
TRACE_DECLARE_INT_COUNTER(CachePutCount, TEXT("OnDemandIoBackend/Cache/PutCount"));
TRACE_DECLARE_INT_COUNTER(CachePutRejectCount, TEXT("OnDemandIoBackend/Cache/RejectCount"));
TRACE_DECLARE_INT_COUNTER(PendingHttpRequestCount, TEXT("OnDemandIoBackend/HTTP/PendingCount"));
TRACE_DECLARE_INT_COUNTER(InflightHttpRequestCount, TEXT("OnDemandIoBackend/HTTP/InflightCount"));
//TRACE_DECLARE_FLOAT_COUNTER(TotalRequestSeconds, TEXT("OnDemandIoBackend/Duration/TotalSeconds"));

///////////////////////////////////////////////////////////////////////////////
FIoHash GetChunkKey(const FIoHash& ChunkHash, const FIoOffsetAndLength& Range)
{
	FIoHashBuilder HashBuilder;
	HashBuilder.Update(ChunkHash.GetBytes(), sizeof(FIoHash::ByteArray));
	HashBuilder.Update(&Range, sizeof(FIoOffsetAndLength));

	return HashBuilder.Finalize();
}

///////////////////////////////////////////////////////////////////////////////
class FHttpClient
{
public:
	FHttpClient(UE::HTTP::FEventLoop& EventLoop);
	void Get(FAnsiStringView Url, FIoReadCallback&& Callback, const TCHAR* DebugName = nullptr);
	void Get(FAnsiStringView Url, const FIoOffsetAndLength& Range, FIoReadCallback&& Callback, const TCHAR* DebugName = nullptr);
	bool Tick();

private:
#if WITH_HTTP_CLIENT
	void Issue(UE::HTTP::FRequest&& Request, FIoReadCallback&& Callback, FAnsiStringView Url = FAnsiStringView(), const TCHAR* DebugName = nullptr);
	UE::HTTP::FEventLoop& EventLoop;
#endif
};

FHttpClient::FHttpClient(UE::HTTP::FEventLoop& Loop)
	: EventLoop(Loop)
{
}

void FHttpClient::Get(FAnsiStringView Url, const FIoOffsetAndLength& Range, FIoReadCallback&& Callback, const TCHAR* DebugName)
{
#if WITH_HTTP_CLIENT
	const uint64 RangeStart = Range.GetOffset();
	const uint64 RangeEnd = Range.GetOffset() + Range.GetLength();
	UE::HTTP::FRequest Request = EventLoop.Get(Url);
	Request.Header(ANSITEXTVIEW("Range"), WriteToAnsiString<64>(ANSITEXTVIEW("bytes="), RangeStart, ANSITEXTVIEW("-"), RangeEnd));
	
	Issue(MoveTemp(Request), MoveTemp(Callback), Url, DebugName);
#endif
}

void FHttpClient::Get(FAnsiStringView Url, FIoReadCallback&& Callback, const TCHAR* DebugName)
{
#if WITH_HTTP_CLIENT
	Issue(EventLoop.Get(Url), MoveTemp(Callback), Url, DebugName);
#endif
}

#if WITH_HTTP_CLIENT
void FHttpClient::Issue(UE::HTTP::FRequest&& Request, FIoReadCallback&& Callback, FAnsiStringView DebugUrl, const TCHAR* DebugName)
{
	using namespace UE::HTTP;
	
	FString Debug = DebugName ? FString(DebugName) : FString();
	auto Sink = [
		Buffer = FIoBuffer(),
		Callback = MoveTemp(Callback),
		Url = FString(DebugUrl),
		StartTime = FPlatformTime::Cycles64(),
		StatusCode = uint32(0),
		DebugName = MoveTemp(Debug)]
		(const FTicketStatus& Status) mutable
		{ 
			if (FTicketStatus::EId::Response == Status.GetId())
			{
				FResponse& Response = Status.GetResponse();
				StatusCode = Response.GetStatusCode();

				if (StatusCode > 199 && StatusCode < 300)
				{
					Response.SetDestination(&Buffer);
				}
				else
				{
					const uint64 Duration = (uint64)FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartTime);
					UE_LOG(LogIoStoreOnDemand, VeryVerbose, TEXT("%s"),
						*WriteToString<256>(TEXT("HTTP GET - "), Url, TEXT(" ("), StatusCode, TEXT(" "), Duration, TEXT("ms)")));
					
					Callback(FIoStatus(EIoErrorCode::ReadError, WriteToString<64>(TEXT("HTTP Error("), Response.GetStatusCode(), TEXT(")"))));
				}
			}
			else if (FTicketStatus::EId::Content == Status.GetId())
			{
				const uint64 Duration = (uint64)FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartTime);
				if (const FIoBuffer& Content = Status.GetContent(); Content.GetSize() > 0)
				{
					UE_LOG(LogIoStoreOnDemand, VeryVerbose, TEXT("%s"),
						*WriteToString<256>(DebugName, TEXT(" GET - "), Url, TEXT(" ("), StatusCode, TEXT(" "), Duration, TEXT("ms "), Content.GetSize(), TEXT(" Bytes)")));

					Callback(Content);
				}
				else
				{
					UE_LOG(LogIoStoreOnDemand, VeryVerbose, TEXT("%s"),
						*WriteToString<256>(TEXT("HTTP GET - "), Url, TEXT(" ("), StatusCode, TEXT(" "), Duration, TEXT("ms)")));
					
					Callback(FIoStatus(EIoErrorCode::ReadError, TEXTVIEW("Invalid Content")));
				}
			}
			else if (FTicketStatus::EId::Error == Status.GetId())
			{
				const uint64 Duration = (uint64)FPlatformTime::ToMilliseconds64(FPlatformTime::Cycles64() - StartTime);
				UE_LOG(LogIoStoreOnDemand, VeryVerbose, TEXT("%s"),
					*WriteToString<256>(TEXT("HTTP GET - "), Url, TEXT(" ("), StatusCode, TEXT(" "), Duration, TEXTVIEW("ms)")));

				Callback(FIoStatus(EIoErrorCode::ReadError, TEXTVIEW("HTTP Error")));
			}
		};

	EventLoop.Send(MoveTemp(Request), MoveTemp(Sink));
}
#endif

bool FHttpClient::Tick()
{
#if WITH_HTTP_CLIENT
	return EventLoop.Tick() != 0;
#else
	return false;
#endif
}

///////////////////////////////////////////////////////////////////////////////
class FOnDemandIoStore
{
public:
	struct FTocEntry
	{
		FIoHash Hash;
		uint64 RawSize = 0;
		uint64 EncodedSize = 0;
		uint32 BlockOffset = ~uint32(0);
		uint32 BlockCount = 0; 
	};

	struct FContainer
	{
		FAES::FAESKey EncryptionKey;
		FString Name;
		FString EncryptionKeyGuid;
		FString ChunksDirectory;
		FName CompressionFormat;
		uint32 BlockSize = 0;

		TMap<FIoChunkId, FTocEntry> TocEntries;
		TArray<uint32> BlockSizes;
	};

	struct FChunkInfo
	{
		const FContainer* Container = nullptr;
		const FTocEntry* Entry = nullptr;

		bool IsValid() const { return Container && Entry; }
		operator bool() const { return IsValid(); }

		TConstArrayView<uint32> GetBlocks() const
		{
			check(Container != nullptr && Entry != nullptr);
			return TConstArrayView<uint32>(Container->BlockSizes.GetData() + Entry->BlockOffset, Entry->BlockCount);
		}
	};
	
	FOnDemandIoStore();
	~FOnDemandIoStore();

	void AddToc(const FString& Endpoint, FOnDemandToc&& Toc);
	TIoStatusOr<uint64> GetChunkSize(const FIoChunkId& ChunkId);
	FChunkInfo GetChunkInfo(const FIoChunkId& ChunkId);

private:
	void OnEncryptionKeyAdded(const FGuid& Id, const FAES::FAESKey& Key);
	void RegisterPendingContainers();

	TChunkedArray<FContainer> Containers;
	TArray<FContainer*> RegisteredContainers;
	TArray<FContainer*> PendingContainers;
	mutable FRWLock Lock;
};

FOnDemandIoStore::FOnDemandIoStore()
{
	FEncryptionKeyManager::Get().OnKeyAdded().AddRaw(this, &FOnDemandIoStore::OnEncryptionKeyAdded);
}

FOnDemandIoStore::~FOnDemandIoStore()
{
	FEncryptionKeyManager::Get().OnKeyAdded().RemoveAll(this);
}

void FOnDemandIoStore::AddToc(const FString& Endpoint, FOnDemandToc&& Toc)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandIoBackend::AddToc);

	{
		FWriteScopeLock _(Lock);

		const FOnDemandTocHeader& Header = Toc.Header;

		for (FOnDemandTocContainerEntry& Container : Toc.Containers)
		{
			FContainer* NewContainer = new(Containers) FContainer();
			NewContainer->Name = Container.ContainerName;
			NewContainer->ChunksDirectory = Header.ChunksDirectory.ToLower();
			NewContainer->CompressionFormat = FName(Header.CompressionFormat);
			NewContainer->BlockSize = Header.BlockSize;
			NewContainer->EncryptionKeyGuid = Container.EncryptionKeyGuid;
			
			NewContainer->TocEntries.Reserve(Container.Entries.Num());
			for (const FOnDemandTocEntry& TocEntry : Container.Entries)
			{
				NewContainer->TocEntries.Add(TocEntry.ChunkId, FTocEntry
				{
					TocEntry.Hash,
					TocEntry.RawSize,
					TocEntry.EncodedSize,
					TocEntry.BlockOffset,
					TocEntry.BlockCount
				});
			}

			NewContainer->BlockSizes = MoveTemp(Container.BlockSizes);

			PendingContainers.Add(NewContainer);
		}
	}

	RegisterPendingContainers();
}

TIoStatusOr<uint64> FOnDemandIoStore::GetChunkSize(const FIoChunkId& ChunkId)
{
	if (FChunkInfo Info = GetChunkInfo(ChunkId))
	{
		return Info.Entry->RawSize;
	}

	return FIoStatus(EIoErrorCode::UnknownChunkID);
}

FOnDemandIoStore::FChunkInfo FOnDemandIoStore::GetChunkInfo(const FIoChunkId& ChunkId)
{
	FReadScopeLock _(Lock);

	for (const FContainer* Container : RegisteredContainers)
	{
		if (const FTocEntry* Entry = Container->TocEntries.Find(ChunkId))
		{
			return FChunkInfo{Container, Entry};
		}
	}

	return {};
}

void FOnDemandIoStore::OnEncryptionKeyAdded(const FGuid& Id, const FAES::FAESKey& Key)
{
	RegisterPendingContainers();
}

void FOnDemandIoStore::RegisterPendingContainers()
{
	FReadScopeLock _(Lock);

	for (auto It = PendingContainers.CreateIterator(); It; ++It)
	{
		FContainer* Container = *It;
		if (Container->EncryptionKeyGuid.IsEmpty())
		{
			UE_LOG(LogIoStoreOnDemand, Log, TEXT("Mounting container '%s'"), *Container->Name);
			RegisteredContainers.Add(Container);
			It.RemoveCurrent();
		}
		else
		{
			FGuid KeyGuid;
			ensure(FGuid::Parse(Container->EncryptionKeyGuid, KeyGuid));
			if (const FAES::FAESKey* Key = FEncryptionKeyManager::Get().GetKey(KeyGuid))
			{
				UE_LOG(LogIoStoreOnDemand, Log, TEXT("Mounting container '%s'"), *Container->Name);
				Container->EncryptionKey = *Key;
				RegisteredContainers.Add(Container);
				It.RemoveCurrent();
			}
			else
			{
				UE_LOG(LogIoStoreOnDemand, Log, TEXT("Defeering container '%s', encryption key '%s' not available"), *Container->Name, *Container->EncryptionKeyGuid);
			}
		}
	}
}

///////////////////////////////////////////////////////////////////////////////
class FIoRequestQueue
{
public:
	void Enqueue(FIoRequestImpl* Request)
	{
		check(Request->NextRequest == nullptr);
		FScopeLock _(&CriticalSection);

		if (Tail)
		{
			Tail->NextRequest = Request;
		}
		else
		{
			check(Head == nullptr);
			Head = Request;	
		}

		Tail = Request;
	}

	FIoRequestImpl* Dequeue()
	{
		FScopeLock _(&CriticalSection);

		FIoRequestImpl* Requests = Head;
		Head = Tail = nullptr;

		return Requests;
	}

private:
	FCriticalSection CriticalSection;
	FIoRequestImpl* Head = nullptr;
	FIoRequestImpl* Tail = nullptr;
};

///////////////////////////////////////////////////////////////////////////////
struct FChunkRequestParams
{
	static FChunkRequestParams Create(FIoRequestImpl* Request, FOnDemandIoStore::FChunkInfo ChunkInfo)
	{
		check(Request);
		check(Request->NextRequest == nullptr);
		const uint64 RawSize = FMath::Min(Request->Options.GetSize(), ChunkInfo.Entry->RawSize);
		
		const FIoOffsetAndLength ChunkRange = FIoChunkEncoding::GetChunkRange(
			ChunkInfo.Entry->RawSize,
			ChunkInfo.Container->BlockSize,
			ChunkInfo.GetBlocks(),
			Request->Options.GetOffset(),
			RawSize).ConsumeValueOrDie();

		return FChunkRequestParams{GetChunkKey(ChunkInfo.Entry->Hash, ChunkRange), ChunkRange, ChunkInfo};
	}

	void GetUrl(FAnsiStringBuilderBase& Url) const
	{
		const FString HashString = LexToString(ChunkInfo.Entry->Hash);
		Url << ChunkInfo.Container->ChunksDirectory << "/" << HashString.Left(2) << "/" << HashString << ANSITEXTVIEW(".iochunk");
	}

	FIoChunkDecodingParams GetDecodingParams() const
	{
		FIoChunkDecodingParams Params;
		Params.EncryptionKey = MakeMemoryView(ChunkInfo.Container->EncryptionKey.Key, FAES::FAESKey::KeySize);
		Params.CompressionFormat = ChunkInfo.Container->CompressionFormat;
		Params.BlockSize = ChunkInfo.Container->BlockSize;
		Params.TotalRawSize = ChunkInfo.Entry->RawSize;
		Params.EncodedBlockSize = ChunkInfo.GetBlocks(); 
		Params.EncodedOffset = ChunkRange.GetOffset();

		return Params;
	}

	FIoHash ChunkKey;
	FIoOffsetAndLength ChunkRange;
	FOnDemandIoStore::FChunkInfo ChunkInfo;
};

///////////////////////////////////////////////////////////////////////////////
struct FChunkRequest
{
	explicit FChunkRequest(FIoRequestImpl* Request, const FChunkRequestParams& RequestParams)
		: Params(RequestParams)
		, RequestHead(Request)
		, RequestTail(Request)
		, StartTime(FPlatformTime::Cycles64())
		, RequestCount(1)
		, bCached(false)
	{
		check(Request);
	}

	void AddDispatcherRequest(FIoRequestImpl* Request)
	{
		check(RequestHead && RequestTail);
		check(Request && !Request->NextRequest);
		RequestTail->NextRequest = Request;
		RequestTail = Request;
		RequestCount++;
	}

	uint32 RemoveDispatcherRequest(FIoRequestImpl* Request)
	{
		check(Request != nullptr);
		check(RequestCount > 0);

		if (RequestHead == Request)
		{
			RequestHead = Request->NextRequest; 
			if (RequestTail == Request)
			{
				check(RequestHead == nullptr);
				RequestTail = nullptr;
			}
		}
		else
		{
			FIoRequestImpl* It = RequestHead;
			while (It->NextRequest != Request)
			{
				It = It->NextRequest;
			}
			check(It->NextRequest == Request);
			It->NextRequest = It->NextRequest->NextRequest;
		}

		Request->NextRequest = nullptr;
		RequestCount--;

		return RequestCount;
	}

	FIoRequestImpl* DeqeueDispatcherRequests()
	{
		FIoRequestImpl* Head = RequestHead;
		RequestHead = RequestTail = nullptr;
		RequestCount = 0;

		return Head;
	}

	double DurationInSeconds() const
	{
		return FPlatformTime::ToSeconds64(FPlatformTime::Cycles64() - StartTime);
	}

	FChunkRequestParams Params;
	FIoRequestImpl* RequestHead;
	FIoRequestImpl* RequestTail;
	FIoBuffer Chunk;
	TTask<TIoStatusOr<FIoBuffer>> CacheTask;
	FTask DecodeTask;
	FIoCancellationToken CancellationToken;
	uint64 StartTime;
	uint16 RequestCount;
	bool bCached;
};

///////////////////////////////////////////////////////////////////////////////
class FOnDemandIoBackend final
	: public UE::IOnDemandIoDispatcherBackend
{
	const uint32 HttpWorkerCount = 4;

	struct FHttpPipe
	{
		explicit FHttpPipe(const TCHAR* InDebugName)
			: DebugName(InDebugName)
			, Pipe(*DebugName)
		{
		}

		FString DebugName;
		UE::Tasks::FPipe Pipe;
		UE::HTTP::FEventLoop HttpLoop;
		FHttpClient Client{HttpLoop};
	};

	struct FBackendData
	{
		static void Attach(FIoRequestImpl* Request, const FIoHash& ChunkKey)
		{
			check(Request->BackendData == nullptr);
			Request->BackendData = new FBackendData{ChunkKey};
		}

		static TUniquePtr<FBackendData> Detach(FIoRequestImpl* Request)
		{
			check(Request->BackendData != nullptr);
			void* BackendData = Request->BackendData;
			Request->BackendData = nullptr;
			return TUniquePtr<FBackendData>(static_cast<FBackendData*>(BackendData));
		}
		
		static FBackendData& Get(FIoRequestImpl* Request)
		{
			check(Request->BackendData != nullptr);
			return *static_cast<FBackendData*>(Request->BackendData);
		}

		FIoHash ChunkKey;
	};

	struct FChunkRequests
	{
		FChunkRequest* Create(FIoRequestImpl* Request, const FChunkRequestParams& Params)
		{
			FScopeLock _(&Mutex);
			
			FBackendData::Attach(Request, Params.ChunkKey);

			if (FChunkRequest** InflightRequest = Inflight.Find(Params.ChunkKey))
			{
				FChunkRequest& ChunkRequest = **InflightRequest;
				check(!ChunkRequest.CancellationToken.IsCancelled());
				ChunkRequest.AddDispatcherRequest(Request);

				return nullptr;
			}

			FChunkRequest* ChunkRequest = Allocator.Construct(Request, Params);
			Inflight.Add(Params.ChunkKey, ChunkRequest);

			return ChunkRequest;
		}

		bool Cancel(FIoRequestImpl* Request)
		{
			FScopeLock _(&Mutex);

			FBackendData& BackendData = FBackendData::Get(Request);
			UE_LOG(LogIoStoreOnDemand, VeryVerbose, TEXT("%s"),
				*WriteToString<256>(TEXT("Cancelling I/O request ChunkId='"), LexToString(Request->ChunkId), TEXT("' ChunkKey='"), BackendData.ChunkKey, TEXT("'")));

			if (FChunkRequest** InflightRequest = Inflight.Find(BackendData.ChunkKey))
			{
				FChunkRequest& ChunkRequest = **InflightRequest;
				const uint32 RemainingCount = ChunkRequest.RemoveDispatcherRequest(Request);
				check(Request->NextRequest == nullptr);

				if (RemainingCount == 0)
				{
					ChunkRequest.CancellationToken.Cancel();
					Inflight.Remove(BackendData.ChunkKey);
				}

				return true;
			}

			return false;
		}

		void Remove(FChunkRequest* Request)
		{
			FScopeLock _(&Mutex);
			Inflight.Remove(Request->Params.ChunkKey);
		}

		void Release(FChunkRequest* Request)
		{
			FScopeLock _(&Mutex);
			check(!Inflight.Contains(Request->Params.ChunkKey));
			Allocator.Destroy(Request);
		}
		
		void RemoveAndRelease(FChunkRequest* Request)
		{
			FScopeLock _(&Mutex);
			Inflight.Remove(Request->Params.ChunkKey);
			Allocator.Destroy(Request);
		}

		TSingleThreadedSlabAllocator<FChunkRequest, 128> Allocator;
		TMap<FIoHash, FChunkRequest*> Inflight;
		FCriticalSection Mutex;
	};
public:

	FOnDemandIoBackend(TSharedPtr<IIoCache> Cache);
	virtual ~FOnDemandIoBackend();

	// I/O dispatcher backend
	virtual void Initialize(TSharedRef<const FIoDispatcherBackendContext> Context) override;
	virtual void Shutdown() override;
	virtual bool Resolve(FIoRequestImpl* Request) override;
	virtual void CancelIoRequest(FIoRequestImpl* Request) override;
	virtual void UpdatePriorityForIoRequest(FIoRequestImpl* Request) override;
	virtual bool DoesChunkExist(const FIoChunkId& ChunkId) const override;
	virtual TIoStatusOr<uint64> GetSizeForChunk(const FIoChunkId& ChunkId) const override;
	virtual FIoRequestImpl* GetCompletedRequests() override;
	virtual TIoStatusOr<FIoMappedRegion> OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options) override;

	// I/O Http backend
	virtual void Mount(const FOnDemandEndpoint& Endpoint) override;

private:
	void CompleteRequest(FChunkRequest* ChunkRequest);

	FHttpPipe& GetHttpPipe()
	{
		const uint32 PipeIndex = CurrentHttpWorker.fetch_add(1, std::memory_order_relaxed) % HttpWorkerCount;
		return *HttpPipes[PipeIndex].Get();
	}

	TSharedPtr<IIoCache> Cache;
	TSharedPtr<const FIoDispatcherBackendContext> BackendContext;
	TUniquePtr<FOnDemandIoStore> IoStore;
	TArray<TUniquePtr<FHttpPipe>> HttpPipes;
	FChunkRequests ChunkRequests;
#if WITH_HTTP_CLIENT
	UE::HTTP::FEventLoop HttpEventLoop;
	TUniquePtr<FHttpClient> HttpClient;
#endif
	FIoRequestQueue CompletedRequests;
	FOnDemandEndpoint CurrentEndpoint;
	std::atomic_bool bStopRequested{false};
	std::atomic_uint32_t CurrentHttpWorker{0};
};

///////////////////////////////////////////////////////////////////////////////
FOnDemandIoBackend::FOnDemandIoBackend(TSharedPtr<IIoCache> InCache)
	: Cache(InCache)
{
	IoStore = MakeUnique<FOnDemandIoStore>();
#if WITH_HTTP_CLIENT
	HttpClient = MakeUnique<FHttpClient>(HttpEventLoop);
#endif

	for (uint32 Idx = 0; Idx < HttpWorkerCount; ++Idx)
	{
		HttpPipes.Add(MakeUnique<FHttpPipe>(*WriteToString<64>(TEXT("HTTP #"), Idx + 1)));
	}
}

FOnDemandIoBackend::~FOnDemandIoBackend()
{
	Shutdown();
}

void FOnDemandIoBackend::Initialize(TSharedRef<const FIoDispatcherBackendContext> Context)
{
	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Initializing HTTP I/O dispatcher backend"));
	BackendContext = Context;
}

void FOnDemandIoBackend::Shutdown()
{
	if (bStopRequested)
	{
		return;
	}

	UE_LOG(LogIoStoreOnDemand, Log, TEXT("Shutting down HTTP I/O dispatcher backend"));

	bStopRequested = true;
	BackendContext.Reset();
}

void FOnDemandIoBackend::CompleteRequest(FChunkRequest* ChunkRequest)
{
	check(ChunkRequest != nullptr);
	const bool bCancelled = ChunkRequest->CancellationToken.IsCancelled();

	if (bCancelled)
	{
		check(ChunkRequest->RequestHead == nullptr);
		check(ChunkRequest->RequestTail == nullptr);
		return ChunkRequests.RemoveAndRelease(ChunkRequest);
	}

	ChunkRequests.Remove(ChunkRequest);
	
	FIoBuffer Chunk = MoveTemp(ChunkRequest->Chunk);
	FIoChunkDecodingParams DecodingParams = ChunkRequest->Params.GetDecodingParams();

	bool bCanCache = Cache.IsValid();
	FIoRequestImpl* NextRequest = ChunkRequest->DeqeueDispatcherRequests();
	while (NextRequest)
	{
		FIoRequestImpl* Request = NextRequest;
		NextRequest = Request->NextRequest;
		Request->NextRequest = nullptr;

		bool bDecoded = false;
		if (Chunk.GetSize() > 0)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandIoBackend::DecodeBlocks);
			const uint64 RawSize = FMath::Min(Request->Options.GetSize(), ChunkRequest->Params.ChunkInfo.Entry->RawSize);
			Request->CreateBuffer(RawSize);
			DecodingParams.RawOffset = Request->Options.GetOffset(); 
			bDecoded = FIoChunkEncoding::Decode(DecodingParams, Chunk.GetView(), Request->GetBuffer().GetMutableView());
		}
		
		if (!bDecoded)
		{
			TRACE_COUNTER_INCREMENT(TotalIoRequestFailCount);
			bCanCache = false;
			Request->SetFailed();
		}

		TRACE_COUNTER_INCREMENT(TotalIoRequestCount);
		CompletedRequests.Enqueue(Request);
		BackendContext->WakeUpDispatcherThreadDelegate.Execute();
	}

	if (bCanCache && !ChunkRequest->bCached && Chunk.GetSize() > 0)
	{
		if (FIoStatus Status = Cache->PutChunk(ChunkRequest->Params.ChunkKey, Chunk.GetView()); Status.IsOk())
		{
			TRACE_COUNTER_INCREMENT(CachePutCount);
		}
		else
		{
			TRACE_COUNTER_INCREMENT(CachePutRejectCount);
		}
	}

	ChunkRequests.Release(ChunkRequest);

	TRACE_COUNTER_INCREMENT(TotalChunkRequestCount);
}

bool FOnDemandIoBackend::Resolve(FIoRequestImpl* Request)
{
	using namespace UE::Tasks;
	
	FOnDemandIoStore::FChunkInfo ChunkInfo = IoStore->GetChunkInfo(Request->ChunkId);
	if (!ChunkInfo.IsValid())
	{
		return false;
	}

	check(HttpPipes.Num());
	FChunkRequestParams RequestParams = FChunkRequestParams::Create(Request, ChunkInfo);
	FChunkRequest* ChunkRequest = ChunkRequests.Create(Request, RequestParams);

	if (ChunkRequest == nullptr)
	{
		// The chunk for the request is already inflight 
		return true;
	}
		
	auto FetchHttp = [this, ChunkRequest]()
	{
		if (ChunkRequest->CacheTask.IsValid())
		{
			if (TIoStatusOr<FIoBuffer> Status = ChunkRequest->CacheTask.GetResult(); Status.IsOk())
			{
				TRACE_COUNTER_INCREMENT(CacheHitCount);
				ChunkRequest->Chunk = Status.ConsumeValueOrDie();
				ChunkRequest->bCached = true;
				return CompleteRequest(ChunkRequest);
			}
		}

		if (ChunkRequest->CancellationToken.IsCancelled())
		{
			return CompleteRequest(ChunkRequest);
		}

		TRACE_COUNTER_INCREMENT(PendingHttpRequestCount);
		FHttpPipe& HttpPipe = GetHttpPipe();
		ChunkRequest->DecodeTask = Launch(TEXT("I/O Decode"), [this, ChunkRequest]()
		{
			TRACE_COUNTER_DECREMENT(InflightHttpRequestCount);
			CompleteRequest(ChunkRequest);
		}, HttpPipe.Pipe.Launch(TEXT("I/O HTTP"), [this, &HttpPipe, ChunkRequest]()
		{
			TRACE_COUNTER_DECREMENT(PendingHttpRequestCount);
			TRACE_COUNTER_INCREMENT(InflightHttpRequestCount);

			if (ChunkRequest->CancellationToken.IsCancelled())
			{
				return;
			}

			TAnsiStringBuilder<256> Url;
			Url << CurrentEndpoint.Url << "/";
			ChunkRequest->Params.GetUrl(Url);

			HttpPipe.Client.Get(Url.ToView(), ChunkRequest->Params.ChunkRange, [ChunkRequest](TIoStatusOr<FIoBuffer> Status)
			{
				if (Status.IsOk())
				{
					ChunkRequest->Chunk = Status.ConsumeValueOrDie();
				}
			}, HttpPipe.Pipe.GetDebugName());

			while (HttpPipe.Client.Tick())
			{
				TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandIoBackend::TickHttpEventLoop);
			}
		}));
	};

	//TODO: Remove if else once 25124120 is merged to FN/Main
	if (Cache.IsValid())
	{
		ChunkRequest->CacheTask = Cache->GetChunk(ChunkRequest->Params.ChunkKey, FIoReadOptions(), &ChunkRequest->CancellationToken);
		Launch(UE_SOURCE_LOCATION, MoveTemp(FetchHttp), ChunkRequest->CacheTask);
	}
	else
	{
		Launch(UE_SOURCE_LOCATION, MoveTemp(FetchHttp));
	}

	return true;
}

void FOnDemandIoBackend::CancelIoRequest(FIoRequestImpl* Request)
{
	if (ChunkRequests.Cancel(Request))
	{
		CompletedRequests.Enqueue(Request);
		BackendContext->WakeUpDispatcherThreadDelegate.Execute();
	}
}

void FOnDemandIoBackend::UpdatePriorityForIoRequest(FIoRequestImpl* Request)
{
}

bool FOnDemandIoBackend::DoesChunkExist(const FIoChunkId& ChunkId) const
{
	TIoStatusOr<uint64> ChunkSize = GetSizeForChunk(ChunkId);
	return ChunkSize.IsOk();
}

TIoStatusOr<uint64> FOnDemandIoBackend::GetSizeForChunk(const FIoChunkId& ChunkId) const
{
	if (IoStore.IsValid())
	{
		return IoStore->GetChunkSize(ChunkId);
	}

	return FIoStatus(EIoErrorCode::UnknownChunkID);
}

FIoRequestImpl* FOnDemandIoBackend::GetCompletedRequests()
{
	FIoRequestImpl* Requests = CompletedRequests.Dequeue();

	for (FIoRequestImpl* It = Requests; It != nullptr; It = It->NextRequest)
	{
		TUniquePtr<FBackendData> BackendData = FBackendData::Detach(It);
		check(It->BackendData == nullptr);
	}

	return Requests;
}

TIoStatusOr<FIoMappedRegion> FOnDemandIoBackend::OpenMapped(const FIoChunkId& ChunkId, const FIoReadOptions& Options)
{
	return FIoStatus::Unknown;
}

void FOnDemandIoBackend::Mount(const FOnDemandEndpoint& Endpoint)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(FOnDemandIoBackend::Mount);

	CurrentEndpoint = Endpoint;

	if (EnumHasAnyFlags(Endpoint.EndpointType, EOnDemandEndpointType::CDN))
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Mounting CDN endpoint, Url='%s', Toc='%s'"), *Endpoint.Url, *Endpoint.TocPath);

		TAnsiStringBuilder<256> Url;
		Url << Endpoint.Url << "/" << Endpoint.TocPath;

#if WITH_HTTP_CLIENT
		HttpClient->Get(Url.ToView(), [this, Url = Endpoint.Url](TIoStatusOr<FIoBuffer> Status)
		{
			if (Status.IsOk())
			{
				FIoBuffer Buffer = Status.ConsumeValueOrDie();
				FOnDemandToc Toc;
				if (LoadFromCompactBinary(FCbFieldView(Buffer.GetData()), Toc))
				{
					IoStore->AddToc(Url, MoveTemp(Toc));
				}
				else
				{
					UE_LOG(LogIoStoreOnDemand, Error, TEXT("Failed loading ondemand TOC from compact binary"));
				}
			}
			else
			{
				UE_LOG(LogIoStoreOnDemand, Error, TEXT("Mounting CDN endpoint failed, reason '%s'"), *Status.Status().ToString());
			}
		});

		while (HttpClient->Tick());
#endif
	}
	else
	{
		UE_LOG(LogIoStoreOnDemand, Log, TEXT("Mounting ZEN endpoint, Url='%s'"), *Endpoint.Url);
	}
}

} // namespace UE::IO::Private

namespace UE
{

TSharedPtr<IOnDemandIoDispatcherBackend> MakeOnDemandIoDispatcherBackend(TSharedPtr<IIoCache> Cache)
{
	return MakeShareable<IOnDemandIoDispatcherBackend>(new UE::IO::Private::FOnDemandIoBackend(Cache));
}

} // namespace UE
