// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/EditorDerivedData.h"

#if WITH_EDITORONLY_DATA

#include "Compression/CompressedBuffer.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildSession.h"
#include "DerivedDataCache.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataCacheKey.h"
#include "DerivedDataRequestOwner.h"
#include "DerivedDataSharedString.h"
#include "DerivedDataValue.h"
#include "DerivedDataValueId.h"
#include "IO/IoDispatcher.h"
#include "Memory/CompositeBuffer.h"
#include "Memory/SharedBuffer.h"
#include "Misc/CoreMisc.h"
#include "UObject/LinkerSave.h"

namespace UE::DerivedData::Private
{

class FEditorDerivedDataBuffer final : public FEditorDerivedData
{
public:
	explicit FEditorDerivedDataBuffer(const FSharedBuffer& InData)
		: Data(InData)
	{
	}

	explicit FEditorDerivedDataBuffer(const FCompositeBuffer& InData)
		: Data(InData)
	{
	}

	TUniquePtr<FEditorDerivedData> Clone() const final { return MakeUnique<FEditorDerivedDataBuffer>(*this); }

	void Read(IoStore::FDerivedDataIoRequest Request) const final;
	bool TryGetSize(uint64& OutSize) const final;
	FIoChunkId Save(FLinkerSave& Linker) const final;

private:
	FCompositeBuffer Data;
};

void FEditorDerivedDataBuffer::Read(IoStore::FDerivedDataIoRequest Request) const
{
	const uint64 DataSize = Data.GetSize();
	const uint64 RequestOffset = Request.GetOffset();
	const uint64 RequestSize = FMath::Min(Request.GetSize(), RequestOffset <= DataSize ? DataSize - RequestOffset : 0);
	if (RequestSize)
	{
		Data.CopyTo(Request.CreateBuffer(RequestSize), RequestOffset);
		Request.SetComplete();
	}
	else
	{
		Request.SetComplete();
	}
}

bool FEditorDerivedDataBuffer::TryGetSize(uint64& OutSize) const
{
	OutSize = Data.GetSize();
	return true;
}

FIoChunkId FEditorDerivedDataBuffer::Save(FLinkerSave& Linker) const
{
	return Linker.AddDerivedData(FValue::Compress(Data).GetData());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FEditorDerivedDataCompressedBuffer final : public FEditorDerivedData
{
public:
	explicit FEditorDerivedDataCompressedBuffer(const FCompressedBuffer& InData)
		: Data(InData)
	{
	}

	TUniquePtr<FEditorDerivedData> Clone() const final { return MakeUnique<FEditorDerivedDataCompressedBuffer>(*this); }

	void Read(IoStore::FDerivedDataIoRequest Request) const final;
	bool TryGetSize(uint64& OutSize) const final;
	FIoChunkId Save(FLinkerSave& Linker) const final;

private:
	FCompressedBuffer Data;
};

void FEditorDerivedDataCompressedBuffer::Read(IoStore::FDerivedDataIoRequest Request) const
{
	const uint64 DataSize = Data.GetRawSize();
	const uint64 RequestOffset = Request.GetOffset();
	const uint64 RequestSize = FMath::Min(Request.GetSize(), RequestOffset <= DataSize ? DataSize - RequestOffset : 0);
	if (RequestSize)
	{
		const FMutableMemoryView View = Request.CreateBuffer(RequestSize);
		if (FCompressedBufferReader(Data).TryDecompressTo(View, RequestOffset))
		{
			Request.SetComplete();
		}
		else
		{
			Request.SetFailed();
		}
	}
	else
	{
		Request.SetComplete();
	}
}

bool FEditorDerivedDataCompressedBuffer::TryGetSize(uint64& OutSize) const
{
	OutSize = Data.GetRawSize();
	return true;
}

FIoChunkId FEditorDerivedDataCompressedBuffer::Save(FLinkerSave& Linker) const
{
	return Linker.AddDerivedData(Data);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FEditorDerivedDataCache final : public FEditorDerivedData
{
public:
	FEditorDerivedDataCache(const FSharedString& InName, const FCacheKey& InKey, const FValueId& InValueId)
		: Name(InName)
		, Key(InKey)
		, ValueId(InValueId)
	{
	}

	TUniquePtr<FEditorDerivedData> Clone() const final { return MakeUnique<FEditorDerivedDataCache>(*this); }

	void Read(IoStore::FDerivedDataIoRequest Request) const final;
	bool TryGetSize(uint64& OutSize) const final;
	FIoChunkId Save(FLinkerSave& Linker) const final;

private:
	FSharedString Name;
	FCacheKey Key;
	FValueId ValueId;
};

void FEditorDerivedDataCache::Read(IoStore::FDerivedDataIoRequest Request) const
{
	FSharedBuffer Data;

	FRequestOwner Owner(EPriority::Blocking);
	GetCache().GetChunks({{Name, Key, ValueId, Request.GetOffset(), Request.GetSize()}}, Owner, [&Data](FCacheGetChunkResponse&& Response)
	{
		Data = MoveTemp(Response.RawData);
	});
	Owner.Wait();

	if (Data)
	{
		Request.CreateBuffer(Data.GetSize()).CopyFrom(Data);
		Request.SetComplete();
	}
	else
	{
		Request.SetFailed();
	}
}

bool FEditorDerivedDataCache::TryGetSize(uint64& OutSize) const
{
	bool bOk = false;
	FRequestOwner Owner(EPriority::Blocking);
	FCacheGetChunkRequest Request{Name, Key, ValueId};
	Request.Policy |= ECachePolicy::SkipData;
	GetCache().GetChunks({Request}, Owner, [&bOk, &OutSize](FCacheGetChunkResponse&& Response)
	{
		if (Response.Status == EStatus::Ok)
		{
			bOk = true;
			OutSize = Response.RawSize;
		}
	});
	Owner.Wait();
	return bOk;
}

FIoChunkId FEditorDerivedDataCache::Save(FLinkerSave& Linker) const
{
	return Linker.AddDerivedData(Key, ValueId);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FEditorDerivedDataBuild final : public FEditorDerivedData
{
public:
	FEditorDerivedDataBuild(const FBuildDefinition& InBuildDefinition, const FValueId& InValueId)
		: BuildDefinition(InBuildDefinition)
		, ValueId(InValueId)
	{
		unimplemented();
	}

	TUniquePtr<FEditorDerivedData> Clone() const final { return MakeUnique<FEditorDerivedDataBuild>(*this); }

	void Read(IoStore::FDerivedDataIoRequest Request) const final;
	bool TryGetSize(uint64& OutSize) const final;
	FIoChunkId Save(FLinkerSave& Linker) const final;

private:
	FBuildDefinition BuildDefinition;
	FValueId ValueId;
};

void FEditorDerivedDataBuild::Read(IoStore::FDerivedDataIoRequest Request) const
{
	Request.SetFailed();
}

bool FEditorDerivedDataBuild::TryGetSize(uint64& OutSize) const
{
	return false;
}

FIoChunkId FEditorDerivedDataBuild::Save(FLinkerSave& Linker) const
{
	unimplemented();
	return FIoChunkId::InvalidChunkId;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FEditorDerivedDataLegacyCache final : public FEditorDerivedData
{
public:
	FEditorDerivedDataLegacyCache(FStringView InCacheKey, FStringView InCacheContext)
		: CacheKey(InCacheKey)
		, CacheContext(InCacheContext)
	{
	}

	TUniquePtr<FEditorDerivedData> Clone() const final { return MakeUnique<FEditorDerivedDataLegacyCache>(*this); }

	void Read(IoStore::FDerivedDataIoRequest Request) const final;
	bool TryGetSize(uint64& OutSize) const final;
	FIoChunkId Save(FLinkerSave& Linker) const final;

private:
	FString CacheKey;
	FString CacheContext;
};

void FEditorDerivedDataLegacyCache::Read(IoStore::FDerivedDataIoRequest Request) const
{
	TArray64<uint8> Data;
	if (GetDerivedDataCacheRef().GetSynchronous(*CacheKey, Data, CacheContext))
	{
		const FMemoryView DataView = MakeMemoryView(Data);
		const uint64 DataSize = DataView.GetSize();
		const uint64 RequestOffset = Request.GetOffset();
		const uint64 RequestSize = FMath::Min(Request.GetSize(), RequestOffset <= DataSize ? DataSize - RequestOffset : 0);
		Request.CreateBuffer(RequestSize).CopyFrom(DataView.Mid(RequestOffset, RequestSize));
		Request.SetComplete();
	}
	else
	{
		Request.SetFailed();
	}
}

bool FEditorDerivedDataLegacyCache::TryGetSize(uint64& OutSize) const
{
	TArray<uint8> Data;
	if (GetDerivedDataCacheRef().GetSynchronous(*CacheKey, Data, CacheContext))
	{
		OutSize = MakeMemoryView(Data).GetSize();
		return true;
	}
	else
	{
		return false;
	}
}

FIoChunkId FEditorDerivedDataLegacyCache::Save(FLinkerSave& Linker) const
{
	TArray64<uint8> Data;
	const bool bOk = GetDerivedDataCacheRef().GetSynchronous(*CacheKey, Data, CacheContext);
	checkf(bOk, TEXT("Failed to fetch %s from the cache for '%s'"), *CacheKey, *CacheContext);
	return Linker.AddDerivedData(FValue::Compress(MakeSharedBufferFromArray(MoveTemp(Data))).GetData());
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(const FSharedBuffer& Data)
{
	return MakeUnique<FEditorDerivedDataBuffer>(Data);
}

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(const FCompositeBuffer& Data)
{
	return MakeUnique<FEditorDerivedDataBuffer>(Data);
}

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(const FCompressedBuffer& Data)
{
	return MakeUnique<FEditorDerivedDataCompressedBuffer>(Data);
}

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(const FSharedString& Name, const FCacheKey& Key, const FValueId& ValueId)
{
	return MakeUnique<FEditorDerivedDataCache>(Name, Key, ValueId);
}

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(const FBuildDefinition& BuildDefinition, const FValueId& ValueId)
{
	return MakeUnique<FEditorDerivedDataBuild>(BuildDefinition, ValueId);
}

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(FStringView CacheKey, FStringView CacheContext)
{
	return MakeUnique<FEditorDerivedDataLegacyCache>(CacheKey, CacheContext);
}

} // UE::DerivedData::Private

#endif // WITH_EDITORONLY_DATA
