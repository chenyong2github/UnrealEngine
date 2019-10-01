// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IO/IoDispatcher.h"
#include "HAL/FileManager.h"
#include "Async/MappedFileHandle.h"
#include "Containers/ChunkedArray.h"
#include "HAL/CriticalSection.h"
#include "Logging/LogMacros.h"
#include "Misc/CString.h"
#include "Misc/EventPool.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/SecureHash.h"
#include "Misc/StringBuilder.h"
#include "Memory/MemoryArena.h"
#include "Misc/LazySingleton.h"

DEFINE_LOG_CATEGORY_STATIC(LogIoDispatch, Log, All);

const TCHAR* GetIoErrorText(EIoErrorCode ErrorCode)
{
	static constexpr const TCHAR* ErrorCodeText[]
	{
		TEXT("OK"),
		TEXT("Unknown Status"),
		TEXT("Invalid Code"),
		TEXT("Cancelled"),
		TEXT("FileOpen Failed"),
		TEXT("File Not Open"),
		TEXT("Write Error"),
		TEXT("Not Found"),
		TEXT("Corrupt Toc"),
	};

	return ErrorCodeText[static_cast<uint32>(ErrorCode)];
}

const FIoStatus FIoStatus::Ok		{ EIoErrorCode::Ok,				TEXT("OK")				};
const FIoStatus FIoStatus::Unknown	{ EIoErrorCode::Unknown,		TEXT("Unknown Status")	};
const FIoStatus FIoStatus::Invalid	{ EIoErrorCode::InvalidCode,	TEXT("Invalid Code")	};

FIoStatus::FIoStatus(EIoErrorCode Code)
:	ErrorCode(Code)
,	ErrorMessage(GetIoErrorText(Code))
{
}

FIoStatus::FIoStatus(EIoErrorCode Code, const FStringView& InErrorMessage)
: ErrorCode(Code)
, ErrorMessage(InErrorMessage.Data(), InErrorMessage.Len())
{
}

FIoStatus& 
FIoStatus::operator=(const FIoStatus& Other)
{
	ErrorCode = Other.ErrorCode;
	ErrorMessage = Other.ErrorMessage;

	return *this;
}

bool		
FIoStatus::operator==(const FIoStatus& Other) const
{
	return ErrorCode == Other.ErrorCode
		&& ErrorMessage == Other.ErrorMessage;
}

FString	
FIoStatus::ToString() const
{
	return ErrorMessage;
}

//////////////////////////////////////////////////////////////////////////

FIoStatusBuilder::FIoStatusBuilder(EIoErrorCode InStatusCode)
:	StatusCode(InStatusCode)
{
}

FIoStatusBuilder::FIoStatusBuilder(const FIoStatus& InStatus, FStringView String)
:	StatusCode(InStatus.ErrorCode)
{
	Message.Append(String.Data(), String.Len());
}

FIoStatusBuilder::~FIoStatusBuilder()
{
}

FIoStatusBuilder::operator FIoStatus()
{
	return FIoStatus(StatusCode, Message);
}

FIoStatusBuilder& 
FIoStatusBuilder::operator<<(FStringView String)
{
	Message.Append(String.Data(), String.Len());

	return *this;
}

FIoStatusBuilder operator<<(const FIoStatus& Status, FStringView String)
{ 
	return FIoStatusBuilder(Status, String);
}

//////////////////////////////////////////////////////////////////////////

void StatusOrCrash(const FIoStatus& Status)
{
	// TODO
}

//////////////////////////////////////////////////////////////////////////

void FIoChunkId::GenerateFromData(const void* InData, SIZE_T InDataSize)
{
	FSHA1 Sha1;
	Sha1.Update(reinterpret_cast<const uint8*>(InData), InDataSize);
	Sha1.Final();
	
	uint8 Sha1Hash[20];
	Sha1.GetHash(Sha1Hash);

	memcpy(Id, Sha1Hash, sizeof Id);
}

//////////////////////////////////////////////////////////////////////////

class FIoBufferManager
{
public:
	TChunkedArray<FIoBuffer::BufCore, 65536> BufCores;

	void AllocBufCore()
	{
		new(BufCores) FIoBuffer::BufCore;
	}
};


//////////////////////////////////////////////////////////////////////////

class FIoBufferPool
{
public:
	FIoBufferPool();
	~FIoBufferPool();

	TIoStatusOr<FIoBuffer>	Alloc(uint64 RequiredCapacity);
	void					Free(FIoBuffer InBuffer);
};

FIoBufferPool::FIoBufferPool()
{
}

FIoBufferPool::~FIoBufferPool()
{
}

TIoStatusOr<FIoBuffer> FIoBufferPool::Alloc(uint64 RequiredCapacity)
{
	return FIoBuffer(RequiredCapacity);
}

void FIoBufferPool::Free(FIoBuffer InBuffer)
{
	auto Destroyer = MoveTemp(InBuffer);
}

//////////////////////////////////////////////////////////////////////////

FIoBuffer::BufCore::BufCore()
{
}

FIoBuffer::BufCore::~BufCore()
{
	if (IsMemoryOwned())
	{
		FMemory::Free(Data());
	}
}

FIoBuffer::BufCore::BufCore(const uint8* InData, uint64 InSize, bool InOwnsMemory)
{
	SetDataAndSize(InData, InSize);

	SetIsOwned(InOwnsMemory);
}

FIoBuffer::BufCore::BufCore(const uint8* InData, uint64 InSize, const BufCore* InOuter)
:	OuterCore(InOuter)
{
	SetDataAndSize(InData, InSize);
}

FIoBuffer::BufCore::BufCore(uint64 InSize)
{
	uint8* NewBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(InSize));

	SetDataAndSize(NewBuffer, InSize);

	SetIsOwned(true);
}

FIoBuffer::BufCore::BufCore(ECloneTag, uint8* InData, uint64 InSize)
:	FIoBuffer::BufCore(InSize)
{
	FMemory::Memcpy(Data(), InData, InSize);
}

void
FIoBuffer::BufCore::CheckRefCount() const
{
	// Verify that Release() is not being called on an object which is already at a zero refcount
	check(NumRefs != 0);
}

void
FIoBuffer::BufCore::SetDataAndSize(const uint8* InData, uint64 InSize)
{
	// This is intentionally not split into SetData and SetSize to enable different storage
	// strategies for flags in the future (in unused pointer bits)

	DataPtr			= const_cast<uint8*>(InData);
	DataSizeLow		= uint32(InSize & 0xffffffffu);
	DataSizeHigh	= (InSize >> 32) & 0xffu;
}

void
FIoBuffer::BufCore::SetSize(uint64 InSize)
{
	SetDataAndSize(Data(), InSize);
}

void
FIoBuffer::BufCore::MakeOwned()
{
	if (IsMemoryOwned())
		return;

	const uint64 BufferSize = DataSize();
	uint8* NewBuffer		= reinterpret_cast<uint8*>(FMemory::Malloc(BufferSize));

	FMemory::Memcpy(NewBuffer, Data(), BufferSize);

	SetDataAndSize(NewBuffer, BufferSize);

	SetIsOwned(true);
}

//////////////////////////////////////////////////////////////////////////

FIoBuffer::FIoBuffer()
:	CorePtr(new BufCore)
{
}

FIoBuffer::FIoBuffer(uint64 InSize)
:	CorePtr(new BufCore(InSize))
{
}

FIoBuffer::FIoBuffer(const void* Data, uint64 InSize, const FIoBuffer& OuterBuffer)
:	CorePtr(new BufCore((uint8*) Data, InSize, OuterBuffer.CorePtr))
{
}

FIoBuffer::FIoBuffer(FIoBuffer::EWrapTag, const void* Data, uint64 InSize)
:	CorePtr(new BufCore((uint8*)Data, InSize, /* ownership */ false))
{
}

FIoBuffer::FIoBuffer(FIoBuffer::EAssumeOwnershipTag, const void* Data, uint64 InSize)
:	CorePtr(new BufCore((uint8*)Data, InSize, /* ownership */ true))
{
}

FIoBuffer::FIoBuffer(FIoBuffer::ECloneTag, const void* Data, uint64 InSize)
:	CorePtr(new BufCore(Clone, (uint8*)Data, InSize))
{
}

void		
FIoBuffer::MakeOwned() const
{
	CorePtr->MakeOwned();
}

//////////////////////////////////////////////////////////////////////////

static const char TocMagicImg[] = "-==--==--==--==-";

struct FIoStoreTocHeader
{
	uint8	TocMagic[16];
	uint32	TocHeaderSize;
	uint32	TocEntryCount;
	uint32	TocEntrySize;	// For sanity checking
	uint32	TocPad[25];

	void MakeMagic()
	{
		FMemory::Memcpy(TocMagic, TocMagicImg, sizeof TocMagic);
	}

	bool CheckMagic()
	{
		return FMemory::Memcmp(TocMagic, TocMagicImg, sizeof TocMagic) == 0;
	}
};

struct FIoStoreTocEntry
{
	// We use 5 bytes for offset and size, this is enough to represent 
	// an offset and size of 1PB
	uint8		OffsetAndLength[5 + 5];

	// TBD: should the chunk ID use content addressing, or names, or a
	//      mix of both?
	FIoChunkId	ChunkId;

	inline uint64 GetOffset() const
	{
		return OffsetAndLength[4]
			| (uint64(OffsetAndLength[3]) << 8)
			| (uint64(OffsetAndLength[2]) << 16)
			| (uint64(OffsetAndLength[1]) << 24)
			| (uint64(OffsetAndLength[0]) << 32)
			;
	}

	inline uint64 GetLength() const
	{
		return OffsetAndLength[9]
			| (uint64(OffsetAndLength[8]) << 8)
			| (uint64(OffsetAndLength[7]) << 16)
			| (uint64(OffsetAndLength[6]) << 24)
			| (uint64(OffsetAndLength[5]) << 32)
			;
	}

	inline void SetOffset(uint64 Offset)
	{
		OffsetAndLength[0] = uint8(Offset >> 32);
		OffsetAndLength[1] = uint8(Offset >> 24);
		OffsetAndLength[2] = uint8(Offset >> 16);
		OffsetAndLength[3] = uint8(Offset >>  8);
		OffsetAndLength[4] = uint8(Offset >>  0);
	}

	inline void SetLength(uint64 Length)
	{
		OffsetAndLength[5] = uint8(Length >> 32);
		OffsetAndLength[6] = uint8(Length >> 24);
		OffsetAndLength[7] = uint8(Length >> 16);
		OffsetAndLength[8] = uint8(Length >> 8);
		OffsetAndLength[9] = uint8(Length >> 0);
	}
};

//////////////////////////////////////////////////////////////////////////

FIoStoreEnvironment::FIoStoreEnvironment()
{
}

FIoStoreEnvironment::~FIoStoreEnvironment()
{
}

void FIoStoreEnvironment::InitializeFileEnvironment(FStringView InRootPath)
{
	RootPath = InRootPath.ToString();
}

//////////////////////////////////////////////////////////////////////////

/** Very simple string class

	The backing memory is 
  */
class FCompactString
{
	typedef TCHAR CharT;

public:
						FCompactString();
	explicit			FCompactString(FStringView InString);
						FCompactString(const FCompactString& InString);
						FCompactString(FCompactString&& InString);
						~FCompactString();

	inline const CharT* operator*() const						{ return Chars; }

	inline FCompactString& operator=(FStringView Rhs)			{ Assign(Rhs.Data(), Rhs.Len()); return *this; }
	inline FCompactString& operator=(const FCompactString& Rhs) { Assign(*Rhs); return *this; }
	inline FCompactString& operator=(FCompactString&& Rhs)		{ /* swap guts */ auto Temp = Chars; Chars = Rhs.Chars; Rhs.Chars = Temp; return *this; }

private:
	void Initialize(const CharT* InChars, SIZE_T InCharCount);
	void Initialize(const CharT* InChars);
	void Assign(const CharT* InChars, SIZE_T InCharCount);
	void Assign(const CharT* InChars);

	TArenaPointer<CharT>	Chars;
};

FCompactString::FCompactString()
{
}

FCompactString::~FCompactString()
{
}

FCompactString::FCompactString(FCompactString&& InString)
{
	Chars = InString.Chars;
	InString.Chars = nullptr;
}

FCompactString::FCompactString(const FCompactString& InString)
{
	Initialize(*InString);
}

FCompactString::FCompactString(FStringView InString)
{
	Initialize(InString.Data(), InString.Len());
}

void 
FCompactString::Initialize(const CharT* InChars, SIZE_T InCharCount)
{
	// The count does not include the NUL terminator
	++InCharCount;

	Chars = reinterpret_cast<CharT*>(FMemory::Malloc(InCharCount * sizeof(CharT)));

	FMemory::Memcpy(Chars, InChars, InCharCount * sizeof(CharT));
}

void
FCompactString::Initialize(const CharT* InChars)
{
	Initialize(InChars, TCString<CharT>::Strlen(InChars));
}

void
FCompactString::Assign(const CharT* InChars, SIZE_T InCharCount)
{
	// The count does not include the NUL terminator
	++InCharCount;

	if (Chars)
	{
		FMemory::Free(Chars);
	}

	Chars = reinterpret_cast<CharT*>(FMemory::Malloc(InCharCount * sizeof(CharT)));

	FMemory::Memcpy(Chars, InChars, InCharCount * sizeof(CharT));
}

void
FCompactString::Assign(const CharT* InChars)
{
	Assign(InChars, TCString<CharT>::Strlen(InChars));
}

//////////////////////////////////////////////////////////////////////////

class FIoStoreReaderImpl
{
public:
	FIoStoreReaderImpl(FIoStoreEnvironment& InEnvironment)
		: Environment(InEnvironment)
	{
	}

	FIoStatus Open(FStringView UniqueId);

	TIoStatusOr<FIoBuffer> Lookup(const FIoChunkId& ChunkId)
	{
		const FIoStoreTocEntry* Entry = Toc.Find(ChunkId);

		if (!Entry)
		{
			return FIoStatus(EIoErrorCode::NotFound);
		}

		return FIoBuffer(FIoBuffer::Wrap, MappedRegion->GetMappedPtr() + Entry->GetOffset(), Entry->GetLength());
	}

private:
	FIoStoreEnvironment&				Environment;
	FCompactString						UniqueId;
	TMap<FIoChunkId, FIoStoreTocEntry>	Toc;
	TUniquePtr<IFileHandle>				ContainerFileHandle;
	TUniquePtr<IMappedFileHandle>		ContainerMappedFileHandle;
	TUniquePtr<IMappedFileRegion>		MappedRegion;
};

FIoStatus FIoStoreReaderImpl::Open(FStringView InUniqueId)
{
	IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();

	UniqueId = InUniqueId;

	const FString& RootPath = Environment.GetRootPath();

	TStringBuilder<256> ContainerFilePath;
	ContainerFilePath.Append(RootPath);
	if (ContainerFilePath.LastChar() != '/')
		ContainerFilePath.Append(TEXT('/'));

	TStringBuilder<256> TocFilePath;
	TocFilePath.Append(ContainerFilePath);
	TocFilePath.Append(TEXT("Container.utoc"));

	TUniquePtr<IFileHandle>	TocFileHandle;
	TocFileHandle.Reset(Ipf.OpenRead(*TocFilePath, /* allowwrite */ false));

	if (!TocFileHandle)
	{
		return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore TOC file '") << *TocFilePath << TEXT("'");
	}

	ContainerFilePath.Append(TEXT("Container.ucas"));
	ContainerFileHandle.Reset(Ipf.OpenRead(*ContainerFilePath, /* allowwrite */ false));
	
	if (!ContainerFileHandle)
	{
		return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *ContainerFilePath << TEXT("'");
	}

	const uint64 ContainerSize = ContainerFileHandle->Size();

	ContainerMappedFileHandle.Reset(Ipf.OpenMapped(*ContainerFilePath));
	MappedRegion.Reset(ContainerMappedFileHandle->MapRegion());

	if (!ContainerMappedFileHandle)
	{
		return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to memory map IoStore container file '") << *ContainerFilePath << TEXT("'");
	}

	// Parse TOC
	//
	// This should ultimately be a read-in-place operation but it looks like this for now

	FIoStoreTocHeader Header;
	TocFileHandle->Read(reinterpret_cast<uint8*>(&Header), sizeof Header);

	if (!Header.CheckMagic())
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC header magic mismatch while reading '") << *TocFilePath << TEXT("'");
	}

	if (Header.TocHeaderSize != sizeof Header)
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC header size mismatch while reading '") << *TocFilePath << TEXT("'");
	}

	if (Header.TocEntrySize != sizeof(FIoStoreTocEntry))
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC entry size mismatch while reading '") << *TocFilePath << TEXT("'");
	}

	uint32 EntryCount = Header.TocEntryCount;

	while(EntryCount--)
	{
		FIoStoreTocEntry Entry;
		bool Success = TocFileHandle->Read(reinterpret_cast<uint8*>(&Entry), sizeof Entry);

		if (!Success)
		{
			return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("failed to read TOC entry while reading '") << *TocFilePath << TEXT("'");
		}

		if ((Entry.GetOffset() + Entry.GetLength()) > ContainerSize)
		{
			// TODO: add details
			return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC entry out of container bounds while reading '") << *TocFilePath << TEXT("'");
		}

		Toc.Add(Entry.ChunkId, Entry);
	}

	if (TocFileHandle->Tell() != TocFileHandle->Size())
	{
		return FIoStatusBuilder(EIoErrorCode::CorruptToc) << TEXT("TOC file contains trailing garbage '") << *TocFilePath << TEXT("'");
	}

	return FIoStatus::Ok;
}

FIoStoreReader::FIoStoreReader(FIoStoreEnvironment& Environment)
:	Impl(new FIoStoreReaderImpl(Environment))
{
}

FIoStoreReader::~FIoStoreReader()
{
	delete Impl;
}

FIoStatus 
FIoStoreReader::Initialize(FStringView UniqueId)
{
	return Impl->Open(UniqueId);
}

//////////////////////////////////////////////////////////////////////////

class FIoStoreWriterImpl
{
public:
	FIoStoreWriterImpl(FIoStoreEnvironment& InEnvironment)
	:	Environment(InEnvironment)
	{
	}

	FIoStatus Open()
	{
		IPlatformFile& Ipf = IPlatformFile::GetPlatformPhysical();

		const FString& RootPath = Environment.GetRootPath();
		Ipf.CreateDirectoryTree(*RootPath);

		TStringBuilder<256> ContainerFilePath;
		ContainerFilePath.Append(RootPath);
		if (ContainerFilePath.LastChar() != '/')
			ContainerFilePath.Append(TEXT('/'));

		TStringBuilder<256> TocFilePath;
		TocFilePath.Append(ContainerFilePath);

		ContainerFilePath.Append(TEXT("Container.ucas"));
		TocFilePath.Append(TEXT("Container.utoc"));

		ContainerFileHandle.Reset(Ipf.OpenWrite(*ContainerFilePath, /* append */ false, /* allowread */ true));

		if (!ContainerFileHandle)
		{ 
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore container file '") << *ContainerFilePath << TEXT("'");
		}

		TocFileHandle.Reset(Ipf.OpenWrite(*TocFilePath, /* append */ false, /* allowread */ true));

		if (!TocFileHandle)
		{
			return FIoStatusBuilder(EIoErrorCode::FileOpenFailed) << TEXT("Failed to open IoStore TOC file '") << *TocFilePath << TEXT("'");
		}

		return FIoStatus::Ok;
	}

	UE_NODISCARD FIoStatus Append(FIoChunkId ChunkId, FIoBuffer Chunk)
	{
		if (!ContainerFileHandle)
		{
			return FIoStatus(EIoErrorCode::FileNotOpen, TEXT("No container file to append to"));
		}

		FIoStoreTocEntry TocEntry;

		TocEntry.SetOffset(ContainerFileHandle->Tell());
		TocEntry.SetLength(Chunk.DataSize());
		TocEntry.ChunkId = ChunkId;

		IsMetadataDirty = true;

		const bool Success = ContainerFileHandle->Write(Chunk.Data(), Chunk.DataSize());

		if (Success)
		{
			Toc.Add(ChunkId, TocEntry);

			return FIoStatus::Ok;
		}
		else
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("Append failed"));
		}
	}

	UE_NODISCARD FIoStatus FlushMetadata()
	{
		TocFileHandle->Seek(0);

		FIoStoreTocHeader TocHeader;
		FMemory::Memset(TocHeader, 0);

		TocHeader.MakeMagic();
		TocHeader.TocHeaderSize = sizeof TocHeader;
		TocHeader.TocEntryCount = Toc.Num();
		TocHeader.TocEntrySize = sizeof(FIoStoreTocEntry);

		const bool Success = TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocHeader), sizeof TocHeader);

		if (!Success)
		{
			return FIoStatus(EIoErrorCode::WriteError, TEXT("TOC write failed"));
		}

		for (auto& _: Toc)
		{
			FIoStoreTocEntry& TocEntry = _.Value;
			
			TocFileHandle->Write(reinterpret_cast<const uint8*>(&TocEntry), sizeof TocEntry);
		}

		return FIoStatus::Ok;
	}

private:
	FIoStoreEnvironment&				Environment;
	TMap<FIoChunkId, FIoStoreTocEntry>	Toc;
	TUniquePtr<IFileHandle>				ContainerFileHandle;
	TUniquePtr<IFileHandle>				TocFileHandle;
	bool								IsMetadataDirty = true;
};

FIoStoreWriter::FIoStoreWriter(FIoStoreEnvironment& InEnvironment)
:	Impl(new FIoStoreWriterImpl(InEnvironment))
{
}

FIoStoreWriter::~FIoStoreWriter()
{
	Impl->FlushMetadata();

	delete Impl;
}

FIoStatus FIoStoreWriter::Initialize()
{
	return Impl->Open();
}

void FIoStoreWriter::Append(FIoChunkId ChunkId, FIoBuffer Chunk)
{
	Impl->Append(ChunkId, Chunk);
}

void FIoStoreWriter::FlushMetadata()
{
	Impl->FlushMetadata();
}

//////////////////////////////////////////////////////////////////////////

class FIoBatchImpl
{
public:
	uint32			FirstRequest = ~uint32(0);
	TAtomic<uint32>	OutstandingRequests { 0 };

	void Reset() 
	{
		// TODO: need to release IO requests back to pool

		FirstRequest		= ~uint32(0);
		OutstandingRequests = 0;
	}
};

class FIoRequestImpl
{
public:
	FIoReadOptions			Options;
	TIoStatusOr<FIoBuffer>	Result;
	uint32					NextRequestId = ~uint32(0);
	FIoChunkId				ChunkId;
};

//////////////////////////////////////////////////////////////////////////

class FIoStoreImpl : public FRefCountBase
{
public:
	void Mount(FIoStoreReader* IoStore)
	{
		FWriteScopeLock _(RwLockIoStores);
		// TODO prevent duplicates?
		IoStores.Add(IoStore);
	}

	void Unmount(FIoStoreReader* IoStore)
	{
		FWriteScopeLock _(RwLockIoStores);
		IoStores.Remove(IoStore);
	}

	TIoStatusOr<FIoBuffer> Resolve(FIoChunkId& ChunkId)
	{
		for (const auto& IoStore : IoStores)
		{
			TIoStatusOr<FIoBuffer> Result = IoStore->Impl->Lookup(ChunkId);

			if (Result.IsOk())
			{
				return Result;
			}
		}

		return FIoStatus(EIoErrorCode::NotFound);
	}

private:
	FRWLock									RwLockIoStores;
	TArray<TRefCountPtr<FIoStoreReader>>	IoStores;
};

//////////////////////////////////////////////////////////////////////////

class FIoWorker : FRefCountBase
{
public:
					FIoWorker(FIoStoreImpl* InIoStore);
	virtual			~FIoWorker();

	virtual void	IssueBatch(FIoBatchImpl& Batch) = 0;

private:
	TRefCountPtr<FIoStoreImpl> IoStore;
};

FIoWorker::FIoWorker(FIoStoreImpl* InIoStore)
:	IoStore(InIoStore)
{
}

FIoWorker::~FIoWorker()
{
}

class FIoWorkerFile : FIoWorker
{
public:
	FIoWorkerFile(FIoStoreImpl* InIoStore)
	:	FIoWorker(InIoStore)
	{
	}

	~FIoWorkerFile()
	{
	}

	virtual void IssueBatch(FIoBatchImpl& Batch) override
	{
	}
};

//////////////////////////////////////////////////////////////////////////

class FIoDispatcherImpl
{
	FIoDispatcher* Owner;

	// Batch allocation - could use something more lightweight but I'm not sure this
	// is a sufficiently high frequency operation to warrant something lock-free

	FRWLock	RwLockBatchId;
	uint32	FirstFreeBatchId = 0;

	// Request allocation - could use something more lightweight but I'm not sure this
	// is a sufficiently high frequency operation to warrant something lock-free

	FRWLock	RwLockRequestId;
	uint32	FirstFreeRequestId = ~0u;

public:
	FIoDispatcherImpl(FIoDispatcher* InOwner)
	:	Owner(InOwner)
	{
		for (int i = 0; i < (MaxBatches - 1); ++i)
		{
			Batches[i].FirstRequest = i + 1;
		}

		Batches[MaxBatches - 1].FirstRequest = ~uint32(0);

		for (int i = 0; i < InitialIoRequests; ++i)
		{
			ReserveNewRequest();
		}

		Batches[MaxBatches - 1].FirstRequest = ~uint32(0);

		IoStore = new FIoStoreImpl;
	}

	static const int	MaxBatches = 65536;
	FIoBatchImpl		Batches[MaxBatches];
	int32				NumBatches = 0;

	static const int	MaxIoRequests = 65536;
	static const int	InitialIoRequests = 1;

	TChunkedArray<FIoRequestImpl, MaxIoRequests> Requests;

	FIoBatchImpl& MapBatch(uint32 BatchId)
	{
		return Batches[BatchId];
	}

	void ReserveNewRequest()
	{
		// NOTE: RwLockRequestId must be held while this is called

		FIoRequestImpl& NewRequest = *new(Requests) FIoRequestImpl;
		NewRequest.NextRequestId = FirstFreeRequestId;
		FirstFreeRequestId = Requests.Num() - 1;
	}

	FIoRequestImpl& AllocRequest(const FIoChunkId& Chunk, FIoReadOptions Options, uint32 BatchId)
	{
		const uint32 RequestId = AllocRequestId();

		FIoRequestImpl& Request = Requests[RequestId];
		FIoBatchImpl& Batch		= MapBatch(BatchId);

		Request.ChunkId			= Chunk;
		Request.Options			= Options;
		Request.NextRequestId	= Batch.FirstRequest;
		Request.Result			= FIoStatus::Unknown;

		Batch.FirstRequest		= RequestId;

		return Request;
	}

	uint32 AllocRequestId()
	{
		FWriteScopeLock _(RwLockRequestId);

		if (FirstFreeRequestId == ~uint32(0))
		{
			ReserveNewRequest();
		}

		const uint32 RequestId	= FirstFreeRequestId;

		FIoRequestImpl& Request	= Requests[RequestId];
		FirstFreeRequestId		= Request.NextRequestId;
		Request.NextRequestId	= ~uint32(0);

		return RequestId;
	}

	uint32 FreeRequestId(const uint32 RequestId)
	{
		FWriteScopeLock _(RwLockRequestId);

		uint32 NextRequestId				= Requests[RequestId].NextRequestId;
		Requests[RequestId].NextRequestId	= FirstFreeRequestId;
		FirstFreeRequestId					= RequestId;

		return NextRequestId;
	}

	uint32 AllocBatchId()
	{
		check(NumBatches < MaxBatches);

		FWriteScopeLock _(RwLockBatchId);

		const uint32 BatchId	= FirstFreeBatchId;
		FIoBatchImpl& Batch		= Batches[BatchId];
		FirstFreeBatchId		= Batch.FirstRequest;
		Batch.FirstRequest		= ~uint32(0);

		NumBatches++;

		return BatchId;
	}

	void FreeBatchId(const uint32 BatchId)
	{
		FIoBatchImpl& Batch = Batches[BatchId];

		uint32 RequestId = Batch.FirstRequest; 
		while (RequestId != ~uint32(0))
		{
			RequestId = FreeRequestId(RequestId);
		}
	
		FWriteScopeLock _(RwLockBatchId);

		Batches[BatchId].FirstRequest	= FirstFreeBatchId;
		FirstFreeBatchId				= BatchId;

		NumBatches--;
		check(NumBatches >= 0);
	}

	template<typename Func>
	void IterateBatch(const uint32 BatchId, Func&& InCallbackFunction)
	{
		FIoBatchImpl& Batch = Batches[BatchId];

		uint32 RequestId = Batch.FirstRequest;

		while (RequestId != ~uint32(0))
		{
			FIoRequestImpl& Request = Requests[RequestId];

			const bool DoContinue = InCallbackFunction(Request);

			if (!DoContinue)
			{
				return;
			}

			RequestId = Request.NextRequestId;
		}
	}

	void IssueBatch(const uint32 BatchId)
	{
		// At this point the batch is immutable and we should start
		// doing the work.

		IterateBatch(BatchId, [this](FIoRequestImpl& Request) {
			TIoStatusOr<FIoBuffer> Resolved = IoStore->Resolve(Request.ChunkId);

			Request.Result = Resolved;

			return true;
		});
	}

	void Mount(FIoStoreReader* IoStoreReader)
	{
		IoStore->Mount(IoStoreReader);
	}

	void Unmount(FIoStoreReader* IoStoreReader)
	{
		IoStore->Unmount(IoStoreReader);
	}

private:
	TRefCountPtr<FIoStoreImpl>	IoStore;
};

//////////////////////////////////////////////////////////////////////////

FIoDispatcher::FIoDispatcher()
:	Impl(new FIoDispatcherImpl(this))
{
}

FIoDispatcher::~FIoDispatcher()
{
}

void		
FIoDispatcher::Mount(FIoStoreReader* IoStore)
{
	Impl->Mount(IoStore);
}

void		
FIoDispatcher::Unmount(FIoStoreReader* IoStore)
{
	Impl->Unmount(IoStore);
}

FIoBatch
FIoDispatcher::NewBatch()
{
	return FIoBatch(Impl, Impl->AllocBatchId());
}

void
FIoDispatcher::FreeBatch(FIoBatch Batch)
{
	Impl->FreeBatchId(Batch.BatchId);
}

//////////////////////////////////////////////////////////////////////////

FIoBatch::FIoBatch(FIoDispatcherImpl* OwningIoDispatcher, uint32 InBatchId)
:	Dispatcher(OwningIoDispatcher)
,	BatchId(InBatchId)
,	CompletionEvent(TLazySingleton<FEventPool<EEventPoolTypes::ManualReset>>::Get().GetEventFromPool())
{
}

FIoRequest
FIoBatch::Read(const FIoChunkId& Chunk, FIoReadOptions Options)
{
	return FIoRequest(Dispatcher->AllocRequest(Chunk, Options, BatchId));
}

void 
FIoBatch::ForEachRequest(TFunction<bool(FIoRequest&)> Callback)
{
	Dispatcher->IterateBatch(BatchId, [&](FIoRequestImpl& InRequest) {
		FIoRequest Request(InRequest);
		return Callback(Request);
	});
}

void 
FIoBatch::Issue()
{
	Dispatcher->IssueBatch(BatchId);
}

void 
FIoBatch::Wait()
{
	unimplemented();
}

void 
FIoBatch::Cancel()
{
	unimplemented();
}

//////////////////////////////////////////////////////////////////////////

bool		
FIoRequest::IsOk() const 
{ 
	return Impl->Result.IsOk(); 
}

FIoStatus	
FIoRequest::Status() const
{ 
	return Impl->Result.Status();
}

FIoBuffer	
FIoRequest::GetChunk()
{
	return Impl->Result.ValueOrDie();
}

const FIoChunkId&
FIoRequest::GetChunkId() const
{
	return Impl->ChunkId;
}
