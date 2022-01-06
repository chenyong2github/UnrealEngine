// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/EditorDerivedData.h"

#if WITH_EDITORONLY_DATA

#include "Compression/CompressedBuffer.h"
#include "DerivedDataBuildDefinition.h"
#include "DerivedDataBuildSession.h"
#include "DerivedDataCacheInterface.h"
#include "DerivedDataValueId.h"
#include "Memory/CompositeBuffer.h"
#include "Memory/SharedBuffer.h"
#include "Misc/CoreMisc.h"
#include "Serialization/BulkDataRegistry.h"

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

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class FEditorDerivedDataCache final : public FEditorDerivedData
{
public:
	FEditorDerivedDataCache(FStringView InCacheKey, FStringView InCacheContext)
		: CacheKey(InCacheKey)
		, CacheContext(InCacheContext)
	{
	}

	TUniquePtr<FEditorDerivedData> Clone() const final { return MakeUnique<FEditorDerivedDataCache>(*this); }

	void Read(IoStore::FDerivedDataIoRequest Request) const final;
	bool TryGetSize(uint64& OutSize) const final;

private:
	FString CacheKey;
	FString CacheContext;
};

void FEditorDerivedDataCache::Read(IoStore::FDerivedDataIoRequest Request) const
{
	TArray<uint8> Data;
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

bool FEditorDerivedDataCache::TryGetSize(uint64& OutSize) const
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

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(FStringView CacheKey, FStringView CacheContext)
{
	return MakeUnique<FEditorDerivedDataCache>(CacheKey, CacheContext);
}

TUniquePtr<FEditorDerivedData> MakeEditorDerivedData(
	const FBuildDefinition& BuildDefinition,
	const FValueId& ValueId)
{
	return MakeUnique<FEditorDerivedDataBuild>(BuildDefinition, ValueId);
}

} // UE::DerivedData::Private

#endif // WITH_EDITORONLY_DATA
