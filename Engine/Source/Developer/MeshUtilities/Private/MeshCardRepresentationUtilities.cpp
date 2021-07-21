// Copyright Epic Games, Inc. All Rights Reserved.

#include "MeshUtilities.h"
#include "MeshUtilitiesPrivate.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "RawMesh.h"
#include "StaticMeshResources.h"
#include "MeshCardRepresentation.h"
#include "DistanceFieldAtlas.h"
#include "MeshRepresentationCommon.h"
#include "Containers/BinaryHeap.h"

// Debug option for investigating card generation issues
#define DEBUG_MESH_CARD_VISUALIZATION 0

int32 constexpr NumAxisAlignedDirections = 6;
int32 constexpr MaxCardsPerMesh = 32;

class FGenerateCardMeshContext
{
public:
	const FString& MeshName;
	const FEmbreeScene& EmbreeScene;
	FCardRepresentationData& OutData;

	FGenerateCardMeshContext(const FString& InMeshName, const FEmbreeScene& InEmbreeScene, FCardRepresentationData& InOutData) :
		MeshName(InMeshName),
		EmbreeScene(InEmbreeScene),
		OutData(InOutData)
	{}
};

#if USE_EMBREE

struct FSurfel
{
	FVector3f Position;
	FVector3f Normal;

	FVector3f LocalSurfelPosition[NumAxisAlignedDirections];
	float RayCache[NumAxisAlignedDirections];
};

struct FClusteringParams
{
	float SurfelRadius = 0.0f;
	float SurfelExtendRadius = 0.0f;
	float NormalWeightTreshold = 0.0f;
	float DistanceWeightTreshold = 0.0f;
	int32 MinSurfelsPerCluster = 0;
	float MinDensityPerCluster = 0.0f;
};

FVector3f AxisAlignedDirectionIndexToNormal(int32 AxisAlignedDirectionIndex)
{
	const int32 AxisIndex = AxisAlignedDirectionIndex / 2;

	FVector3f Normal(0.0f, 0.0f, 0.0f);
	Normal[AxisIndex] = AxisAlignedDirectionIndex & 1 ? 1.0f : -1.0f;
	return Normal;
}

uint8 NormalToAxisAlignedDirectionIndex(FVector3f Normal)
{
	const float AbsMaxComponent = Normal.GetAbsMax();

	int32 AxisIndex = 0;
	if (FMath::Abs(Normal.X) >= AbsMaxComponent)
	{
		AxisIndex = 0;
	}
	else if (FMath::Abs(Normal.Y) >= AbsMaxComponent)
	{
		AxisIndex = 1;
	}
	else
	{
		AxisIndex = 2;
	}

	return AxisIndex * 2 + (Normal[AxisIndex] >= 0.0f ? 1 : 0);
}

FMatrix44f GetCardBasis(FVector3f Normal)
{
	FVector3f XAxis;
	FVector3f YAxis;
	Normal.FindBestAxisVectors(XAxis, YAxis);
	XAxis = FVector::CrossProduct(Normal, YAxis);
	XAxis.Normalize();
	return FMatrix44f(XAxis, YAxis, Normal, FVector::ZeroVector).GetTransposed();
}

struct FSurfelScene
{
	TArray<FSurfel> Surfels;
	TArray<uint16> SurfelIndicesPerDirection[NumAxisAlignedDirections];
	float TwoSidedTriangleRatio = 0.0f;
};

class FSurfelCluster
{
public:
	uint8 AxisAlignedDirectionIndex;

	FMatrix44f WorldToLocal;
	FVector3f Normal;
	FBox Bounds;
	TArray<int32> SurfelIndices;

	float MinRayZ = FLT_MAX;

	// Best surfels to add to this cluster
	int32 BestSurfelIndex = -1;
	float BestSurfelWeight = 0.0f;

	void Reset()
	{
		Bounds.Init();
		SurfelIndices.Reset();
		MinRayZ = FLT_MAX;
	}

	void SetDirection(uint8 InAxisAlignedDirectionIndex)
	{
		AxisAlignedDirectionIndex = InAxisAlignedDirectionIndex;
		Normal = AxisAlignedDirectionIndexToNormal(InAxisAlignedDirectionIndex);
		WorldToLocal = GetCardBasis(Normal);
	}

	bool IsValid(const FClusteringParams& ClusteringParams) const
	{
		const float SurfelArea = PI * ClusteringParams.SurfelRadius * ClusteringParams.SurfelRadius;
		const FVector3f CardSize  = 2.0f * Bounds.GetExtent();
		const float Density = (SurfelIndices.Num() * SurfelArea) / (CardSize.X * CardSize.Y);

		return SurfelIndices.Num() >= ClusteringParams.MinSurfelsPerCluster 
			&& Density > ClusteringParams.MinDensityPerCluster;
	}

	void AddSurfel(const FClusteringParams& ClusteringParams, const FSurfelScene& SurfelScene, int32 SurfelToAddIndex);
	void UpdateBestSurfel(const FClusteringParams& ClusteringParams, const FSurfelScene& SurfelScene, TBitArray<>& SurfelAssignedToAnyCluster);
};

bool CanAddSurfelToCluster(const FClusteringParams& ClusteringParams, const FSurfelScene& SurfelScene, const FSurfelCluster& Cluster, int32 SurfelToAddIndex)
{
	const FSurfel& SurfelToAdd = SurfelScene.Surfels[SurfelToAddIndex];

	float BoundsMaxZ = Cluster.Bounds.Max.Z;
	float MinRayZ = Cluster.MinRayZ;

	const float LocalSpacePositonMaxZ = (Cluster.Normal | SurfelToAdd.Position) + ClusteringParams.SurfelExtendRadius;
	BoundsMaxZ = FMath::Max(BoundsMaxZ, LocalSpacePositonMaxZ);

	const float RayTFar = SurfelToAdd.RayCache[Cluster.AxisAlignedDirectionIndex];
	if (RayTFar < FLT_MAX)
	{
		const float ClusterSpaceHitPointZ = LocalSpacePositonMaxZ - ClusteringParams.SurfelExtendRadius + RayTFar;
		MinRayZ = FMath::Min(MinRayZ, ClusterSpaceHitPointZ);
	}

	return MinRayZ >= BoundsMaxZ;
}

float SurfelNormalWeight(const FSurfel& Surfel, FVector3f ClusterNormal, const FClusteringParams& ClusteringParams)
{
	return ((ClusterNormal | Surfel.Normal) - ClusteringParams.NormalWeightTreshold) / (1.0f - ClusteringParams.NormalWeightTreshold);
}

void FSurfelCluster::AddSurfel(const FClusteringParams& ClusteringParams, const FSurfelScene& SurfelScene, int32 SurfelToAddIndex)
{
	const FSurfel& SurfelToAdd = SurfelScene.Surfels[SurfelToAddIndex];
	SurfelIndices.Add(SurfelToAddIndex);

	const FVector LocalSpacePositon = SurfelToAdd.LocalSurfelPosition[AxisAlignedDirectionIndex];
	Bounds += (LocalSpacePositon - ClusteringParams.SurfelExtendRadius);
	Bounds += (LocalSpacePositon + ClusteringParams.SurfelExtendRadius);
		
	const float RayTFar = SurfelToAdd.RayCache[AxisAlignedDirectionIndex];
	if (RayTFar < FLT_MAX)
	{
		const float ClusterSpaceHitPointZ = LocalSpacePositon.Z + RayTFar;
		MinRayZ = FMath::Min(MinRayZ, ClusterSpaceHitPointZ);
	}

	// Check if all surfels are visible after add
	check(MinRayZ >= Bounds.Max.Z);
}

void FSurfelCluster::UpdateBestSurfel(const FClusteringParams& ClusteringParams, const FSurfelScene& SurfelScene, TBitArray<>& SurfelAssignedToAnyCluster)
{
	BestSurfelIndex = -1;
	BestSurfelWeight = 0.0f;

	for (int32 SurfelIndex : SurfelScene.SurfelIndicesPerDirection[AxisAlignedDirectionIndex])
	{
		if (!SurfelAssignedToAnyCluster[SurfelIndex])
		{
			const FSurfel& Surfel = SurfelScene.Surfels[SurfelIndex];

			float SurfelWeight = SurfelNormalWeight(Surfel, Normal, ClusteringParams);
			if (SurfelWeight > BestSurfelWeight)
			{
				const FVector3f LocalSpaceSurfelPosition = Surfel.LocalSurfelPosition[AxisAlignedDirectionIndex];

				if (SurfelIndices.Num() > 0)
				{
					const FVector3f BoundsCenter = Bounds.GetCenter();
					const FVector3f BoundsExtent = Bounds.GetExtent();

					const FVector3f Distance = FVector3f::Max((LocalSpaceSurfelPosition - BoundsCenter).GetAbs() - BoundsExtent, FVector3f(0.0f, 0.0f, 0.0f));
					const float ManhattanDistance = Distance.X + Distance.Y + Distance.Z / 4.0f;

					const float DistanceWeight = FMath::Clamp(1.0f - ManhattanDistance / ClusteringParams.DistanceWeightTreshold, 0.0f, 1.0f);
					SurfelWeight *= DistanceWeight;
				}

				// Weight by visibility
				const float RayTFar = Surfel.RayCache[AxisAlignedDirectionIndex];
				if (RayTFar < FLT_MAX)
				{
					SurfelWeight *= FMath::Clamp(RayTFar / 1100.0f + 0.1f, 0.1f, 1.0f);
				}

				if (SurfelWeight > BestSurfelWeight)
				{
					if (CanAddSurfelToCluster(ClusteringParams, SurfelScene, *this, SurfelIndex))
					{
						BestSurfelIndex = SurfelIndex;
						BestSurfelWeight = SurfelWeight;
					}
				}
			}
		}
	}
}

float SurfelHalton(int32 Index, int32 Base)
{
	float Result = 0.0f;
	float InvBase = 1.0f / Base;
	float Fraction = InvBase;
	while (Index > 0)
	{
		Result += (Index % Base) * Fraction;
		Index /= Base;
		Fraction *= InvBase;
	}
	return Result;
}

void GenerateSurfels(
	const FGenerateCardMeshContext& Context,
	const FBox& MeshCardsBounds,
	FSurfelScene& SurfelScene,
	FClusteringParams& ClusteringParams,
	FLumenCardBuildDebugData& DebugData)
{
	const uint32 NumSourceVertices = Context.EmbreeScene.Geometry.VertexArray.Num();
	const uint32 NumSourceIndices = Context.EmbreeScene.Geometry.IndexArray.Num();
	const int32 NumSourceTriangles = NumSourceIndices / 3;

	if (NumSourceTriangles == 0)
	{
		return;
	}

	// Generate random ray directions over a hemisphere
	constexpr uint32 NumRayDirectionsOverHemisphere = 64;
	TArray<FVector3f> RayDirectionsOverHemisphere;
	{
		FRandomStream RandomStream(0);
		MeshUtilities::GenerateStratifiedUniformHemisphereSamples(NumRayDirectionsOverHemisphere, RandomStream, RayDirectionsOverHemisphere);
	}

	FVector3f NormalPerDirection[NumAxisAlignedDirections];
	FMatrix44f WorldToLocalPerDirection[NumAxisAlignedDirections];
	for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
	{
		NormalPerDirection[AxisAlignedDirectionIndex] = AxisAlignedDirectionIndexToNormal(AxisAlignedDirectionIndex);
		WorldToLocalPerDirection[AxisAlignedDirectionIndex] = GetCardBasis(NormalPerDirection[AxisAlignedDirectionIndex]);
	}

	float TotalArea = 0.0f;
	int32 NumTwoSidedTriangles = 0;
	TArray<float> TriangleArea;
	TArray<FVector3f> TriangleNormal;
	TArray<int32> TriangleIndices;
	TriangleArea.Reserve(NumSourceTriangles);
	TriangleNormal.Reserve(NumSourceTriangles);
	TriangleIndices.Reserve(NumSourceIndices);

	for (int32 SourceTriangleIndex = 0; SourceTriangleIndex < NumSourceTriangles; ++SourceTriangleIndex)
	{
		const int32 Index0 = Context.EmbreeScene.Geometry.IndexArray[SourceTriangleIndex * 3 + 0];
		const int32 Index1 = Context.EmbreeScene.Geometry.IndexArray[SourceTriangleIndex * 3 + 1];
		const int32 Index2 = Context.EmbreeScene.Geometry.IndexArray[SourceTriangleIndex * 3 + 2];

		const FVector3f Pos0 = Context.EmbreeScene.Geometry.VertexArray[Index0];
		const FVector3f Pos1 = Context.EmbreeScene.Geometry.VertexArray[Index1];
		const FVector3f Pos2 = Context.EmbreeScene.Geometry.VertexArray[Index2];

		const FVector3f Cross = (Pos1 - Pos0) ^ (Pos2 - Pos0);
		const FVector3f Normal = -Cross.GetSafeNormal();
		const float Area = 0.5f * Cross.Size();

		TriangleIndices.Add(Index0);
		TriangleIndices.Add(Index1);
		TriangleIndices.Add(Index2);

		TriangleNormal.Add(Normal);
		TriangleArea.Add(Area);
		TotalArea += Area;

		// Add an extra triangle if it's a two sided one
		if (Context.EmbreeScene.Geometry.TriangleDescs[SourceTriangleIndex].IsTwoSided())
		{
			TriangleIndices.Add(Index0);
			TriangleIndices.Add(Index1);
			TriangleIndices.Add(Index2);
			TriangleNormal.Add(-Normal);
			TriangleArea.Add(Area);
			TotalArea += Area;
			++NumTwoSidedTriangles;
		}
	}

	SurfelScene.TwoSidedTriangleRatio = NumTwoSidedTriangles / float(NumSourceTriangles);

	TArray<float> TriangleCDF;
	TriangleCDF.SetNumUninitialized(TriangleArea.Num());
	for (int32 TriangleIndex = 0; TriangleIndex < TriangleArea.Num(); ++TriangleIndex)
	{
		TriangleCDF[TriangleIndex] = TriangleArea[TriangleIndex];
		if (TriangleIndex > 0)
		{
			TriangleCDF[TriangleIndex] += TriangleCDF[TriangleIndex - 1];
		}
	}

	const float TargetSurfelRadius = 10.0f;
	const float TargetSurfelArea = PI * TargetSurfelRadius * TargetSurfelRadius;
	const int32 TargetNumSurfels = int32(TotalArea / TargetSurfelArea + 0.5f);
	const int32 NumSurfels = FMath::Clamp(TargetNumSurfels, 128, 5000);
	SurfelScene.Surfels.Reserve(NumSurfels);

	// Derive clustering params from surface area and CVars
	{
		const float SurfelArea = TotalArea / NumSurfels;
		ClusteringParams.SurfelRadius = FMath::Sqrt(SurfelArea / PI);
		ClusteringParams.SurfelExtendRadius = 1.5f * ClusteringParams.SurfelRadius;

		static const auto CVarMeshCardRepresentationMinDensity = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.MeshCardRepresentation.MinDensity"));
		static const auto CVarMeshCardRepresentationNormalTreshold = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.MeshCardRepresentation.NormalTreshold"));
		static const auto CVarMeshCardRepresentationDistanceTreshold = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.MeshCardRepresentation.DistanceTreshold"));

		ClusteringParams.NormalWeightTreshold = FMath::Clamp(CVarMeshCardRepresentationNormalTreshold->GetValueOnAnyThread(), 0.0f, 1.0f);
		ClusteringParams.DistanceWeightTreshold = FMath::Clamp(CVarMeshCardRepresentationDistanceTreshold->GetValueOnAnyThread(), 0.0f, FLT_MAX) * FMath::Max(TargetSurfelRadius, ClusteringParams.SurfelRadius);
		ClusteringParams.MinSurfelsPerCluster = 20;

		ClusteringParams.MinDensityPerCluster = FMath::Clamp(CVarMeshCardRepresentationMinDensity->GetValueOnAnyThread(), 0.0f, 1.0f);
	}

	int32 TriangleIndex = 0;

	for (int32 SampleIndex = 0; SampleIndex < NumSurfels; ++SampleIndex)
	{
		const float SampleArea = ((SampleIndex + 0.5f) / NumSurfels) * TotalArea;

		while (SampleArea > TriangleCDF[TriangleIndex] && TriangleIndex + 1 < TriangleArea.Num())
		{
			++TriangleIndex;
		}

		// Pick random sample in a triangle
		const float R0 = SurfelHalton(SampleIndex + 1, 2);
		const float R1 = SurfelHalton(SampleIndex + 1, 3);
		const float L0 = 1.0f - sqrtf(R0);
		const float L1 = sqrtf(R0) * (1.0f - R1);
		const float L2 = sqrtf(R0) * R1;

		const int32 Index0 = TriangleIndices[TriangleIndex * 3 + 0];
		const int32 Index1 = TriangleIndices[TriangleIndex * 3 + 1];
		const int32 Index2 = TriangleIndices[TriangleIndex * 3 + 2];

		const FVector3f Position0 = Context.EmbreeScene.Geometry.VertexArray[Index0];
		const FVector3f Position1 = Context.EmbreeScene.Geometry.VertexArray[Index1];
		const FVector3f Position2 = Context.EmbreeScene.Geometry.VertexArray[Index2];

		FSurfel Surfel;
		Surfel.Position = L0 * Position0 + L1 * Position1 + L2 * Position2;
		Surfel.Normal = TriangleNormal[TriangleIndex];

		uint32 NumHits = 0;
		uint32 NumBackFaceHits = 0;
		constexpr float SurfaceRayBias = 0.1f;

		// Check if point can be clustered
		const FMatrix44f SurfaceBasis = MeshRepresentation::GetTangentBasisFrisvad(Surfel.Normal);
		for (int32 RayIndex = 0; RayIndex < RayDirectionsOverHemisphere.Num(); ++RayIndex)
		{
			const FVector3f RayOrigin = Surfel.Position;
			const FVector3f RayDirection = SurfaceBasis.TransformVector(RayDirectionsOverHemisphere[RayIndex]);

			FEmbreeRay EmbreeRay;
			EmbreeRay.ray.org_x = RayOrigin.X;
			EmbreeRay.ray.org_y = RayOrigin.Y;
			EmbreeRay.ray.org_z = RayOrigin.Z;
			EmbreeRay.ray.dir_x = RayDirection.X;
			EmbreeRay.ray.dir_y = RayDirection.Y;
			EmbreeRay.ray.dir_z = RayDirection.Z;
			EmbreeRay.ray.tnear = SurfaceRayBias;
			EmbreeRay.ray.tfar = FLT_MAX;

			FEmbreeIntersectionContext EmbreeContext;
			rtcInitIntersectContext(&EmbreeContext);
			rtcIntersect1(Context.EmbreeScene.EmbreeScene, &EmbreeContext, &EmbreeRay);

			if (EmbreeRay.hit.geomID != RTC_INVALID_GEOMETRY_ID && EmbreeRay.hit.primID != RTC_INVALID_GEOMETRY_ID)
			{
				++NumHits;

				if (FVector::DotProduct(RayDirection, EmbreeRay.GetHitNormal()) > 0.0f && !EmbreeContext.IsHitTwoSided())
				{
					++NumBackFaceHits;
				}
			}
		}
		const bool bInsideGeometry = NumHits > 0 && NumBackFaceHits > NumRayDirectionsOverHemisphere * 0.2f;

		// Fill ray cache
		for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
		{
			const FVector3f WorldSpaceDirection = AxisAlignedDirectionIndexToNormal(AxisAlignedDirectionIndex);
			Surfel.RayCache[AxisAlignedDirectionIndex] = 0.0f;

			const float SurfelNormalDotRayDir = FMath::Clamp(Surfel.Normal | WorldSpaceDirection, 0.0f, 1.0f);

			if (SurfelNormalDotRayDir > 0.0f)
			{
				const FVector3f RayOrigin = Surfel.Position;
				const FVector3f RayDirection = WorldSpaceDirection;

				FEmbreeRay EmbreeRay;
				EmbreeRay.ray.org_x = RayOrigin.X;
				EmbreeRay.ray.org_y = RayOrigin.Y;
				EmbreeRay.ray.org_z = RayOrigin.Z;
				EmbreeRay.ray.dir_x = RayDirection.X;
				EmbreeRay.ray.dir_y = RayDirection.Y;
				EmbreeRay.ray.dir_z = RayDirection.Z;
				EmbreeRay.ray.tnear = SurfaceRayBias;
				EmbreeRay.ray.tfar = FLT_MAX;

				FEmbreeIntersectionContext EmbreeContext;
				rtcInitIntersectContext(&EmbreeContext);
				rtcIntersect1(Context.EmbreeScene.EmbreeScene, &EmbreeContext, &EmbreeRay);

				Surfel.RayCache[AxisAlignedDirectionIndex] = EmbreeRay.ray.tfar;
			}
		}

		// Fill local surfel positions
		{
			for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
			{
				Surfel.LocalSurfelPosition[AxisAlignedDirectionIndex] = WorldToLocalPerDirection[AxisAlignedDirectionIndex].TransformPosition(Surfel.Position);
			}
		}

		const int32 SurfelAxisAlignedDirectionIndex = NormalToAxisAlignedDirectionIndex(Surfel.Normal);
		const bool bValidSurfel = !bInsideGeometry && Surfel.RayCache[SurfelAxisAlignedDirectionIndex] > 2.0f * ClusteringParams.SurfelRadius;
		
		if (bValidSurfel)
		{
			SurfelScene.Surfels.Add(Surfel);
		}

#if DEBUG_MESH_CARD_VISUALIZATION
		FLumenCardBuildDebugData::FSurfel& DebugSurfel = DebugData.Surfels.AddDefaulted_GetRef();
		DebugSurfel.Position = Surfel.Position;
		DebugSurfel.Normal = Surfel.Normal;
		DebugSurfel.SourceSurfelIndex = SurfelScene.Surfels.Num() - 1;
		DebugSurfel.Type = bValidSurfel ? FLumenCardBuildDebugData::ESurfelType::Valid : FLumenCardBuildDebugData::ESurfelType::Invalid;
#endif
	}

	TArray<uint16> SurfelIndicesPerDirection[NumAxisAlignedDirections];
	for (int32 SurfelIndex = 0; SurfelIndex < SurfelScene.Surfels.Num(); ++SurfelIndex)
	{
		const FSurfel& Surfel = SurfelScene.Surfels[SurfelIndex];

		for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
		{
			const float SurfelWeight = SurfelNormalWeight(Surfel, NormalPerDirection[AxisAlignedDirectionIndex], ClusteringParams);
			if (SurfelWeight > 0.0f)
			{
				SurfelScene.SurfelIndicesPerDirection[AxisAlignedDirectionIndex].Add(SurfelIndex);
			}
		}
	}
}

void GrowSingleCluster(
	const FClusteringParams& ClusteringParams,
	const FSurfelScene& SurfelScene,
	TBitArray<>& SurfelAssignedToAnyCluster,
	FSurfelCluster& Cluster)
{
	do
	{
		Cluster.UpdateBestSurfel(ClusteringParams, SurfelScene, SurfelAssignedToAnyCluster);

		if (Cluster.BestSurfelIndex >= 0)
		{
			Cluster.AddSurfel(ClusteringParams, SurfelScene, Cluster.BestSurfelIndex);
			SurfelAssignedToAnyCluster[Cluster.BestSurfelIndex] = true;
		}

	} while (Cluster.BestSurfelIndex >= 0);
}

int32 FindBestSeed(const FSurfelScene& SurfelScene, const FSurfelCluster& Cluster)
{
	int32 NormalHistogram[NumAxisAlignedDirections];
	for (int32 DirectionIndex = 0; DirectionIndex < NumAxisAlignedDirections; ++DirectionIndex)
	{
		NormalHistogram[DirectionIndex] = 0;
	}

	FVector3f PositionSum = FVector3f(0.0f, 0.0f, 0.0f);
	for (int32 SurfelIndex : Cluster.SurfelIndices)
	{
		const FSurfel& Surfel = SurfelScene.Surfels[SurfelIndex];
		PositionSum += Surfel.Position;

		const int32 NormalBucketIndex = NormalToAxisAlignedDirectionIndex(Surfel.Normal);
		NormalHistogram[NormalBucketIndex] += 1;
	}
	const FVector3f AveragePosition = PositionSum / Cluster.SurfelIndices.Num();

	int32 BestSurfelIndex = 0;
	int32 BestSurfelNormalBucketSize = 0;
	float BestSurfelDistanceSq = FLT_MAX;

	for (int32 SurfelIndex : Cluster.SurfelIndices)
	{
		const FSurfel& Surfel = SurfelScene.Surfels[SurfelIndex];
		const int32 NormalBucketIndex = NormalToAxisAlignedDirectionIndex(Surfel.Normal);
		const int32 NormalBucketSize = NormalHistogram[NormalBucketIndex];
		float SurfelDistanceSq = (AveragePosition - Surfel.Position).SizeSquared();

		if (NormalBucketSize > BestSurfelNormalBucketSize
			|| (NormalBucketSize == BestSurfelNormalBucketSize && SurfelDistanceSq < BestSurfelDistanceSq))
		{
			BestSurfelIndex = SurfelIndex;
			BestSurfelNormalBucketSize = NormalBucketSize;
			BestSurfelDistanceSq = SurfelDistanceSq;
		}
	}

	return BestSurfelIndex;
}

void GrowAllClusters(
	const FClusteringParams& ClusteringParams,
	TArray<FSurfelCluster>& Clusters,
	const FSurfelScene& SurfelScene,
	TBitArray<>& SurfelAssignedToAnyCluster
)
{
	// Reset all clusters and find their new best seeds
	for (FSurfelCluster& Cluster : Clusters)
	{
		const int32 ClusterSeedIndex = FindBestSeed(SurfelScene, Cluster);
		for (int32 SurfelIndex : Cluster.SurfelIndices)
		{
			SurfelAssignedToAnyCluster[SurfelIndex] = false;
		}

		Cluster.Reset();
		Cluster.AddSurfel(ClusteringParams, SurfelScene, ClusterSeedIndex);
		SurfelAssignedToAnyCluster[ClusterSeedIndex] = true;
	}

	// Fill surfel heap
	for (FSurfelCluster& Cluster : Clusters)
	{
		Cluster.UpdateBestSurfel(ClusteringParams, SurfelScene, SurfelAssignedToAnyCluster);
	}

	// Cluster surfels
	int32 BestClusterToAddIndex = -1;
	float BestSurfelToAddWeight = 0.0f;

	do 
	{
		BestClusterToAddIndex = -1;
		BestSurfelToAddWeight = 0.0f;

		for (int32 ClusterIndex = 0; ClusterIndex < Clusters.Num(); ++ClusterIndex)
		{
			FSurfelCluster& Cluster = Clusters[ClusterIndex];

			if (Cluster.BestSurfelIndex >= 0 && SurfelAssignedToAnyCluster[Cluster.BestSurfelIndex])
			{
				Cluster.UpdateBestSurfel(ClusteringParams, SurfelScene, SurfelAssignedToAnyCluster);
			}

			if (Cluster.BestSurfelIndex >= 0 && Cluster.BestSurfelWeight > BestSurfelToAddWeight)
			{
				BestClusterToAddIndex = ClusterIndex;
				BestSurfelToAddWeight = Cluster.BestSurfelWeight;
			}
		}

		if (BestClusterToAddIndex >= 0)
		{
			FSurfelCluster& Cluster = Clusters[BestClusterToAddIndex];

			Cluster.AddSurfel(ClusteringParams, SurfelScene, Cluster.BestSurfelIndex);
			SurfelAssignedToAnyCluster[Cluster.BestSurfelIndex] = true;

			Cluster.UpdateBestSurfel(ClusteringParams, SurfelScene, SurfelAssignedToAnyCluster);
		}

	} while (BestClusterToAddIndex >= 0);
}


struct FMeshCardsLODLevel
{
	TArray<FSurfelCluster> Clusters;
	float SurfaceCoverage;
	float ClusterArea;
};

void UpdateLODLevelCoverage(const FSurfelScene& SurfelScene, const FClusteringParams& ClusteringParams, FMeshCardsLODLevel& LODLevel)
{
	LODLevel.ClusterArea = 0.0f;
	TArray<float> SurfelCoverage;
	SurfelCoverage.SetNumZeroed(SurfelScene.Surfels.Num());
	for (const FSurfelCluster& Cluster : LODLevel.Clusters)
	{
		for (int32 SurfelIndex : Cluster.SurfelIndices)
		{
			const FSurfel& Surfel = SurfelScene.Surfels[SurfelIndex];

			const float SurfelWeight = SurfelNormalWeight(Surfel, Cluster.Normal, ClusteringParams);
			SurfelCoverage[SurfelIndex] = FMath::Max(SurfelCoverage[SurfelIndex], SurfelWeight);
		}

		const FVector3f BoundsSize = Cluster.Bounds.GetSize();
		LODLevel.ClusterArea += BoundsSize.X * BoundsSize.Y;
	}

	float SurfaceCoverageSum = 0.0f;
	for (int32 SurfelIndex = 0; SurfelIndex < SurfelScene.Surfels.Num(); ++SurfelIndex)
	{
		SurfaceCoverageSum += SurfelCoverage[SurfelIndex];
	}

	LODLevel.SurfaceCoverage = 0.0f;
	if (SurfelScene.Surfels.Num() > 0)
	{
		LODLevel.SurfaceCoverage = SurfaceCoverageSum / SurfelScene.Surfels.Num();
	}
}

// Cluster only by direction
void BuildMeshCardsLOD0(const FBox& MeshBounds, const FGenerateCardMeshContext& Context, const FSurfelScene& SurfelScene, const FClusteringParams& ClusteringParams, FMeshCardsLODLevel& LODLevel)
{
	FSurfelCluster TempCluster;

	for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
	{
		TempCluster.Reset();
		TempCluster.SetDirection(AxisAlignedDirectionIndex);

		for (int32 SurfelIndex : SurfelScene.SurfelIndicesPerDirection[AxisAlignedDirectionIndex])
		{
			const FSurfel& Surfel = SurfelScene.Surfels[SurfelIndex];

			const float SurfelWeight = SurfelNormalWeight(Surfel, TempCluster.Normal, ClusteringParams);

			if (Surfel.RayCache[TempCluster.AxisAlignedDirectionIndex] == FLT_MAX && SurfelWeight > 0.0f)
			{
				const FVector3f LocalPositon = Surfel.LocalSurfelPosition[TempCluster.AxisAlignedDirectionIndex];

				TempCluster.SurfelIndices.Add(SurfelIndex);
				TempCluster.Bounds += LocalPositon - ClusteringParams.SurfelExtendRadius;
				TempCluster.Bounds += LocalPositon + ClusteringParams.SurfelExtendRadius;
			}
		}

		if (TempCluster.IsValid(ClusteringParams))
		{
			LODLevel.Clusters.Add(TempCluster);
		}
	}

	UpdateLODLevelCoverage(SurfelScene, ClusteringParams, LODLevel);
}

// Cluster by direction and position
void BuildMeshCardsLOD1(const FBox& MeshBounds, const FGenerateCardMeshContext& Context, const FSurfelScene& SurfelScene, const FClusteringParams& ClusteringParams, FMeshCardsLODLevel& LODLevel)
{
	static const auto CVarMeshCardRepresentationSeedIterations = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MeshCardRepresentation.SeedIterations"));
	static const auto CVarMeshCardRepresentationGrowIterations = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.MeshCardRepresentation.GrowIterations"));
	const int32 NumSeedIterations = FMath::Clamp(CVarMeshCardRepresentationSeedIterations->GetValueOnAnyThread(), 1, 32);
	const int32 NumGrowIterations = FMath::Clamp(CVarMeshCardRepresentationGrowIterations->GetValueOnAnyThread(), 0, 32);

	// Init list of cluster seeds
	TArray<int32> ClusterSeeds;
	ClusterSeeds.SetNumUninitialized(SurfelScene.Surfels.Num());
	for (int32 SurfelIndex = 0; SurfelIndex < SurfelScene.Surfels.Num(); ++SurfelIndex)
	{
		ClusterSeeds[SurfelIndex] = SurfelIndex;
	}

	FRandomStream ClusterSeedRandomStream(0);
	TBitArray<> SurfelAssignedToAnyCluster(false, SurfelScene.Surfels.Num());
	FSurfelCluster TempCluster;

	// Generate initial list of clusters
	while (ClusterSeeds.Num() > 0)
	{
		// Select next seed
		int32 NextSeedSurfelIndex = -1;
		while (ClusterSeeds.Num() > 0)
		{
			int32 RandomIndex = ClusterSeedRandomStream.RandHelper(ClusterSeeds.Num());
			const int32 ClusterSeed = ClusterSeeds[RandomIndex];
			ClusterSeeds.RemoveAtSwap(RandomIndex);

			if (!SurfelAssignedToAnyCluster[ClusterSeed])
			{
				NextSeedSurfelIndex = ClusterSeed;
				break;
			}
		}

		// Try to build a cluster using selected seed
		if (NextSeedSurfelIndex >= 0)
		{
			for (int32 ClusteringIterationIndex = 0; ClusteringIterationIndex < NumSeedIterations; ++ClusteringIterationIndex)
			{
				TempCluster.Reset();
				TempCluster.SetDirection(NormalToAxisAlignedDirectionIndex(SurfelScene.Surfels[NextSeedSurfelIndex].Normal));
				TempCluster.AddSurfel(ClusteringParams, SurfelScene, NextSeedSurfelIndex);
				SurfelAssignedToAnyCluster[NextSeedSurfelIndex] = true;

				GrowSingleCluster(
					ClusteringParams,
					SurfelScene,
					SurfelAssignedToAnyCluster,
					TempCluster);

				// Restore global state
				for (int32 SurfelIndex : TempCluster.SurfelIndices)
				{
					SurfelAssignedToAnyCluster[SurfelIndex] = false;
				}

				if (TempCluster.IsValid(ClusteringParams))
				{
					const int32 AverageSurfelIndex = FindBestSeed(SurfelScene, TempCluster);
					if (AverageSurfelIndex != NextSeedSurfelIndex)
					{
						ClusterSeeds.RemoveSwap(AverageSurfelIndex);
						NextSeedSurfelIndex = AverageSurfelIndex;
					}
					else
					{
						break;
					}
				}
				else
				{
					break;
				}
			}

			// Add new cluster only if it has least MinSurfelsPerCluster points
			if (TempCluster.IsValid(ClusteringParams))
			{
				LODLevel.Clusters.Add(TempCluster);

				for (int32 SurfelIndex : TempCluster.SurfelIndices)
				{
					SurfelAssignedToAnyCluster[SurfelIndex] = true;
				}
			}
		}
	}

	// Grow all clusters simultaneously from the best seed
	for (int32 ClusteringIterationIndex = 0; ClusteringIterationIndex < NumGrowIterations; ++ClusteringIterationIndex)
	{
		GrowAllClusters(
			ClusteringParams,
			LODLevel.Clusters,
			SurfelScene,
			SurfelAssignedToAnyCluster
		);

		bool bAnyClusterSeedChanged = false;
		for (FSurfelCluster& Cluster : LODLevel.Clusters)
		{
			const int32 ClusterSeedIndex = FindBestSeed(SurfelScene, Cluster);
			if (ClusterSeedIndex != Cluster.SurfelIndices[0])
			{
				bAnyClusterSeedChanged = true;
				break;
			}
		}

		if (!bAnyClusterSeedChanged)
		{
			break;
		}
	}

	UpdateLODLevelCoverage(SurfelScene, ClusteringParams, LODLevel);
}

void SerializeLOD(const FGenerateCardMeshContext& Context, const FClusteringParams& ClusteringParams, const FSurfelScene& SurfelScene, FMeshCardsLODLevel const& LODLevel, int32 LODLevelIndex, FMeshCardsBuildData& MeshCardsBuildData)
{
	MeshCardsBuildData.MaxLODLevel = FMath::Max(MeshCardsBuildData.MaxLODLevel, LODLevelIndex);

#if DEBUG_MESH_CARD_VISUALIZATION
	TBitArray<> DebugSurfelInCluster;
	TBitArray<> DebugSurfelInAnyCluster(false, SurfelScene.Surfels.Num());

	UE_LOG(LogMeshUtilities, Log, TEXT("CardGen Mesh:%s LOD:%d Surfels:%d Clusters:%d SurfaceCoverage:%f ClusterArea:%f"),
		*Context.MeshName,
		LODLevelIndex,
		SurfelScene.Surfels.Num(),
		LODLevel.Clusters.Num(),
		LODLevel.SurfaceCoverage,
		LODLevel.ClusterArea);
#endif

	for (const FSurfelCluster& Cluster : LODLevel.Clusters)
	{
		if (Cluster.IsValid(ClusteringParams))
		{
			const FMatrix LocalToWorld = Cluster.WorldToLocal.Inverse();

			FLumenCardBuildData BuiltData;
			BuiltData.OBB.Origin = LocalToWorld.TransformPosition(Cluster.Bounds.GetCenter());
			BuiltData.OBB.Extent = Cluster.Bounds.GetExtent();
			BuiltData.OBB.AxisX = LocalToWorld.GetScaledAxis(EAxis::X);
			BuiltData.OBB.AxisY = LocalToWorld.GetScaledAxis(EAxis::Y);
			BuiltData.OBB.AxisZ = LocalToWorld.GetScaledAxis(EAxis::Z);
			BuiltData.LODLevel = LODLevelIndex;
			BuiltData.AxisAlignedDirectionIndex = Cluster.AxisAlignedDirectionIndex;
			MeshCardsBuildData.CardBuildData.Add(BuiltData);

#if DEBUG_MESH_CARD_VISUALIZATION
			DebugSurfelInCluster.Reset();
			DebugSurfelInCluster.Add(false, SurfelScene.Surfels.Num());

			FLumenCardBuildDebugData::FSurfelCluster& DebugCluster = MeshCardsBuildData.DebugData.Clusters.AddDefaulted_GetRef();
			DebugCluster.Surfels.Reserve(SurfelScene.Surfels.Num());

			// Cluster seed
			{
				FLumenCardBuildDebugData::FSurfel DebugSurfel;
				DebugSurfel.Position = SurfelScene.Surfels[Cluster.SurfelIndices[0]].Position;
				DebugSurfel.Normal = SurfelScene.Surfels[Cluster.SurfelIndices[0]].Normal;
				DebugSurfel.SourceSurfelIndex = 0;
				DebugSurfel.Type = FLumenCardBuildDebugData::ESurfelType::Seed;
				DebugCluster.Surfels.Add(DebugSurfel);
			}

			{
				const int32 AverageSurfelIndex = FindBestSeed(SurfelScene, Cluster);

				FLumenCardBuildDebugData::FSurfel DebugSurfel;
				DebugSurfel.Position = SurfelScene.Surfels[AverageSurfelIndex].Position;
				DebugSurfel.Normal = SurfelScene.Surfels[AverageSurfelIndex].Normal;
				DebugSurfel.SourceSurfelIndex = AverageSurfelIndex;
				DebugSurfel.Type = FLumenCardBuildDebugData::ESurfelType::Seed2;
				DebugCluster.Surfels.Add(DebugSurfel);
			}

			for (int32 SurfelIndex : Cluster.SurfelIndices)
			{
				FLumenCardBuildDebugData::FSurfel DebugSurfel;
				DebugSurfel.Position = SurfelScene.Surfels[SurfelIndex].Position;
				DebugSurfel.Normal = SurfelScene.Surfels[SurfelIndex].Normal;
				DebugSurfel.SourceSurfelIndex = SurfelIndex;
				DebugSurfel.Type = FLumenCardBuildDebugData::ESurfelType::Cluster;
				DebugCluster.Surfels.Add(DebugSurfel);

				const float RayT = SurfelScene.Surfels[SurfelIndex].RayCache[Cluster.AxisAlignedDirectionIndex];
				if (RayT < FLT_MAX)
				{
					FLumenCardBuildDebugData::FRay DebugRay;
					DebugRay.RayStart = DebugSurfel.Position;
					DebugRay.RayEnd = DebugRay.RayStart + RayT * Cluster.Normal;
					DebugRay.bHit = false;
					DebugCluster.Rays.Add(DebugRay);
				}

				DebugSurfelInAnyCluster[SurfelIndex] = true;
				DebugSurfelInCluster[SurfelIndex] = true;
			}

			for (int32 SurfelIndex = 0; SurfelIndex < SurfelScene.Surfels.Num(); ++SurfelIndex)
			{
				if (!DebugSurfelInCluster[SurfelIndex])
				{
					FLumenCardBuildDebugData::FSurfel DebugSurfel;
					DebugSurfel.Position = SurfelScene.Surfels[SurfelIndex].Position;
					DebugSurfel.Normal = SurfelScene.Surfels[SurfelIndex].Normal;
					DebugSurfel.SourceSurfelIndex = SurfelIndex;
					DebugSurfel.Type = DebugSurfelInAnyCluster[SurfelIndex] ? FLumenCardBuildDebugData::ESurfelType::Used : FLumenCardBuildDebugData::ESurfelType::Idle;
					DebugCluster.Surfels.Add(DebugSurfel);
				}
			}
#endif
		}
	}
}

void BuildMeshCards(const FBox& MeshBounds, const FGenerateCardMeshContext& Context, FCardRepresentationData& OutData)
{
	// Make sure BBox isn't empty and we can generate card representation for it. This handles e.g. infinitely thin planes.
	const FVector MeshCardsBoundsCenter = MeshBounds.GetCenter();
	const FVector MeshCardsBoundsExtent = FVector::Max(MeshBounds.GetExtent() + 1.0f, FVector(5.0f));
	const FBox MeshCardsBounds(MeshCardsBoundsCenter - MeshCardsBoundsExtent, MeshCardsBoundsCenter + MeshCardsBoundsExtent);

	// Prepare a list of surfels for cluster fitting
	FSurfelScene SurfelScene;
	FClusteringParams ClusteringParamsLOD1;
	GenerateSurfels(Context, MeshCardsBounds, SurfelScene, ClusteringParamsLOD1, OutData.MeshCardsBuildData.DebugData);

	FClusteringParams ClusteringParamsLOD0 = ClusteringParamsLOD1;
	ClusteringParamsLOD0.MinSurfelsPerCluster /= 2;

	FMeshCardsLODLevel MeshCardsLOD0;
	BuildMeshCardsLOD0(MeshBounds, Context, SurfelScene, ClusteringParamsLOD0, MeshCardsLOD0);

	FMeshCardsLODLevel MeshCardsLOD1;
	BuildMeshCardsLOD1(MeshBounds, Context, SurfelScene, ClusteringParamsLOD1, MeshCardsLOD1);

	OutData.MeshCardsBuildData.Bounds = MeshCardsBounds;
	OutData.MeshCardsBuildData.MaxLODLevel = 0;
	OutData.MeshCardsBuildData.CardBuildData.Reset();

	SerializeLOD(Context, ClusteringParamsLOD0, SurfelScene, MeshCardsLOD0, 0, OutData.MeshCardsBuildData);

	// Optionally serialize LOD1 if it's of higher quality without exceeding the budget
	if (MeshCardsLOD1.Clusters.Num() <= MaxCardsPerMesh 
		&& MeshCardsLOD1.SurfaceCoverage > MeshCardsLOD0.SurfaceCoverage)
	{
		float WeightedSurfaceCoverageIncrease = (MeshCardsLOD1.SurfaceCoverage - MeshCardsLOD0.SurfaceCoverage) / FMath::Max(MeshCardsLOD1.Clusters.Num() - MeshCardsLOD0.Clusters.Num(), 1);
		if (WeightedSurfaceCoverageIncrease > 0.02f * (1.0f + SurfelScene.TwoSidedTriangleRatio))
		{
			SerializeLOD(Context, ClusteringParamsLOD1, SurfelScene, MeshCardsLOD1, 1, OutData.MeshCardsBuildData);
		}
	}
}

#endif // #if USE_EMBREE

bool FMeshUtilities::GenerateCardRepresentationData(
	FString MeshName,
	const FSourceMeshDataForDerivedDataTask& SourceMeshData,
	const FStaticMeshLODResources& LODModel,
	class FQueuedThreadPool& ThreadPool,
	const TArray<FSignedDistanceFieldBuildMaterialData>& MaterialBlendModes,
	const FBoxSphereBounds& Bounds,
	const FDistanceFieldVolumeData* DistanceFieldVolumeData,
	bool bGenerateAsIfTwoSided,
	FCardRepresentationData& OutData)
{
#if USE_EMBREE
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshUtilities::GenerateCardRepresentationData);
	const double StartTime = FPlatformTime::Seconds();

	FEmbreeScene EmbreeScene;
	MeshRepresentation::SetupEmbreeScene(MeshName,
		SourceMeshData,
		LODModel,
		MaterialBlendModes,
		bGenerateAsIfTwoSided,
		EmbreeScene);

	if (!EmbreeScene.EmbreeScene)
	{
		return false;
	}

	FGenerateCardMeshContext Context(MeshName, EmbreeScene, OutData);

	// Note: must operate on the SDF bounds because SDF generation can expand the mesh's bounds
	BuildMeshCards(DistanceFieldVolumeData ? DistanceFieldVolumeData->LocalSpaceMeshBounds : Bounds.GetBox(), Context, OutData);

	MeshRepresentation::DeleteEmbreeScene(EmbreeScene);

	const float TimeElapsed = (float)(FPlatformTime::Seconds() - StartTime);
	if (TimeElapsed > 1.0f)
	{
		UE_LOG(LogMeshUtilities, Log, TEXT("Finished mesh card build in %.1fs %s tris:%d"),
			TimeElapsed,
			*MeshName,
			EmbreeScene.NumIndices / 3);
	}

	return true;
#else
	UE_LOG(LogMeshUtilities, Warning, TEXT("Platform did not set USE_EMBREE, GenerateCardRepresentationData failed."));
	return false;
#endif
}
