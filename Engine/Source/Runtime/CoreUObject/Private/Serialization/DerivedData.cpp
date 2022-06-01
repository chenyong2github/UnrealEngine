// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/DerivedData.h"

#include "Containers/ArrayView.h"
#include "Hash/xxhash.h"
#include "IO/IoDispatcher.h"
#include "Memory/SharedBuffer.h"
#include "Misc/StringBuilder.h"
#include "Misc/TVariant.h"
#include "String/BytesToHex.h"
#include <atomic>

#if WITH_EDITORONLY_DATA
#include "Compression/CompressedBuffer.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataRequestTypes.h"
#include "DerivedDataSharedString.h"
#include "DerivedDataValue.h"
#include "DerivedDataValueId.h"
#include "IO/IoHash.h"
#include "Memory/CompositeBuffer.h"
#include "UObject/LinkerSave.h"
#endif // WITH_EDITORONLY_DATA

namespace UE::DerivedData::Private { class FIoResponseDispatcher; }

#if WITH_EDITORONLY_DATA
namespace UE::DerivedData::Private
{

struct FCacheKeyWithId
{
	FCacheKey Key;
	FValueId Id;

	inline FCacheKeyWithId(const FCacheKey& InKey, const FValueId& InId)
		: Key(InKey)
		, Id(InId)
	{
	}

	inline bool operator==(const FCacheKeyWithId& Other) const
	{
		return Key == Other.Key && Id == Other.Id;
	}
};

struct FCompositeBufferWithHash
{
	FCompositeBuffer Buffer;
	FIoHash Hash;

	template <typename... ArgTypes>
	inline explicit FCompositeBufferWithHash(ArgTypes&&... Args)
		: Buffer(Forward<ArgTypes>(Args)...)
		, Hash(FIoHash::HashBuffer(Buffer))
	{
	}

	inline bool operator==(const FCompositeBufferWithHash& Other) const
	{
		return Hash == Other.Hash;
	}
};

class FEditorData
{
public:
	template <typename... ArgTypes>
	inline FEditorData(const FSharedString& InName, ArgTypes&&... Args)
		: Name(InName)
		, Data(Forward<ArgTypes>(Args)...)
	{
	}

	const FSharedString& GetName() const { return Name; }

	friend FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const FEditorData& EditorData);

	bool ReferenceEquals(const FEditorData& Other) const;
	uint32 ReferenceHash() const;

	void Serialize(FArchive& Ar, UObject* Owner) const;

	template <typename VisitorType>
	inline void Visit(VisitorType&& Visitor) const
	{
		::Visit(Visitor, Data);
	}

private:
	FSharedString Name;
	TVariant<FCompositeBufferWithHash, FCompressedBuffer, FCacheKeyWithId> Data;
};

FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const FEditorData& EditorData)
{
	struct FVisitor
	{
		inline void operator()(const FCompositeBufferWithHash& BufferWithHash) const
		{
			Builder
				<< TEXTVIEW("Buffer: Size ") << BufferWithHash.Buffer.GetSize()
				<< TEXTVIEW(" Hash ") << BufferWithHash.Hash;
		}

		inline void operator()(const FCompressedBuffer& Buffer) const
		{
			Builder
				<< TEXTVIEW("Buffer: CompressedSize ") << Buffer.GetCompressedSize()
				<< TEXTVIEW(" Size ") << Buffer.GetRawSize()
				<< TEXTVIEW(" Hash ") << Buffer.GetRawHash();
		}

		inline void operator()(const FCacheKeyWithId& CacheKeyWithId) const
		{
			Builder << TEXTVIEW("Cache: Key ") << CacheKeyWithId.Key;
			if (CacheKeyWithId.Id.IsValid())
			{
				Builder << TEXTVIEW(" ID ") << CacheKeyWithId.Id;
			}
		}

		FStringBuilderBase& Builder;
	};

	Visit(FVisitor{Builder}, EditorData.Data);
	if (!EditorData.Name.IsEmpty())
	{
		Builder << TEXTVIEW(" for ") << EditorData.Name;
	}
	return Builder;
}

bool FEditorData::ReferenceEquals(const FEditorData& Other) const
{
	if (Data.GetIndex() != Other.Data.GetIndex())
	{
		return false;
	}

	if (const FCompositeBufferWithHash* BufferWithHash = Data.TryGet<FCompositeBufferWithHash>())
	{
		return Other.Data.Get<FCompositeBufferWithHash>().Hash == BufferWithHash->Hash;
	}

	if (const FCompressedBuffer* Buffer = Data.TryGet<FCompressedBuffer>())
	{
		return Other.Data.Get<FCompressedBuffer>().GetRawHash() == Buffer->GetRawHash();
	}

	if (const FCacheKeyWithId* CacheKeyWithId = Data.TryGet<FCacheKeyWithId>())
	{
		return Other.Data.Get<FCacheKeyWithId>() == *CacheKeyWithId;
	}

	checkNoEntry();
	return false;
}

uint32 FEditorData::ReferenceHash() const
{
	struct FVisitor
	{
		inline void operator()(const FCompositeBufferWithHash& BufferWithHash)
		{
			Hash = GetTypeHash(BufferWithHash.Hash);
		}

		inline void operator()(const FCompressedBuffer& Buffer)
		{
			Hash = GetTypeHash(Buffer.GetRawHash());
		}

		inline void operator()(const FCacheKeyWithId& CacheKeyWithId)
		{
			Hash = HashCombineFast(GetTypeHash(CacheKeyWithId.Key), GetTypeHash(CacheKeyWithId.Id));
		}
		uint32 Hash = 0;
	};

	FVisitor Visitor;
	Visit(Visitor);
	return Visitor.Hash;
}

void FEditorData::Serialize(FArchive& Ar, UObject* Owner) const
{
	checkf(Ar.IsSaving() && Ar.IsCooking(), TEXT("FEditorData for FDerivedData only supports saving to cooked packages."));

	struct FVisitor
	{
		inline explicit FVisitor(FArchive& Ar)
			: Linker(Cast<FLinkerSave>(Ar.GetLinker()))
		{
			checkf(Linker, TEXT("Serializing FDerivedData requires a linker."));
			checkf(Ar.IsCooking(), TEXT("Serializing FDerivedData is only supported for cooked packages."));
		}

		inline void operator()(const FCompositeBufferWithHash& BufferWithHash)
		{
			ChunkId = Linker->AddDerivedData(FValue::Compress(BufferWithHash.Buffer).GetData());
		}

		inline void operator()(const FCompressedBuffer& Buffer)
		{
			ChunkId = Linker->AddDerivedData(Buffer);
		}

		inline void operator()(const FCacheKeyWithId& CacheKeyWithId)
		{
			ChunkId = Linker->AddDerivedData(CacheKeyWithId.Key, CacheKeyWithId.Id);
		}

		FLinkerSave* Linker;
		uint64 ChunkOffset = 0;
		uint64 ChunkSize = 0;
		FIoChunkId ChunkId;
	};

	static_assert(sizeof(FIoChunkId) == 12);
	FVisitor Visitor(Ar);
	Visit(Visitor);

	Ar << Visitor.ChunkOffset;
	Ar << Visitor.ChunkSize;
	Ar.Serialize(&Visitor.ChunkId, sizeof(Visitor.ChunkId));
}

} // UE::DerivedData::Private
#endif // WITH_EDITORONLY_DATA

namespace UE
{

FStringBuilderBase& operator<<(FStringBuilderBase& Builder, const FDerivedData& Data)
{
#if WITH_EDITORONLY_DATA
	using namespace DerivedData::Private;
	if (Data.EditorData)
	{
		return Builder << *Data.EditorData;
	}
#endif

	if (Data)
	{
		Builder << TEXTVIEW("Chunk: ID ");
		String::BytesToHexLower(MakeArrayView(Data.ChunkId), Builder);
		if (Data.ChunkOffset)
		{
			Builder << TEXTVIEW(" / Offset ") << Data.ChunkOffset;
		}
		if (Data.ChunkSize != MAX_uint64)
		{
			Builder << TEXTVIEW(" / Size ") << Data.ChunkSize;
		}
		return Builder;
	}

	return Builder << TEXT("Null");
}

bool FDerivedData::ReferenceEquals(const FDerivedData& Other) const
{
#if WITH_EDITORONLY_DATA
	if (EditorData && Other.EditorData)
	{
		return EditorData->ReferenceEquals(*Other.EditorData);
	}
#endif

	if (ChunkOffset == Other.ChunkOffset && ChunkSize == Other.ChunkSize && MakeArrayView(ChunkId) == Other.ChunkId)
	{
		return true;
	}

	return false;
}

uint32 FDerivedData::ReferenceHash() const
{
#if WITH_EDITORONLY_DATA
	if (EditorData)
	{
		return EditorData->ReferenceHash();
	}
#endif

	FXxHash64Builder Builder;
	Builder.Update(&ChunkOffset, sizeof(ChunkOffset));
	Builder.Update(&ChunkSize, sizeof(ChunkSize));
	Builder.Update(MakeMemoryView(ChunkId));
	return uint32(Builder.Finalize().Hash);
}

void FDerivedData::Serialize(FArchive& Ar, UObject* Owner)
{
	if (!Ar.IsPersistent() || Ar.IsObjectReferenceCollector() || Ar.ShouldSkipBulkData())
	{
		return;
	}

#if WITH_EDITORONLY_DATA
	if (EditorData)
	{
		EditorData->Serialize(Ar, Owner);
		Ar << Flags;
		return;
	}
#endif

	Ar << ChunkOffset;
	Ar << ChunkSize;
	Ar.Serialize(ChunkId, sizeof(ChunkId));
	Ar << Flags;
}

#if WITH_EDITORONLY_DATA

FDerivedData::FDerivedData(const DerivedData::FSharedString& Name, const FSharedBuffer& Data)
	: EditorData(MakePimpl<DerivedData::Private::FEditorData, EPimplPtrMode::DeepCopy>(
		Name, TInPlaceType<DerivedData::Private::FCompositeBufferWithHash>(), Data))
	, Flags(EDerivedDataFlags::Required)
{
}

FDerivedData::FDerivedData(const DerivedData::FSharedString& Name, const FCompositeBuffer& Data)
	: EditorData(MakePimpl<DerivedData::Private::FEditorData, EPimplPtrMode::DeepCopy>(
		Name, TInPlaceType<DerivedData::Private::FCompositeBufferWithHash>(), Data))
	, Flags(EDerivedDataFlags::Required)
{
}

FDerivedData::FDerivedData(const DerivedData::FSharedString& Name, const FCompressedBuffer& Data)
	: EditorData(MakePimpl<DerivedData::Private::FEditorData, EPimplPtrMode::DeepCopy>(
		Name, TInPlaceType<FCompressedBuffer>(), Data))
	, Flags(EDerivedDataFlags::Required)
{
}

FDerivedData::FDerivedData(const DerivedData::FSharedString& Name, const DerivedData::FCacheKey& Key)
	: EditorData(MakePimpl<DerivedData::Private::FEditorData, EPimplPtrMode::DeepCopy>(
		Name, TInPlaceType<DerivedData::Private::FCacheKeyWithId>(), Key, DerivedData::FValueId::Null))
	, Flags(EDerivedDataFlags::Required)
{
}

FDerivedData::FDerivedData(
	const DerivedData::FSharedString& Name,
	const DerivedData::FCacheKey& Key,
	const DerivedData::FValueId& ValueId)
	: EditorData(MakePimpl<DerivedData::Private::FEditorData, EPimplPtrMode::DeepCopy>(
		Name, TInPlaceType<DerivedData::Private::FCacheKeyWithId>(), Key, ValueId))
	, Flags(EDerivedDataFlags::Required)
{
}

const DerivedData::FSharedString& FDerivedData::GetName() const
{
	return EditorData ? EditorData->GetName() : DerivedData::FSharedString::Empty;
}

void FDerivedData::SetFlags(EDerivedDataFlags InFlags)
{
	Flags = InFlags;
}

#endif // WITH_EDITORONLY_DATA

} // UE

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData::Private
{

#if WITH_EDITORONLY_DATA
static inline EDerivedDataIoStatus ConvertToIoStatus(EStatus Status)
{
	switch (Status)
	{
	case EStatus::Ok:
		return EDerivedDataIoStatus::Ok;
	case EStatus::Error:
		return EDerivedDataIoStatus::Error;
	case EStatus::Canceled:
		return EDerivedDataIoStatus::Canceled;
	default:
		return EDerivedDataIoStatus::Unknown;
	}
}

static inline EPriority ConvertToDerivedDataPriority(FDerivedDataIoPriority Priority)
{
	if (Priority == FDerivedDataIoPriority::Blocking())
	{
		return EPriority::Blocking;
	}
	if (FDerivedDataIoPriority::Highest().InterpolateTo(FDerivedDataIoPriority::High(), 0.8f) < Priority)
	{
		return EPriority::Highest;
	}
	if (FDerivedDataIoPriority::High().InterpolateTo(FDerivedDataIoPriority::Normal(), 0.6f) < Priority)
	{
		return EPriority::High;
	}
	if (FDerivedDataIoPriority::Normal().InterpolateTo(FDerivedDataIoPriority::Low(), 0.4f) < Priority)
	{
		return EPriority::Normal;
	}
	if (FDerivedDataIoPriority::Low().InterpolateTo(FDerivedDataIoPriority::Lowest(), 0.2f) < Priority)
	{
		return EPriority::Low;
	}
	else
	{
		return EPriority::Lowest;
	}
}
#endif

static inline int32 ConvertToIoDispatcherPriority(FDerivedDataIoPriority Priority)
{
	static_assert(IoDispatcherPriority_Min == FDerivedDataIoPriority::Lowest().Value);
	static_assert(IoDispatcherPriority_Max == FDerivedDataIoPriority::Blocking().Value);
	static_assert(IoDispatcherPriority_Medium == FDerivedDataIoPriority::Normal().Value);
	return Priority.Value;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

enum class EIoRequestType : uint8
{
	Read,
	Cache,
	Exists,
};

struct FIoChunkState
{
	FIoRequest Request;
	uint64 ChunkOffset = 0;
	uint64 ChunkSize = 0;
	FIoChunkId ChunkId;
	std::atomic<bool> bCanceled = false;
};

struct FIoEditorState
{
#if WITH_EDITORONLY_DATA
	FEditorData EditorData;
	FCacheKey CacheKey;
	FIoHash Hash;

	inline explicit FIoEditorState(const FEditorData& InEditorData)
		: EditorData(InEditorData)
	{
	}
#endif
};

struct FIoRequestState
{
	TVariant<TYPE_OF_NULLPTR, FIoChunkState, FIoEditorState> State;
	FDerivedDataIoOptions Options;
	FSharedBuffer Data;
	uint64 Size = 0;
	EIoRequestType Type = EIoRequestType::Read;
	std::atomic<EDerivedDataIoStatus> Status = EDerivedDataIoStatus::Unknown;

	inline void SetStatus(EDerivedDataIoStatus NewStatus)
	{
		const bool bOk = NewStatus == EDerivedDataIoStatus::Ok;
		Status.store(NewStatus, bOk ? std::memory_order_release : std::memory_order_relaxed);
	}
};

class FIoResponse
{
public:
	static FDerivedDataIoRequest Queue(
		TPimplPtr<FIoResponse>& Self,
		const FDerivedData& Data,
		const FDerivedDataIoOptions& Options,
		EIoRequestType Type);

	static void Dispatch(
		TPimplPtr<FIoResponse>& Self,
		FDerivedDataIoResponse& OutResponse,
		FDerivedDataIoPriority Priority,
		FDerivedDataIoComplete&& OnComplete);

	static inline FIoRequestState* TryGetRequest(const TPimplPtr<FIoResponse>& Self, const FDerivedDataIoRequest Handle)
	{
		return Self && Self->Requests.IsValidIndex(Handle.Index) ? &Self->Requests[Handle.Index] : nullptr;
	}

	~FIoResponse();

	void SetPriority(FDerivedDataIoPriority Priority);
	bool Cancel();
	bool Poll() const;

	EDerivedDataIoStatus GetOverallStatus() const;

private:
	inline void BeginRequest() { RemainingRequests.fetch_add(1, std::memory_order_relaxed); }
	void EndRequest();

#if WITH_EDITORONLY_DATA
	FRequestOwner Owner{EPriority::Normal};
#endif
	TArray<FIoRequestState> Requests;
	std::atomic<uint32> RemainingRequests = 0;
	std::atomic<EDerivedDataIoStatus> OverallStatus = EDerivedDataIoStatus::Unknown;
	FDerivedDataIoComplete ResponseComplete;

	friend FIoResponseDispatcher;
};

class FIoResponseDispatcher
{
public:
	static void Dispatch(FIoResponse& Response, FDerivedDataIoPriority Priority);

private:
	FIoResponseDispatcher() = default;

	void DispatchChunk(FIoResponse& Response, FIoRequestState& Request, FIoChunkState& Chunk);

	static FIoReadOptions MakeIoReadOptions(const FIoChunkState& State, const FDerivedDataIoOptions& Options);
	static void OnIoRequestComplete(FIoResponse& Response, FIoRequestState& Request, TIoStatusOr<FIoBuffer> Buffer);

#if WITH_EDITORONLY_DATA
	void DispatchEditor(FIoResponse& Response, FIoRequestState& Request, FIoEditorState& Editor, int32 RequestIndex);
	static void OnCacheRequestComplete(FIoResponse& Response, FCacheGetChunkResponse&& Chunk);

	TArray<FCacheGetChunkRequest> CacheRequests;
#endif

	FIoBatch Batch;
	FDerivedDataIoPriority Priority;
};

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FDerivedDataIoRequest FIoResponse::Queue(
	TPimplPtr<FIoResponse>& Self,
	const FDerivedData& Data,
	const FDerivedDataIoOptions& Options,
	const EIoRequestType Type)
{
	if (!Self)
	{
		Self = MakePimpl<FIoResponse>();
	}

	const int32 Index = Self->Requests.AddDefaulted();
	FIoRequestState& RequestState = Self->Requests[Index];
	RequestState.Options = Options;
	RequestState.Type = Type;

	FDerivedDataIoRequest Handle;
	Handle.Index = Index;

#if WITH_EDITORONLY_DATA
	if (Data.EditorData)
	{
		RequestState.State.Emplace<FIoEditorState>(*Data.EditorData);
		return Handle;
	}
#endif

	if (Data)
	{
		RequestState.State.Emplace<FIoChunkState>();
		FIoChunkState& ChunkState = RequestState.State.Get<FIoChunkState>();
		ChunkState.ChunkOffset = Data.ChunkOffset;
		ChunkState.ChunkSize = Data.ChunkSize;
		ChunkState.ChunkId.Set(MakeMemoryView(Data.ChunkId));
	}

	return Handle;
}

void FIoResponse::Dispatch(
	TPimplPtr<FIoResponse>& InResponse,
	FDerivedDataIoResponse& OutResponse,
	FDerivedDataIoPriority Priority,
	FDerivedDataIoComplete&& OnComplete)
{
	// An empty batch completes immediately.
	if (!InResponse)
	{
		OutResponse.Reset();
		if (OnComplete)
		{
			Invoke(OnComplete);
		}
	}

	// Assign OutResponse early because OnComplete may reference it.
	FIoResponse& Self = *InResponse;
	Self.ResponseComplete = MoveTemp(OnComplete);
	OutResponse.Response = MoveTemp(InResponse);

	// The Begin/End pair blocks completion until dispatch is complete.
	Self.BeginRequest();
	FIoResponseDispatcher::Dispatch(Self, Priority);
	Self.EndRequest();
}

FIoResponse::~FIoResponse()
{
	verifyf(Cancel(), TEXT("Requests must be complete before the response is destroyed but it has %u remaining."),
		RemainingRequests.load(std::memory_order_relaxed));
}

void FIoResponse::SetPriority(FDerivedDataIoPriority Priority)
{
#if WITH_EDITORONLY_DATA
	Owner.SetPriority(ConvertToDerivedDataPriority(Priority));
#endif

	const int32 IoDispatcherPriority = ConvertToIoDispatcherPriority(Priority);
	for (FIoRequestState& Request : Requests)
	{
		if (FIoChunkState* ChunkState = Request.State.TryGet<FIoChunkState>())
		{
			ChunkState->Request.UpdatePriority(IoDispatcherPriority);
		}
	}
}

bool FIoResponse::Cancel()
{
	if (Poll())
	{
		return true;
	}

#if WITH_EDITORONLY_DATA
	// Request cancellation is synchronous but is expected to be very fast.
	Owner.Cancel();
#endif

	for (FIoRequestState& Request : Requests)
	{
		if (FIoChunkState* ChunkState = Request.State.TryGet<FIoChunkState>())
		{
			// Request cancellation only once because every call wakes the dispatcher.
			if (!ChunkState->bCanceled.exchange(true, std::memory_order_relaxed))
			{
				ChunkState->Request.Cancel();
			}
		}
	}

	return Poll();
}

bool FIoResponse::Poll() const
{
	return OverallStatus.load(std::memory_order_relaxed) != EDerivedDataIoStatus::Unknown;
}

EDerivedDataIoStatus FIoResponse::GetOverallStatus() const
{
	return OverallStatus.load(std::memory_order_relaxed);
}

void FIoResponse::EndRequest()
{
	if (RemainingRequests.fetch_sub(1, std::memory_order_acq_rel) == 1)
	{
		// Calculate the overall status for the response.
		static_assert(EDerivedDataIoStatus::Ok < EDerivedDataIoStatus::Error);
		static_assert(EDerivedDataIoStatus::Error < EDerivedDataIoStatus::Canceled);
		static_assert(EDerivedDataIoStatus::Canceled < EDerivedDataIoStatus::Unknown);
		EDerivedDataIoStatus Status = EDerivedDataIoStatus::Ok;
		for (FIoRequestState& Request : Requests)
		{
			Status = FMath::Max(Status, Request.Status.load(std::memory_order_relaxed));
		}
		OverallStatus.store(Status, std::memory_order_relaxed);

		// Invoke the completion callback, but move it to the stack first because it may delete the response.
		if (FDerivedDataIoComplete OnComplete = MoveTemp(ResponseComplete))
		{
			Invoke(OnComplete);
		}
	}
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void FIoResponseDispatcher::Dispatch(FIoResponse& Response, FDerivedDataIoPriority Priority)
{
#if WITH_EDITORONLY_DATA
	FRequestBarrier Barrier(Response.Owner);
#endif

	FIoResponseDispatcher Dispatcher;
	Dispatcher.Priority = Priority;

	int32 RequestIndex = -1;
	for (FIoRequestState& Request : Response.Requests)
	{
		++RequestIndex;
		if (FIoChunkState* ChunkState = Request.State.TryGet<FIoChunkState>())
		{
			Dispatcher.DispatchChunk(Response, Request, *ChunkState);
		}
	#if WITH_EDITORONLY_DATA
		else if (FIoEditorState* EditorState = Request.State.TryGet<FIoEditorState>())
		{
			Dispatcher.DispatchEditor(Response, Request, *EditorState, RequestIndex);
		}
	#endif
		else
		{
			Request.SetStatus(EDerivedDataIoStatus::Error);
		}
	}

	Dispatcher.Batch.Issue();

#if WITH_EDITORONLY_DATA
	if (!Dispatcher.CacheRequests.IsEmpty())
	{
		GetCache().GetChunks(Dispatcher.CacheRequests, Response.Owner,
			[Response = &Response](FCacheGetChunkResponse&& Chunk)
			{
				OnCacheRequestComplete(*Response, MoveTemp(Chunk));
			});
	}
#endif
}

void FIoResponseDispatcher::DispatchChunk(FIoResponse& Response, FIoRequestState& Request, FIoChunkState& Chunk)
{
	if (Request.Type == EIoRequestType::Read)
	{
		Response.BeginRequest();
		Chunk.Request = Batch.ReadWithCallback(
			Chunk.ChunkId,
			MakeIoReadOptions(Chunk, Request.Options),
			ConvertToIoDispatcherPriority(Priority),
			[Response = &Response, &Request](TIoStatusOr<FIoBuffer> Buffer)
			{
				OnIoRequestComplete(*Response, Request, MoveTemp(Buffer));
			});
	}
	else
	{
		TIoStatusOr<uint64> Size = FIoDispatcher::Get().GetSizeForChunk(Chunk.ChunkId);
		if (Size.IsOk())
		{
			const uint64 TotalSize = Size.ValueOrDie();
			const uint64 RequestOffset = Request.Options.GetOffset();
			const uint64 AvailableSize = RequestOffset <= TotalSize ? TotalSize - RequestOffset : 0;
			Request.Size = FMath::Min(Request.Options.GetSize(), AvailableSize);
			Request.SetStatus(EDerivedDataIoStatus::Ok);
		}
		else
		{
			Request.SetStatus(EDerivedDataIoStatus::Error);
		}
	}
}

FIoReadOptions FIoResponseDispatcher::MakeIoReadOptions(
	const FIoChunkState& State,
	const FDerivedDataIoOptions& Options)
{
	const uint64 LocalOffset = Options.GetOffset();
	const uint64 TotalOffset = State.ChunkOffset + LocalOffset;

	FIoReadOptions ReadOptions;
	ReadOptions.SetTargetVa(Options.GetTarget());

	if (Options.GetSize() == MAX_uint64)
	{
		if (State.ChunkSize == MAX_uint64)
		{
			ReadOptions.SetRange(TotalOffset, MAX_uint64);
		}
		else
		{
			ReadOptions.SetRange(TotalOffset, LocalOffset <= State.ChunkSize ? State.ChunkSize - LocalOffset : 0);
		}
	}
	else
	{
		ReadOptions.SetRange(TotalOffset, Options.GetSize());
	}

	return ReadOptions;
}

void FIoResponseDispatcher::OnIoRequestComplete(
	FIoResponse& Response,
	FIoRequestState& Request,
	TIoStatusOr<FIoBuffer> StatusOrBuffer)
{
	FIoChunkState& Chunk = Request.State.Get<FIoChunkState>();

	if (StatusOrBuffer.IsOk())
	{
		FIoBuffer Data = StatusOrBuffer.ConsumeValueOrDie();
		const uint64 DataSize = Data.GetSize();

		// Return a view of the target when one was provided, otherwise take ownership of the buffer.
		if (Request.Options.GetTarget())
		{
			Request.Data = FSharedBuffer::MakeView(Request.Options.GetTarget(), DataSize);
		}
		else
		{
			Request.Data = FSharedBuffer::TakeOwnership(Data.Release().ConsumeValueOrDie(), DataSize, FMemory::Free);
		}

		Request.Size = DataSize;

		Request.SetStatus(EDerivedDataIoStatus::Ok);
	}
	else
	{
		const bool bCanceled = StatusOrBuffer.Status().GetErrorCode() == EIoErrorCode::Cancelled;
		Request.SetStatus(bCanceled ? EDerivedDataIoStatus::Canceled : EDerivedDataIoStatus::Error);
	}

	Chunk.Request.Release();
	Response.EndRequest();
}

#if WITH_EDITORONLY_DATA

void FIoResponseDispatcher::DispatchEditor(
	FIoResponse& Response,
	FIoRequestState& Request,
	FIoEditorState& Editor,
	int32 RequestIndex)
{
	struct FVisitor
	{
		void operator()(const FCompositeBufferWithHash& BufferWithHash) const
		{
			const uint64 TotalSize = BufferWithHash.Buffer.GetSize();
			Editor->Hash = BufferWithHash.Hash;

			const uint64 RequestOffset = Request->Options.GetOffset();
			const uint64 AvailableSize = RequestOffset <= TotalSize ? TotalSize - RequestOffset : 0;
			const uint64 RequestSize = FMath::Min(Request->Options.GetSize(), AvailableSize);
			Request->Size = RequestSize;

			if (Request->Type == EIoRequestType::Read)
			{
				auto Execute = [Response = Response, Request = Request, &BufferWithHash, RequestSize]
				{
					if (void* Target = Request->Options.GetTarget())
					{
						const FMutableMemoryView TargetView(Target, RequestSize);
						BufferWithHash.Buffer.CopyTo(TargetView, Request->Options.GetOffset());
						Request->Data = FSharedBuffer::MakeView(TargetView);
					}
					else
					{
						Request->Data = BufferWithHash.Buffer.Mid(Request->Options.GetOffset(), RequestSize).ToShared();
					}
					Request->SetStatus(EDerivedDataIoStatus::Ok);
					Response->EndRequest();
				};

				// Execute small copy tasks inline to avoid task overhead.
				Response->BeginRequest();
				if (RequestSize <= 64 * 1024)
				{
					Execute();
				}
				else
				{
					FRequestBarrier Barrier(Response->Owner);
					Response->Owner.LaunchTask(TEXT("DerivedDataCopy"), MoveTemp(Execute));
				}
			}
			else
			{
				Request->SetStatus(EDerivedDataIoStatus::Ok);
			}
		}

		void operator()(const FCompressedBuffer& Buffer) const
		{
			const uint64 TotalSize = Buffer.GetRawSize();
			Editor->Hash = Buffer.GetRawHash();

			const uint64 RequestOffset = Request->Options.GetOffset();
			const uint64 AvailableSize = RequestOffset <= TotalSize ? TotalSize - RequestOffset : 0;
			const uint64 RequestSize = FMath::Min(Request->Options.GetSize(), AvailableSize);
			Request->Size = RequestSize;

			if (Request->Type == EIoRequestType::Read)
			{
				auto Execute = [Response = Response, Request = Request, &Buffer, RequestSize]
				{
					FCompressedBufferReader Reader(Buffer);
					if (void* Target = Request->Options.GetTarget())
					{
						const FMutableMemoryView TargetView(Target, RequestSize);
						if (Reader.TryDecompressTo(TargetView, Request->Options.GetOffset()))
						{
							Request->Data = FSharedBuffer::MakeView(TargetView);
						}
					}
					else
					{
						Request->Data = Reader.Decompress(Request->Options.GetOffset(), RequestSize);
					}
					Request->SetStatus(Request->Data ? EDerivedDataIoStatus::Ok : EDerivedDataIoStatus::Error);
					Response->EndRequest();
				};

				// Execute small decompression tasks inline to avoid task overhead.
				Response->BeginRequest();
				if (RequestSize <= 16 * 1024)
				{
					Execute();
				}
				else
				{
					FRequestBarrier Barrier(Response->Owner);
					Response->Owner.LaunchTask(TEXT("DerivedDataDecompress"), MoveTemp(Execute));
				}
			}
			else
			{
				Request->SetStatus(EDerivedDataIoStatus::Ok);
			}
		}

		void operator()(const FCacheKeyWithId& CacheKeyWithId) const
		{
			FCacheGetChunkRequest& Chunk = Dispatcher->CacheRequests.AddDefaulted_GetRef();
			Chunk.Name = Editor->EditorData.GetName();
			Chunk.Key = CacheKeyWithId.Key;
			Chunk.Id = CacheKeyWithId.Id;
			Chunk.RawOffset = Request->Options.GetOffset();
			Chunk.RawSize = Request->Options.GetSize();
			Chunk.Policy =
				Request->Type == EIoRequestType::Read  ? (ECachePolicy::Default) :
				Request->Type == EIoRequestType::Cache ? (ECachePolicy::Default | ECachePolicy::SkipData) :
				                                         (ECachePolicy::Query   | ECachePolicy::SkipData);
			Chunk.UserData = uint64(RequestIndex);
			Response->BeginRequest();
		}

		FIoResponseDispatcher* Dispatcher;
		FIoResponse* Response;
		FIoRequestState* Request;
		FIoEditorState* Editor;
		int32 RequestIndex;
	};

	FVisitor Visitor{this, &Response, &Request, &Editor, RequestIndex};
	Editor.EditorData.Visit(Visitor);
}

void FIoResponseDispatcher::OnCacheRequestComplete(FIoResponse& Response, FCacheGetChunkResponse&& Chunk)
{
	FIoRequestState& Request = Response.Requests[int32(Chunk.UserData)];
	FIoEditorState& Editor = Request.State.Get<FIoEditorState>();
	Editor.Hash = Chunk.RawHash;
	Request.Size = Chunk.RawSize;
	Request.Data = MoveTemp(Chunk.RawData);
	Request.SetStatus(ConvertToIoStatus(Chunk.Status));
	Response.EndRequest();
}

#endif // WITH_EDITORONLY_DATA

} // UE::DerivedData::Private

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE
{

void FDerivedDataIoResponse::SetPriority(FDerivedDataIoPriority Priority)
{
	if (Response)
	{
		Response->SetPriority(Priority);
	}
}

bool FDerivedDataIoResponse::Cancel()
{
	return Response ? Response->Cancel() : true;
}

bool FDerivedDataIoResponse::Poll() const
{
	return Response ? Response->Poll() : true;
}

EDerivedDataIoStatus FDerivedDataIoResponse::GetOverallStatus() const
{
	return Response ? Response->GetOverallStatus() : EDerivedDataIoStatus::Ok;
}

EDerivedDataIoStatus FDerivedDataIoResponse::GetStatus(FDerivedDataIoRequest Handle) const
{
	using namespace DerivedData::Private;
	if (FIoRequestState* Request = FIoResponse::TryGetRequest(Response, Handle))
	{
		return Request->Status.load(std::memory_order_relaxed);
	}
	return EDerivedDataIoStatus::Error;
}

FSharedBuffer FDerivedDataIoResponse::GetData(FDerivedDataIoRequest Handle) const
{
	using namespace DerivedData::Private;
	if (FIoRequestState* Request = FIoResponse::TryGetRequest(Response, Handle))
	{
		if (Request->Status.load(std::memory_order_acquire) == EDerivedDataIoStatus::Ok)
		{
			return Request->Data;
		}
	}
	return FSharedBuffer();
}

uint64 FDerivedDataIoResponse::GetSize(FDerivedDataIoRequest Handle) const
{
	using namespace DerivedData::Private;
	if (FIoRequestState* Request = FIoResponse::TryGetRequest(Response, Handle))
	{
		if (Request->Status.load(std::memory_order_acquire) == EDerivedDataIoStatus::Ok)
		{
			return Request->Size;
		}
	}
	return 0;
}

#if WITH_EDITORONLY_DATA
const FIoHash* FDerivedDataIoResponse::GetHash(FDerivedDataIoRequest Handle) const
{
	using namespace DerivedData::Private;
	if (FIoRequestState* Request = FIoResponse::TryGetRequest(Response, Handle))
	{
		if (Request->Status.load(std::memory_order_relaxed) == EDerivedDataIoStatus::Ok)
		{
			if (FIoEditorState* EditorState = Request->State.TryGet<FIoEditorState>())
			{
				return &EditorState->Hash;
			}
		}
	}
	return nullptr;
}
#endif // WITH_EDITORONLY_DATA

#if WITH_EDITORONLY_DATA
const DerivedData::FCacheKey* FDerivedDataIoResponse::GetCacheKey(FDerivedDataIoRequest Handle) const
{
	using namespace DerivedData;
	using namespace DerivedData::Private;
	if (FIoRequestState* Request = FIoResponse::TryGetRequest(Response, Handle))
	{
		if (Request->Status.load(std::memory_order_relaxed) == EDerivedDataIoStatus::Ok)
		{
			if (FIoEditorState* EditorState = Request->State.TryGet<FIoEditorState>())
			{
				if (EditorState->CacheKey != FCacheKey::Empty)
				{
					return &EditorState->CacheKey;
				}
			}
		}
	}
	return nullptr;
}
#endif // WITH_EDITORONLY_DATA

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

FDerivedDataIoRequest FDerivedDataIoBatch::Read(const FDerivedData& Data, const FDerivedDataIoOptions& Options)
{
	using namespace DerivedData::Private;
	return FIoResponse::Queue(Response, Data, Options, EIoRequestType::Read);
}

FDerivedDataIoRequest FDerivedDataIoBatch::Cache(const FDerivedData& Data, const FDerivedDataIoOptions& Options)
{
	using namespace DerivedData::Private;
	return FIoResponse::Queue(Response, Data, Options, EIoRequestType::Cache);
}

FDerivedDataIoRequest FDerivedDataIoBatch::Exists(const FDerivedData& Data, const FDerivedDataIoOptions& Options)
{
	using namespace DerivedData::Private;
	return FIoResponse::Queue(Response, Data, Options, EIoRequestType::Exists);
}

void FDerivedDataIoBatch::Dispatch(
	FDerivedDataIoResponse& OutResponse,
	FDerivedDataIoPriority Priority,
	FDerivedDataIoComplete&& OnComplete)
{
	using namespace DerivedData::Private;
	FIoResponse::Dispatch(Response, OutResponse, Priority, MoveTemp(OnComplete));
}

} // UE
