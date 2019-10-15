// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreTypes.h"
#include "Containers/UnrealString.h"
#include "Misc/StringView.h"
#include "Misc/StringBuilder.h"
#include "Templates/RefCounting.h"
#include "Templates/UnrealTemplate.h"
#include "Templates/TypeCompatibleBytes.h"
#include "HAL/PlatformAtomics.h"

#if __cplusplus >= 201703L
#	define UE_NODISCARD		[[nodiscard]]
#	define UE_NORETURN		[[noreturn]]
#else
#	define UE_NODISCARD		
#	define UE_NORETURN		
#endif

class FIoRequest;
class FIoDispatcher;
class FIoStoreReader;
class FIoStoreWriter;

class FIoQueueImpl;
class FIoRequestImpl;
class FIoBatchImpl;
class FIoDispatcherImpl;
class FIoStoreReaderImpl;
class FIoStoreWriterImpl;

//////////////////////////////////////////////////////////////////////////
//
// IO Status classes modeled after Google Status / StatusOr
//
// TODO: prevent nullptr value in StatusOr
//

enum class EIoErrorCode
{
	Ok,
	Unknown,
	InvalidCode,
	Cancelled,
	FileOpenFailed,
	FileNotOpen,
	WriteError,
	NotFound,
	CorruptToc,
	UnknownChunkID,
	InvalidParameter
};

class FIoStatus
{
public:
	CORE_API			FIoStatus();
	CORE_API			~FIoStatus();

	CORE_API			FIoStatus(EIoErrorCode Code, const FStringView& InErrorMessage);
	CORE_API			FIoStatus(EIoErrorCode Code);
	CORE_API FIoStatus&	operator=(const FIoStatus& Other);

	CORE_API bool		operator==(const FIoStatus& Other) const;
			 bool		operator!=(const FIoStatus& Other) const { return !operator==(Other); }

	inline bool			IsOk() const { return ErrorCode == EIoErrorCode::Ok; }
	inline bool			IsCompleted() const { return ErrorCode != EIoErrorCode::Unknown; }
	inline EIoErrorCode	GetErrorCode() const { return ErrorCode; }
	CORE_API FString	ToString() const;

	CORE_API static const FIoStatus Ok;
	CORE_API static const FIoStatus Unknown;
	CORE_API static const FIoStatus Invalid;

private:
	static constexpr int32 MaxErrorMessageLength = 256;
	using FErrorMessage = TCHAR[MaxErrorMessageLength];

	EIoErrorCode	ErrorCode = EIoErrorCode::Ok;
	FErrorMessage	ErrorMessage;

	friend class FIoStatusBuilder;
};

/** Helper to make it easier to generate meaningful error messages
    for FIoStatusBuilder.
  */
class FIoStatusBuilder
{
	EIoErrorCode		StatusCode;
	FString				Message;
public:
	CORE_API explicit	FIoStatusBuilder(EIoErrorCode StatusCode);
	CORE_API			FIoStatusBuilder(const FIoStatus& InStatus, FStringView String);
	CORE_API			~FIoStatusBuilder();

	inline operator FIoStatus();

	CORE_API FIoStatusBuilder& operator<<(FStringView String);
};

CORE_API FIoStatusBuilder operator<<(const FIoStatus& Status, FStringView String);

template<typename T>
class TIoStatusOr
{
	template<typename U> friend class TIoStatusOr;

public:
	TIoStatusOr() : StatusValue(FIoStatus::Unknown) { }
	TIoStatusOr(const TIoStatusOr& Other);
	explicit TIoStatusOr(TIoStatusOr&& Other);

	TIoStatusOr(FIoStatus Status);
	TIoStatusOr(const T& Value);
	explicit TIoStatusOr(T&& Value);

	~TIoStatusOr();

	template <typename... ArgTypes>
	explicit TIoStatusOr(ArgTypes&&... Args);

	template<typename U>
	TIoStatusOr(const TIoStatusOr<U>& Other);

	TIoStatusOr<T>& operator=(const TIoStatusOr<T>& Other);
	TIoStatusOr<T>& operator=(TIoStatusOr<T>&& Other);
	TIoStatusOr<T>& operator=(T&& OtherValue);

	template<typename U>
	TIoStatusOr<T>& operator=(const TIoStatusOr<U>& Other);

	const FIoStatus&	Status() const;
	bool				IsOk() const;

	const T&			ValueOrDie();
	T					ConsumeValueOrDie();

	void				Reset();

private:
	FIoStatus				StatusValue;
	TTypeCompatibleBytes<T>	Value;
};

CORE_API void StatusOrCrash(const FIoStatus& Status);

template<typename T>
void TIoStatusOr<T>::Reset()
{
	if (StatusValue.IsOk())
	{
		((T*)&Value)->~T();
	}

	StatusValue = FIoStatus::Unknown;
}

template<typename T>
const T& TIoStatusOr<T>::ValueOrDie()
{
	if (!StatusValue.IsOk())
	{
		StatusOrCrash(StatusValue);
	}

	return *Value.GetTypedPtr();
}

template<typename T>
T TIoStatusOr<T>::ConsumeValueOrDie()
{
	if (!StatusValue.IsOk())
	{
		StatusOrCrash(StatusValue);
	}

	StatusValue = FIoStatus::Unknown;

	return MoveTemp(*Value.GetTypedPtr());
}

template<typename T>
TIoStatusOr<T>::TIoStatusOr(const TIoStatusOr& Other)
{
	StatusValue = Other.StatusValue;
	if (StatusValue.IsOk())
	{
		new(&Value) T(*(const T*)&Other.Value);
	}
}

template<typename T>
TIoStatusOr<T>::TIoStatusOr(TIoStatusOr&& Other)
{
	StatusValue = Other.StatusValue;
	if (StatusValue.IsOk())
	{
		new(&Value) T(MoveTempIfPossible(*(T*)&Other.Value));
	}
}

template<typename T>
TIoStatusOr<T>::TIoStatusOr(FIoStatus Status)
{
	check(!Status.IsOk());
	StatusValue = Status;
}

template<typename T>
TIoStatusOr<T>::TIoStatusOr(const T& InValue)
{
	StatusValue = FIoStatus::Ok;
	new(&Value) T(InValue);
}

template<typename T>
TIoStatusOr<T>::TIoStatusOr(T&& Value)
{
	StatusValue = FIoStatus::Ok;
	new(&Value) T(MoveTempIfPossible(Value));
}

template <typename T>
template <typename... ArgTypes>
TIoStatusOr<T>::TIoStatusOr(ArgTypes&&... Args)
{
	StatusValue = FIoStatus::Ok;
	new(&Value) T(Forward<ArgTypes>(Args)...);
}

template<typename T>
TIoStatusOr<T>::~TIoStatusOr()
{
	Reset();
}

template<typename T>
bool TIoStatusOr<T>::IsOk() const
{
	return StatusValue.IsOk();
}

template<typename T>
const FIoStatus& TIoStatusOr<T>::Status() const
{
	return StatusValue;
}

template<typename T>
TIoStatusOr<T>&
TIoStatusOr<T>::operator=(const TIoStatusOr<T>& Other)
{
	Reset();
	StatusValue = Other.StatusValue;

	if (StatusValue.IsOk())
	{
		new(&Value) T(*(const T*)&Other.Value);
	}

	return *this;
}

template<typename T>
TIoStatusOr<T>&
TIoStatusOr<T>::operator=(TIoStatusOr<T>&& Other)
{
	if (&Other != this)
	{
		Reset();
		StatusValue = Other.StatusValue;
 
		if (StatusValue.IsOk())
		{
			new(&Value) T(MoveTempIfPossible(*(T*)&Other.Value));
		}
	}

	return *this;
}

template<typename T>
TIoStatusOr<T>&
TIoStatusOr<T>::operator=(T&& OtherValue)
{
	Reset();
	
	StatusValue = FIoStatus::Ok;
	new(&Value) T(MoveTempIfPossible(Value));
}

template<typename T>
template<typename U>
TIoStatusOr<T>::TIoStatusOr(const TIoStatusOr<U>& Other)
:	StatusValue(Other.StatusValue)
{
	if (StatusValue.IsOk())
	{
		new(&Value) T(*(const U*)&Other.Value);
	}
}

template<typename T>
template<typename U>
TIoStatusOr<T>& TIoStatusOr<T>::operator=(const TIoStatusOr<U>& Other)
{
	Reset();

	StatusValue = Other.StatusValue;

	if (StatusValue.IsOk())
	{
		new(&Value) T(*(const U*)&Other.Value);
	}

	return *this;
}

/** Reference to buffer data used by I/O dispatcher APIs
  */
class FIoBuffer
{
public:
	enum EAssumeOwnershipTag	{ AssumeOwnership };
	enum ECloneTag				{ Clone };
	enum EWrapTag				{ Wrap };

	CORE_API			FIoBuffer();
	CORE_API explicit	FIoBuffer(uint64 InSize);
	CORE_API			FIoBuffer(const void* Data, uint64 InSize, const FIoBuffer& OuterBuffer);

	CORE_API			FIoBuffer(EAssumeOwnershipTag,	const void* Data, uint64 InSize);
	CORE_API			FIoBuffer(ECloneTag,			const void* Data, uint64 InSize);
	CORE_API			FIoBuffer(EWrapTag,				const void* Data, uint64 InSize);

	// Note: we currently rely on implicit move constructor, thus we do not declare any
	//		 destructor or copy/assignment operators or copy constructors

	inline const uint8*	Data() const			{ return CorePtr->Data(); }
	inline uint8*		Data()					{ return CorePtr->Data(); }
	inline uint64		DataSize() const		{ return CorePtr->DataSize(); }

	inline void			SetSize(uint64 InSize)	{ return CorePtr->SetSize(InSize); }

	inline bool			IsAvailable() const;
	inline bool			IsMemoryOwned() const	{ return CorePtr->IsMemoryOwned(); }

	inline void			EnsureOwned() const		{ if (!CorePtr->IsMemoryOwned()) { MakeOwned(); } }

	CORE_API void		MakeOwned() const;

private:
	/** Core buffer object. For internal use only, used by FIoBuffer

		Contains all state pertaining to a buffer.
	  */
	struct BufCore
	{
					BufCore();
		CORE_API	~BufCore();

		explicit	BufCore(uint64 InSize);
					BufCore(const uint8* InData, uint64 InSize, bool InOwnsMemory);
					BufCore(const uint8* InData, uint64 InSize, const BufCore* InOuter);
					BufCore(ECloneTag, uint8* InData, uint64 InSize);

					BufCore(const BufCore& Rhs) = delete;
		
		BufCore& operator=(const BufCore& Rhs) = delete;

		inline uint8* Data()			{ return DataPtr; }
		inline uint64 DataSize() const	{ return DataSizeLow | (uint64(DataSizeHigh) << 32); }

		//

		void	SetDataAndSize(const uint8* InData, uint64 InSize);
		void	SetSize(uint64 InSize);

		void	MakeOwned();

		inline void SetIsOwned(bool InOwnsMemory)
		{
			if (InOwnsMemory)
			{
				Flags |= OwnsMemory;
			}
			else
			{
				Flags &= ~OwnsMemory;
			}
		}

		inline uint32 AddRef() const
		{
			return uint32(FPlatformAtomics::InterlockedIncrement(&NumRefs));
		}

		inline uint32 Release() const
		{
#if DO_CHECK
			CheckRefCount();
#endif

			const int32 Refs = FPlatformAtomics::InterlockedDecrement(&NumRefs);
			if (Refs == 0)
			{
				delete this;
			}

			return uint32(Refs);
		}

		uint32 GetRefCount() const
		{
			return uint32(NumRefs);
		}

		bool			IsMemoryOwned() const	{ return Flags & OwnsMemory; }

	private:
		CORE_API void				CheckRefCount() const;

		uint8*						DataPtr = nullptr;

		uint32						DataSizeLow = 0;
		mutable int32				NumRefs = 0;

		// Reference-counted outer "core", used for views into other buffer
		//
		// Ultimately this should probably just be an index into a pool
		TRefCountPtr<const BufCore>	OuterCore;

		// TODO: These two could be packed in the MSB of DataPtr on x64
		uint8		DataSizeHigh = 0;	// High 8 bits of size (40 bits total)
		uint8		Flags = 0;

		enum
		{
			OwnsMemory		= 1 << 0,	// Buffer memory is owned by this instance
			ReadOnlyBuffer	= 1 << 1,	// Buffer memory is immutable
			
			FlagsMask		= (1 << 2) - 1
		};

		void EnsureDataIsResident() {}
	};

	// Reference-counted "core"
	//
	// Ultimately this should probably just be an index into a pool
	TRefCountPtr<BufCore>	CorePtr;
	
	friend class FIoBufferManager;
};

class FIoChunkId
{
public:
	friend uint32 GetTypeHash(FIoChunkId InId)
	{
		uint32 Hash = 5381;
		for (int i = 0; i < sizeof Id; ++i)
		{
			Hash = Hash * 33 + InId.Id[i];
		}
		return Hash;
	}

	inline bool operator==(const FIoChunkId& Rhs) const
	{
		return 0 == FMemory::Memcmp(Id, Rhs.Id, sizeof Id);
	}

	CORE_API void GenerateFromData(const void* InData, SIZE_T InDataSize);

	void Set(const void* InIdPtr, SIZE_T InSize)
	{
		check(InSize == sizeof Id);
		FMemory::Memcpy(Id, InIdPtr, sizeof Id);
	}

private:
	uint8	Id[12];
};

//////////////////////////////////////////////////////////////////////////

class FIoReadOptions
{
public:
	FIoReadOptions() = default;
	~FIoReadOptions() = default;

	void SetRange(uint64 Offset, uint32 Size)
	{
		RequestedOffset = Offset;
		RequestedSize	= Size;
	}

	void SetTargetVa(uint64 VaTargetAddress)
	{
		TargetVa = VaTargetAddress;
	}

	void ForGPU()
	{
		Flags |= EFlags::GPUMemory;
	}

private:
	uint64	TargetVa		= 0;
	uint64	RequestedOffset = 0;
	uint32	RequestedSize	= ~uint32(0);
	uint32	Flags			= 0;

	enum EFlags
	{
		GPUMemory = 1 << 0,
	};
};

//////////////////////////////////////////////////////////////////////////

/**
  */
class FIoRequest
{
public:
	FIoRequest() = default;

	FIoRequest(const FIoRequest&) = default;
	FIoRequest& operator=(const FIoRequest&) = default;

	CORE_API bool							IsOk() const;
	CORE_API FIoStatus						Status() const;
	CORE_API FIoBuffer						GetChunk();
	CORE_API const FIoChunkId&				GetChunkId() const;
	CORE_API const TIoStatusOr<FIoBuffer>&	GetResult() const;

private:
	FIoRequestImpl* Impl = nullptr;

	explicit FIoRequest(FIoRequestImpl* InImpl)
	: Impl(InImpl)
	{
	}

	friend class FIoDispatcher;
	friend class FIoDispatcherImpl;
	friend class FIoBatch;
};

/** I/O batch

	This is a primitive used to group I/O requests for synchronization
	purposes
  */
class FIoBatch
{
	friend class FIoDispatcher;

	FIoBatch(FIoDispatcherImpl* InDispatcher, FIoBatchImpl* InImpl);

public:
	CORE_API FIoRequest Read(const FIoChunkId& Chunk, FIoReadOptions Options);

	CORE_API void ForEachRequest(TFunction<bool(FIoRequest&)> Callback);

	CORE_API void Issue();
	CORE_API void Wait();
	CORE_API void Cancel();

private:
	FIoDispatcherImpl*	Dispatcher		= nullptr;
	FIoBatchImpl*		Impl			= nullptr;
	FEvent*				CompletionEvent = nullptr;
};

/** I/O dispatcher
  */
class FIoDispatcher
{
public:
	CORE_API			FIoDispatcher();
	CORE_API virtual	~FIoDispatcher();

	CORE_API void		Mount(FIoStoreReader* IoStore);
	CORE_API void		Unmount(FIoStoreReader* IoStore);

	CORE_API FIoBatch	NewBatch();
	CORE_API void		FreeBatch(FIoBatch Batch);

	FIoDispatcher(const FIoDispatcher&) = default;
	FIoDispatcher& operator=(const FIoDispatcher&) = delete;

private:
	FIoDispatcherImpl* Impl = nullptr;

	friend class FIoRequest;
	friend class FIoBatch;
	friend class FIoQueue;
};

//////////////////////////////////////////////////////////////////////////

/** Helper used to manage creation of I/O store file handles etc 
  */
class FIoStoreEnvironment
{
public:
	CORE_API FIoStoreEnvironment();
	CORE_API ~FIoStoreEnvironment();

	CORE_API void InitializeFileEnvironment(FStringView InRootPath);

	CORE_API const FString& GetRootPath() const { return RootPath; }

private:
	FString			RootPath;
};

class FIoStoreReader : public FRefCountBase
{
public:
	CORE_API FIoStoreReader(FIoStoreEnvironment& Environment);
	CORE_API ~FIoStoreReader();

	/** This will parse the manifests in the environment and
		populate the table of contents. To be useful the IO dispatcher
		needs to have access to the information, which is what
		the I/O dispatcher Mount()/Unmount() calls are for.
	  */
	CORE_API FIoStatus Initialize(FStringView UniqueId);

private:
	FIoStoreReaderImpl* Impl;

	friend class FIoDispatcherImpl;
	friend class FIoStoreImpl;
};

//////////////////////////////////////////////////////////////////////////

class FIoStoreWriter
{
public:
	CORE_API 			FIoStoreWriter(FIoStoreEnvironment& InEnvironment);
	CORE_API virtual	~FIoStoreWriter();

	FIoStoreWriter(const FIoStoreWriter&) = delete;
	FIoStoreWriter& operator=(const FIoStoreWriter&) = delete;

	CORE_API FIoStatus	Initialize();
	CORE_API void		Append(FIoChunkId ChunkId, FIoBuffer Chunk);

	/**
	 * Creates an addressable range in an already mapped Chunk.
	 *
	 * @param OriginalChunkId The FIoChunkId of the original chunk from which you want to create the range
	 * @param Offset The number of bytes into the original chunk that the range should start
	 * @param Length The length of the range in bytes
	 * @param ChunkIdPartialRange The FIoChunkId that will map to the range
	 */
	CORE_API void		MapPartialRange(FIoChunkId OriginalChunkId, uint64 Offset, uint64 Length, FIoChunkId ChunkIdPartialRange);
	CORE_API void		FlushMetadata();

private:
	FIoStoreWriterImpl*		Impl;
};

//////////////////////////////////////////////////////////////////////////

class FIoQueue
{
public:
	using FBatchReadyCallback = TFunction<void()>;

	CORE_API		FIoQueue(FIoDispatcher& IoDispatcher, FBatchReadyCallback BatchReadyCallback);
	CORE_API		~FIoQueue();

					FIoQueue(const FIoQueue&) = delete;
					FIoQueue(FIoQueue&&) = delete;
					FIoQueue& operator=(const FIoQueue&) = delete;
					FIoQueue& operator=(FIoQueue&&) = delete;

	CORE_API void	Enqueue(const FIoChunkId& ChunkId, FIoReadOptions ReadOptions, uint64 UserData, bool bDeferBatch = false);
	CORE_API bool	Dequeue(FIoChunkId& ChunkId, TIoStatusOr<FIoBuffer>& Result, uint64& UserData);
	CORE_API void	IssueBatch();
	CORE_API bool	IsEmpty() const;
private:
	TUniquePtr<FIoQueueImpl> Impl;
};
