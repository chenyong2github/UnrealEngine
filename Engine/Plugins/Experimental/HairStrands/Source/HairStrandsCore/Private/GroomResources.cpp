// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomResources.h"
#include "EngineUtils.h"
#include "GroomAssetImportData.h"
#include "GroomBuilder.h"
#include "GroomImportOptions.h"
#include "GroomSettings.h"
#include "RenderingThread.h"
#include "Engine/AssetUserData.h"
#include "HairStrandsVertexFactory.h"
#include "Misc/Paths.h"
#include "Serialization/LargeMemoryReader.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "UObject/PhysicsObjectVersion.h"
#include "UObject/ReleaseObjectVersion.h"
#include "NiagaraSystem.h"
#include "Async/ParallelFor.h"
#include "RenderGraph.h"
#include "RenderGraphUtils.h"
#include "RenderTargetPool.h"

/////////////////////////////////////////////////////////////////////////////////////////

void UploadDataToBuffer(FReadBuffer& OutBuffer, uint32 DataSizeInBytes, void* InCpuData)
{
	void* BufferData = RHILockVertexBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memcpy(BufferData, InCpuData, DataSizeInBytes);
	RHIUnlockVertexBuffer(OutBuffer.Buffer);
}

void UploadDataToBuffer(FRWBufferStructured& OutBuffer, uint32 DataSizeInBytes, void* InCpuData)
{
	void* BufferData = RHILockStructuredBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memcpy(BufferData, InCpuData, DataSizeInBytes);
	RHIUnlockStructuredBuffer(OutBuffer.Buffer);
}

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

static UTexture2D* CreateCardTexture(FIntPoint Resolution)
{
	UTexture2D* Out = nullptr;

	// Pass NAME_None as name to ensure an unique name is picked, so GC dont delete the new texture when it wants to delete the old one 
	Out = NewObject<UTexture2D>(GetTransientPackage(), NAME_None /*TEXT("ProceduralFollicleMaskTexture")*/, RF_Transient);
	Out->AddToRoot();
	Out->PlatformData = new FTexturePlatformData();
	Out->PlatformData->SizeX = Resolution.X;
	Out->PlatformData->SizeY = Resolution.Y;
	Out->PlatformData->PixelFormat = PF_R32_FLOAT;
	Out->SRGB = false;

	const uint32 MipCount = 1; // FMath::Min(FMath::FloorLog2(Resolution), 5u);// Don't need the full chain
	for (uint32 MipIt = 0; MipIt < MipCount; ++MipIt)
	{
		const uint32 MipResolutionX = Resolution.X >> MipIt;
		const uint32 MipResolutionY = Resolution.Y >> MipIt;
		const uint32 SizeInBytes = sizeof(float) * MipResolutionX * MipResolutionY;

		FTexture2DMipMap* MipMap = new FTexture2DMipMap();
		Out->PlatformData->Mips.Add(MipMap);
		MipMap->SizeX = MipResolutionX;
		MipMap->SizeY = MipResolutionY;
		MipMap->BulkData.Lock(LOCK_READ_WRITE);
		float* MipMemory = (float*)MipMap->BulkData.Realloc(SizeInBytes);
		for (uint32 Y = 0; Y < MipResolutionY; Y++)
			for (uint32 X = 0; X < MipResolutionX; X++)
			{
				MipMemory[X + Y * MipResolutionY] = X / float(MipResolutionX);
			}
		//FMemory::Memzero(MipMemory, SizeInBytes);
		MipMap->BulkData.Unlock();
	}
	Out->UpdateResource();

	return Out;
}

/////////////////////////////////////////////////////////////////////////////////////////

void FHairCardIndexBuffer::InitRHI()
{
	const uint32 DataSizeInBytes = FHairCardsIndexFormat::SizeInByte * Indices.Num();

	FRHIResourceCreateInfo CreateInfo;
	void* Buffer = nullptr;
	IndexBufferRHI = RHICreateAndLockIndexBuffer(FHairCardsIndexFormat::SizeInByte, DataSizeInBytes, BUF_Static, CreateInfo, Buffer);
	FMemory::Memcpy(Buffer, Indices.GetData(), DataSizeInBytes);
	RHIUnlockIndexBuffer(IndexBufferRHI);
}

FHairCardsRestResource::FHairCardsRestResource(const FHairCardsDatas::FRenderData& InRenderData, uint32 InVertexCount, uint32 InPrimitiveCount) :
	RestPositionBuffer(),
	RestIndexBuffer(InRenderData.Indices),
	VertexCount(InVertexCount),
	PrimitiveCount(InPrimitiveCount),
	NormalsBuffer(),
	UVsBuffer(),
	RenderData(InRenderData)
{

}

void FHairCardsRestResource::InitRHI()
{
	CreateBuffer<FHairCardsPositionFormat>(RenderData.Positions, RestPositionBuffer);
	CreateBuffer<FHairCardsNormalFormat>(RenderData.Normals, NormalsBuffer);
	CreateBuffer<FHairCardsUVFormat>(RenderData.UVs, UVsBuffer);

	FSamplerStateRHIRef DefaultSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
	DepthSampler = DefaultSampler;
	TangentSampler = DefaultSampler;
	CoverageSampler = DefaultSampler;
	AttributeSampler = DefaultSampler;
}

void FHairCardsRestResource::ReleaseRHI()
{
	RestPositionBuffer.Release();
	NormalsBuffer.Release();
	UVsBuffer.Release();
}

void FHairCardsRestResource::InitResource()
{
	FRenderResource::InitResource();
	RestIndexBuffer.InitResource();
}

void FHairCardsRestResource::ReleaseResource()
{
	FRenderResource::ReleaseResource();
	RestIndexBuffer.ReleaseResource();
}

/////////////////////////////////////////////////////////////////////////////////////////
FHairCardsProceduralResource::FHairCardsProceduralResource(const FHairCardsProceduralDatas::FRenderData& InRenderData, const FIntPoint& InAtlasResolution, const FHairCardsVoxel& InVoxel):
	CardBoundCount(InRenderData.ClusterBounds.Num()),
	AtlasResolution(InAtlasResolution),
	AtlasRectBuffer(),
	LengthBuffer(),
	CardItToClusterBuffer(),
	ClusterIdToVerticesBuffer(),
	ClusterBoundBuffer(),
	CardsStrandsPositions(),
	CardsStrandsAttributes(),
	RenderData(InRenderData)
{
	CardVoxel = InVoxel;
}

void FHairCardsProceduralResource::InitRHI()
{
	CreateBuffer<FHairCardsAtlasRectFormat>(RenderData.CardsRect, AtlasRectBuffer);
	CreateBuffer<FHairCardsDimensionFormat>(RenderData.CardsLengths, LengthBuffer);

	CreateBuffer<FHairCardsOffsetAndCount>(RenderData.CardItToCluster, CardItToClusterBuffer);
	CreateBuffer<FHairCardsOffsetAndCount>(RenderData.ClusterIdToVertices, ClusterIdToVerticesBuffer);
	CreateBuffer<FHairCardsBoundsFormat>(RenderData.ClusterBounds, ClusterBoundBuffer);

	CreateBuffer<FHairCardsVoxelDensityFormat>(RenderData.VoxelDensity, CardVoxel.DensityBuffer);
	CreateBuffer<FHairCardsVoxelTangentFormat>(RenderData.VoxelTangent, CardVoxel.TangentBuffer);
	CreateBuffer<FHairCardsVoxelTangentFormat>(RenderData.VoxelNormal, CardVoxel.NormalBuffer);

	CreateBuffer<FHairCardsStrandsPositionFormat>(RenderData.CardsStrandsPositions, CardsStrandsPositions);
	CreateBuffer<FHairCardsStrandsAttributeFormat>(RenderData.CardsStrandsAttributes, CardsStrandsAttributes);
}

void FHairCardsProceduralResource::ReleaseRHI()
{
	AtlasRectBuffer.Release();
	LengthBuffer.Release();

	CardItToClusterBuffer.Release();
	ClusterIdToVerticesBuffer.Release();
	ClusterBoundBuffer.Release();
	CardsStrandsPositions.Release();
	CardsStrandsAttributes.Release();

	CardVoxel.DensityBuffer.Release();
	CardVoxel.TangentBuffer.Release();
	CardVoxel.NormalBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairCardsDeformedResource::FHairCardsDeformedResource(const FHairCardsDatas::FRenderData& HairCardsRenderData, bool bInInitializedData) :
	RenderData(HairCardsRenderData), bInitializedData(bInInitializedData)
{}

void FHairCardsDeformedResource::InitRHI()
{
	const uint32 VertexCount = RenderData.Positions.Num();
	if (bInitializedData)
	{
		CreateBuffer<FHairCardsPositionFormat>(RenderData.Positions, DeformedPositionBuffer[0]);
		CreateBuffer<FHairCardsPositionFormat>(RenderData.Positions, DeformedPositionBuffer[1]);
	}
	else
	{
		CreateBuffer<FHairCardsPositionFormat>(VertexCount, DeformedPositionBuffer[0]);
		CreateBuffer<FHairCardsPositionFormat>(VertexCount, DeformedPositionBuffer[1]);
	}
}

void FHairCardsDeformedResource::ReleaseRHI()
{
	DeformedPositionBuffer[0].Release();
	DeformedPositionBuffer[1].Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairMeshesResource::FHairMeshesResource(const FHairMeshesDatas::FRenderData& InRenderData, uint32 InVertexCount, uint32 InPrimitiveCount) :
	PositionBuffer(),
	IndexBuffer(InRenderData.Indices),
	VertexCount(InVertexCount),
	PrimitiveCount(InPrimitiveCount),
	NormalsBuffer(),
	UVsBuffer(),
	RenderData(InRenderData)
{
	check(VertexCount > 0);
	check(IndexBuffer.Indices.Num() > 0);
}

void FHairMeshesResource::InitRHI()
{
	CreateBuffer<FHairCardsPositionFormat>(RenderData.Positions, PositionBuffer);
	CreateBuffer<FHairCardsNormalFormat>(RenderData.Normals, NormalsBuffer);
	CreateBuffer<FHairCardsUVFormat>(RenderData.UVs, UVsBuffer);
}

void FHairMeshesResource::ReleaseRHI()
{
	PositionBuffer.Release();
	NormalsBuffer.Release();
	UVsBuffer.Release();
}

void FHairMeshesResource::InitResource()
{
	FRenderResource::InitResource();
	IndexBuffer.InitResource();
}

void FHairMeshesResource::ReleaseResource()
{
	FRenderResource::ReleaseResource();
	IndexBuffer.ReleaseResource();

}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsRestResource::FHairStrandsRestResource(const FHairStrandsDatas::FRenderData& HairStrandRenderData, const FVector& InPositionOffset) :
	RestPositionBuffer(), AttributeBuffer(), MaterialBuffer(), PositionOffset(InPositionOffset), RenderData(HairStrandRenderData)
{}

void FHairStrandsRestResource::InitRHI()
{
	const TArray<FHairStrandsPositionFormat::Type>& Positions = RenderData.Positions;
	const TArray<FHairStrandsAttributeFormat::Type>& Attributes = RenderData.Attributes;
	const TArray<FHairStrandsMaterialFormat::Type>& Materials = RenderData.Materials;
	const TArray<FHairStrandsRootIndexFormat::Type>& RootIndices = RenderData.RootIndices;

	CreateBuffer<FHairStrandsPositionFormat>(Positions, RestPositionBuffer);
	CreateBuffer<FHairStrandsAttributeFormat>(Attributes, AttributeBuffer);
	CreateBuffer<FHairStrandsMaterialFormat>(Materials, MaterialBuffer);
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
	const uint32 VertexCount = RenderData.Positions.Num();
	if (bInitializedData)
	{
		CreateBuffer<FHairStrandsPositionFormat>(RenderData.Positions, DeformedPositionBuffer[0]);
		CreateBuffer<FHairStrandsPositionFormat>(RenderData.Positions, DeformedPositionBuffer[1]);
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

struct FClusterGrid
{
	struct FCurve
	{
		FCurve()
		{
			for (uint8 LODIt = 0; LODIt < FHairStrandsClusterCullingResource::MaxLOD; ++LODIt)
			{
				CountPerLOD[LODIt] = 0;
			}
		}
		uint32 Offset = 0;
		uint32 Count = 0;
		float Area = 0;
		float AvgRadius = 0;
		float MaxRadius = 0;
		uint32 CountPerLOD[FHairStrandsClusterCullingResource::MaxLOD];
	};

	struct FCluster
	{
		float CurveAvgRadius = 0;
		float CurveMaxRadius = 0;
		float RootBoundRadius = 0;
		float Area = 0;
		TArray<FCurve> ClusterCurves;
	};

	FClusterGrid(const FIntVector& InResolution, const FVector& InMinBound, const FVector& InMaxBound)
	{
		MinBound = InMinBound;
		MaxBound = InMaxBound;
		GridResolution = InResolution;
		Clusters.SetNum(GridResolution.X * GridResolution.Y * GridResolution.Z);
	}

	FORCEINLINE bool IsValid(const FIntVector& P) const
	{
		return	0 <= P.X && P.X < GridResolution.X &&
			0 <= P.Y && P.Y < GridResolution.Y &&
			0 <= P.Z && P.Z < GridResolution.Z;
	}

	FORCEINLINE FIntVector ClampToVolume(const FIntVector& CellCoord, bool& bIsValid) const
	{
		bIsValid = IsValid(CellCoord);
		return FIntVector(
			FMath::Clamp(CellCoord.X, 0, GridResolution.X - 1),
			FMath::Clamp(CellCoord.Y, 0, GridResolution.Y - 1),
			FMath::Clamp(CellCoord.Z, 0, GridResolution.Z - 1));
	}

	FORCEINLINE FIntVector ToCellCoord(const FVector& P) const
	{
		bool bIsValid = false;
		const FVector F = ((P - MinBound) / (MaxBound - MinBound));
		const FIntVector CellCoord = FIntVector(FMath::FloorToInt(F.X * GridResolution.X), FMath::FloorToInt(F.Y * GridResolution.Y), FMath::FloorToInt(F.Z * GridResolution.Z));
		return ClampToVolume(CellCoord, bIsValid);
	}

	uint32 ToIndex(const FIntVector& CellCoord) const
	{
		uint32 CellIndex = CellCoord.X + CellCoord.Y * GridResolution.X + CellCoord.Z * GridResolution.X * GridResolution.Y;
		check(CellIndex < uint32(Clusters.Num()));
		return CellIndex;
	}

	void InsertRenderingCurve(FCurve& Curve, const FVector& Root)
	{
		FIntVector CellCoord = ToCellCoord(Root);
		uint32 Index = ToIndex(CellCoord);
		FCluster& Cluster = Clusters[Index];
		Cluster.ClusterCurves.Add(Curve);
	}

	FVector MinBound;
	FVector MaxBound;
	FIntVector GridResolution;
	TArray<FCluster> Clusters;
};

static void DecimateCurve(
	const TArray<FVector>& InPoints,
	const uint32 InOffset,
	const uint32 InCount,
	const TArray<FHairLODSettings>& InSettings,
	uint32* OutCountPerLOD,
	TArray<uint8>& OutVertexLODMask)
{
	// Insure that all settings have more and more agressive, and rectify it is not the case.
	TArray<FHairLODSettings> Settings = InSettings;
	{
		float PrevFactor = 1;
		float PrevAngle = 0;
		for (FHairLODSettings& S : Settings)
		{
			if (S.VertexDecimation > PrevFactor)
			{
				S.VertexDecimation = PrevFactor;
			}

			if (S.AngularThreshold < PrevAngle)
			{
				S.AngularThreshold = PrevAngle;
			}

			PrevFactor = S.VertexDecimation;
			PrevAngle = S.AngularThreshold;
		}
	}

	check(InCount > 2);

	// Array containing the remaining vertex indices. This list get trimmed down as we process over all LODs.
	TArray<uint32> OutIndices;
	OutIndices.SetNum(InCount);
	for (uint32 CurveIt = 0; CurveIt < InCount; ++CurveIt)
	{
		OutIndices[CurveIt] = CurveIt;
	}

	const uint32 LODCount = Settings.Num();
	check(LODCount <= FHairStrandsClusterCullingResource::MaxLOD);

	for (uint8 LODIt = 0; LODIt < LODCount; ++LODIt)
	{
		const int32 LODTargetVertexCount = FMath::Max(2.f, FMath::CeilToFloat(InCount * Settings[LODIt].VertexDecimation));
		const float LODAngularThreshold = FMath::DegreesToRadians(Settings[LODIt].AngularThreshold);

		// 'bCanDecimate' tracks if it is possible to reduce the remaining vertives even more while respecting the user angular constrain
		bool bCanDecimate = true;
		while (OutIndices.Num() > LODTargetVertexCount && bCanDecimate)
		{
			float MinError = FLT_MAX;
			int32 ElementToRemove = -1;
			const uint32 Count = OutIndices.Num();
			for (uint32 IndexIt = 1; IndexIt < Count - 1; ++IndexIt)
			{
				const FVector& P0 = InPoints[InOffset + OutIndices[IndexIt - 1]];
				const FVector& P1 = InPoints[InOffset + OutIndices[IndexIt]];
				const FVector& P2 = InPoints[InOffset + OutIndices[IndexIt + 1]];

				const float Area = FVector::CrossProduct(P0 - P1, P2 - P1).Size() * 0.5f;

				//     P0 .       . P2
				//         \Inner/
				//   ` .    \   /
				// Thres(` . \^/ ) Angle
				//    --------.---------
				//            P1
				const FVector V0 = (P0 - P1).GetSafeNormal();
				const FVector V1 = (P2 - P1).GetSafeNormal();
				const float InnerAngle = FMath::Abs(FMath::Acos(FVector::DotProduct(V0, V1)));
				const float Angle = (PI - InnerAngle) * 0.5f;

				if (Area < MinError && Angle < LODAngularThreshold)
				{
					MinError = Area;
					ElementToRemove = IndexIt;
				}
			}
			bCanDecimate = ElementToRemove >= 0;
			if (bCanDecimate)
			{
				OutIndices.RemoveAt(ElementToRemove);
			}
		}

		OutCountPerLOD[LODIt] = OutIndices.Num();

		// For all remaining vertices, we mark them as 'used'/'valid' for the current LOD levl
		for (uint32 LocalIndex : OutIndices)
		{
			const uint32 VertexIndex = InOffset + LocalIndex;
			OutVertexLODMask[VertexIndex] |= 1 << LODIt;
		}
	}

	// Sanity check to insure that vertex LOD in a continuous fashion.
	for (uint32 VertexIt = 0; VertexIt < InCount; ++VertexIt)
	{
		const uint8 Mask = OutVertexLODMask[InOffset + VertexIt];
		check(Mask == 0 || Mask == 1 || Mask == 3 || Mask == 7 || Mask == 15 || Mask == 31 || Mask == 63 || Mask == 127 || Mask == 255);
	}
}

FHairStrandsClusterCullingResource::FHairStrandsClusterCullingResource(
	const FHairStrandsDatas& InRenStrandsData, 
	const float InGroomAssetRadius, 
	const FHairGroupsLOD& InSettings)
{
	const uint32 LODCount = FMath::Min(uint32(InSettings.LODs.Num()), MaxLOD);
	check(LODCount > 0);

	const uint32 RenCurveCount	= InRenStrandsData.GetNumCurves();
	VertexCount					= InRenStrandsData.GetNumPoints();
	check(VertexCount);

	// 1. Allocate cluster per voxel containing contains >=1 render curve root
	const FVector GroupMinBound = InRenStrandsData.BoundingBox.Min;
	FVector GroupMaxBound = InRenStrandsData.BoundingBox.Max;
	const float GroupRadius = FVector::Distance(GroupMaxBound, GroupMinBound) * 0.5f;

	// Compute the voxel volume resolution, and snap the max bound to the voxel grid
	FIntVector VoxelResolution = FIntVector::ZeroValue;
	{
		FVector VoxelResolutionF = (GroupMaxBound - GroupMinBound) / InSettings.ClusterWorldSize;
		VoxelResolution = FIntVector(FMath::CeilToInt(VoxelResolutionF.X), FMath::CeilToInt(VoxelResolutionF.Y), FMath::CeilToInt(VoxelResolutionF.Z));
		GroupMaxBound = GroupMinBound + FVector(VoxelResolution) * InSettings.ClusterWorldSize;
	}

	// 2. Insert all rendering curves into the voxel structure
	FClusterGrid ClusterGrid(VoxelResolution, GroupMinBound, GroupMaxBound);
	for (uint32 RenCurveIndex = 0; RenCurveIndex < RenCurveCount; ++RenCurveIndex)
	{
		FClusterGrid::FCurve RCurve;
		RCurve.Count  = InRenStrandsData.StrandsCurves.CurvesCount[RenCurveIndex];
		RCurve.Offset = InRenStrandsData.StrandsCurves.CurvesOffset[RenCurveIndex];
		RCurve.Area   = 0.0f;
		RCurve.AvgRadius = 0;
		RCurve.MaxRadius = 0;

		// Compute area of each curve to later compute area correction
		for (uint32 RenPointIndex = 0; RenPointIndex < RCurve.Count; ++RenPointIndex)
		{
			uint32 PointGlobalIndex = RenPointIndex + RCurve.Offset;
			const FVector& V0 = InRenStrandsData.StrandsPoints.PointsPosition[PointGlobalIndex];
			if (RenPointIndex > 0)
			{
				const FVector& V1 = InRenStrandsData.StrandsPoints.PointsPosition[PointGlobalIndex-1];
				FVector OutDir;
				float OutLength;
				(V1 - V0).ToDirectionAndLength(OutDir, OutLength);
				RCurve.Area += InRenStrandsData.StrandsPoints.PointsRadius[PointGlobalIndex] * OutLength;
			}

			const float PointRadius = InRenStrandsData.StrandsPoints.PointsRadius[PointGlobalIndex] * InRenStrandsData.StrandsCurves.MaxRadius;
			RCurve.AvgRadius += PointRadius;
			RCurve.MaxRadius = FMath::Max(RCurve.MaxRadius, PointRadius);
		}
		RCurve.AvgRadius /= FMath::Max(1u, RCurve.Count);

		const FVector Root = InRenStrandsData.StrandsPoints.PointsPosition[RCurve.Offset];
		ClusterGrid.InsertRenderingCurve(RCurve, Root);
	}

	// 3. Count non-empty clusters
	TArray<uint32> ValidClusterIndices;
	{
		uint32 GridLinearIndex = 0;
		ValidClusterIndices.Reserve(ClusterGrid.Clusters.Num() * 0.2);
		for (FClusterGrid::FCluster& Cluster : ClusterGrid.Clusters)
		{
			if (Cluster.ClusterCurves.Num() > 0)
			{
				ValidClusterIndices.Add(GridLinearIndex);
			}
			++GridLinearIndex;
		}
	}
	ClusterCount = ValidClusterIndices.Num();
	ClusterInfos.Init(FHairClusterInfo(), ClusterCount);
	VertexToClusterIds.SetNum(VertexCount);

	// Conservative allocation for inserting vertex indices for the various curves LOD
	uint32* RawClusterVertexIds = new uint32[LODCount * InRenStrandsData.GetNumPoints()];
	TAtomic<uint32> RawClusterVertexCount(0);

	// 4. Write out cluster information
	ClusterLODInfos.SetNum(LODCount * ClusterCount);
	TArray<uint8> VertexLODMasks;
	VertexLODMasks.SetNum(InRenStrandsData.GetNumPoints());

	// Local variable for being capture by the lambda
	TArray<FHairClusterInfo>& LocalClusterInfos = ClusterInfos;
	TArray<FHairClusterLODInfo>& LocalClusterLODInfos = ClusterLODInfos;
	TArray<uint32>& LocalVertexToClusterIds = VertexToClusterIds;
#define USE_PARALLE_FOR 1
#if USE_PARALLE_FOR
	ParallelFor(ClusterCount,
	[
		LODCount,
		InGroomAssetRadius,
		InSettings,
		&ValidClusterIndices,
		&InRenStrandsData,
		&ClusterGrid,
		&LocalClusterInfos,
		&LocalClusterLODInfos,
		&LocalVertexToClusterIds,
		&VertexLODMasks,
		&RawClusterVertexIds,
		&RawClusterVertexCount
	]
	(uint32 ClusterIt)
#else
	for (uint32 ClusterIt=0; ClusterIt<ClusterCount; ++ClusterIt)
#endif
	{
		const uint32 GridLinearIndex = ValidClusterIndices[ClusterIt];
		FClusterGrid::FCluster& Cluster = ClusterGrid.Clusters[GridLinearIndex];
		check(Cluster.ClusterCurves.Num() != 0);

		// 4.1 Sort curves
		// Sort curve to have largest area first, so that lower area curves with less influence are removed first.
		// This also helps the radius scaling to not explode.
		Cluster.ClusterCurves.Sort([](const FClusterGrid::FCurve& A, const FClusterGrid::FCurve& B) -> bool
		{
			return A.Area > B.Area;
		});

		// 4.2 Compute cluster's area & fill in the vertex to cluster ID mapping
		float ClusterArea = 0;
		FVector ClusterMinBound( FLT_MAX);
		FVector ClusterMaxBound(-FLT_MAX);

		FVector RootMinBound(FLT_MAX);
		FVector RootMaxBound(-FLT_MAX);

		Cluster.CurveMaxRadius = 0;
		Cluster.CurveAvgRadius = 0;
		Cluster.Area = 0;
		for (FClusterGrid::FCurve& ClusterCurve : Cluster.ClusterCurves)
		{
			for (uint32 RenPointIndex = 0; RenPointIndex < ClusterCurve.Count; ++RenPointIndex)
			{
				const uint32 PointGlobalIndex = RenPointIndex + ClusterCurve.Offset;
				LocalVertexToClusterIds[PointGlobalIndex] = ClusterIt;

				const FVector& P = InRenStrandsData.StrandsPoints.PointsPosition[PointGlobalIndex];
				{
					ClusterMinBound.X = FMath::Min(ClusterMinBound.X, P.X);
					ClusterMinBound.Y = FMath::Min(ClusterMinBound.Y, P.Y);
					ClusterMinBound.Z = FMath::Min(ClusterMinBound.Z, P.Z);

					ClusterMaxBound.X = FMath::Max(ClusterMaxBound.X, P.X);
					ClusterMaxBound.Y = FMath::Max(ClusterMaxBound.Y, P.Y);
					ClusterMaxBound.Z = FMath::Max(ClusterMaxBound.Z, P.Z);
				}

				if (RenPointIndex == 0)
				{
					RootMinBound.X = FMath::Min(RootMinBound.X, P.X);
					RootMinBound.Y = FMath::Min(RootMinBound.Y, P.Y);
					RootMinBound.Z = FMath::Min(RootMinBound.Z, P.Z);

					RootMaxBound.X = FMath::Max(RootMaxBound.X, P.X);
					RootMaxBound.Y = FMath::Max(RootMaxBound.Y, P.Y);
					RootMaxBound.Z = FMath::Max(RootMaxBound.Z, P.Z);
				}
			}
			Cluster.CurveMaxRadius  = FMath::Max(Cluster.CurveMaxRadius, ClusterCurve.MaxRadius);
			Cluster.CurveAvgRadius += ClusterCurve.AvgRadius;
			Cluster.Area		   += ClusterCurve.Area;
		}
		Cluster.CurveAvgRadius /= FMath::Max(1, Cluster.ClusterCurves.Num());
		Cluster.RootBoundRadius = (RootMaxBound - RootMinBound).GetMax() * 0.5f + Cluster.CurveAvgRadius;

		// Compute the max radius that a cluster can have. This is done by computing an estimate of the cluster coverage (using pre-computed LUT) 
		// and computing how much is visible
		// This supposes the radius is proportional to the radius of the roots bounding volume
		const float NormalizedAvgRadius = Cluster.CurveAvgRadius / Cluster.RootBoundRadius;
		const float ClusterCoverage = GetHairCoverage(Cluster.ClusterCurves.Num(), NormalizedAvgRadius);
		const float ClusterVisibleRadius = Cluster.RootBoundRadius * ClusterCoverage;

		const float ClusterRadius = FVector::Distance(ClusterMaxBound, ClusterMinBound) * 0.5f;


		// 4.3 Compute the number of curve per LOD
		// Compute LOD infos (vertx count, vertex offset, radius scale ...)
		// Compute the ratio of the cluster related the actual groom and scale the screen size accordingly
		TArray<uint32> LODCurveCount;
		LODCurveCount.SetNum(LODCount);
		for (uint8 LODIt = 0; LODIt < LODCount; ++LODIt)
		{
			LODCurveCount[LODIt] = FMath::Max(1u, uint32(FMath::CeilToInt(Cluster.ClusterCurves.Num() * InSettings.LODs[LODIt].CurveDecimation)));
		}

		// 4.4 Decimate each curve for all LODs
		// This fill in a bitfield per vertex which indiates on which LODs a vertex can be used
		for (uint32 CurveIt = 0, CurveCount = uint32(Cluster.ClusterCurves.Num()); CurveIt < CurveCount; ++CurveIt)
		{
			FClusterGrid::FCurve& ClusterCurve = Cluster.ClusterCurves[CurveIt];

			DecimateCurve(
				InRenStrandsData.StrandsPoints.PointsPosition,
				ClusterCurve.Offset,
				ClusterCurve.Count,
				InSettings.LODs,
				ClusterCurve.CountPerLOD,
				VertexLODMasks);
		}

		// 4.5 Record/Insert vertex indices for each LOD of the current cluster
		// Vertex offset is stored into the cluster LOD info
		// Stores the accumulated vertex count per LOD
		//
		// ClusterVertexIds contains the vertex index of curve belonging to a cluster.
		// Since for a given LOD, both the number of curve and vertices varies, we stores 
		// this information per LOD.
		//
		//  Global Vertex index
		//            v
		// ||0 1 2 3 4 5 6 7 8 9 ||0 1 3 5 7 9 ||0 5 9 | |0 1 2 3 4 5 6 7 || 0 1 5 7 ||0 9 ||||11 12 ...
		// ||____________________||____________||______| |________________||_________||____||||_____ _ _ 
		// ||        LOD 0			 LOD 1		 LOD2  | |    LOD 0			 LOD 1	  LOD2 ||||  LOD 0
		// ||__________________________________________| | ________________________________||||_____ _ _ 
		// |                   Curve 0								Curve 1				    ||   Curve 0
		// |________________________________________________________________________________||_____ _ _ 
		//										Cluster 0										Cluster 1

		TArray<uint32> LocalClusterVertexIds;
		LocalClusterVertexIds.Reserve(LODCount * Cluster.ClusterCurves.Num() * 32); // Guestimate pre-allocation (32 points per curve in average)

		FHairClusterInfo& ClusterInfo = LocalClusterInfos[ClusterIt];
		ClusterInfo.LODCount = LODCount;
		ClusterInfo.LODInfoOffset = LODCount * ClusterIt;
		for (uint8 LODIt = 0; LODIt < LODCount; ++LODIt)
		{
			FHairClusterLODInfo& ClusterLODInfo = LocalClusterLODInfos[ClusterInfo.LODInfoOffset + LODIt];
			ClusterLODInfo.VertexOffset = LocalClusterVertexIds.Num(); // At the end, it will be the offset at which the data are inserted into ClusterVertexIds
			ClusterLODInfo.VertexCount0 = 0;
			ClusterLODInfo.VertexCount1 = 0;
			ClusterLODInfo.RadiusScale0 = 0;
			ClusterLODInfo.RadiusScale1 = 0;

			const uint32 CurveCount = LODCurveCount[LODIt];
			const uint32 NextCurveCount = LODIt < LODCount-1 ? LODCurveCount[LODIt+1] : CurveCount;
			for (uint32 CurveIt = 0; CurveIt < CurveCount; ++CurveIt)
			{
				FClusterGrid::FCurve& ClusterCurve = Cluster.ClusterCurves[CurveIt];

				for (uint32 PointIt = 0; PointIt < ClusterCurve.Count; ++PointIt)
				{
					const uint32 GlobalPointIndex = PointIt + ClusterCurve.Offset;
					const uint8 LODMask = VertexLODMasks[GlobalPointIndex];
					if (LODMask & (1 << LODIt))
					{
						// Count the number of vertices for all curves in the cluster as well as the vertex 
						// of the remaining curves once the cluster has been decimated with the current LOD 
						// settings
						++ClusterLODInfo.VertexCount0;
						if (CurveIt < NextCurveCount)
						{
							++ClusterLODInfo.VertexCount1;
						}

						LocalClusterVertexIds.Add(GlobalPointIndex);
					}
				}
			}
		}

		// 4.5.1 Insert vertex indices for each LOD into the final array
		// Since this runs in parallel, we prefill LocalClusterVertexIds with 
		// all indices, then we insert the indices into the final array with a single allocation + memcopy
		// We also patch the vertex offset so that it is correct
		const uint32 AllocOffset = RawClusterVertexCount.AddExchange(LocalClusterVertexIds.Num());
		FMemory::Memcpy(RawClusterVertexIds + AllocOffset, LocalClusterVertexIds.GetData(), LocalClusterVertexIds.Num() * sizeof(uint32));
		for (uint8 LODIt = 0; LODIt < LODCount; ++LODIt)
		{
			FHairClusterLODInfo& ClusterLODInfo = LocalClusterLODInfos[ClusterInfo.LODInfoOffset + LODIt];
			ClusterLODInfo.VertexOffset += AllocOffset;
		}
		
		// 4.6 Compute the radius scaling to preserve the cluster apperance as we decimate 
		// the number of strands
		for (uint8 LODIt = 0; LODIt < LODCount; ++LODIt)
		{
			// Compute the visible area for various orientation? 
			// Reference: Stochastic Simplification of Aggregate Detail
			float  LODArea = 0;
			float  LODAvgRadiusRef = 0;
			float  LODMaxRadiusRef = 0;
			uint32 LODVertexCount = 0;

			const uint32 ClusterCurveCount = LODCurveCount[LODIt];
			for (uint32 CurveIt=0; CurveIt< ClusterCurveCount; ++CurveIt)
			{
				const FClusterGrid::FCurve& ClusterCurve = Cluster.ClusterCurves[CurveIt];
				LODVertexCount += ClusterCurve.Count;
				LODArea += ClusterCurve.Area;
				LODAvgRadiusRef += ClusterCurve.AvgRadius;
				LODMaxRadiusRef = FMath::Max(LODMaxRadiusRef, ClusterCurve.MaxRadius);
			}
			LODAvgRadiusRef /= ClusterCurveCount;

			// Compute what should be the average (normalized) radius of the strands, and scale it 
			// with the radius of the clusters/roots to get an actual world radius.
			const float LODAvgRadiusTarget = Cluster.RootBoundRadius * GetHairAvgRadius(ClusterCurveCount, ClusterCoverage);

			// Compute the ratio between the size of the cluster and the size of the groom (at rest position)
			// On the GPU, we compute the screen size of the cluster, and use the LOD screensize to know which 
			// LOD needs to be pick up. Since the screen area are setup by artists based the entire groom (not 
			// based on the cluster size), we precompute the correcting ratio here, and pre-scale the LOD screensize
			const float ScreenSizeScale = InSettings.ClusterScreenSizeScale * ClusterRadius / InGroomAssetRadius;

			float LODScale = LODAvgRadiusTarget / LODAvgRadiusRef;
			if (LODMaxRadiusRef * LODScale > ClusterVisibleRadius)
			{
				LODScale = FMath::Max(LODMaxRadiusRef, ClusterVisibleRadius) / LODMaxRadiusRef;
			}
			LODScale *= FMath::Max(InSettings.LODs[LODIt].ThicknessScale, 0.f);
			//if (LODMaxRadiusRef * LODScale > Cluster.RootBoundRadius)
			//{
			//	LODScale = Cluster.RootBoundRadius / LODMaxRadiusRef;
			//}

			ClusterInfo.ScreenSize[LODIt] = InSettings.LODs[LODIt].ScreenSize * ScreenSizeScale;
			ClusterInfo.bIsVisible[LODIt] = InSettings.LODs[LODIt].bVisible;
			FHairClusterLODInfo& ClusterLODInfo = LocalClusterLODInfos[ClusterInfo.LODInfoOffset + LODIt];
			ClusterLODInfo.RadiusScale0 = LODScale;
			ClusterLODInfo.RadiusScale1 = LODScale;
		}

		// Fill in transition radius between LOD to insure that the interpolation is continuous
		for (uint8 LODIt = 0; LODIt < LODCount-1; ++LODIt)
		{
			FHairClusterLODInfo& Curr = LocalClusterLODInfos[ClusterInfo.LODInfoOffset + LODIt];
			FHairClusterLODInfo& Next = LocalClusterLODInfos[ClusterInfo.LODInfoOffset + LODIt+1];
			Curr.RadiusScale1 = Next.RadiusScale0;
		}
	}
#if USE_PARALLE_FOR
	);
#endif

	// Compute the screen size of the entire group at which the groom need to change LOD
	for (uint32 LODIt=0; LODIt < LODCount; ++LODIt)
	{
		CPULODScreenSize.Add(InSettings.LODs[LODIt].ScreenSize);
		LODVisibility.Add(InSettings.LODs[LODIt].bVisible);
	}

	// Copy the final value to the array which will be used to copy data to the GPU.
	// This operations is not needer per se. We could just keep & use RawClusterVertexIds
	ClusterVertexIds.SetNum(RawClusterVertexCount);
	FMemory::Memcpy(ClusterVertexIds.GetData(), RawClusterVertexIds, RawClusterVertexCount * sizeof(uint32));

	delete[] RawClusterVertexIds;
}

inline uint32 to10Bits(float V)
{
	return FMath::Clamp(uint32(V * 1024), 0u, 1023u);
}

void FHairStrandsClusterCullingResource::InitRHI()
{
	check(uint32(ClusterInfos.Num()) == ClusterCount);
	check(uint32(VertexToClusterIds.Num()) == VertexCount);

	TArray<FHairClusterInfo::Packed> PackedClusterInfos;
	PackedClusterInfos.Reserve(ClusterInfos.Num());
	for (const FHairClusterInfo& Info : ClusterInfos)
	{
		FHairClusterInfo::Packed& PackedInfo = PackedClusterInfos.AddDefaulted_GetRef();
		PackedInfo.LODCount = FMath::Clamp(Info.LODCount, 0u, 0xFFu);
		PackedInfo.LODInfoOffset = FMath::Clamp(Info.LODInfoOffset, 0u, (1u<<24u)-1u);
		PackedInfo.LOD_ScreenSize_0 = to10Bits(Info.ScreenSize[0]);
		PackedInfo.LOD_ScreenSize_1 = to10Bits(Info.ScreenSize[1]);
		PackedInfo.LOD_ScreenSize_2 = to10Bits(Info.ScreenSize[2]);
		PackedInfo.LOD_ScreenSize_3 = to10Bits(Info.ScreenSize[3]);
		PackedInfo.LOD_ScreenSize_4 = to10Bits(Info.ScreenSize[4]);
		PackedInfo.LOD_ScreenSize_5 = to10Bits(Info.ScreenSize[5]);
		PackedInfo.LOD_ScreenSize_6 = to10Bits(Info.ScreenSize[6]);
		PackedInfo.LOD_ScreenSize_7 = to10Bits(Info.ScreenSize[7]);
		PackedInfo.LOD_bIsVisible = 0;
		for (uint32 LODIt = 0; LODIt < MaxLOD; ++LODIt)
		{
			if (Info.bIsVisible[LODIt])
			{
				PackedInfo.LOD_bIsVisible = PackedInfo.LOD_bIsVisible | (1 << LODIt);
			}
		}

		PackedInfo.Pad0 = 0;
		PackedInfo.Pad1 = 0;
		PackedInfo.Pad2 = 0;
	}
	ClusterInfoBuffer.Initialize(sizeof(FHairClusterInfo::Packed), PackedClusterInfos.Num());
	UploadDataToBuffer(ClusterInfoBuffer, sizeof(FHairClusterInfo::Packed) * PackedClusterInfos.Num(), PackedClusterInfos.GetData());

	ClusterLODInfoBuffer.Initialize(sizeof(FHairClusterLODInfo), ClusterLODInfos.Num());
	UploadDataToBuffer(ClusterLODInfoBuffer, sizeof(FHairClusterLODInfo) * ClusterLODInfos.Num(), ClusterLODInfos.GetData());

	ClusterVertexIdBuffer.Initialize(sizeof(uint32), ClusterVertexIds.Num(), EPixelFormat::PF_R32_UINT, BUF_Static);
	UploadDataToBuffer(ClusterVertexIdBuffer, sizeof(uint32) * ClusterVertexIds.Num(), ClusterVertexIds.GetData());

	VertexToClusterIdBuffer.Initialize(sizeof(uint32), VertexToClusterIds.Num(), EPixelFormat::PF_R32_UINT, BUF_Static);
	UploadDataToBuffer(VertexToClusterIdBuffer, sizeof(uint32) * VertexToClusterIds.Num(), VertexToClusterIds.GetData());
}

void FHairStrandsClusterCullingResource::ReleaseRHI()
{
	ClusterInfoBuffer.Release();
	VertexToClusterIdBuffer.Release();
	ClusterVertexIdBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsRestRootResource::FHairStrandsRestRootResource(const FHairStrandsRootData& InRootData):
RootData(InRootData)
{
	PopulateFromRootData();
}

FHairStrandsRestRootResource::FHairStrandsRestRootResource(const FHairStrandsDatas* HairStrandsDatas, uint32 LODCount, const TArray<uint32>& NumSamples):
	RootData(HairStrandsDatas, LODCount, NumSamples)
{
	PopulateFromRootData();
}

void FHairStrandsRestRootResource::PopulateFromRootData()
{
	uint32 LODIndex = 0;
	for (FHairStrandsRootData::FMeshProjectionLOD& MeshProjectionLOD : RootData.MeshProjectionLODs)
	{
		FLOD& LOD = LODs.AddDefaulted_GetRef();

		LOD.LODIndex = MeshProjectionLOD.LODIndex;
		LOD.Status = FLOD::EStatus::Invalid;
		LOD.SampleCount = MeshProjectionLOD.SampleCount;
	}
}

void FHairStrandsRestRootResource::InitRHI()
{
	if (RootData.VertexToCurveIndexBuffer.Num() > 0)
	{
		CreateBuffer<FHairStrandsIndexFormat>(RootData.VertexToCurveIndexBuffer, VertexToCurveIndexBuffer);
		CreateBuffer<FHairStrandsRootPositionFormat>(RootData.RootPositionBuffer, RootPositionBuffer);
		CreateBuffer<FHairStrandsRootNormalFormat>(RootData.RootNormalBuffer, RootNormalBuffer);
		
		check(LODs.Num() == RootData.MeshProjectionLODs.Num());
		for (uint32 LODIt=0, LODCount = LODs.Num(); LODIt<LODCount; ++LODIt)
		{
			FLOD& GPUData = LODs[LODIt];
			const FHairStrandsRootData::FMeshProjectionLOD& CPUData = RootData.MeshProjectionLODs[LODIt];

			const bool bHasValidCPUData = CPUData.RootTriangleBarycentricBuffer.Num() > 0;
			if (bHasValidCPUData)
			{
				GPUData.Status = FLOD::EStatus::Completed;

				check(CPUData.RootTriangleBarycentricBuffer.Num() > 0);
				CreateBuffer<FHairStrandsCurveTriangleBarycentricFormat>(CPUData.RootTriangleBarycentricBuffer, GPUData.RootTriangleBarycentricBuffer);

				check(CPUData.RootTriangleIndexBuffer.Num() > 0);
				CreateBuffer<FHairStrandsCurveTriangleIndexFormat>(CPUData.RootTriangleIndexBuffer, GPUData.RootTriangleIndexBuffer);

				check(CPUData.RestRootTrianglePosition0Buffer.Num() > 0);
				check(CPUData.RestRootTrianglePosition1Buffer.Num() > 0);
				check(CPUData.RestRootTrianglePosition2Buffer.Num() > 0);
				CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(CPUData.RestRootTrianglePosition0Buffer, GPUData.RestRootTrianglePosition0Buffer);
				CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(CPUData.RestRootTrianglePosition1Buffer, GPUData.RestRootTrianglePosition1Buffer);
				CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(CPUData.RestRootTrianglePosition2Buffer, GPUData.RestRootTrianglePosition2Buffer);
			}
			else
			{
				GPUData.Status = FLOD::EStatus::Initialized;

				CreateBuffer<FHairStrandsCurveTriangleBarycentricFormat>(RootData.RootCount, GPUData.RootTriangleBarycentricBuffer);
				CreateBuffer<FHairStrandsCurveTriangleIndexFormat>(RootData.RootCount, GPUData.RootTriangleIndexBuffer);

				// Create buffers. Initialization will be done by render passes
				CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootData.RootCount, GPUData.RestRootTrianglePosition0Buffer);
				CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootData.RootCount, GPUData.RestRootTrianglePosition1Buffer);
				CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootData.RootCount, GPUData.RestRootTrianglePosition2Buffer);
			}

			GPUData.SampleCount = CPUData.SampleCount;
			const bool bHasValidCPUWeights = CPUData.MeshSampleIndicesBuffer.Num() > 0;
			if(bHasValidCPUWeights)
			{
				//check(CPUData.MeshInterpolationWeightsBuffer.Num() == (CPUData.SampleCount+4) * (CPUData.SampleCount+4));
				check(CPUData.MeshSampleIndicesBuffer.Num() == CPUData.SampleCount);
				check(CPUData.RestSamplePositionsBuffer.Num() == CPUData.SampleCount);

				CreateBuffer<FHairStrandsWeightFormat>(CPUData.MeshInterpolationWeightsBuffer, GPUData.MeshInterpolationWeightsBuffer);
				CreateBuffer<FHairStrandsIndexFormat>(CPUData.MeshSampleIndicesBuffer, GPUData.MeshSampleIndicesBuffer);
				CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(CPUData.RestSamplePositionsBuffer, GPUData.RestSamplePositionsBuffer);
			}
			else
			{
				CreateBuffer<FHairStrandsWeightFormat>((CPUData.SampleCount+4) * (CPUData.SampleCount+4), GPUData.MeshInterpolationWeightsBuffer);
				CreateBuffer<FHairStrandsIndexFormat>(CPUData.SampleCount, GPUData.MeshSampleIndicesBuffer);
				CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(CPUData.SampleCount, GPUData.RestSamplePositionsBuffer);
			}
		}
	}
}

void FHairStrandsRestRootResource::ReleaseRHI()
{
	RootPositionBuffer.Release();
	RootNormalBuffer.Release();
	VertexToCurveIndexBuffer.Release();
	
	for (FLOD& GPUData : LODs)
	{
		GPUData.Status = FLOD::EStatus::Invalid;
		GPUData.RootTriangleIndexBuffer.Release();
		GPUData.RootTriangleBarycentricBuffer.Release();
		GPUData.RestRootTrianglePosition0Buffer.Release();
		GPUData.RestRootTrianglePosition1Buffer.Release();
		GPUData.RestRootTrianglePosition2Buffer.Release();
		GPUData.SampleCount = 0;
		GPUData.MeshInterpolationWeightsBuffer.Release();
		GPUData.MeshSampleIndicesBuffer.Release();
		GPUData.RestSamplePositionsBuffer.Release();
	}
	LODs.Empty();

	// Once empty, the MeshProjectionLODsneeds to be repopulate as it might be re-initialized. 
	// E.g., when a resource is updated, it is first released, then re-init. 
	PopulateFromRootData();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsDeformedRootResource::FHairStrandsDeformedRootResource()
{

}

FHairStrandsDeformedRootResource::FHairStrandsDeformedRootResource(const FHairStrandsRestRootResource* InRestResources)
{
	check(InRestResources);
	uint32 LODIndex = 0;
	RootCount = InRestResources->RootData.RootCount;
	for (const FHairStrandsRestRootResource::FLOD& InLOD : InRestResources->LODs)
	{
		FLOD& LOD = LODs.AddDefaulted_GetRef();

		LOD.Status = FLOD::EStatus::Invalid;
		LOD.LODIndex = InLOD.LODIndex;
		LOD.SampleCount = InLOD.SampleCount;
	}
}

void FHairStrandsDeformedRootResource::InitRHI()
{
	if (RootCount > 0)
	{
		for (FLOD& LOD : LODs)
		{		
			LOD.Status = FLOD::EStatus::Initialized;
			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(LOD.SampleCount, LOD.DeformedSamplePositionsBuffer);
			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(LOD.SampleCount + 4, LOD.MeshSampleWeightsBuffer);

			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootCount, LOD.DeformedRootTrianglePosition0Buffer);
			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootCount, LOD.DeformedRootTrianglePosition1Buffer);
			CreateBuffer<FHairStrandsMeshTrianglePositionFormat>(RootCount, LOD.DeformedRootTrianglePosition2Buffer);
		}
	}
}

void FHairStrandsDeformedRootResource::ReleaseRHI()
{
	for (FLOD& GPUData : LODs)
	{
		GPUData.Status = FLOD::EStatus::Invalid;
		GPUData.DeformedRootTrianglePosition0Buffer.Release();
		GPUData.DeformedRootTrianglePosition1Buffer.Release();
		GPUData.DeformedRootTrianglePosition2Buffer.Release();
		GPUData.DeformedSamplePositionsBuffer.Release();
		GPUData.MeshSampleWeightsBuffer.Release();
	}
	LODs.Empty();
}

/////////////////////////////////////////////////////////////////////////////////////////

FHairStrandsRootData::FHairStrandsRootData()
{

}

FHairStrandsRootData::FHairStrandsRootData(const FHairStrandsDatas* HairStrandsDatas, uint32 LODCount, const TArray<uint32>& NumSamples):
	RootCount(HairStrandsDatas ? HairStrandsDatas->GetNumCurves() : 0)
{
	if (!HairStrandsDatas)
		return;

	const uint32 CurveCount = HairStrandsDatas->GetNumCurves();
	VertexToCurveIndexBuffer.SetNum(HairStrandsDatas->GetNumPoints());
	RootPositionBuffer.SetNum(RootCount);
	RootNormalBuffer.SetNum(RootCount);

	for (uint32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
	{
		const uint32 RootIndex = HairStrandsDatas->StrandsCurves.CurvesOffset[CurveIndex];
		const uint32 PointCount = HairStrandsDatas->StrandsCurves.CurvesCount[CurveIndex];
		for (uint32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
		{
			VertexToCurveIndexBuffer[RootIndex + PointIndex] = CurveIndex; // RootIndex;
		}

		check(PointCount > 1);

		const FVector P0 = HairStrandsDatas->StrandsPoints.PointsPosition[RootIndex];
		const FVector P1 = HairStrandsDatas->StrandsPoints.PointsPosition[RootIndex + 1];
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

		RootPositionBuffer[CurveIndex] = P;
		RootNormalBuffer[CurveIndex] = N;
	}
	check(NumSamples.Num() == LODCount);

	MeshProjectionLODs.SetNum(LODCount);
	uint32 LODIndex = 0;
	for (FMeshProjectionLOD& MeshProjectionLOD : MeshProjectionLODs)
	{
		MeshProjectionLOD.SampleCount = NumSamples[LODIndex];
		MeshProjectionLOD.LODIndex = LODIndex++;
		MeshProjectionLOD.MeshInterpolationWeightsBuffer.Empty();
		MeshProjectionLOD.MeshSampleIndicesBuffer.Empty();
		MeshProjectionLOD.RestSamplePositionsBuffer.Empty();
	}
}

bool FHairStrandsRootData::HasProjectionData() const
{
	bool bIsValid = MeshProjectionLODs.Num() > 0;
	for (const FMeshProjectionLOD& LOD : MeshProjectionLODs)
	{
		const bool bHasValidCPUData = LOD.RootTriangleBarycentricBuffer.Num() > 0;
		if (bHasValidCPUData)
		{
			bIsValid = bIsValid && LOD.RootTriangleBarycentricBuffer.Num() > 0;
			bIsValid = bIsValid && LOD.RootTriangleIndexBuffer.Num() > 0;
			bIsValid = bIsValid && LOD.RestRootTrianglePosition0Buffer.Num() > 0;
			bIsValid = bIsValid && LOD.RestRootTrianglePosition1Buffer.Num() > 0;
			bIsValid = bIsValid && LOD.RestRootTrianglePosition2Buffer.Num() > 0;

			if (!bIsValid) break;
		}
	}

	return bIsValid;
}

FArchive& operator<<(FArchive& Ar, FHairStrandsRootData::FMeshProjectionLOD& LOD)
{
	Ar << LOD.LODIndex;
	Ar << LOD.RootTriangleIndexBuffer;
	Ar << LOD.RootTriangleBarycentricBuffer;
	Ar << LOD.RestRootTrianglePosition0Buffer;
	Ar << LOD.RestRootTrianglePosition1Buffer;
	Ar << LOD.RestRootTrianglePosition2Buffer;

	Ar << LOD.SampleCount;
	Ar << LOD.MeshInterpolationWeightsBuffer;
	Ar << LOD.MeshSampleIndicesBuffer;
	Ar << LOD.RestSamplePositionsBuffer;

	//Ar.UsingCustomVersion(FAnimObjectVersion::GUID);
	/*if (Ar.CustomVer(FAnimObjectVersion::GUID) >= FAnimObjectVersion::GroomAssetWithRbfWeights)
	{
		Ar << LOD.SampleCount;
		Ar << LOD.MeshInterpolationWeightsBuffer;
		Ar << LOD.MeshSampleIndicesBuffer;
		Ar << LOD.RestSamplePositionsBuffer;
	}*/
	return Ar;
}

void FHairStrandsRootData::Serialize(FArchive& Ar)
{
	Ar << RootCount;
	Ar << VertexToCurveIndexBuffer;
	Ar << RootPositionBuffer;
	Ar << RootNormalBuffer;
	Ar << MeshProjectionLODs;
}

void FHairStrandsRootData::Reset()
{
	RootCount = 0;
	VertexToCurveIndexBuffer.Empty();
	RootPositionBuffer.Empty();
	RootNormalBuffer.Empty();
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
	//SimRootPointIndex.SetNum(0);
}

void FHairStrandsInterpolationResource::ReleaseRHI()
{
	Interpolation0Buffer.Release();
	Interpolation1Buffer.Release();
	SimRootPointIndexBuffer.Release();
}

/////////////////////////////////////////////////////////////////////////////////////////
FArchive& operator<<(FArchive& Ar, FHairCardsInterpolationDatas& CardInterpData)
{
	Ar << CardInterpData.PointsSimCurvesIndex;
	Ar << CardInterpData.PointsSimCurvesVertexIndex;
	Ar << CardInterpData.PointsSimCurvesVertexLerp;
	Ar << CardInterpData.RenderData.Interpolation;

	return Ar;
}

void FHairCardsInterpolationDatas::SetNum(const uint32 NumPoints)
{
	PointsSimCurvesIndex.SetNum(NumPoints);
	PointsSimCurvesVertexIndex.SetNum(NumPoints);
	PointsSimCurvesVertexLerp.SetNum(NumPoints);
}

void FHairCardsInterpolationDatas::Reset()
{
	PointsSimCurvesIndex.Empty();
	PointsSimCurvesVertexIndex.Empty();
	PointsSimCurvesVertexLerp.Empty();
}

FHairCardsInterpolationResource::FHairCardsInterpolationResource(const FHairCardsInterpolationDatas::FRenderData& InterpolationRenderData) :
	InterpolationBuffer(), RenderData(InterpolationRenderData)
{
}

void FHairCardsInterpolationResource::InitRHI()
{
	CreateBuffer<FHairCardsInterpolationFormat>(RenderData.Interpolation, InterpolationBuffer);
}

void FHairCardsInterpolationResource::ReleaseRHI()
{
	InterpolationBuffer.Release();
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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Debug data

static uint32 ToLinearCoord(const FIntVector& T, const FIntVector& Resolution)
{
	// Morton instead for better locality?
	return T.X + T.Y * Resolution.X + T.Z * Resolution.X * Resolution.Y;
}

static FIntVector ToCoord(const FVector& T, const FIntVector& Resolution, const FVector& MinBound, const float VoxelSize)
{
	const FVector C = (T - MinBound) / VoxelSize;
	return FIntVector(
		FMath::Clamp(FMath::FloorToInt(C.X), 0, Resolution.X - 1),
		FMath::Clamp(FMath::FloorToInt(C.Y), 0, Resolution.Y - 1),
		FMath::Clamp(FMath::FloorToInt(C.Z), 0, Resolution.Z - 1));
}

void CreateHairStrandsDebugDatas(
	const FHairStrandsDatas& InData,
	float WorldVoxelSize,
	FHairStrandsDebugDatas& Out)
{
	const FVector BoundSize = InData.BoundingBox.Max - InData.BoundingBox.Min;
	Out.VoxelDescription.VoxelSize = WorldVoxelSize;
	Out.VoxelDescription.VoxelResolution = FIntVector(FMath::CeilToFloat(BoundSize.X / Out.VoxelDescription.VoxelSize), FMath::CeilToFloat(BoundSize.Y / Out.VoxelDescription.VoxelSize), FMath::CeilToFloat(BoundSize.Z / Out.VoxelDescription.VoxelSize));
	Out.VoxelDescription.VoxelMinBound = InData.BoundingBox.Min;
	Out.VoxelDescription.VoxelMaxBound = FVector(Out.VoxelDescription.VoxelResolution) * Out.VoxelDescription.VoxelSize + InData.BoundingBox.Min;
	Out.VoxelOffsetAndCount.Init(FHairStrandsDebugDatas::FOffsetAndCount(), Out.VoxelDescription.VoxelResolution.X * Out.VoxelDescription.VoxelResolution.Y * Out.VoxelDescription.VoxelResolution.Z);

	uint32 AllocationCount = 0;
	TArray<TArray<FHairStrandsDebugDatas::FVoxel>> TempVoxelData;

	// Fill in voxel (TODO: make it parallel)
	const uint32 CurveCount = InData.StrandsCurves.Num();
	for (uint32 CurveIndex = 0; CurveIndex < CurveCount; ++CurveIndex)
	{
		const uint32 PointOffset = InData.StrandsCurves.CurvesOffset[CurveIndex];
		const uint32 PointCount = InData.StrandsCurves.CurvesCount[CurveIndex];

		for (uint32 PointIndex = 0; PointIndex < PointCount - 1; ++PointIndex)
		{
			const uint32 Index0 = PointOffset + PointIndex;
			const uint32 Index1 = PointOffset + PointIndex + 1;
			const FVector& P0 = InData.StrandsPoints.PointsPosition[Index0];
			const FVector& P1 = InData.StrandsPoints.PointsPosition[Index1];
			const FVector Segment = P1 - P0;

			const float Length = Segment.Size();
			const uint32 StepCount = FMath::CeilToInt(Length / Out.VoxelDescription.VoxelSize);
			uint32 PrevLinearCoord = ~0;
			for (uint32 StepIt = 0; StepIt < StepCount + 1; ++StepIt)
			{
				const FVector P = P0 + Segment * StepIt / float(StepCount);
				const FIntVector Coord = ToCoord(P, Out.VoxelDescription.VoxelResolution, Out.VoxelDescription.VoxelMinBound, Out.VoxelDescription.VoxelSize);
				const uint32 LinearCoord = ToLinearCoord(Coord, Out.VoxelDescription.VoxelResolution);
				if (LinearCoord != PrevLinearCoord)
				{
					if (Out.VoxelOffsetAndCount[LinearCoord].Count == 0)
					{
						Out.VoxelOffsetAndCount[LinearCoord].Offset = TempVoxelData.Num();
						TempVoxelData.Add(TArray<FHairStrandsDebugDatas::FVoxel>());
					}

					const uint32 Offset = Out.VoxelOffsetAndCount[LinearCoord].Offset;
					const uint32 LocalOffset = Out.VoxelOffsetAndCount[LinearCoord].Count;
					Out.VoxelOffsetAndCount[LinearCoord].Count += 1;
					TempVoxelData[Offset].Add({Index0, Index1});

					PrevLinearCoord = LinearCoord;

					++AllocationCount;
				}
			}
		}

	}

	Out.VoxelData.Reserve(AllocationCount);

	for (int32 Index = 0, Count = Out.VoxelOffsetAndCount.Num(); Index < Count; ++Index)
	{
		const uint32 ArrayIndex = Out.VoxelOffsetAndCount[Index].Offset;
		Out.VoxelOffsetAndCount[Index].Offset = Out.VoxelData.Num();
		Out.VoxelData.Append(TempVoxelData[ArrayIndex]);

		// Sanity check
		//check(Out.VoxelOffsetAndCount[Index].Offset + Out.VoxelOffsetAndCount[Index].Count == Out.VoxelData.Num());
	}

	check(Out.VoxelData.Num()>0);
}

void CreateHairStrandsDebugResources(FRDGBuilder& GraphBuilder, const FHairStrandsDebugDatas* In, FHairStrandsDebugDatas::FResources* Out)
{
	check(In);
	check(Out);

	Out->VoxelDescription = In->VoxelDescription;

	FRDGBufferRef VoxelOffsetAndCount = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("HairStrandsDebug_VoxelOffsetAndCount"),
		sizeof(FHairStrandsDebugDatas::FOffsetAndCount),
		In->VoxelOffsetAndCount.Num(),
		In->VoxelOffsetAndCount.GetData(),
		sizeof(FHairStrandsDebugDatas::FOffsetAndCount) * In->VoxelOffsetAndCount.Num());

	FRDGBufferRef VoxelData = CreateStructuredBuffer(
		GraphBuilder,
		TEXT("HairStrandsDebug_VoxelData"),
		sizeof(FHairStrandsDebugDatas::FVoxel),
		In->VoxelData.Num(),
		In->VoxelData.GetData(),
		sizeof(FHairStrandsDebugDatas::FVoxel) * In->VoxelData.Num());

	
	ConvertToExternalBuffer(GraphBuilder, VoxelOffsetAndCount, Out->VoxelOffsetAndCount);
	ConvertToExternalBuffer(GraphBuilder, VoxelData, Out->VoxelData);
}
