// Copyright Epic Games, Inc. All Rights Reserved.

#include "Serialization/DerivedData.h"

#include "Memory/SharedBuffer.h"

#if WITH_EDITORONLY_DATA

#include "DerivedDataBuildDefinition.h"
#include "DerivedDataValueId.h"
#include "Serialization/EditorDerivedData.h"
#include "Serialization/EditorDerivedDataIoStore.h"
#include "Templates/SharedPointer.h"

namespace UE::DerivedData::IoStore
{

TSharedPtr<IEditorDerivedDataIoStore> GEditorDerivedDataIoStore;

} // UE::DerivedData::IoStore

#endif // WITH_EDITORONLY_DATA

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE
{

void FDerivedData::Serialize(FArchive& Ar, UObject* Owner)
{
	unimplemented();
}

FUniqueBuffer FDerivedData::LoadData(FArchive& Ar, FDerivedDataBufferAllocator Allocator)
{
	check(Ar.IsLoading());
	unimplemented();
	return FUniqueBuffer();
}

#if WITH_EDITORONLY_DATA

void FDerivedData::SaveData(FArchive& Ar, const FDerivedData& Data)
{
	checkf(Data.EditorData, TEXT("Inline serialization is not supported for non-editor derived data references."));
	check(Ar.IsSaving());
	unimplemented();
}

FDerivedData::FDerivedData() = default;
FDerivedData::FDerivedData(FDerivedData&& Other) = default;
FDerivedData& FDerivedData::operator=(FDerivedData&& Other) = default;

FDerivedData::FDerivedData(const FDerivedData& Other)
{
	if (Other.EditorData)
	{
		EditorData = Other.EditorData->Clone();
		ChunkId = DerivedData::IoStore::GEditorDerivedDataIoStore->AddData(EditorData.Get());
	}
	else
	{
		ChunkId = Other.ChunkId;
	}
}

FDerivedData& FDerivedData::operator=(const FDerivedData& Other)
{
	if (EditorData)
	{
		DerivedData::IoStore::GEditorDerivedDataIoStore->RemoveData(ChunkId);
	}
	if (Other.EditorData)
	{
		EditorData = Other.EditorData->Clone();
		ChunkId = DerivedData::IoStore::GEditorDerivedDataIoStore->AddData(EditorData.Get());
	}
	else
	{
		EditorData.Reset();
		ChunkId = Other.ChunkId;
	}
	return *this;
}

FDerivedData::FDerivedData(const FSharedBuffer& Data)
	: EditorData(DerivedData::Private::MakeEditorDerivedData(Data))
	, ChunkId(DerivedData::IoStore::GEditorDerivedDataIoStore->AddData(EditorData.Get()))
{
}

FDerivedData::FDerivedData(const FCompositeBuffer& Data)
	: EditorData(DerivedData::Private::MakeEditorDerivedData(Data))
	, ChunkId(DerivedData::IoStore::GEditorDerivedDataIoStore->AddData(EditorData.Get()))
{
}

FDerivedData::FDerivedData(const FCompressedBuffer& Data)
	: EditorData(DerivedData::Private::MakeEditorDerivedData(Data))
	, ChunkId(DerivedData::IoStore::GEditorDerivedDataIoStore->AddData(EditorData.Get()))
{
}

FDerivedData::FDerivedData(FStringView CacheKey, FStringView CacheContext)
	: EditorData(DerivedData::Private::MakeEditorDerivedData(CacheKey, CacheContext))
	, ChunkId(DerivedData::IoStore::GEditorDerivedDataIoStore->AddData(EditorData.Get()))
{
}

FDerivedData::FDerivedData(const DerivedData::FBuildDefinition& BuildDefinition, const DerivedData::FValueId& ValueId)
	: EditorData(DerivedData::Private::MakeEditorDerivedData(BuildDefinition, ValueId))
	, ChunkId(DerivedData::IoStore::GEditorDerivedDataIoStore->AddData(EditorData.Get()))
{
}

FDerivedData::~FDerivedData()
{
	if (EditorData)
	{
		using namespace DerivedData::IoStore;
		if (IEditorDerivedDataIoStore* EditorBackend = GEditorDerivedDataIoStore.Get())
		{
			verify(EditorBackend->RemoveData(ChunkId) == EditorData.Get());
		}
	}
}

void FDerivedData::SetFlags(EDerivedDataFlags InFlags)
{
	checkf(!EnumHasAllFlags(InFlags, EDerivedDataFlags::Optional | EDerivedDataFlags::MemoryMapped),
		TEXT("Optional and MemoryMapped cannot be used together on derived data references."));
	Flags = InFlags;
}

#endif // WITH_EDITORONLY_DATA

} // UE

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace UE::DerivedData::IoStore
{

void InitializeIoDispatcher()
{
#if WITH_EDITORONLY_DATA
	FIoDispatcher& IoDispatcher = FIoDispatcher::Get();
	TSharedRef<IEditorDerivedDataIoStore> EditorBackend = CreateEditorDerivedDataIoStore();
	GEditorDerivedDataIoStore = EditorBackend;
	IoDispatcher.Mount(EditorBackend);
#endif // WITH_EDITORONLY_DATA
}

void TearDownIoDispatcher()
{
#if WITH_EDITORONLY_DATA
	GEditorDerivedDataIoStore.Reset();
#endif // WITH_EDITORONLY_DATA
}

} // UE::DerivedData::IoStore
