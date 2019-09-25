// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GroomAsset.h"
#include "RenderingThread.h"
#include "HairStrandsVertexFactory.h"
#include "Misc/Paths.h"
#include "UObject/PhysicsObjectVersion.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

template<typename FormatType>
void CreateBuffer(const TArray<typename FormatType::Type>& InData, FRWBuffer& OutBuffer)
{
	const uint32 DataCount = InData.Num();
	const uint32 DataSizeInBytes = FormatType::SizeInByte*DataCount;

	if (DataSizeInBytes == 0) return;

	OutBuffer.Initialize(FormatType::SizeInByte, DataCount, FormatType::Format, BUF_Static);
	void* BufferData = RHILockVertexBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);

	FMemory::Memcpy(BufferData, InData.GetData(), DataSizeInBytes);
	RHIUnlockVertexBuffer(OutBuffer.Buffer);
}

template<typename FormatType>
void CreateBuffer(uint32 InVertexCount , FRWBuffer& OutBuffer)
{
	const uint32 DataCount = InVertexCount;
	const uint32 DataSizeInBytes = FormatType::SizeInByte*DataCount;

	if (DataSizeInBytes == 0) return;

	OutBuffer.Initialize(FormatType::SizeInByte, DataCount, FormatType::Format, BUF_Static);
	void* BufferData = RHILockVertexBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memset(BufferData, 0, DataSizeInBytes);
	RHIUnlockVertexBuffer(OutBuffer.Buffer);
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsResource::FHairStrandsResource(FHairStrandsDatas* HairStrandsDatas) : 
	RestPositionBuffer(), TangentBuffer(), AttributeBuffer(), StrandsDatas(HairStrandsDatas)
{}

void FHairStrandsResource::InitRHI()
{
	if (StrandsDatas != nullptr)
	{
		TArray<FHairStrandsPositionFormat::Type> RenderingPositions;
		TArray<FHairStrandsAttributeFormat::Type> RenderingAttributes;
		StrandsDatas->BuildRenderingDatas(RenderingPositions, RenderingAttributes);

		CreateBuffer<FHairStrandsPositionFormat>(RenderingPositions, RestPositionBuffer);
		CreateBuffer<FHairStrandsPositionFormat>(RenderingPositions, DeformedPositionBuffer[0]);
		CreateBuffer<FHairStrandsPositionFormat>(RenderingPositions, DeformedPositionBuffer[1]);
		CreateBuffer<FHairStrandsTangentFormat>(RenderingPositions.Num() * FHairStrandsTangentFormat::ComponentCount, TangentBuffer);
		CreateBuffer<FHairStrandsAttributeFormat>(RenderingAttributes, AttributeBuffer);
	}
}

void FHairStrandsResource::ReleaseRHI()
{
	RestPositionBuffer.Release();
	DeformedPositionBuffer[0].Release();
	DeformedPositionBuffer[1].Release();
	TangentBuffer.Release();
	AttributeBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsInterpolationResource::FHairStrandsInterpolationResource(const FHairStrandsInterpolationDatas& InData, const FHairStrandsDatas& SimDatas) :
	Interpolation0Buffer(), Interpolation1Buffer()
{
	check(Interpolation0.Num()==0);
	check(Interpolation1.Num()==0);
	InData.BuildRenderingDatas(Interpolation0, Interpolation1);

	const uint32 RootCount = SimDatas.GetNumCurves();
	SimRootPointIndex.SetNum(SimDatas.GetNumPoints());
	for (uint32 CurveIndex = 0; CurveIndex < RootCount; ++CurveIndex)
	{
		const uint16 PointCount = SimDatas.StrandsCurves.CurvesCount[CurveIndex];
		const uint32 PointOffset = SimDatas.StrandsCurves.CurvesOffset[CurveIndex];
		for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			SimRootPointIndex[PointIndex + PointOffset] = PointOffset;
		}
	}
}

void FHairStrandsInterpolationResource::InitRHI()
{
	check(Interpolation0.Num() == Interpolation1.Num());
	if (Interpolation0.Num() > 0)
	{
		CreateBuffer<FHairStrandsInterpolation0Format>(Interpolation0, Interpolation0Buffer);
		CreateBuffer<FHairStrandsInterpolation1Format>(Interpolation1, Interpolation1Buffer);
		CreateBuffer<FHairStrandsRootIndexFormat>(SimRootPointIndex, SimRootPointIndexBuffer);
		Interpolation0.SetNum(0);
		Interpolation1.SetNum(0);
		SimRootPointIndex.SetNum(0);
	}
}

void FHairStrandsInterpolationResource::ReleaseRHI()
{
	Interpolation0Buffer.Release();
	Interpolation1Buffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

#if RHI_RAYTRACING
// RT geometry is built to for a cross around the fiber.
// 4 triangles per hair vertex => 12 vertices per hair vertex
FHairStrandsRaytracingResource::FHairStrandsRaytracingResource(FHairStrandsDatas* InData) :
	PositionBuffer(), VertexCount(InData->GetNumPoints()*12)  
{}

void FHairStrandsRaytracingResource::InitRHI()
{
	check(IsInRenderingThread());
	CreateBuffer<FHairStrandsRaytracingFormat>(VertexCount, PositionBuffer);
}

void FHairStrandsRaytracingResource::ReleaseRHI()
{
	PositionBuffer.Release();
	RayTracingGeometry.ReleaseResource();
}
#endif

/////////////////////////////////////////////////////////////////////////////////////////

void UGroomAsset::Serialize(FArchive& Ar)
{
	Super::Serialize(Ar);

	Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);

	// #ueent_todo: Serialize HairDescription

	HairRenderData.Serialize(Ar);

	if (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::HairAssetSerialization)
	{
		HairSimulationData.Serialize(Ar);
	}
}

void UGroomAsset::InitResource()
{
	check(!HairStrandsResource);

	HairStrandsResource = new FHairStrandsResource(&HairRenderData);

	BeginInitResource(HairStrandsResource);

	HairSimulationResource = new FHairStrandsResource(&HairSimulationData);

	BeginInitResource(HairSimulationResource);
}

void UGroomAsset::UpdateResource()
{
	if (HairStrandsResource)
	{
		BeginUpdateResourceRHI(HairStrandsResource);
	}

	if (HairSimulationResource)
	{
		BeginUpdateResourceRHI(HairSimulationResource);
	}
}

void UGroomAsset::ReleaseResource()
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

	if (HairSimulationResource)
	{
		FHairStrandsResource* InResource = HairSimulationResource;
		ENQUEUE_RENDER_COMMAND(ReleaseHairStrandsResourceCommand)(
			[InResource](FRHICommandList& RHICmdList)
		{
			InResource->ReleaseResource();
			delete InResource;
		});
		HairSimulationResource = nullptr;
	}
}

void UGroomAsset::Reset()
{
	ReleaseResource();

	HairRenderData.Reset();
	HairSimulationData.Reset();

	RenderHairGroups.Reset();
	SimulationHairGroups.Reset();
}

void UGroomAsset::PostLoad()
{
	Super::PostLoad();

	// If the asset was previously serialized without hair sim data, generate it here
	bool bAutoGeneratedGuides = false;
	if (HairSimulationData.StrandsPoints.Num() == 0 || HairSimulationData.StrandsCurves.Num() == 0)
	{
		DecimateStrandData(HairRenderData, FMath::Clamp(HairToGuideDensity, 0.f, 1.f), HairSimulationData);
		bAutoGeneratedGuides = true;
	}

	// If the asset was previously serialized without group settings, fill them out here
	if (RenderHairGroups.Num() == 0)
	{
		FHairGroupRenderSettings& GroupSettings = RenderHairGroups.AddDefaulted_GetRef();
		GroupSettings.GroupID = 0;
		GroupSettings.NumCurves = HairRenderData.GetNumCurves();
	}

	if (SimulationHairGroups.Num() == 0)
	{
		FHairGroupSimulationSettings& GroupSettings = SimulationHairGroups.AddDefaulted_GetRef();
		GroupSettings.GroupID = 0;
		GroupSettings.NumCurves = HairSimulationData.GetNumCurves();
		GroupSettings.bIsAutoGenerated = bAutoGeneratedGuides;
	}

	if (!IsTemplate())
	{
		InitResource();
	}
}

void UGroomAsset::BeginDestroy()
{
	ReleaseResource();
	Super::BeginDestroy();
}

#if WITH_EDITOR
void UGroomAsset::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateResource();
}
#endif // WITH_EDITOR

#if WITH_EDITORONLY_DATA
void UGroomAsset::GetAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
{
	if (AssetImportData)
	{
		OutTags.Add(FAssetRegistryTag(SourceFileTagName(), AssetImportData->GetSourceData().ToJson(), FAssetRegistryTag::TT_Hidden));
	}

	Super::GetAssetRegistryTags(OutTags);
}

void UGroomAsset::PostInitProperties()
{
	if (!HasAnyFlags(RF_ClassDefaultObject))
	{
		AssetImportData = NewObject<UAssetImportData>(this, TEXT("AssetImportData"));
	}

	Super::PostInitProperties();
}
#endif


