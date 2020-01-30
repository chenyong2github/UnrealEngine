// Copyright Epic Games, Inc. All Rights Reserved.

#include "GroomAsset.h"
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

#if WITH_EDITORONLY_DATA
#include "DerivedDataCacheInterface.h"
#include "EditorFramework/AssetImportData.h"
#endif

template<typename BufferType>
void UploadDataToBuffer(BufferType& OutBuffer, uint32 DataSizeInBytes, void* InCpuData)
{
	void* BufferData = RHILockVertexBuffer(OutBuffer.Buffer, 0, DataSizeInBytes, RLM_WriteOnly);
	FMemory::Memcpy(BufferData, InCpuData, DataSizeInBytes);
	RHIUnlockVertexBuffer(OutBuffer.Buffer);
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

struct FClusterGrid
{
	struct FRenderingCurve
	{
		uint32 RenOffset;
		uint32 RendPointCount;
		float Area;
	};
	struct FCluster
	{
		TArray<FRenderingCurve> ClusterCurves;
	};

	FClusterGrid(float VoxelCentimeterSize, const FVector& InMinBound, const FVector& InMaxBound)
	{
		MinBound = InMinBound;
		MaxBound = InMaxBound;

		FVector VoxelCount = (MaxBound - MinBound) * FVector(1.0f / VoxelCentimeterSize);
		GridResolution = FIntVector(FMath::CeilToInt(VoxelCount.X), FMath::CeilToInt(VoxelCount.Y), FMath::CeilToInt(VoxelCount.Z));

		ClusterInfo.SetNum(GridResolution.X * GridResolution.Y * GridResolution.Z);
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
		check(CellIndex < uint32(ClusterInfo.Num()));
		return CellIndex;
	}

	void InsertRenderingCurve(FRenderingCurve& Curve, const FVector& Root)
	{
		FIntVector CellCoord = ToCellCoord(Root);
		uint32 Index = ToIndex(CellCoord);
		FCluster& Cluster = ClusterInfo[Index];
		Cluster.ClusterCurves.Add(Curve);
	}

	TArray<FCluster> GetAllClusters() { return ClusterInfo; }

private:

	FVector MinBound;
	FVector MaxBound;

	FIntVector GridResolution;

	TArray<FCluster> ClusterInfo;
};

FHairStrandsClusterCullingResource::FHairStrandsClusterCullingResource(const FHairGroupData& GroupData)
{
	const FHairStrandsDatas& SimStrandsData = GroupData.HairSimulationData;
	const FHairStrandsDatas& RenStrandsData = GroupData.HairRenderData;
	const FHairStrandsInterpolationDatas& InterpolationData = GroupData.HairInterpolationData;
	const uint32 RenCurveCount = RenStrandsData.GetNumCurves();
	const uint32 SimCurveCount = SimStrandsData.GetNumCurves();
	const uint32 PointCount = RenStrandsData.GetNumPoints();


	// Allocate look up arrays for as many hair vertices as needed.
	VertexCount = PointCount;
	VertexToClusterIdArray.SetNum(PointCount);
	ClusterVertexIdArray.SetNum(PointCount);

	// Allocate cluster per voxel containing contains >=1 render curve root
	const uint32 VoxelcountAlongLargerSide = 256; // TODO expose in UI
	const float VoxelCentimeterSize = GroupData.HairRenderData.BoundingBox.GetSize().GetAbsMax() / VoxelcountAlongLargerSide;
	FClusterGrid ClusterGrid(VoxelCentimeterSize, GroupData.HairRenderData.BoundingBox.Min, GroupData.HairRenderData.BoundingBox.Max);

	for (uint32 RenCurveIndex = 0; RenCurveIndex < RenCurveCount; ++RenCurveIndex)
	{
		FClusterGrid::FRenderingCurve RCurve;
		RCurve.RendPointCount = RenStrandsData.StrandsCurves.CurvesCount[RenCurveIndex];
		RCurve.RenOffset = RenStrandsData.StrandsCurves.CurvesOffset[RenCurveIndex];

		RCurve.Area = 0.0f;
		for (uint32 RenPointIndex = 0; RenPointIndex < RCurve.RendPointCount; ++RenPointIndex)
		{
			uint32 PointGlobalIndex = RenPointIndex + RCurve.RenOffset;
			const FVector& V0 = RenStrandsData.StrandsPoints.PointsPosition[PointGlobalIndex];
			if (RenPointIndex > 0)
			{
				const FVector& V1 = RenStrandsData.StrandsPoints.PointsPosition[PointGlobalIndex-1];
				FVector OutDir;
				float OutLength;
				(V1 - V0).ToDirectionAndLength(OutDir, OutLength);
				RCurve.Area += RenStrandsData.StrandsPoints.PointsRadius[PointGlobalIndex] * OutLength;
			}
		}

		const FVector Root = RenStrandsData.StrandsPoints.PointsPosition[RCurve.RenOffset];
		ClusterGrid.InsertRenderingCurve(RCurve, Root);
	}

	// Count Cluster
	ClusterCount = 0;
	for (auto& Cluster : ClusterGrid.GetAllClusters())
	{
		ClusterCount += Cluster.ClusterCurves.Num() > 0 ? 1 : 0;
	}
	ClusterInfoArray.SetNum(ClusterCount);
	ClusterIndexRadiusScaleInfoArray.SetNum(ClusterCount);

	TArray<TArray<uint32>> ClusterToVertexIndexLod0;	// List of indices per cluster
	ClusterToVertexIndexLod0.SetNum(ClusterCount);
	TArray<uint32> ClusterVertexIndexCountLod1;			// Index count per cluster
	ClusterVertexIndexCountLod1.SetNum(ClusterCount);

	// Write out cluster information
	uint32 Index = 0;
	for (auto& Cluster : ClusterGrid.GetAllClusters())
	{
		if (Cluster.ClusterCurves.Num() == 0)
		{
			continue;
		}

		uint32 ClusterCurveCount = Cluster.ClusterCurves.Num();
		uint32 Lod1CurveCount = FMath::Max(ClusterCurveCount / 4, 1u);		// a fourth of the curves for the lowest lod
		uint32 Lod0VertexCount = 0;
		uint32 Lod1VertexCount = 0;

		// Prepare data to reconstruct vertex/index count to radius scale
		float Lod0StrandArea = 0.0f;
		float Lod1StrandArea = 0.0f;

		// Sort curve to have largest area first, so that lower area curves with less influence are removed first.
		// This also helps the radius scaling to not explode.
		Cluster.ClusterCurves.Sort([](const FClusterGrid::FRenderingCurve& A, const FClusterGrid::FRenderingCurve& B) -> bool
		{
				return A.Area > B.Area;
		});

		uint32 CurveIndex = 0;
		for (auto& ClusterCurve : Cluster.ClusterCurves)
		{
			for (uint32 RenPointIndex = 0; RenPointIndex < ClusterCurve.RendPointCount; ++RenPointIndex)
			{
				uint32 PointGlobalIndex = RenPointIndex + ClusterCurve.RenOffset;
				VertexToClusterIdArray[PointGlobalIndex] = Index;
				ClusterToVertexIndexLod0[Index].Add(PointGlobalIndex);
			}
			Lod0VertexCount += ClusterCurve.RendPointCount;
			Lod0StrandArea += ClusterCurve.Area;
			if (CurveIndex < Lod1CurveCount)
			{
				Lod1VertexCount += ClusterCurve.RendPointCount;
				Lod1StrandArea += ClusterCurve.Area;
			}
			CurveIndex++;
		}
		if (Cluster.ClusterCurves.Num() > 0)
		{
			ClusterVertexIndexCountLod1[Index] = Lod1VertexCount;

			const float ClusterRadiusScaleLod1 = Lod0StrandArea / Lod1StrandArea; // Could use coverage ratio (relative to cluster 0&2 bounding boxes) instead of hair area.
			const float Numer = ClusterRadiusScaleLod1 - 1.0f;
			const float Denom = float(Lod1VertexCount) - float(Lod0VertexCount);
			const float AValue = Denom == 0.0f ? 0.0f : Numer / Denom;
			ClusterIndexRadiusScaleInfoArray[Index] = AValue;
			// ClusterIndexRadiusScaleInfoArray contains the 'a' value from the linear equation y=ax+b. Radius scale can then be recovered as ( 1.0 + a * (VertexCount - Lod0VertexCount) )
		}

		Index += Cluster.ClusterCurves.Num() > 0 ? 1 : 0;
	}

	// Now compute the cluster/strand information
	uint32 VertexCountSum = 0;
	for (uint32 ClusterId = 0; ClusterId < ClusterCount; ++ClusterId)
	{
		const uint32 ClusterVertexCountLod0 = ClusterToVertexIndexLod0[ClusterId].Num();
		ClusterInfoArray[ClusterId].FirstVertexId = VertexCountSum;
		ClusterInfoArray[ClusterId].VertexIdCountLodHigh = ClusterVertexCountLod0;
		ClusterInfoArray[ClusterId].VertexIdCountLodLow = ClusterVertexIndexCountLod1[ClusterId];
		ClusterInfoArray[ClusterId].UnusedUint = 0;
		VertexCountSum += ClusterVertexCountLod0;
	}
	check(ClusterVertexIdArray.Num() == VertexCountSum);

	// Flatten global VertexId for each cluster
	for (uint32 ClusterId = 0; ClusterId < ClusterCount; ++ClusterId)
	{
		uint32 ClusterFirstVertexId = ClusterInfoArray[ClusterId].FirstVertexId;
		uint32 ClusterVertexIdCount = ClusterInfoArray[ClusterId].VertexIdCountLodHigh;
		TArray<uint32>& ClusterVertices = ClusterToVertexIndexLod0[ClusterId];
		for (uint32 ClusterVertexId = 0; ClusterVertexId < ClusterVertexIdCount; ++ClusterVertexId)
		{
			ClusterVertexIdArray[ClusterFirstVertexId + ClusterVertexId] = ClusterVertices[ClusterVertexId];
		}
	}
}

void FHairStrandsClusterCullingResource::InitRHI()
{
	ClusterInfoBuffer.Initialize(sizeof(uint32) * 4, ClusterCount, EPixelFormat::PF_R32G32B32A32_UINT, BUF_Static);
	UploadDataToBuffer(ClusterInfoBuffer, sizeof(uint32) * 4 * ClusterCount, ClusterInfoArray.GetData());

	VertexToClusterIdBuffer.Initialize(sizeof(uint32), VertexToClusterIdArray.Num(), EPixelFormat::PF_R32_UINT, BUF_Static);
	UploadDataToBuffer(VertexToClusterIdBuffer, sizeof(uint32) * VertexToClusterIdArray.Num(), VertexToClusterIdArray.GetData());

	ClusterVertexIdBuffer.Initialize(sizeof(uint32), ClusterVertexIdArray.Num(), EPixelFormat::PF_R32_UINT, BUF_Static);
	UploadDataToBuffer(ClusterVertexIdBuffer, sizeof(uint32) * ClusterVertexIdArray.Num(), ClusterVertexIdArray.GetData());

	ClusterIndexRadiusScaleInfoBuffer.Initialize(sizeof(float), ClusterIndexRadiusScaleInfoArray.Num(), EPixelFormat::PF_R32_FLOAT, BUF_Static); // TODO use fp16
	UploadDataToBuffer(ClusterIndexRadiusScaleInfoBuffer, sizeof(float) * ClusterIndexRadiusScaleInfoArray.Num(), ClusterIndexRadiusScaleInfoArray.GetData());
}

void FHairStrandsClusterCullingResource::ReleaseRHI()
{
	ClusterInfoBuffer.Release();
	VertexToClusterIdBuffer.Release();
	ClusterVertexIdBuffer.Release();
	ClusterIndexRadiusScaleInfoBuffer.Release();
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
		MeshProjectionLOD.Status = FHairStrandsProjectionHairData::LODData::EStatus::Invalid;
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
			LOD.Status = FHairStrandsProjectionHairData::LODData::EStatus::Initialized;

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
		LOD.Status = FHairStrandsProjectionHairData::LODData::EStatus::Invalid;
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

	Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);
	Ar.UsingCustomVersion(FReleaseObjectVersion::GUID); // Needed to support MeshDescription AttributesSet serialization

	if (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::GroomWithDescription)
	{
		FStripDataFlags StripFlags(Ar);
		if (StripFlags.IsEditorDataStripped() || (Ar.IsSaving() && !CanRebuildFromDescription()))
		{
			// When cooking data or serializing old format to new format,
			// serialize the computed groom data
			Ar << HairGroupsData;
		}
#if WITH_EDITORONLY_DATA
		else
		{
			// When serializing data for editor, serialize the HairDescription as bulk data
			// The computed groom data is fetched from the Derived Data Cache
			if (!HairDescriptionBulkData)
			{
				// When loading, bulk data can be null so instantiate a new one to serialize into
				HairDescriptionBulkData = MakeUnique<FHairDescriptionBulkData>();
			}

			HairDescriptionBulkData->Serialize(Ar, this);

			// Serialize the HairGroupsData directly into the asset if it couldn't be cached in the DDC
			if (!bIsCacheable)
			{
				Ar << HairGroupsData;
			}
		}
#endif // WITH_EDITORONLY_DATA
	}
	else
	{
		// Old format serialized the computed groom data directly
		Ar << HairGroupsData;
	}
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

#if WITH_EDITORONLY_DATA
	if (bIsCacheable)
	{
		CacheDerivedData();
	}
#endif

	check(HairGroupsData.Num() > 0);
	if (HairGroupsData[0].HairSimulationData.GetNumCurves() == 0 || HairGroupsData[0].HairInterpolationData.Num() == 0)
	{
		FGroomBuildSettings Settings;
		Settings.bOverrideGuides = false;
		Settings.HairToGuideDensity = 0.1f;
		Settings.InterpolationQuality = EGroomInterpolationQuality::High;
		Settings.InterpolationDistance = EGroomInterpolationWeight::Parametric;
		Settings.bRandomizeGuide = false;
		Settings.bUseUniqueGuide = false;
		FGroomBuilder::BuildData(this, Settings);
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

FArchive& operator<<(FArchive& Ar, FHairGroupInfo& GroupInfo)
{
	Ar.UsingCustomVersion(FPhysicsObjectVersion::GUID);

	Ar << GroupInfo.GroupID;
	Ar << GroupInfo.NumCurves;
	Ar << GroupInfo.NumGuides;

	if (Ar.CustomVer(FPhysicsObjectVersion::GUID) >= FPhysicsObjectVersion::GroomWithImportSettings)
	{
		Ar << GroupInfo.ImportSettings;
	}
	else
	{
		if (Ar.IsLoading())
		{
			bool bIsAutoGenerated = false;
			Ar << bIsAutoGenerated;

			GroupInfo.ImportSettings = FGroomBuildSettings();
			GroupInfo.ImportSettings.InterpolationQuality = EGroomInterpolationQuality::Unknown;
			GroupInfo.ImportSettings.InterpolationDistance = EGroomInterpolationWeight::Unknown;
			GroupInfo.ImportSettings.bOverrideGuides = bIsAutoGenerated;
		}
		else
		{
			bool bIsAutoGenerated = GroupInfo.ImportSettings.bOverrideGuides;
			Ar << bIsAutoGenerated;
		}
	}

	// Ignoring Material since it's null

	return Ar;
}

bool UGroomAsset::CanRebuildFromDescription() const
{
#if WITH_EDITORONLY_DATA
	return HairDescriptionBulkData.IsValid() && !HairDescriptionBulkData->IsEmpty();
#else
	return false;
#endif
}

// If groom derived data needs to be rebuilt (new format, serialization
// differences, etc.) replace the version GUID below with a new one.
// In case of merge conflicts with DDC versions, you MUST generate a new GUID
// and set this new GUID as the version.
#define GROOM_DERIVED_DATA_VERSION TEXT("42406925F1934F9794A08A8EE51BA05D")

#if WITH_EDITORONLY_DATA

namespace GroomDerivedDataCacheUtils
{
	const FString& GetGroomDerivedDataVersion()
	{
		static FString CachedVersionString(GROOM_DERIVED_DATA_VERSION);
		return CachedVersionString;
	}

	FString BuildGroomDerivedDataKey(const FString& KeySuffix)
	{
		return FDerivedDataCacheInterface::BuildCacheKey(TEXT("GROOM"), *GetGroomDerivedDataVersion(), *KeySuffix);
	}

	void SerializeGroomBuildSettingsForDDC(FArchive& Ar, FGroomBuildSettings& BuildSettings)
	{
		// Note: this serializer is only used to build the groom DDC key, no versioning is required
		Ar << BuildSettings.bOverrideGuides;
		Ar << BuildSettings.HairToGuideDensity;
		Ar << BuildSettings.InterpolationQuality;
		Ar << BuildSettings.InterpolationDistance;
		Ar << BuildSettings.bRandomizeGuide;
		Ar << BuildSettings.bUseUniqueGuide;
	}
}

FString UGroomAsset::BuildDerivedDataKeySuffix(const FGroomBuildSettings& BuildSettings)
{
	// Serialize the build settings into a temporary array
	// The archive is flagged as persistent so that machines of different endianness produce identical binary results.
	TArray<uint8> TempBytes;
	TempBytes.Reserve(64);
	FMemoryWriter Ar(TempBytes, /*bIsPersistent=*/ true);

	GroomDerivedDataCacheUtils::SerializeGroomBuildSettingsForDDC(Ar, const_cast<FGroomBuildSettings&>(BuildSettings));

	FString KeySuffix;
	if (HairDescriptionBulkData)
	{
		// Reserve twice the size of TempBytes because of ByteToHex below + 3 for "ID" and \0
		KeySuffix.Reserve(HairDescriptionBulkData->GetIdString().Len() + TempBytes.Num() * 2 + 3);
		KeySuffix += TEXT("ID");
		KeySuffix += HairDescriptionBulkData->GetIdString();
	}
	else
	{
		KeySuffix.Reserve(TempBytes.Num() * 2 + 1);
	}

	// Now convert the raw bytes to a string
	const uint8* SettingsAsBytes = TempBytes.GetData();
	for (int32 ByteIndex = 0; ByteIndex < TempBytes.Num(); ++ByteIndex)
	{
		ByteToHex(SettingsAsBytes[ByteIndex], KeySuffix);
	}

	return KeySuffix;
}

void UGroomAsset::CommitHairDescription(FHairDescription&& InHairDescription)
{
	HairDescription = MakeUnique<FHairDescription>(InHairDescription);

	if (!HairDescriptionBulkData)
	{
		HairDescriptionBulkData = MakeUnique<FHairDescriptionBulkData>();
	}
	HairDescriptionBulkData->SaveHairDescription(*HairDescription);
}

bool UGroomAsset::CacheDerivedData(const FGroomBuildSettings* InBuildSettings)
{
	if (!HairDescriptionBulkData)
	{
		return false;
	}

	static FGroomBuildSettings DefaultBuildSettings;
	const FGroomBuildSettings* BuildSettings = &DefaultBuildSettings;
	if (InBuildSettings)
	{
		// Use the provided build settings since a new build is requested
		BuildSettings = InBuildSettings;
	}
	else if (AssetImportData)
	{
		// Otherwise, use the build settings that were used previously at import/re-import
		UGroomAssetImportData* GroomImportData = Cast<UGroomAssetImportData>(AssetImportData);
		if (GroomImportData && GroomImportData->ImportOptions)
		{
			BuildSettings = &GroomImportData->ImportOptions->BuildSettings;
		}
	}

	bool bSuccess = true;

	const FString KeySuffix = BuildDerivedDataKeySuffix(*BuildSettings);
	const FString DerivedDataKey = GroomDerivedDataCacheUtils::BuildGroomDerivedDataKey(KeySuffix);

	TArray<uint8> DerivedData;
	if (GetDerivedDataCacheRef().GetSynchronous(*DerivedDataKey, DerivedData))
	{
		FMemoryReader Ar(DerivedData, /*bIsPersistent=*/ true);

		int64 UncompressedSize = 0;
		Ar << UncompressedSize;

		uint8* DecompressionBuffer = reinterpret_cast<uint8*>(FMemory::Malloc(UncompressedSize));
		Ar.SerializeCompressed(DecompressionBuffer, 0, NAME_Zlib);

		FLargeMemoryReader LargeMemReader(DecompressionBuffer, UncompressedSize, ELargeMemoryReaderFlags::Persistent | ELargeMemoryReaderFlags::TakeOwnership);
		if (HairGroupsInfo.Num() > 0)
		{
			// HairGroupsInfo serialized from the asset, so ignore the one from the DDC
			TArray<FHairGroupInfo> TempHairGroupsInfo;
			LargeMemReader << TempHairGroupsInfo;
		}
		else
		{
			// This is the case where a groom with the same build settings was previously pushed to DDC,
			// but this is a new asset so HairGroupsInfo isn't serialized yet
			LargeMemReader << HairGroupsInfo;
		}

		LargeMemReader << HairGroupsData;
	}
	else
	{
		// Load the HairDescription from the bulk data if needed
		if (!HairDescription)
		{
			HairDescription = MakeUnique<FHairDescription>();
			HairDescriptionBulkData->LoadHairDescription(*HairDescription);
		}

		// Build groom data with the new build settings
		bSuccess = FGroomBuilder::BuildGroom(*HairDescription, *BuildSettings, this);
		if (bSuccess)
		{
			// Using a LargeMemoryWriter for serialization since the data can be bigger than 2 GB
			FLargeMemoryWriter LargeMemWriter(0, /*bIsPersistent=*/ true);
			LargeMemWriter << HairGroupsInfo;
			LargeMemWriter << HairGroupsData;

			int64 UncompressedSize = LargeMemWriter.TotalSize();

			// Then the content of the LargeMemWriter is compressed into a MemoryWriter
			// Compression ratio can reach about 5:2 depending on the data
			// Since the DDC doesn't support data bigger than 2 GB
			// we can compute a size threshold to skip the caching when
			// the uncompressed size exceeds the threshold
			static constexpr const int64 SizeThreshold =  (int64) MAX_int32 * 2.5;
			bIsCacheable = UncompressedSize < SizeThreshold;
			if (bIsCacheable)
			{
				FMemoryWriter CompressedArchive(DerivedData, true);

				CompressedArchive << UncompressedSize; // needed for allocating decompression buffer
				CompressedArchive.SerializeCompressed(LargeMemWriter.GetData(), UncompressedSize, NAME_Zlib);

				GetDerivedDataCacheRef().Put(*DerivedDataKey, DerivedData);
			}
		}
	}
	return bSuccess;
}

#endif // WITH_EDITORONLY_DATA
