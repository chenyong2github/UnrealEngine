// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "IO/IoDispatcher.h"
#include "Async/MappedFileHandle.h"
#include "Containers/ChunkedArray.h"
#include "HAL/CriticalSection.h"
#include "HAL/FileManager.h"
#include "HAL/Runnable.h"
#include "HAL/RunnableThread.h"
#include "Logging/LogMacros.h"
#include "Memory/MemoryArena.h"
#include "Misc/CString.h"
#include "Misc/EventPool.h"
#include "Misc/ScopeRWLock.h"
#include "Misc/ScopeLock.h"
#include "Misc/SecureHash.h"
#include "Misc/StringBuilder.h"
#include "Misc/LazySingleton.h"
#include "Misc/CoreDelegates.h"
#include "Trace/Trace.h"

#define IODISPATCHER_TRACE_ENABLED !UE_BUILD_SHIPPING

DEFINE_LOG_CATEGORY_STATIC(LogIoDispatch, Log, All);

//////////////////////////////////////////////////////////////////////////

UE_TRACE_EVENT_BEGIN(IoDispatcher, BatchIssued, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, BatchId)
UE_TRACE_EVENT_END()

UE_TRACE_EVENT_BEGIN(IoDispatcher, BatchResolved, Always)
	UE_TRACE_EVENT_FIELD(uint64, Cycle)
	UE_TRACE_EVENT_FIELD(uint64, BatchId)
	UE_TRACE_EVENT_FIELD(uint64, TotalSize)
UE_TRACE_EVENT_END()

template <typename T, uint32 BlockSize = 128>
class TBlockAllocator
{
public:
	~TBlockAllocator()
	{
		FreeBlocks();
	}

	FORCEINLINE T* Alloc()
	{
		FScopeLock _(&CriticalSection);

		if (!NextFree)
		{
			//TODO: Virtual alloc
			FBlock* Block = new FBlock;
			
			for (int32 ElementIndex = 0; ElementIndex < BlockSize; ++ElementIndex)
			{
				FElement* Element	= &Block->Elements[ElementIndex];
				Element->Next		= NextFree;
				NextFree			= Element;
			}

			Block->Next	= Blocks;
			Blocks		= Block;
		}

		FElement* Element	= NextFree;
		NextFree			= Element->Next;
		
		++NumElements;

		return Element->Buffer.GetTypedPtr();
	}

	FORCEINLINE void Free(T* Ptr)
	{
		FScopeLock _(&CriticalSection);

		FElement* Element	= reinterpret_cast<FElement*>(Ptr);
		Element->Next		= NextFree;
		NextFree			= Element;

		--NumElements;
	}

	template <typename... ArgsType>
	T* Construct(ArgsType&&... Args)
	{
		return new(Alloc()) T(Forward<ArgsType>(Args)...);
	}

	void Destroy(T* Ptr)
	{
		Ptr->~T();
		Free(Ptr);
	}

	void Trim()
	{
		FScopeLock _(&CriticalSection);
		if (!NumElements)
		{
			FreeBlocks();
		}
	}

private:
	void FreeBlocks()
	{
		FBlock* Block = Blocks;
		while (Block)
		{
			FBlock* Tmp = Block;
			Block = Block->Next;
			delete Tmp;
		}

		Blocks		= nullptr;
		NextFree	= nullptr;
		NumElements = 0;
	}

	struct FElement
	{
		TTypeCompatibleBytes<T> Buffer;
		FElement* Next;
	};

	struct FBlock
	{
		FElement Elements[BlockSize];
		FBlock* Next = nullptr;
	};

	FBlock*				Blocks		= nullptr;
	FElement*			NextFree	= nullptr;
	int32				NumElements = 0;
	FCriticalSection	CriticalSection;
};

//////////////////////////////////////////////////////////////////////////

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
		TEXT("Unknown ChunkID")
		TEXT("Invalid Parameter")
	};

	return ErrorCodeText[static_cast<uint32>(ErrorCode)];
}

const FIoStatus FIoStatus::Ok		{ EIoErrorCode::Ok,				TEXT("OK")				};
const FIoStatus FIoStatus::Unknown	{ EIoErrorCode::Unknown,		TEXT("Unknown Status")	};
const FIoStatus FIoStatus::Invalid	{ EIoErrorCode::InvalidCode,	TEXT("Invalid Code")	};

FIoStatus::FIoStatus()
{
}

FIoStatus::~FIoStatus()
{
}

FIoStatus::FIoStatus(EIoErrorCode Code)
:	ErrorCode(Code)
{
	ErrorMessage[0] = 0;
}

FIoStatus::FIoStatus(EIoErrorCode Code, const FStringView& InErrorMessage)
: ErrorCode(Code)
{
	const int32 ErrorMessageLength = FMath::Min(MaxErrorMessageLength - 1, InErrorMessage.Len());
	FPlatformString::Convert(ErrorMessage, ErrorMessageLength, InErrorMessage.Data(), ErrorMessageLength);
	ErrorMessage[ErrorMessageLength] = 0;
}

FIoStatus& 
FIoStatus::operator=(const FIoStatus& Other)
{
	ErrorCode = Other.ErrorCode;
	FMemory::Memcpy(ErrorMessage, Other.ErrorMessage, MaxErrorMessageLength * sizeof(TCHAR));

	return *this;
}

bool		
FIoStatus::operator==(const FIoStatus& Other) const
{
	return ErrorCode == Other.ErrorCode &&
		FPlatformString::Stricmp(ErrorMessage, Other.ErrorMessage) == 0;
}

FString	
FIoStatus::ToString() const
{
	return FString::Format(TEXT("{0} ({1})"), { ErrorMessage, GetIoErrorText(ErrorCode) });
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

	UE_NODISCARD FIoStatus MapPartialRange(FIoChunkId OriginalChunkId, uint64 Offset, uint64 Length, FIoChunkId ChunkIdPartialRange)
	{
		//TODO: Does RelativeOffset + Length overflow?

		const FIoStoreTocEntry* Entry = Toc.Find(OriginalChunkId);
		if (Entry == nullptr)
		{
			return FIoStatus(EIoErrorCode::UnknownChunkID, TEXT("OriginalChunkId does not exist in the container"));
		}

		if (Toc.Find(ChunkIdPartialRange) != nullptr)
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("ChunkIdPartialRange is already mapped"));
		}

		if (Offset + Length > Entry->GetLength())
		{
			return FIoStatus(EIoErrorCode::InvalidParameter, TEXT("The given range (Offset/Length) is not within the bounds of OriginalChunkId's data"));
		}

		FIoStoreTocEntry TocEntry;

		TocEntry.SetOffset(Entry->GetOffset() + Offset);
		TocEntry.SetLength(Length);
		TocEntry.ChunkId = ChunkIdPartialRange;

		Toc.Add(ChunkIdPartialRange, TocEntry);

		IsMetadataDirty = true;

		return FIoStatus::Ok;
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

void FIoStoreWriter::MapPartialRange(FIoChunkId OriginalChunkId, uint64 Offset, uint64 Length, FIoChunkId ChunkIdPartialRange)
{
	Impl->MapPartialRange(OriginalChunkId, Offset, Length, ChunkIdPartialRange);
}

void FIoStoreWriter::FlushMetadata()
{
	Impl->FlushMetadata();
}

//////////////////////////////////////////////////////////////////////////

class FIoBatchImpl
{
public:
	FIoRequestImpl* FirstRequest	= nullptr;
	FIoBatchImpl*	NextBatch		= nullptr;
	TAtomic<uint32>	OutstandingRequests { 0 };
};

class FIoRequestImpl
{
public:
	FIoChunkId				ChunkId;
	FIoReadOptions			Options;
	TIoStatusOr<FIoBuffer>	Result;
	uint64					UserData	= 0;
	FIoRequestImpl*			NextRequest = nullptr;
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

using FRequestAllocator	= TBlockAllocator<FIoRequestImpl, 4096>;
using FBatchAllocator	= TBlockAllocator<FIoBatchImpl, 4096>;

//////////////////////////////////////////////////////////////////////////

class FIoDispatcherImpl
{
public:
	FIoDispatcherImpl()
	{
		IoStore = new FIoStoreImpl;

		FCoreDelegates::GetMemoryTrimDelegate().AddLambda([this]()
		{
			RequestAllocator.Trim();
			BatchAllocator.Trim();
		});
	}

	FIoRequestImpl* AllocRequest(const FIoChunkId& ChunkId, FIoReadOptions Options, uint64 UserData = 0)
	{
		FIoRequestImpl* Request = RequestAllocator.Construct();

		Request->ChunkId		= ChunkId;
		Request->Options		= Options;
		Request->Result			= FIoStatus::Unknown;
		Request->UserData		= UserData;
		Request->NextRequest	= nullptr;

		return Request;
	}

	FIoRequestImpl* AllocRequest(FIoBatchImpl* Batch, const FIoChunkId& ChunkId, FIoReadOptions Options, uint64 UserData = 0)
	{
		FIoRequestImpl* Request	= AllocRequest(ChunkId, Options, UserData);

		Request->NextRequest	= Batch->FirstRequest;
		Batch->FirstRequest		= Request;

		return Request;
	}

	void FreeRequest(FIoRequestImpl* Request)
	{
		RequestAllocator.Destroy(Request);
	}

	FIoBatchImpl* AllocBatch(FIoRequestImpl* FirstRequest = nullptr)
	{
		FIoBatchImpl* Batch			= BatchAllocator.Construct();

		Batch->FirstRequest			= FirstRequest;
		Batch->OutstandingRequests	= 0;
	
		return Batch;
	}

	void FreeBatch(FIoBatchImpl* Batch)
	{
		FIoRequestImpl* Request = Batch->FirstRequest;

		while (Request)
		{
			FIoRequestImpl* Tmp = Request;	
			Request = Request->NextRequest;
			FreeRequest(Tmp);
		}

		BatchAllocator.Destroy(Batch);
	}

	template<typename Func>
	void IterateBatch(const FIoBatchImpl* Batch, Func&& InCallbackFunction)
	{
		FIoRequestImpl* Request = Batch->FirstRequest;

		while (Request)
		{
			const bool bDoContinue = InCallbackFunction(Request);

			Request = bDoContinue ? Request->NextRequest : nullptr;
		}
	}

	void IssueBatch(const FIoBatchImpl* Batch)
	{
		// At this point the batch is immutable and we should start
		// doing the work.

#if IODISPATCHER_TRACE_ENABLED
		UE_TRACE_LOG(IoDispatcher, BatchIssued)
			<< BatchIssued.Cycle(FPlatformTime::Cycles64())
			<< BatchIssued.BatchId(uint64(Batch));
#endif
		uint64 TotalBatchSize = 0;
		IterateBatch(Batch, [this, &TotalBatchSize](FIoRequestImpl* Request)
		{
			Request->Result = IoStore->Resolve(Request->ChunkId);

#if IODISPATCHER_TRACE_ENABLED
			TotalBatchSize += Request->Result.ValueOrDie().DataSize();
#endif
			return true;
		});

#if IODISPATCHER_TRACE_ENABLED
		UE_TRACE_LOG(IoDispatcher, BatchResolved)
			<< BatchResolved.Cycle(FPlatformTime::Cycles64())
			<< BatchResolved.BatchId(uint64(Batch))
			<< BatchResolved.TotalSize(TotalBatchSize);
#endif
	}

	bool IsBatchReady(const FIoBatchImpl* Batch)
	{
		bool bIsReady = true;

		IterateBatch(Batch, [&bIsReady](FIoRequestImpl* Request)
		{
			bIsReady &= Request->Result.Status().IsCompleted();
			return bIsReady;
		});

		return bIsReady;
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
	FRequestAllocator			RequestAllocator;
	FBatchAllocator				BatchAllocator;
};

//////////////////////////////////////////////////////////////////////////

FIoDispatcher::FIoDispatcher()
:	Impl(new FIoDispatcherImpl())
{
}

FIoDispatcher::~FIoDispatcher()
{
	delete Impl;
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
	return FIoBatch(Impl, Impl->AllocBatch());
}

void
FIoDispatcher::FreeBatch(FIoBatch Batch)
{
	Impl->FreeBatch(Batch.Impl);
}

//////////////////////////////////////////////////////////////////////////

FIoBatch::FIoBatch(FIoDispatcherImpl* InDispatcher, FIoBatchImpl* InImpl)
:	Dispatcher(InDispatcher)
,	Impl(InImpl)
,	CompletionEvent()
{
}

FIoRequest
FIoBatch::Read(const FIoChunkId& ChunkId, FIoReadOptions Options)
{
	return FIoRequest(Dispatcher->AllocRequest(Impl, ChunkId, Options));
}

void 
FIoBatch::ForEachRequest(TFunction<bool(FIoRequest&)> Callback)
{
	Dispatcher->IterateBatch(Impl, [&](FIoRequestImpl* InRequest) {
		FIoRequest Request(InRequest);
		return Callback(Request);
	});
}

void 
FIoBatch::Issue()
{
	Dispatcher->IssueBatch(Impl);
}

void 
FIoBatch::Wait()
{
	//TODO: Create synchronization event here when it's actually needed
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

const TIoStatusOr<FIoBuffer>&
FIoRequest::GetResult() const
{
	return Impl->Result;
}

//////////////////////////////////////////////////////////////////////////

class FIoQueueImpl
	: private FRunnable
{
public:
	FIoQueueImpl(FIoDispatcherImpl& InDispatcher, FIoQueue::FBatchReadyCallback&& InBatchReadyCallback)
		: Dispatcher(InDispatcher)
		, BatchReadyCallback(Forward<FIoQueue::FBatchReadyCallback>(InBatchReadyCallback))
	{
		WakeUpEvent = FGenericPlatformProcess::GetSynchEventFromPool(true);
		Thread = FRunnableThread::Create(this, TEXT("IoQueueThread"), 0, TPri_Normal);
	}

	~FIoQueueImpl()
	{
		Stop();
		Thread->Kill(true);
		Thread = nullptr;
		FPlatformProcess::ReturnSynchEventToPool(WakeUpEvent);
	}

	void Enqueue(const FIoChunkId& ChunkId, FIoReadOptions ReadOptions, uint64 UserData, bool bDeferBatch)
	{
		FIoRequestImpl* Request = Dispatcher.AllocRequest(ChunkId, ReadOptions, UserData);
		check(Request->NextRequest == nullptr);

		{
			// TODO: CAS
			FScopeLock _(&QueuedLock);

			Request->NextRequest	= FirstQueuedRequest;
			FirstQueuedRequest		= Request;
		}

		if (!bDeferBatch)
		{
			IssueBatch();
		}
	
		{
			FScopeLock _(&NumPendingLock);

			if (0 == NumPending++)
			{
				WakeUpEvent->Trigger();
			}
		}
	}

	bool Dequeue(FIoChunkId& ChunkId, TIoStatusOr<FIoBuffer>& Result, uint64& UserData)
	{
		FIoRequestImpl* CompletedRequest = nullptr;
		{
			// TODO: CAS
			FScopeLock _(&CompletedLock);
			
			CompletedRequest		= FirstCompletedRequest;
			FirstCompletedRequest	= CompletedRequest ? CompletedRequest->NextRequest : FirstCompletedRequest;
		}

		if (CompletedRequest)
		{
			ChunkId		= CompletedRequest->ChunkId;
			Result		= CompletedRequest->Result;
			UserData	= CompletedRequest->UserData;

			Dispatcher.FreeRequest(CompletedRequest);

			{
				FScopeLock _(&NumPendingLock);

				if (0 == --NumPending)
				{
					check(NumPending >= 0);
					WakeUpEvent->Reset();
				}
			}

			return true;
		}

		return false;
	}

	void IssueBatch()
	{
		FIoRequestImpl* QueuedRequests = nullptr;
		{
			// TODO: CAS
			FScopeLock _(&QueuedLock);

			QueuedRequests 		= FirstQueuedRequest;
			FirstQueuedRequest	= nullptr;
		}

		if (QueuedRequests)
		{
			FIoBatchImpl* NewBatch = Dispatcher.AllocBatch(QueuedRequests);
			{
				// TODO: CAS
				FScopeLock _(&PendingLock);

				NewBatch->NextBatch	= FirstPendingBatch;
				FirstPendingBatch	= NewBatch;
			}
		}
	}

	bool IsEmpty() const
	{
		FScopeLock _(&NumPendingLock);
		return NumPending == 0;
	}

private:
	
	uint32 Run() override
	{
		bIsRunning = true;

		FBatchQueue IssuedBatches;

		while (bIsRunning)
		{
			if (!bIsRunning)
			{
				break;
			}

			// Dispatch pending batch
			{
				FIoBatchImpl* PendingBatch = nullptr;
				{
					// TODO: CAS
					FScopeLock _(&PendingLock);

					PendingBatch		= FirstPendingBatch;
					FirstPendingBatch	= PendingBatch ? PendingBatch->NextBatch : nullptr;
				}

				if (PendingBatch)
				{
					PendingBatch->NextBatch = nullptr;
					
					Dispatcher.IssueBatch(PendingBatch);
					Enqueue(PendingBatch, IssuedBatches);
				}
			}

			// Process issued batches
			if (FIoBatchImpl* IssuedBatch = Peek(IssuedBatches))
			{
				if (Dispatcher.IsBatchReady(IssuedBatch))
				{
					Dequeue(IssuedBatches);

					FIoRequestImpl* Request = IssuedBatch->FirstRequest;
					while (Request)
					{
						FIoRequestImpl* CompletedRequest	= Request;
						Request								= Request->NextRequest;
						
						{
							// TODO: CAS
							FScopeLock _(&CompletedLock);

							CompletedRequest->NextRequest	= FirstCompletedRequest;
							FirstCompletedRequest			= CompletedRequest;
						}
					}

					BatchReadyCallback();

					IssuedBatch->FirstRequest = nullptr;
					Dispatcher.FreeBatch(IssuedBatch);
				}
			}
			else
			{
				WakeUpEvent->Wait();
			}
		}

		return 0;
	}

	void Stop() override
	{
		if (bIsRunning)
		{
			bIsRunning = false;
			WakeUpEvent->Trigger();
		}
	}

	struct FBatchQueue
	{
		FIoBatchImpl* Head = nullptr;
		FIoBatchImpl* Tail = nullptr;
	};

	void Enqueue(FIoBatchImpl* Batch, FBatchQueue& Batches)
	{
		if (Batches.Tail == nullptr)
		{
			Batches.Head = Batches.Tail	= Batch;
		}
		else
		{
			Batches.Tail->NextBatch	= Batch;
			Batches.Tail			= Batch;
		}
	}

	FIoBatchImpl* Dequeue(FBatchQueue& Batches)
	{
		if (FIoBatchImpl* Batch = Batches.Head)
		{
			Batches.Head		= Batch->NextBatch;
			Batches.Tail		= Batches.Head == nullptr ? nullptr : Batches.Tail;
			Batch->NextBatch	= nullptr;

			return Batch;
		}
		
		return nullptr;
	}

	FIoBatchImpl* Peek(FBatchQueue& Queue)
	{
		return Queue.Head;
	}

	FIoDispatcherImpl&				Dispatcher;
	FIoQueue::FBatchReadyCallback	BatchReadyCallback;
	FRunnableThread*				Thread					= nullptr;
	FEvent*							WakeUpEvent				= nullptr;
	bool							bIsRunning				= false;
	FIoRequestImpl*					FirstQueuedRequest		= nullptr;
	FIoBatchImpl*					FirstPendingBatch		= nullptr;
	FIoRequestImpl*					FirstCompletedRequest	= nullptr;
	int32							NumPending				= 0;
	FCriticalSection				QueuedLock;
	FCriticalSection				PendingLock;
	FCriticalSection				CompletedLock;
	mutable FCriticalSection		NumPendingLock;
};

//////////////////////////////////////////////////////////////////////////
FIoQueue::FIoQueue(FIoDispatcher& IoDispatcher, FBatchReadyCallback BatchReadyCallback)
: Impl(new FIoQueueImpl(*IoDispatcher.Impl, MoveTemp(BatchReadyCallback)))
{ 
}

FIoQueue::~FIoQueue() = default;

void
FIoQueue::Enqueue(const FIoChunkId& ChunkId, FIoReadOptions ReadOptions, uint64 UserData, bool bDeferBatch)
{
	Impl->Enqueue(ChunkId, ReadOptions, UserData, bDeferBatch);
}

bool
FIoQueue::Dequeue(FIoChunkId& ChunkId, TIoStatusOr<FIoBuffer>& Result, uint64& UserData)
{
	return Impl->Dequeue(ChunkId, Result, UserData);
}

void
FIoQueue::IssueBatch()
{
	return Impl->IssueBatch();
}

bool
FIoQueue::IsEmpty() const
{
	return Impl->IsEmpty();
}
