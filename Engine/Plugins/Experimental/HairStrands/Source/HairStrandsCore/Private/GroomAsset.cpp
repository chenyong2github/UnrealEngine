// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "GroomAsset.h"
#include "GroomBuilder.h"
#include "RenderingThread.h"
#include "Engine/AssetUserData.h"
#include "HairStrandsVertexFactory.h"
#include "Misc/Paths.h"

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
void CreateBuffer(uint32 InVertexCount, FRWBuffer& OutBuffer)
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

FHairStrandsRestResource::FHairStrandsRestResource(const FHairStrandsDatas::FRenderData& HairStrandRenderData, const FVector& InPositionOffset) :
	RestPositionBuffer(), AttributeBuffer(), MaterialBuffer(), PositionOffset(InPositionOffset), RenderData(HairStrandRenderData)
{}

void FHairStrandsRestResource::InitRHI()
{
	const TArray<FHairStrandsPositionFormat::Type>& RenderingPositions	 = RenderData.RenderingPositions;
	const TArray<FHairStrandsAttributeFormat::Type>& RenderingAttributes = RenderData.RenderingAttributes;
	const TArray<FHairStrandsMaterialFormat::Type>& RenderingMaterials	 = RenderData.RenderingMaterials;

	CreateBuffer<FHairStrandsPositionFormat>(RenderingPositions, RestPositionBuffer);
	CreateBuffer<FHairStrandsAttributeFormat>(RenderingAttributes, AttributeBuffer);
	CreateBuffer<FHairStrandsMaterialFormat>(RenderingMaterials, MaterialBuffer);
}

void FHairStrandsRestResource::ReleaseRHI()
{
	RestPositionBuffer.Release();
	AttributeBuffer.Release();
	MaterialBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsDeformedResource::FHairStrandsDeformedResource(const FHairStrandsDatas::FRenderData& HairStrandRenderData, bool bInInitializedData) :
	RenderData(HairStrandRenderData), bInitializedData(bInInitializedData)
{}

void FHairStrandsDeformedResource::InitRHI()
{
	const uint32 VertexCount = RenderData.RenderingPositions.Num();
	if (bInitializedData)
	{
		CreateBuffer<FHairStrandsPositionFormat>(RenderData.RenderingPositions, DeformedPositionBuffer[0]);
		CreateBuffer<FHairStrandsPositionFormat>(RenderData.RenderingPositions, DeformedPositionBuffer[1]);
	}
	else
	{
		CreateBuffer<FHairStrandsPositionFormat>(VertexCount, DeformedPositionBuffer[0]);
		CreateBuffer<FHairStrandsPositionFormat>(VertexCount, DeformedPositionBuffer[1]);
	}
	CreateBuffer<FHairStrandsTangentFormat>(VertexCount * FHairStrandsTangentFormat::ComponentCount, TangentBuffer);
}

void FHairStrandsDeformedResource::ReleaseRHI()
{
	DeformedPositionBuffer[0].Release();
	DeformedPositionBuffer[1].Release();
	TangentBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsRootResource::FHairStrandsRootResource(const FHairStrandsDatas* HairStrandsDatas, uint32 LODCount):
	RootCount(HairStrandsDatas ? HairStrandsDatas->GetNumCurves() : 0)
{
	if (!HairStrandsDatas)
		return;

	const uint32 CurveCount = HairStrandsDatas->GetNumCurves();
	CurveIndices.SetNum(HairStrandsDatas->GetNumPoints());
	RootPositions.SetNum(RootCount);
	RootNormals.SetNum(RootCount);

	for (uint32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
	{
		const uint32 RootIndex = HairStrandsDatas->StrandsCurves.CurvesOffset[CurveIndex];
		const uint32 PointCount = HairStrandsDatas->StrandsCurves.CurvesCount[CurveIndex];
		for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			CurveIndices[RootIndex + PointIndex] = CurveIndex; // RootIndex;
		}

		check(PointCount > 1);

		const FVector P0 = HairStrandsDatas->StrandsPoints.PointsPosition[RootIndex];
		const FVector P1 = HairStrandsDatas->StrandsPoints.PointsPosition[RootIndex+1];
		FVector N0 = (P1 - P0).GetSafeNormal();

		// Fallback in case the initial points are too close (this happens on certain assets)
		if (FVector::DotProduct(N0, N0) == 0)
		{
			N0 = FVector(0, 0, 1);
		}

		FHairStrandsRootPositionFormat::Type P;
		P.X = P0.X;
		P.Y = P0.Y;
		P.Z = P0.Z;
		P.W = 1;

		FHairStrandsRootNormalFormat::Type N;
		N.X = N0.X;
		N.Y = N0.Y;
		N.Z = N0.Z;
		N.W = 0;

		RootPositions[CurveIndex] = P;
		RootNormals[CurveIndex] = N;
	}
	
	MeshProjectionLODs.SetNum(LODCount);
	uint32 LODIndex = 0;
	for (FMeshProjectionLOD& MeshProjectionLOD : MeshProjectionLODs)
	{
		MeshProjectionLOD.Status = FMeshProjectionLOD::EStatus::Invalid;
		MeshProjectionLOD.RestRootOffset = HairStrandsDatas->BoundingBox.GetCenter();
		MeshProjectionLOD.LODIndex = LODIndex++;
	}
}

void FHairStrandsRootResource::InitRHI()
{
	if (CurveIndices.Num() > 0)
	{
		CreateBuffer<FHairStrandsIndexFormat>(CurveIndices, VertexToCurveIndexBuffer);
		CreateBuffer<FHairStrandsRootPositionFormat>(RootPositions, RootPositionBuffer);
		CreateBuffer<FHairStrandsRootNormalFormat>(RootNormals, RootNormalBuffer);	
		for (FMeshProjectionLOD& LOD : MeshProjectionLODs)
		{
			LOD.Status = FHairStrandsRootResource::FMeshProjectionLOD::EStatus::Initialized;

			// Create buffers. Initialization will be done by render passes
			CreateBuffer<FHairStrandsCurveTriangleIndexFormat>(RootCount, LOD.RootTriangleIndexBuffer);
			CreateBuffer<FHairStrandsCurveTriangleBarycentricFormat>(RootCount, LOD.RootTriangleBarycentricBuffer);
			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootCount, LOD.RestRootTrianglePosition0Buffer);
			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootCount, LOD.RestRootTrianglePosition1Buffer);
			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootCount, LOD.RestRootTrianglePosition2Buffer);

			// Strand hair roots translation and rotation in triangle-deformed position relative to the bound triangle 
			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootCount, LOD.DeformedRootTrianglePosition0Buffer);
			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootCount, LOD.DeformedRootTrianglePosition1Buffer);
			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootCount, LOD.DeformedRootTrianglePosition2Buffer);
		}
	}
}

void FHairStrandsRootResource::ReleaseRHI()
{
	RootPositionBuffer.Release();
	RootNormalBuffer.Release();
	VertexToCurveIndexBuffer.Release();

	for (FMeshProjectionLOD& LOD : MeshProjectionLODs)
	{
		LOD.Status = FHairStrandsRootResource::FMeshProjectionLOD::EStatus::Invalid;
		LOD.RootTriangleIndexBuffer.Release();
		LOD.RootTriangleBarycentricBuffer.Release();
		LOD.RestRootTrianglePosition0Buffer.Release();
		LOD.RestRootTrianglePosition1Buffer.Release();
		LOD.RestRootTrianglePosition2Buffer.Release();
		LOD.DeformedRootTrianglePosition0Buffer.Release();
		LOD.DeformedRootTrianglePosition1Buffer.Release();
		LOD.DeformedRootTrianglePosition2Buffer.Release();
	}
	MeshProjectionLODs.Empty();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsInterpolationResource::FHairStrandsInterpolationResource(const FHairStrandsInterpolationDatas::FRenderData& InterpolationRenderData, const FHairStrandsDatas& SimDatas) :
	Interpolation0Buffer(), Interpolation1Buffer(), RenderData(InterpolationRenderData)
{
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
	CreateBuffer<FHairStrandsInterpolation0Format>(RenderData.Interpolation0, Interpolation0Buffer);
	CreateBuffer<FHairStrandsInterpolation1Format>(RenderData.Interpolation1, Interpolation1Buffer);
	CreateBuffer<FHairStrandsRootIndexFormat>(SimRootPointIndex, SimRootPointIndexBuffer);
	SimRootPointIndex.SetNum(0);
}

void FHairStrandsInterpolationResource::ReleaseRHI()
{
	Interpolation0Buffer.Release();
	Interpolation1Buffer.Release();
	SimRootPointIndexBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

#if RHI_RAYTRACING
// RT geometry is built to for a cross around the fiber.
// 4 triangles per hair vertex => 12 vertices per hair vertex
FHairStrandsRaytracingResource::FHairStrandsRaytracingResource(const FHairStrandsDatas& InData) :
	PositionBuffer(), VertexCount(InData.GetNumPoints()*12)  
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

	Ar << HairGroupsData;
}

void UGroomAsset::InitResource()
{
	for (FHairGroupData& GroupData : HairGroupsData)
	{
		GroupData.HairStrandsRestResource = new FHairStrandsRestResource(GroupData.HairRenderData.RenderData, GroupData.HairRenderData.BoundingBox.GetCenter());

		BeginInitResource(GroupData.HairStrandsRestResource);

		GroupData.HairSimulationRestResource = new FHairStrandsRestResource(GroupData.HairSimulationData.RenderData, GroupData.HairSimulationData.BoundingBox.GetCenter());

		BeginInitResource(GroupData.HairSimulationRestResource);
	}
}

void UGroomAsset::UpdateResource()
{
	for (FHairGroupData& GroupData : HairGroupsData)
	{
		if (GroupData.HairStrandsRestResource)
		{
			BeginUpdateResourceRHI(GroupData.HairStrandsRestResource);
		}

		if (GroupData.HairSimulationRestResource)
		{
			BeginUpdateResourceRHI(GroupData.HairSimulationRestResource);
		}
	}
}

void UGroomAsset::ReleaseResource()
{
	for (FHairGroupData& GroupData : HairGroupsData)
	{
		if (GroupData.HairStrandsRestResource)
		{
			FHairStrandsRestResource* InResource = GroupData.HairStrandsRestResource;
			ENQUEUE_RENDER_COMMAND(ReleaseHairStrandsResourceCommand)(
				[InResource](FRHICommandList& RHICmdList)
			{
				InResource->ReleaseResource();
				delete InResource;
			});
			GroupData.HairStrandsRestResource = nullptr;
		}

		if (GroupData.HairSimulationRestResource)
		{
			FHairStrandsRestResource* InResource = GroupData.HairSimulationRestResource;
			ENQUEUE_RENDER_COMMAND(ReleaseHairStrandsResourceCommand)(
				[InResource](FRHICommandList& RHICmdList)
			{
				InResource->ReleaseResource();
				delete InResource;
			});
			GroupData.HairSimulationRestResource = nullptr;
		}
	}
}

void UGroomAsset::Reset()
{
	ReleaseResource();

	HairGroupsInfo.Reset();
	HairGroupsData.Reset();
}

void UGroomAsset::PostLoad()
{
	Super::PostLoad();

	check(HairGroupsData.Num() > 0);
	if (HairGroupsData[0].HairSimulationData.GetNumCurves() == 0 || HairGroupsData[0].HairInterpolationData.Num() == 0)
	{
		FGroomBuilder::BuildData(this, 2, 0, false, false);
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
	OnGroomAssetChanged.Broadcast();
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

int32 UGroomAsset::GetNumHairGroups() const
{
	return HairGroupsData.Num();
}

FArchive& operator<<(FArchive& Ar, FHairGroupData& GroupData)
{
	GroupData.HairRenderData.Serialize(Ar);
	GroupData.HairSimulationData.Serialize(Ar);
	GroupData.HairInterpolationData.Serialize(Ar);

	return Ar;
}

void UGroomAsset::AddAssetUserData(UAssetUserData* InUserData)
{
	if (InUserData != nullptr)
	{
		UAssetUserData* ExistingData = GetAssetUserDataOfClass(InUserData->GetClass());
		if (ExistingData != nullptr)
		{
			AssetUserData.Remove(ExistingData);
		}
		AssetUserData.Add(InUserData);
	}
}

UAssetUserData* UGroomAsset::GetAssetUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != nullptr && Datum->IsA(InUserDataClass))
		{
			return Datum;
		}
	}
	return NULL;
}

void UGroomAsset::RemoveUserDataOfClass(TSubclassOf<UAssetUserData> InUserDataClass)
{
	for (int32 DataIdx = 0; DataIdx < AssetUserData.Num(); DataIdx++)
	{
		UAssetUserData* Datum = AssetUserData[DataIdx];
		if (Datum != nullptr && Datum->IsA(InUserDataClass))
		{
			AssetUserData.RemoveAt(DataIdx);
			return;
		}
	}
}

const TArray<UAssetUserData*>* UGroomAsset::GetAssetUserDataArray() const
{
	return &AssetUserData;
}