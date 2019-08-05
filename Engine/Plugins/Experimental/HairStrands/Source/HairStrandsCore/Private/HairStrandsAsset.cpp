// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "HairStrandsAsset.h"
#include "RenderingThread.h"

#include "Misc/Paths.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

FHairStrandsInstance::~FHairStrandsInstance()
{
	if (RenderResource && bInstancedResource)
	{
		FHairStrandsResource* InResource = RenderResource;
		ENQUEUE_RENDER_COMMAND(FDestroyHairStrandsResourceCommand)(
			[InResource](FRHICommandList& RHICmdList)
		{
			InResource->ReleaseResource();
		});
	}
	RenderResource = nullptr;
}

void FHairStrandsInstance::InitResource(FHairStrandsResource* InResource, bool bInstanced)
{
	check(!RenderResource);
	RenderResource = InResource;
	bInstancedResource = bInstanced;
}

void FHairStrandsInstance::UpdateTransforms(const FMatrix& LocalToWorld)
{
	LocalToGlobal = LocalToWorld;
}

FHairStrandsResource::FHairStrandsResource(FHairStrandsDatas* HairStrandsDatas) : 
	PositionBuffer(), TangentBuffer(), StrandsDatas(HairStrandsDatas)
{}

void FHairStrandsResource::InitRHI()
{
	if (StrandsDatas != nullptr)
	{
		TArray<FHairStrandsPositionFormat::Type> RenderingPositions;
		TArray<FHairStrandsTangentFormat::Type> RenderingTangents;
		StrandsDatas->BuildRenderingDatas(RenderingPositions, RenderingTangents);
		{
			const uint32 PositionCount = RenderingPositions.Num();
			const uint32 PositionBytes = FHairStrandsPositionFormat::SizeInByte*PositionCount;

			PositionBuffer.Initialize(FHairStrandsPositionFormat::SizeInByte, PositionCount, FHairStrandsPositionFormat::Format, BUF_Static);
			void* PositionBufferData = RHILockVertexBuffer(PositionBuffer.Buffer, 0, PositionBytes, RLM_WriteOnly);

			FMemory::Memcpy(PositionBufferData, RenderingPositions.GetData(), PositionBytes);
			RHIUnlockVertexBuffer(PositionBuffer.Buffer);
		}
		{
			const uint32 TangentCount = RenderingTangents.Num();
			const uint32 TangentBytes = FHairStrandsTangentFormat::SizeInByte*TangentCount;

			TangentBuffer.Initialize(FHairStrandsTangentFormat::SizeInByte, TangentCount, FHairStrandsTangentFormat::Format, BUF_Static);
			void* TangentBufferData = RHILockVertexBuffer(TangentBuffer.Buffer, 0, TangentBytes, RLM_WriteOnly);

			FMemory::Memcpy(TangentBufferData, RenderingTangents.GetData(), TangentBytes);
			RHIUnlockVertexBuffer(TangentBuffer.Buffer);
		}
	}
}

void FHairStrandsResource::ReleaseRHI()
{
	PositionBuffer.Release();
	TangentBuffer.Release();
}

void UHairStrandsAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);
	StrandsDatas.Serialize(Ar);
}

void UHairStrandsAsset::InitResource()
{
	check(!HairStrandsResource);

	HairStrandsResource = new FHairStrandsResource(&StrandsDatas);

	BeginInitResource(HairStrandsResource);
}

void UHairStrandsAsset::UpdateResource()
{
	if (HairStrandsResource)
	{
		BeginUpdateResourceRHI(HairStrandsResource);
	}
}

void UHairStrandsAsset::ReleaseResource()
{
	if (HairStrandsResource)
	{
		FHairStrandsResource* InResource = HairStrandsResource;
		ENQUEUE_RENDER_COMMAND(ReleaseHairStrandsResourceCommand)(
			[InResource](FRHICommandList& RHICmdList)
		{
			InResource->ReleaseResource();
			delete InResource;
		});
		HairStrandsResource = nullptr;
	}
}

void UHairStrandsAsset::PostLoad()
{
	Super::PostLoad();

	if (!IsTemplate())
	{
		InitResource();
	}

#if WITH_EDITORONLY_DATA
	if (!FilePath.IsEmpty() && AssetImportData)
	{
		FAssetImportInfo Info;
		Info.Insert(FAssetImportInfo::FSourceFile(FilePath));
		AssetImportData->SourceData = MoveTemp(Info);
	}
#endif
}

void UHairStrandsAsset::BeginDestroy()
{
	ReleaseResource();
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UHairStrandsAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateResource();
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
void UHairStrandsAsset::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (AssetImportData)
	{
		OutTags.Add(FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden));
	}

	Super::GetAssetRegistryTags(OutTags);
}

void UHairStrandsAsset::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}

	Super::PostInitProperties();
}
#endif


