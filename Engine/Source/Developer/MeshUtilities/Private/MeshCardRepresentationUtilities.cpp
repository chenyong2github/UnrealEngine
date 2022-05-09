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

static TAutoConsoleVariable<int32> CVarCardRepresentationParallelBuild(
	TEXT("r.MeshCardRepresentation.ParallelBuild"),
	1,
	TEXT("Whether to use task for mesh card building."),
	ECVF_Scalability);

namespace MeshCardGen
{
	int32 constexpr NumAxisAlignedDirections = 6;
	int32 constexpr MaxCardsPerMesh = 32;
};

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

struct FIntBox
{
	FIntBox()
		: Min(INT32_MAX)
		, Max(-INT32_MAX)
	{}

	FIntBox(const FIntVector& InMin, const FIntVector& InMax)
		: Min(InMin)
		, Max(InMax)
	{}

	void Init()
	{
		Min = FIntVector(INT32_MAX);
		Max = FIntVector(-INT32_MAX);
	}

	void Add(const FIntVector& Point)
	{
		Min = FIntVector(FMath::Min(Min.X, Point.X), FMath::Min(Min.Y, Point.Y), FMath::Min(Min.Z, Point.Z));
		Max = FIntVector(FMath::Max(Max.X, Point.X), FMath::Max(Max.Y, Point.Y), FMath::Max(Max.Z, Point.Z));
	}

	FIntVector2 GetFaceXY() const
	{
		return FIntVector2(Max.X + 1 - Min.X, Max.Y + 1 - Min.Y);
	}

	int32 GetFaceArea() const
	{
		return (Max.X + 1 - Min.X) * (Max.Y + 1 - Min.Y);
	}

	bool Contains(const FIntBox& Other) const
	{
		if (Other.Min.X >= Min.X && Other.Max.X <= Max.X
			&& Other.Min.Y >= Min.Y && Other.Max.Y <= Max.Y
			&& Other.Min.Z >= Min.Z && Other.Max.Z <= Max.Z)
		{
			return true;
		}

		return false;
	}

	FIntVector GetAxisDistanceFromBox(const FIntBox& Box)
	{
		const FIntVector CenterDelta2 = (Max - Min) - (Box.Max - Box.Min);
		const FIntVector ExtentSum2 = (Max + Min) + (Box.Max + Box.Min);

		FIntVector AxisDistance;
		AxisDistance.X = FMath::Max(FMath::Abs(CenterDelta2.X) - ExtentSum2.X, 0) / 2;
		AxisDistance.Y = FMath::Max(FMath::Abs(CenterDelta2.Y) - ExtentSum2.Y, 0) / 2;
		AxisDistance.Z = FMath::Max(FMath::Abs(CenterDelta2.Z) - ExtentSum2.Z, 0) / 2;
		return AxisDistance;
	}

	FIntVector GetAxisDistanceFromPoint(const FIntVector& Point)
	{
		FIntVector AxisDistance;
		AxisDistance.X = FMath::Max(FMath::Max(Min.X - Point.X, Point.X - Max.X), 0);
		AxisDistance.Y = FMath::Max(FMath::Max(Min.Y - Point.Y, Point.Y - Max.Y), 0);
		AxisDistance.Z = FMath::Max(FMath::Max(Min.Z - Point.Z, Point.Z - Max.Z), 0);
		return AxisDistance;
	}

	FIntVector Min;
	FIntVector Max;
};

#if USE_EMBREE

typedef uint16 FSurfelIndex;
constexpr FSurfelIndex INVALID_SURFEL_INDEX = UINT16_MAX;

struct FSurfel
{
	FIntVector Coord;

	// Card's min near plane distance from this surfel
	int32 MinRayZ;

	// Percentage of rays which hit something in this cell
	float Coverage;

	// Coverage weighted by the visibility of this surfel from outside the mesh, decides how important is to cover this surfel
	float WeightedCoverage;

	float GetDistanceWeight(const FVector3f AxisDistances) const
	{
		return AxisDistances.X + AxisDistances.Y + AxisDistances.Z / 16.0f;
	}

	float GetMinRayZWeight() const
	{
		float Weight = 0.0f;
		if (MinRayZ > 0)
		{
			Weight = FMath::Clamp((16.0f - (Coord.Z - MinRayZ)) / 16.0f, 0.0f, 1.0f);
		}
		return Weight;
	}

};

struct FSurfelScenePerDirection
{
	TArray<FSurfel> Surfels;
	FLumenCardBuildDebugData DebugData;

	void Init()
	{
		DebugData.Init();
		Surfels.Reset();
	}
};

struct FSurfelScene
{
	FSurfelScenePerDirection Directions[MeshCardGen::NumAxisAlignedDirections];
	int32 NumSurfels = 0;
};

struct FAxisAlignedDirectionBasis
{
	FMatrix44f LocalToWorldRotation;
	FVector3f LocalToWorldOffset;
	FIntVector VolumeSize;
	float VoxelSize;

	FVector3f TransformSurfel(FIntVector SurfelCoord) const
	{
		return LocalToWorldRotation.TransformPosition(FVector3f(SurfelCoord.X + 0.5f, SurfelCoord.Y + 0.5f, SurfelCoord.Z)) * VoxelSize + LocalToWorldOffset;
	}
};

struct FClusteringParams
{
	float VoxelSize = 0.0f;
	int32 MaxSurfelDistanceXY = 0;
	float MinClusterCoverage = 0.0f;
	float MinDensityPerCluster = 0.0f;
	int32 MaxLumenMeshCards = 0;
	bool bDebug = false;
	bool bSingleThreadedBuild = true;

	FAxisAlignedDirectionBasis ClusterBasis[MeshCardGen::NumAxisAlignedDirections];
};

FVector3f AxisAlignedDirectionIndexToNormal(int32 AxisAlignedDirectionIndex)
{
	const int32 AxisIndex = AxisAlignedDirectionIndex / 2;

	FVector3f Normal(0.0f, 0.0f, 0.0f);
	Normal[AxisIndex] = AxisAlignedDirectionIndex & 1 ? 1.0f : -1.0f;
	return Normal;
}

class FSurfelCluster
{
public:
	FIntBox Bounds;
	TArray<FSurfelIndex> SurfelIndices;

	FIntBox PotentialSurfelsBounds;
	TArray<int32> PotentialSurfels;

	int32 MinRayZ = 0;
	float Coverage = 0.0f;

	// Coverage weighted by visibility
	float WeightedCoverage = 0.0f;

	// Best surfels to add to this cluster
	FSurfelIndex BestSurfelIndex = INVALID_SURFEL_INDEX;
	float BestSurfelDistance = FLT_MAX;

	void Init()
	{
		Bounds.Init();
		PotentialSurfelsBounds.Init();
		SurfelIndices.Reset();
		PotentialSurfels.Reset();
		MinRayZ = 0;
		Coverage = 0.0f;
		WeightedCoverage = 0.0f;
		BestSurfelIndex = INVALID_SURFEL_INDEX;
		BestSurfelDistance = FLT_MAX;
	}

	bool IsValid(const FClusteringParams& ClusteringParams) const
	{
		return Coverage >= ClusteringParams.MinClusterCoverage
			&& GetDensity() > ClusteringParams.MinDensityPerCluster;
	}

	float GetDensity() const
	{
		const float Density = Coverage / (float)Bounds.GetFaceArea();
		return Density;
	}

	float GetDensityAfterAdd(const FIntVector& Expand, const FSurfel& Surfel) const
	{
		FIntVector2 FaceXY = Bounds.GetFaceXY();
		FaceXY.X += Expand.X;
		FaceXY.Y += Expand.Y;

		const float Density = (Coverage + Surfel.Coverage) / float(FaceXY.X * FaceXY.Y);
		return Density;
	}

	void AddSurfel(const FClusteringParams& ClusteringParams, const FSurfelScenePerDirection& SurfelScene, FSurfelIndex SurfelToAddIndex);
	void UpdateBestSurfel(const FClusteringParams& ClusteringParams, const FSurfelScenePerDirection& SurfelScene, const TBitArray<>& SurfelAssignedToAnyCluster);
};

void FSurfelCluster::AddSurfel(const FClusteringParams& ClusteringParams, const FSurfelScenePerDirection& SurfelScene, FSurfelIndex SurfelToAddIndex)
{
	const FSurfel& SurfelToAdd = SurfelScene.Surfels[SurfelToAddIndex];
	SurfelIndices.Add(SurfelToAddIndex);

	Bounds.Add(SurfelToAdd.Coord);

	MinRayZ = FMath::Max(MinRayZ, SurfelToAdd.MinRayZ);
	Coverage += SurfelToAdd.Coverage;
	WeightedCoverage += SurfelToAdd.WeightedCoverage;

	// Check if all surfels are visible after add
	check(MinRayZ <= Bounds.Min.Z);
}

void FSurfelCluster::UpdateBestSurfel(
	const FClusteringParams& ClusteringParams,
	const FSurfelScenePerDirection& SurfelScene,
	const TBitArray<>& SurfelAssignedToAnyCluster)
{
	BestSurfelIndex = INVALID_SURFEL_INDEX;
	BestSurfelDistance = FLT_MAX;

	// Update potential surfel array if required
	if (!PotentialSurfelsBounds.Contains(Bounds))
	{
		const FIntVector PotentialSurfelMargin(2, 2, 2);

		PotentialSurfels.Reset();
		PotentialSurfelsBounds.Min = Bounds.Min - FIntVector(PotentialSurfelMargin);
		PotentialSurfelsBounds.Max = Bounds.Max + FIntVector(PotentialSurfelMargin);

		for (int32 SurfelIndex = 0; SurfelIndex < SurfelScene.Surfels.Num(); ++SurfelIndex)
		{
			if (!SurfelAssignedToAnyCluster[SurfelIndex])
			{
				const FSurfel& Surfel = SurfelScene.Surfels[SurfelIndex];
				if (Surfel.Coord.Z >= MinRayZ && Surfel.MinRayZ <= Bounds.Min.Z)
				{
					const FIntVector AxisDistances = PotentialSurfelsBounds.GetAxisDistanceFromPoint(Surfel.Coord);
					if (AxisDistances.X + AxisDistances.Y <= ClusteringParams.MaxSurfelDistanceXY)
					{
						PotentialSurfels.Add(SurfelIndex);
					}
				}
			}
		}
	}

	for (int32 SurfelIndex : PotentialSurfels)
	{
		if (!SurfelAssignedToAnyCluster[SurfelIndex])
		{
			const FSurfel& Surfel = SurfelScene.Surfels[SurfelIndex];

			const FIntVector AxisDistances = Bounds.GetAxisDistanceFromPoint(Surfel.Coord);

			const float DensityAfterAdd = GetDensityAfterAdd(AxisDistances, Surfel);

			const bool bPassDistanceTest = AxisDistances.X + AxisDistances.Y <= ClusteringParams.MaxSurfelDistanceXY;
			const bool bPassMinZTest = Surfel.Coord.Z >= MinRayZ && Surfel.MinRayZ <= Bounds.Min.Z;
			const bool bPassDensityTest = DensityAfterAdd > ClusteringParams.MinDensityPerCluster;

			if (bPassDistanceTest && bPassMinZTest && bPassDensityTest)
			{
				// Weight by distance
				float SurfelDistance = Surfel.GetDistanceWeight(FVector3f(AxisDistances));

				// Weight by aspect ratio
				const FIntVector2 FaceXY = Bounds.GetFaceXY();
				if (FaceXY.X > FaceXY.Y && AxisDistances.X < AxisDistances.Y)
				{
					SurfelDistance -= 0.5f;
				}
				else if (FaceXY.X < FaceXY.Y && AxisDistances.X > AxisDistances.Y)
				{
					SurfelDistance -= 0.5f;
				}

				// Weight by density
				{
					SurfelDistance -= DensityAfterAdd;
				}

				// Weight by MinRayZ
				if (Surfel.MinRayZ > 0)
				{
					SurfelDistance += Surfel.GetMinRayZWeight();
				}

				if (SurfelDistance < BestSurfelDistance)
				{
					BestSurfelIndex = SurfelIndex;
					BestSurfelDistance = SurfelDistance;
				}
			}
		}
	}
}

struct FSurfelSample
{
	FVector3f Position;
	FVector3f Normal;
	int32 MinRayZ;
	int32 CellZ;
};

struct FSurfelVisibility
{
	float Visibility;
	bool bValid;
};

// Trace rays over the hemisphere and discard surfels which mostly hit back faces
FSurfelVisibility ComputeSurfelVisibility(
	const FGenerateCardMeshContext& Context,
	const TArray<FSurfelSample>& SurfelSamples,
	uint32 SurfelSamplesOffset,
	uint32 SurfelSamplesNum,
	const TArray<FVector3f>& RayDirectionsOverHemisphere,
	FLumenCardBuildDebugData& DebugData)
{
	uint32 SurfelSampleIndex = 0;
	uint32 NumHits = 0;
	uint32 NumBackFaceHits = 0;
	const float SurfaceRayBias = 0.1f;
	float VisibilitySum = 0.0f;

	for (int32 RayIndex = 0; RayIndex < RayDirectionsOverHemisphere.Num(); ++RayIndex)
	{
		const FSurfelSample& SurfelSample = SurfelSamples[SurfelSampleIndex + SurfelSamplesOffset];
		const FMatrix44f SurfaceBasis = MeshRepresentation::GetTangentBasisFrisvad(SurfelSample.Normal);
		const FVector3f RayOrigin = SurfelSample.Position;
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
			if (FVector::DotProduct((FVector)RayDirection, (FVector)EmbreeRay.GetHitNormal()) > 0.0f && !EmbreeContext.IsHitTwoSided())
			{
				++NumBackFaceHits;
			}
			else
			{
				VisibilitySum += FMath::Clamp(EmbreeRay.ray.tfar / 1000.0f, 0.0f, 0.5f);
			}
		}
		else
		{
			VisibilitySum += 1.0f;
		}

#if 0
		FLumenCardBuildDebugData::FRay& SurfelRay = DebugData.SurfelRays.AddDefaulted_GetRef();
		SurfelRay.RayStart = RayOrigin;
		SurfelRay.RayEnd = RayOrigin + RayDirection * (EmbreeRay.ray.tfar < FLT_MAX ? EmbreeRay.ray.tfar : 200.0f);
		SurfelRay.bHit = EmbreeRay.ray.tfar < FLT_MAX;
#endif

		SurfelSampleIndex = (SurfelSampleIndex + 1) % SurfelSamplesNum;
	}

	const bool bInsideGeometry = NumHits > 0 && NumBackFaceHits > RayDirectionsOverHemisphere.Num() * 0.2f;

	FSurfelVisibility SurfelVisibility;
	SurfelVisibility.Visibility = VisibilitySum / RayDirectionsOverHemisphere.Num();
	SurfelVisibility.bValid = !bInsideGeometry;
	return SurfelVisibility;
}

/**
 * Voxelize mesh by casting multiple rays per cell
 */
void GenerateSurfelsForDirection(
	const FGenerateCardMeshContext& Context,
	const FAxisAlignedDirectionBasis& ClusterBasis,	
	const TArray<FVector3f>& RayDirectionsOverHemisphere,
	const FClusteringParams& ClusteringParams,
	FSurfelScenePerDirection& SurfelScenePerDirection)
{
	const float NormalWeightTreshold = MeshCardRepresentation::GetNormalTreshold();
	const FVector3f RayDirection = ClusterBasis.LocalToWorldRotation.GetScaledAxis(EAxis::Type::Z);

	const uint32 NumSurfelSamples = 32;
	const uint32 MinSurfelSamples = 1;

	TArray<FSurfelSample> SurfelSamples;
	TArray<uint32> NumSurfelSamplesPerCell;
	TArray<uint32> SurfelSamplesOffsetPerCell;

	for (int32 CoordY = 0; CoordY < ClusterBasis.VolumeSize.Y; ++CoordY)
	{
		for (int32 CoordX = 0; CoordX < ClusterBasis.VolumeSize.X; ++CoordX)
		{
			SurfelSamples.Reset();
			NumSurfelSamplesPerCell.SetNum(ClusterBasis.VolumeSize.Z);
			SurfelSamplesOffsetPerCell.SetNum(ClusterBasis.VolumeSize.Z);
			for (int32 CoordZ = 0; CoordZ < ClusterBasis.VolumeSize.Z; ++CoordZ)
			{
				NumSurfelSamplesPerCell[CoordZ] = 0;
				SurfelSamplesOffsetPerCell[CoordZ] = 0;
			}

			// Trace multiple rays per cell and mark cells which need to spawn a surfel
			for (uint32 SampleIndex = 0; SampleIndex < NumSurfelSamples; ++SampleIndex)
			{
				FVector3f Jitter;
				Jitter.X = (SampleIndex + 0.5f) / NumSurfelSamples;
				Jitter.Y = (double)ReverseBits(SampleIndex) / (double)0x100000000LL;

				FVector3f RayOrigin = ClusterBasis.LocalToWorldRotation.TransformPosition(FVector3f(CoordX + Jitter.X, CoordY + Jitter.Y, 0.0f)) * ClusteringParams.VoxelSize + ClusterBasis.LocalToWorldOffset;

				int32 LastHitCoordZ = -2;
				while (LastHitCoordZ < ClusterBasis.VolumeSize.Z)
				{
					FEmbreeRay EmbreeRay;
					EmbreeRay.ray.org_x = RayOrigin.X;
					EmbreeRay.ray.org_y = RayOrigin.Y;
					EmbreeRay.ray.org_z = RayOrigin.Z;
					EmbreeRay.ray.dir_x = RayDirection.X;
					EmbreeRay.ray.dir_y = RayDirection.Y;
					EmbreeRay.ray.dir_z = RayDirection.Z;
					EmbreeRay.ray.tnear = (LastHitCoordZ + 1) * ClusteringParams.VoxelSize;
					EmbreeRay.ray.tfar = FLT_MAX;

					FEmbreeIntersectionContext EmbreeContext;
					rtcInitIntersectContext(&EmbreeContext);
					rtcIntersect1(Context.EmbreeScene.EmbreeScene, &EmbreeContext, &EmbreeRay);

					if (EmbreeRay.hit.geomID != RTC_INVALID_GEOMETRY_ID && EmbreeRay.hit.primID != RTC_INVALID_GEOMETRY_ID)
					{
						const int32 HitCoordZ = FMath::Clamp(EmbreeRay.ray.tfar / ClusteringParams.VoxelSize, 0, ClusterBasis.VolumeSize.Z - 1);

						FVector SurfaceNormal = (FVector)EmbreeRay.GetHitNormal();
						float NdotD = FVector::DotProduct((FVector)-RayDirection, SurfaceNormal);

						// Handle two sided hits
						if (NdotD < 0.0f && EmbreeContext.IsHitTwoSided())
						{
							NdotD = -NdotD;
							SurfaceNormal = -SurfaceNormal;
						}

						const bool bPassProjectionTest = NdotD >= NormalWeightTreshold;
						if (bPassProjectionTest && HitCoordZ > LastHitCoordZ + 1 && HitCoordZ < ClusterBasis.VolumeSize.Z)
						{
							FSurfelSample& SurfelSample = SurfelSamples.AddDefaulted_GetRef();
							SurfelSample.Position = RayOrigin + RayDirection * EmbreeRay.ray.tfar;
							SurfelSample.Normal = (FVector3f)SurfaceNormal;
							SurfelSample.CellZ = HitCoordZ;
							SurfelSample.MinRayZ = 0;

							if (LastHitCoordZ >= 0)
							{
								SurfelSample.MinRayZ = FMath::Max(SurfelSample.MinRayZ, LastHitCoordZ + 1);
							}
						}

						LastHitCoordZ = HitCoordZ + 1;
					}
					else
					{
						LastHitCoordZ = INT32_MAX;
					}
				}
			}

			// Sort surfel candidates and compact arrays
			{
				struct FSortByZ
				{
					FORCEINLINE bool operator()(const FSurfelSample& A, const FSurfelSample& B) const
					{
						if (A.CellZ != B.CellZ)
						{
							return A.CellZ < B.CellZ;
						}

						return A.MinRayZ > B.MinRayZ;
					}
				};

				SurfelSamples.Sort(FSortByZ());

				for (int32 SampleIndex = 0; SampleIndex < SurfelSamples.Num(); ++SampleIndex)
				{
					const FSurfelSample& SurfelSample = SurfelSamples[SampleIndex];
					++NumSurfelSamplesPerCell[SurfelSample.CellZ];
				}

				for (int32 CoordZ = 1; CoordZ < ClusterBasis.VolumeSize.Z; ++CoordZ)
				{
					SurfelSamplesOffsetPerCell[CoordZ] = SurfelSamplesOffsetPerCell[CoordZ - 1] + NumSurfelSamplesPerCell[CoordZ - 1];
				}
			}

			// Convert surfel candidates into actual surfels
			for (int32 CoordZ = 0; CoordZ < ClusterBasis.VolumeSize.Z; ++CoordZ)
			{
				const uint32 CellNumSurfelSamples = NumSurfelSamplesPerCell[CoordZ];
				const uint32 CellSurfelSamplesOffset = SurfelSamplesOffsetPerCell[CoordZ];

				if (CellNumSurfelSamples >= MinSurfelSamples)
				{
					FSurfelVisibility SurfelVisibility = ComputeSurfelVisibility(
						Context,
						SurfelSamples,
						CellSurfelSamplesOffset,
						CellNumSurfelSamples,
						RayDirectionsOverHemisphere,
						SurfelScenePerDirection.DebugData);

					if (SurfelVisibility.bValid)
					{
						const int32 MedianMinRayZ = SurfelSamples[CellSurfelSamplesOffset + CellNumSurfelSamples / 2].MinRayZ;
						const float Coverage = CellNumSurfelSamples / float(NumSurfelSamples);
						
						FSurfel& Surfel = SurfelScenePerDirection.Surfels.AddDefaulted_GetRef();
						Surfel.Coord = FIntVector(CoordX, CoordY, CoordZ);
						Surfel.MinRayZ = MedianMinRayZ;
						Surfel.Coverage = Coverage;
						Surfel.WeightedCoverage = Coverage * ((SurfelVisibility.Visibility + 0.5f) / 1.5f);
						check(Surfel.Coord.Z > Surfel.MinRayZ || Surfel.MinRayZ == 0);
					}

					if (ClusteringParams.bDebug)
					{
						FLumenCardBuildDebugData::FSurfel& DebugSurfel = SurfelScenePerDirection.DebugData.Surfels.AddDefaulted_GetRef();
						DebugSurfel.Position = ClusterBasis.TransformSurfel(FIntVector(CoordX, CoordY, CoordZ));
						DebugSurfel.Normal = -RayDirection;
						DebugSurfel.SourceSurfelIndex = SurfelScenePerDirection.Surfels.Num() - 1;
						DebugSurfel.Type = SurfelVisibility.bValid ? FLumenCardBuildDebugData::ESurfelType::Valid : FLumenCardBuildDebugData::ESurfelType::Invalid;
					}
				}
			}
		}
	}
}

void InitClusteringParams(FClusteringParams& ClusteringParams, const FBox& MeshCardsBounds, int32 MaxVoxels, int32 MaxLumenMeshCards, int32 LODLevel)
{
	const float TargetVoxelSize = 20.0f;

	const FVector3f MeshCardsBoundsSize = 2.0f * (FVector3f)MeshCardsBounds.GetExtent();
	const float MaxMeshCardsBounds = MeshCardsBoundsSize.GetMax();

	// Target object space detail size
	const float MaxSizeInVoxels = FMath::Clamp(MaxMeshCardsBounds / TargetVoxelSize + 0.5f, 1, MaxVoxels);
	const float VoxelSize = FMath::Max(TargetVoxelSize, MaxMeshCardsBounds / MaxSizeInVoxels);

	FIntVector SizeInVoxels;
	SizeInVoxels.X = FMath::Clamp(FMath::CeilToFloat(MeshCardsBoundsSize.X / VoxelSize), 1, MaxVoxels);
	SizeInVoxels.Y = FMath::Clamp(FMath::CeilToFloat(MeshCardsBoundsSize.Y / VoxelSize), 1, MaxVoxels);
	SizeInVoxels.Z = FMath::Clamp(FMath::CeilToFloat(MeshCardsBoundsSize.Z / VoxelSize), 1, MaxVoxels);

	const FVector3f VoxelBoundsCenter = (FVector3f)MeshCardsBounds.GetCenter();
	const FVector3f VoxelBoundsExtent = FVector3f(SizeInVoxels) * VoxelSize * 0.5f;
	const FVector3f VoxelBoundsMin = VoxelBoundsCenter - VoxelBoundsExtent;
	const FVector3f VoxelBoundsMax = VoxelBoundsCenter + VoxelBoundsExtent;

	for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < MeshCardGen::NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
	{
		FAxisAlignedDirectionBasis& ClusterBasis = ClusteringParams.ClusterBasis[AxisAlignedDirectionIndex];
		ClusterBasis.VoxelSize = VoxelSize;

		FVector3f XAxis = FVector3f(1.0f, 0.0f, 0.0f);
		FVector3f YAxis = FVector3f(0.0f, 1.0f, 0.0f);
		switch (AxisAlignedDirectionIndex / 2)
		{
		case 0:
			XAxis = FVector3f(0.0f, 1.0f, 0.0f);
			YAxis = FVector3f(0.0f, 0.0f, 1.0f);
			break;

		case 1:
			XAxis = FVector3f(1.0f, 0.0f, 0.0f);
			YAxis = FVector3f(0.0f, 0.0f, 1.0f);
			break;

		case 2:
			XAxis = FVector3f(1.0f, 0.0f, 0.0f);
			YAxis = FVector3f(0.0f, 1.0f, 0.0f);
			break;
		}

		FVector3f ZAxis = AxisAlignedDirectionIndexToNormal(AxisAlignedDirectionIndex);

		ClusterBasis.LocalToWorldRotation = FMatrix44f(XAxis, YAxis, -ZAxis, FVector3f::ZeroVector);

		ClusterBasis.LocalToWorldOffset = VoxelBoundsMin;
		if (AxisAlignedDirectionIndex & 1)
		{
			ClusterBasis.LocalToWorldOffset[AxisAlignedDirectionIndex / 2] = VoxelBoundsMax[AxisAlignedDirectionIndex / 2];
		}

		switch (AxisAlignedDirectionIndex / 2)
		{
		case 0:
			ClusterBasis.VolumeSize.X = SizeInVoxels.Y;
			ClusterBasis.VolumeSize.Y = SizeInVoxels.Z;
			ClusterBasis.VolumeSize.Z = SizeInVoxels.X;
			break;

		case 1:
			ClusterBasis.VolumeSize.X = SizeInVoxels.X;
			ClusterBasis.VolumeSize.Y = SizeInVoxels.Z;
			ClusterBasis.VolumeSize.Z = SizeInVoxels.Y;
			break;

		case 2:
			ClusterBasis.VolumeSize.X = SizeInVoxels.X;
			ClusterBasis.VolumeSize.Y = SizeInVoxels.Y;
			ClusterBasis.VolumeSize.Z = SizeInVoxels.Z;
			break;
		}
	}

	ClusteringParams.VoxelSize = VoxelSize;
	ClusteringParams.MaxSurfelDistanceXY = MeshCardRepresentation::GetMaxSurfelDistanceXY();
	ClusteringParams.MinClusterCoverage = LODLevel == 0 ? 0.5f : 10.0f;
	ClusteringParams.MinDensityPerCluster = MeshCardRepresentation::GetMinDensity() * (LODLevel == 0 ? 0.1f : 1.0f);
	ClusteringParams.MaxLumenMeshCards = MaxLumenMeshCards;
	ClusteringParams.bDebug = MeshCardRepresentation::IsDebugMode();
	ClusteringParams.bSingleThreadedBuild = CVarCardRepresentationParallelBuild.GetValueOnAnyThread() == 0;
}

void InitSurfelScene(
	const FGenerateCardMeshContext& Context,
	const FBox& MeshCardsBounds,
	int32 MaxLumenMeshCards,
	FSurfelScene& SurfelScene,
	FClusteringParams& ClusteringParamsLOD0,
	FClusteringParams& ClusteringParamsLOD1)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(GenerateSurfels);

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

	const int32 DebugSurfelDirection = MeshCardRepresentation::GetDebugSurfelDirection();

	// Limit max number of surfels to prevent generation time from exploding, as dense two sided meshes can generate many more surfels than simple walls
	int32 TargetNumSufels = 10000;
	float MaxVoxels = 64;

	do
	{
		InitClusteringParams(ClusteringParamsLOD0, MeshCardsBounds, MaxVoxels, MaxLumenMeshCards, 0);
		InitClusteringParams(ClusteringParamsLOD1, MeshCardsBounds, MaxVoxels, MaxLumenMeshCards, 1);

		ParallelFor(TEXT("InitSurfelScene.PF"), MeshCardGen::NumAxisAlignedDirections, 1,
			[&](int32 AxisAlignedDirectionIndex)
			{
				if (DebugSurfelDirection < 0 || DebugSurfelDirection == AxisAlignedDirectionIndex)
				{
					FSurfelScenePerDirection& SurfelScenePerDirection = SurfelScene.Directions[AxisAlignedDirectionIndex];
					SurfelScenePerDirection.Init();

					GenerateSurfelsForDirection(
						Context,
						ClusteringParamsLOD1.ClusterBasis[AxisAlignedDirectionIndex],
						RayDirectionsOverHemisphere,
						ClusteringParamsLOD1,
						SurfelScenePerDirection
					);
				}
			}, ClusteringParamsLOD1.bSingleThreadedBuild ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

		SurfelScene.NumSurfels = 0;
		for (const FSurfelScenePerDirection& SurfelScenePerDirection : SurfelScene.Directions)
		{
			SurfelScene.NumSurfels += SurfelScenePerDirection.Surfels.Num();
		}

		MaxVoxels = MaxVoxels / 2;
	} while (SurfelScene.NumSurfels > TargetNumSufels && MaxVoxels > 1);

	if (ClusteringParamsLOD1.bDebug)
	{
		for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < MeshCardGen::NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
		{
			FLumenCardBuildDebugData& MergedDebugData = Context.OutData.MeshCardsBuildData.DebugData;
			FLumenCardBuildDebugData& DirectionDebugData = SurfelScene.Directions[AxisAlignedDirectionIndex].DebugData;

			const int32 SurfelOffset = MergedDebugData.Surfels.Num();

			MergedDebugData.Surfels.Append(DirectionDebugData.Surfels);
			MergedDebugData.SurfelRays.Append(DirectionDebugData.SurfelRays);

			for (FSurfelIndex SurfelIndex = SurfelOffset; SurfelIndex < MergedDebugData.Surfels.Num(); ++SurfelIndex)
			{
				MergedDebugData.Surfels[SurfelIndex].SourceSurfelIndex += SurfelOffset;
			}
		}
	}
}

void GrowSingleCluster(
	const FClusteringParams& ClusteringParams,
	const FSurfelScenePerDirection& SurfelScene,
	TBitArray<>& SurfelAssignedToAnyCluster,
	FSurfelCluster& Cluster)
{
	do
	{
		Cluster.UpdateBestSurfel(ClusteringParams, SurfelScene, SurfelAssignedToAnyCluster);

		if (Cluster.BestSurfelIndex != INVALID_SURFEL_INDEX)
		{
			Cluster.AddSurfel(ClusteringParams, SurfelScene, Cluster.BestSurfelIndex);
			SurfelAssignedToAnyCluster[Cluster.BestSurfelIndex] = true;
		}

	} while (Cluster.BestSurfelIndex != INVALID_SURFEL_INDEX);
}

FSurfelIndex FindBestSeed(const FClusteringParams& ClusteringParams, const FSurfelScenePerDirection& SurfelScene, const FSurfelCluster& Cluster)
{
	FIntVector CoordSum = FIntVector(0, 0, 0);
	for (FSurfelIndex SurfelIndex : Cluster.SurfelIndices)
	{
		const FSurfel& Surfel = SurfelScene.Surfels[SurfelIndex];
		CoordSum += Surfel.Coord;
	}

	const FVector3f AverageCoord = FVector3f(CoordSum) / Cluster.SurfelIndices.Num();

	FSurfelIndex BestSurfelIndex = INVALID_SURFEL_INDEX;
	float BestSurfelDistance = FLT_MAX;
	for (FSurfelIndex SurfelIndex : Cluster.SurfelIndices)
	{
		const FSurfel& Surfel = SurfelScene.Surfels[SurfelIndex];

		FVector3f AxisDistances = (FVector3f(Surfel.Coord) - AverageCoord).GetAbs();
		float SurfelDistance = Surfel.GetDistanceWeight(AxisDistances);
		SurfelDistance += Surfel.GetMinRayZWeight();

		if (SurfelDistance < BestSurfelDistance)
		{
			BestSurfelIndex = SurfelIndex;
			BestSurfelDistance = SurfelDistance;
		}
	}

	return BestSurfelIndex;
}

void GrowAllClusters(
	const FClusteringParams& ClusteringParams,
	int32 AxisAlignedDirectionIndex,
	const FSurfelScenePerDirection& SurfelScene,
	TBitArray<>& SurfelAssignedToAnyCluster,
	TArray<FSurfelCluster>& Clusters)
{
	// Reset all clusters and find their new best seeds
	SurfelAssignedToAnyCluster.Init(false, SurfelScene.Surfels.Num());
	for (FSurfelCluster& Cluster : Clusters)
	{
		const FSurfelIndex ClusterSeedIndex = FindBestSeed(ClusteringParams, SurfelScene, Cluster);

		Cluster.Init();
		Cluster.AddSurfel(ClusteringParams, SurfelScene, ClusterSeedIndex);
		SurfelAssignedToAnyCluster[ClusterSeedIndex] = true;
	}

	for (FSurfelCluster& Cluster : Clusters)
	{
		Cluster.UpdateBestSurfel(ClusteringParams, SurfelScene, SurfelAssignedToAnyCluster);
	}

	// Cluster surfels
	int32 BestClusterToAddIndex = -1;
	float BestSurfelToAddDistance = FLT_MAX;

	do 
	{
		BestClusterToAddIndex = -1;
		BestSurfelToAddDistance = FLT_MAX;

		// Pick best surfel across all clusters
		for (int32 ClusterIndex = 0; ClusterIndex < Clusters.Num(); ++ClusterIndex)
		{
			FSurfelCluster& Cluster = Clusters[ClusterIndex];

			if (Cluster.BestSurfelIndex != INVALID_SURFEL_INDEX && SurfelAssignedToAnyCluster[Cluster.BestSurfelIndex])
			{
				Cluster.UpdateBestSurfel(ClusteringParams, SurfelScene, SurfelAssignedToAnyCluster);
			}

			if (Cluster.BestSurfelIndex != INVALID_SURFEL_INDEX)
			{
				if (Cluster.BestSurfelDistance < BestSurfelToAddDistance)
				{
					BestClusterToAddIndex = ClusterIndex;
					BestSurfelToAddDistance = Cluster.BestSurfelDistance;
				}
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

struct FMeshCardsLODLevelPerDirection
{
	TArray<FSurfelCluster> Clusters;
	bool bNeedsToReGrow = false;
};

struct FMeshCardsLODLevel
{
	FMeshCardsLODLevelPerDirection Directions[MeshCardGen::NumAxisAlignedDirections];
	float WeightedSurfaceCoverage = 0.0f;
	float SurfaceArea = 0.0f;
	int32 NumSurfels = 0;
	int32 NumClusters = 0;

	bool NeedToReGrow() const
	{
		for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < MeshCardGen::NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
		{
			if (Directions[AxisAlignedDirectionIndex].bNeedsToReGrow)
			{
				return true;
			}
		}

		return false;
	}
};

void UpdateLODLevelCoverage(const FSurfelScene& SurfelScene, const FClusteringParams& ClusteringParams, FMeshCardsLODLevel& LODLevel)
{
	LODLevel.WeightedSurfaceCoverage = 0.0f;
	LODLevel.SurfaceArea = 0.0f;
	LODLevel.NumSurfels = 0;
	LODLevel.NumClusters = 0;

	for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < MeshCardGen::NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
	{
		TArray<FSurfelCluster>& Clusters = LODLevel.Directions[AxisAlignedDirectionIndex].Clusters;
		const TArray<FSurfel>& Surfels = SurfelScene.Directions[AxisAlignedDirectionIndex].Surfels;

		for (FSurfelCluster& Cluster : Clusters)
		{
			LODLevel.WeightedSurfaceCoverage += Cluster.WeightedCoverage;
			LODLevel.SurfaceArea += Cluster.Bounds.GetFaceArea();
		}

		LODLevel.NumSurfels += Surfels.Num();
		LODLevel.NumClusters += Clusters.Num();
	}
}

/** 
 * Cluster only by direction, trying to represent mesh by up to 6 axis aligned projections (like a cubemap)
 */
void BuildMeshCardsLOD0(const FBox& MeshBounds, const FGenerateCardMeshContext& Context, const FSurfelScene& SurfelScene, const FClusteringParams& ClusteringParams, FMeshCardsLODLevel& LODLevel)
{
	for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < MeshCardGen::NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
	{
		const TArray<FSurfel>& Surfels = SurfelScene.Directions[AxisAlignedDirectionIndex].Surfels;

		FSurfelCluster TempCluster;
		TempCluster.Init();

		for (FSurfelIndex SurfelIndex = 0; SurfelIndex < Surfels.Num(); ++SurfelIndex)
		{
			const FSurfel& Surfel = Surfels[SurfelIndex];
			if (Surfel.MinRayZ == 0)
			{
				TempCluster.SurfelIndices.Add(SurfelIndex);
				TempCluster.Bounds.Add(Surfel.Coord);
				TempCluster.Coverage += Surfel.Coverage;
				TempCluster.WeightedCoverage += Surfel.WeightedCoverage;
			}
		}

		if (TempCluster.IsValid(ClusteringParams))
		{
			LODLevel.Directions[AxisAlignedDirectionIndex].Clusters.Add(TempCluster);
		}
	}
	
	UpdateLODLevelCoverage(SurfelScene, ClusteringParams, LODLevel);
}

// Process cluster seeds one by one and try to grow clusters from each one of them
void GenerateClustersFromSeeds(
	const FClusteringParams& ClusteringParams,
	int32 AxisAlignedDirectionIndex,
	const FSurfelScenePerDirection& SurfelScene,
	TBitArray<>& SurfelAssignedToAnyCluster,
	TArray<FSurfelIndex>& ClusterSeeds,
	TArray<FSurfelCluster>& Clusters)
{
	const int32 NumSeedIterations = MeshCardRepresentation::GetSeedIterations();

	FRandomStream ClusterSeedRandomStream(0);
	FSurfelCluster TempCluster;

	while (ClusterSeeds.Num() > 0)
	{
		// Select next seed
		FSurfelIndex NextSeedSurfelIndex = INVALID_SURFEL_INDEX;
		while (ClusterSeeds.Num() > 0)
		{
			int32 RandomIndex = ClusterSeedRandomStream.RandHelper(ClusterSeeds.Num());
			const FSurfelIndex ClusterSeed = ClusterSeeds[RandomIndex];
			ClusterSeeds.RemoveAtSwap(RandomIndex);

			if (!SurfelAssignedToAnyCluster[ClusterSeed])
			{
				NextSeedSurfelIndex = ClusterSeed;
				break;
			}
		}

		// Try to build a cluster using selected seed
		if (NextSeedSurfelIndex != INVALID_SURFEL_INDEX)
		{
			for (int32 ClusteringIterationIndex = 0; ClusteringIterationIndex < NumSeedIterations; ++ClusteringIterationIndex)
			{
				TempCluster.Init();
				TempCluster.AddSurfel(ClusteringParams, SurfelScene, NextSeedSurfelIndex);
				SurfelAssignedToAnyCluster[NextSeedSurfelIndex] = true;

				GrowSingleCluster(
					ClusteringParams,
					SurfelScene,
					SurfelAssignedToAnyCluster,
					TempCluster);

				// Restore global state
				for (FSurfelIndex SurfelIndex : TempCluster.SurfelIndices)
				{
					SurfelAssignedToAnyCluster[SurfelIndex] = false;
				}

				if (TempCluster.IsValid(ClusteringParams))
				{
					const FSurfelIndex AverageSurfelIndex = FindBestSeed(ClusteringParams, SurfelScene, TempCluster);
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
				Clusters.Add(TempCluster);

				for (FSurfelIndex SurfelIndex : TempCluster.SurfelIndices)
				{
					SurfelAssignedToAnyCluster[SurfelIndex] = true;
				}
			}
		}
	}
}

// Limit number of clusters to fit in user provided limit
void LimitClusters(const FClusteringParams& ClusteringParams, const FSurfelScene& SurfelScene, FMeshCardsLODLevel& LODLevel)
{
	int32 NumClusters = 0;

	for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < MeshCardGen::NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
	{
		TArray<FSurfelCluster>& Clusters = LODLevel.Directions[AxisAlignedDirectionIndex].Clusters;
		const TArray<FSurfel>& Surfels = SurfelScene.Directions[AxisAlignedDirectionIndex].Surfels;

		struct FSortByClusterWeightedCoverage
		{
			FORCEINLINE bool operator()(const FSurfelCluster& A, const FSurfelCluster& B) const
			{
				return A.WeightedCoverage > B.WeightedCoverage;
			}
		};

		Clusters.Sort(FSortByClusterWeightedCoverage());
		NumClusters += Clusters.Num();
	}

	while (NumClusters > ClusteringParams.MaxLumenMeshCards)
	{
		float SmallestClusterWeightedCoverage = FLT_MAX;
		int32 SmallestClusterDirectionIndex = 0;

		for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < MeshCardGen::NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
		{
			const TArray<FSurfelCluster>& Clusters = LODLevel.Directions[AxisAlignedDirectionIndex].Clusters;
			if (Clusters.Num() > 0)
			{
				const FSurfelCluster& Cluster = Clusters.Last();
				if (Cluster.WeightedCoverage < SmallestClusterWeightedCoverage)
				{
					SmallestClusterDirectionIndex = AxisAlignedDirectionIndex;
					SmallestClusterWeightedCoverage = Cluster.WeightedCoverage;
				}
			}
		}

		FMeshCardsLODLevelPerDirection& MeshCardsLODLevelPerDirection = LODLevel.Directions[SmallestClusterDirectionIndex];
		MeshCardsLODLevelPerDirection.Clusters.Pop();
		MeshCardsLODLevelPerDirection.bNeedsToReGrow = true;
		--NumClusters;
	}
}

// Cluster by direction and position
void BuildMeshCardsLOD1(const FBox& MeshBounds, const FGenerateCardMeshContext& Context, const FSurfelScene& SurfelScene, const FClusteringParams& ClusteringParams, FMeshCardsLODLevel& LODLevel)
{
	const int32 NumGrowIterations = MeshCardRepresentation::GetGrowIterations();

	TBitArray<> SurfelAssignedToAnyClusterArray[MeshCardGen::NumAxisAlignedDirections];
	for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < MeshCardGen::NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
	{
		SurfelAssignedToAnyClusterArray[AxisAlignedDirectionIndex].Init(false, SurfelScene.Directions[AxisAlignedDirectionIndex].Surfels.Num());
	}

	// Generate initial list of clusters
	ParallelFor(TEXT("BuildMeshCardsLOD1.PF"), MeshCardGen::NumAxisAlignedDirections, 1,
		[&](int32 AxisAlignedDirectionIndex)
		{
			const FSurfelScenePerDirection& SurfelScenePerDirection = SurfelScene.Directions[AxisAlignedDirectionIndex];
			TArray<FSurfelCluster>& Clusters = LODLevel.Directions[AxisAlignedDirectionIndex].Clusters;
			TBitArray<>& SurfelAssignedToAnyCluster = SurfelAssignedToAnyClusterArray[AxisAlignedDirectionIndex];

			TArray<FSurfelIndex> ClusterSeeds;
			ClusterSeeds.SetNumUninitialized(SurfelScenePerDirection.Surfels.Num());
			for (FSurfelIndex SurfelIndex = 0; SurfelIndex < SurfelScenePerDirection.Surfels.Num(); ++SurfelIndex)
			{
				ClusterSeeds[SurfelIndex] = SurfelIndex;
			}

			GenerateClustersFromSeeds(
				ClusteringParams,
				AxisAlignedDirectionIndex,
				SurfelScenePerDirection,
				SurfelAssignedToAnyCluster,
				ClusterSeeds,
				Clusters
			);

			// Grow all clusters simultaneously from the best seed
			for (int32 ClusteringIterationIndex = 0; ClusteringIterationIndex < NumGrowIterations; ++ClusteringIterationIndex)
			{
				GrowAllClusters(
					ClusteringParams,
					AxisAlignedDirectionIndex,
					SurfelScenePerDirection,
					SurfelAssignedToAnyCluster,
					Clusters
				);

				bool bAnyClusterSeedChanged = false;
				for (FSurfelCluster& Cluster : Clusters)
				{
					const FSurfelIndex ClusterSeedIndex = FindBestSeed(ClusteringParams, SurfelScenePerDirection, Cluster);
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
		}, ClusteringParams.bSingleThreadedBuild ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);

	LimitClusters(ClusteringParams, SurfelScene, LODLevel);

	if (LODLevel.NeedToReGrow())
	{
		ParallelFor(TEXT("BuildMeshCardsLOD1.PF"), MeshCardGen::NumAxisAlignedDirections, 1,
			[&](int32 AxisAlignedDirectionIndex)
			{
				if (LODLevel.Directions[AxisAlignedDirectionIndex].bNeedsToReGrow)
				{
					const FSurfelScenePerDirection& SurfelScenePerDirection = SurfelScene.Directions[AxisAlignedDirectionIndex];
					TArray<FSurfelCluster>& Clusters = LODLevel.Directions[AxisAlignedDirectionIndex].Clusters;
					TBitArray<>& SurfelAssignedToAnyCluster = SurfelAssignedToAnyClusterArray[AxisAlignedDirectionIndex];

					// Grow all clusters simultaneously from the best seed
					for (int32 ClusteringIterationIndex = 0; ClusteringIterationIndex < NumGrowIterations; ++ClusteringIterationIndex)
					{
						GrowAllClusters(
							ClusteringParams,
							AxisAlignedDirectionIndex,
							SurfelScenePerDirection,
							SurfelAssignedToAnyCluster,
							Clusters
						);

						bool bAnyClusterSeedChanged = false;
						for (FSurfelCluster& Cluster : Clusters)
						{
							const FSurfelIndex ClusterSeedIndex = FindBestSeed(ClusteringParams, SurfelScenePerDirection, Cluster);
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
				}
			}, ClusteringParams.bSingleThreadedBuild ? EParallelForFlags::ForceSingleThread : EParallelForFlags::None);
	}

	UpdateLODLevelCoverage(SurfelScene, ClusteringParams, LODLevel);
}

void SerializeLOD(
	const FGenerateCardMeshContext& Context,
	const FClusteringParams& ClusteringParams,
	const FSurfelScene& SurfelScene,
	FMeshCardsLODLevel const& LODLevel,
	int32 LODLevelIndex,
	const FBox& MeshCardsBounds,
	FMeshCardsBuildData& MeshCardsBuildData)
{
	MeshCardsBuildData.MaxLODLevel = FMath::Max(MeshCardsBuildData.MaxLODLevel, LODLevelIndex);

	int32 SourceSurfelOffset = 0;

	for (int32 AxisAlignedDirectionIndex = 0; AxisAlignedDirectionIndex < MeshCardGen::NumAxisAlignedDirections; ++AxisAlignedDirectionIndex)
	{
		const FAxisAlignedDirectionBasis& ClusterBasis = ClusteringParams.ClusterBasis[AxisAlignedDirectionIndex];
		const FSurfelScenePerDirection& SurfelScenePerDirection = SurfelScene.Directions[AxisAlignedDirectionIndex];
		const TArray<FSurfel>& Surfels = SurfelScenePerDirection.Surfels;
		const TArray<FSurfelCluster>& Clusters = LODLevel.Directions[AxisAlignedDirectionIndex].Clusters;

		TBitArray<> DebugSurfelInCluster;
		TBitArray<> DebugSurfelInAnyCluster(false, Surfels.Num());

		const FBox3f LocalMeshCardsBounds = FBox3f(MeshCardsBounds.ShiftBy((FVector)-ClusterBasis.LocalToWorldOffset).TransformBy(FMatrix(ClusterBasis.LocalToWorldRotation.GetTransposed())));

		for (const FSurfelCluster& Cluster : Clusters)
		{
			// Clamp to mesh bounds
			FVector3f ClusterBoundsMin = (FVector3f(Cluster.Bounds.Min) - FVector3f(0.0f, 0.0f, 0.5f)) * ClusteringParams.VoxelSize;
			FVector3f ClusterBoundsMax = (FVector3f(Cluster.Bounds.Max) + FVector3f(1.0f, 1.0f, 1.0f)) * ClusteringParams.VoxelSize;
			ClusterBoundsMin = FVector3f::Max(ClusterBoundsMin, LocalMeshCardsBounds.Min);
			ClusterBoundsMax = FVector3f::Min(ClusterBoundsMax, LocalMeshCardsBounds.Max);

			const FVector3f ClusterBoundsOrigin = (ClusterBoundsMax + ClusterBoundsMin) * 0.5f;
			const FVector3f ClusterBoundsExtent = (ClusterBoundsMax - ClusterBoundsMin) * 0.5f;
			const FVector3f MeshClusterBoundsOrigin = ClusterBasis.LocalToWorldRotation.TransformPosition(ClusterBoundsOrigin) + ClusterBasis.LocalToWorldOffset;

			FLumenCardBuildData BuiltData;
			BuiltData.OBB.Origin = MeshClusterBoundsOrigin;
			BuiltData.OBB.Extent = ClusterBoundsExtent;
			BuiltData.OBB.AxisX = ClusterBasis.LocalToWorldRotation.GetScaledAxis(EAxis::X);
			BuiltData.OBB.AxisY = ClusterBasis.LocalToWorldRotation.GetScaledAxis(EAxis::Y);
			BuiltData.OBB.AxisZ = -ClusterBasis.LocalToWorldRotation.GetScaledAxis(EAxis::Z);
			BuiltData.LODLevel = LODLevelIndex;
			BuiltData.AxisAlignedDirectionIndex = AxisAlignedDirectionIndex;
			MeshCardsBuildData.CardBuildData.Add(BuiltData);

			if (ClusteringParams.bDebug)
			{
				DebugSurfelInCluster.Reset();
				DebugSurfelInCluster.Add(false, Surfels.Num());

				FLumenCardBuildDebugData::FSurfelCluster& DebugCluster = MeshCardsBuildData.DebugData.Clusters.AddDefaulted_GetRef();
				DebugCluster.Surfels.Reserve(DebugCluster.Surfels.Num() + Surfels.Num());

				// Cluster seed
				{
					FLumenCardBuildDebugData::FSurfel DebugSurfel;
					DebugSurfel.Position = ClusterBasis.TransformSurfel(Surfels[Cluster.SurfelIndices[0]].Coord);
					DebugSurfel.Normal = AxisAlignedDirectionIndexToNormal(AxisAlignedDirectionIndex);
					DebugSurfel.SourceSurfelIndex = SourceSurfelOffset + Cluster.SurfelIndices[0];
					DebugSurfel.Type = FLumenCardBuildDebugData::ESurfelType::Seed;
					DebugCluster.Surfels.Add(DebugSurfel);
				}

				{
					const FSurfelIndex AverageSurfelIndex = FindBestSeed(ClusteringParams, SurfelScenePerDirection, Cluster);

					FLumenCardBuildDebugData::FSurfel DebugSurfel;
					DebugSurfel.Position = ClusterBasis.TransformSurfel(Surfels[AverageSurfelIndex].Coord);
					DebugSurfel.Normal = AxisAlignedDirectionIndexToNormal(AxisAlignedDirectionIndex);
					DebugSurfel.SourceSurfelIndex = SourceSurfelOffset + AverageSurfelIndex;
					DebugSurfel.Type = FLumenCardBuildDebugData::ESurfelType::Seed2;
					DebugCluster.Surfels.Add(DebugSurfel);
				}

				for (FSurfelIndex SurfelIndex : Cluster.SurfelIndices)
				{
					FLumenCardBuildDebugData::FSurfel DebugSurfel;
					DebugSurfel.Position = ClusterBasis.TransformSurfel(Surfels[SurfelIndex].Coord);
					DebugSurfel.Normal = AxisAlignedDirectionIndexToNormal(AxisAlignedDirectionIndex);
					DebugSurfel.SourceSurfelIndex = SourceSurfelOffset + SurfelIndex;
					DebugSurfel.Type = FLumenCardBuildDebugData::ESurfelType::Cluster;
					DebugCluster.Surfels.Add(DebugSurfel);

					const int32 SurfelMinRayZ = Surfels[SurfelIndex].MinRayZ;
					if (SurfelMinRayZ > 0)
					{
						FIntVector MinRayZCoord = Surfels[SurfelIndex].Coord;
						MinRayZCoord.Z = SurfelMinRayZ;

						FLumenCardBuildDebugData::FRay DebugRay;
						DebugRay.RayStart = DebugSurfel.Position;
						DebugRay.RayEnd = ClusterBasis.TransformSurfel(MinRayZCoord);
						DebugRay.bHit = false;
						DebugCluster.Rays.Add(DebugRay);
					}

					DebugSurfelInAnyCluster[SurfelIndex] = true;
					DebugSurfelInCluster[SurfelIndex] = true;
				}

				for (FSurfelIndex SurfelIndex = 0; SurfelIndex < Surfels.Num(); ++SurfelIndex)
				{
					if (!DebugSurfelInCluster[SurfelIndex])
					{
						FLumenCardBuildDebugData::FSurfel DebugSurfel;
						DebugSurfel.Position = ClusterBasis.TransformSurfel(Surfels[SurfelIndex].Coord);
						DebugSurfel.Normal = AxisAlignedDirectionIndexToNormal(AxisAlignedDirectionIndex);
						DebugSurfel.SourceSurfelIndex = SourceSurfelOffset + SurfelIndex;
						DebugSurfel.Type = DebugSurfelInAnyCluster[SurfelIndex] ? FLumenCardBuildDebugData::ESurfelType::Used : FLumenCardBuildDebugData::ESurfelType::Idle;
						DebugCluster.Surfels.Add(DebugSurfel);
					}
				}
			}
		}

		SourceSurfelOffset += Surfels.Num();
	}

	if (ClusteringParams.bDebug)
	{
		UE_LOG(LogMeshUtilities, Log, TEXT("CardGen Mesh:%s LOD:%d Surfels:%d Clusters:%d WeightedSurfaceCoverage:%f ClusterArea:%f"),
			*Context.MeshName,
			LODLevelIndex,
			LODLevel.NumSurfels,
			LODLevel.NumClusters,
			LODLevel.WeightedSurfaceCoverage,
			LODLevel.SurfaceArea);
	}
}

void BuildMeshCards(const FBox& MeshBounds, const FGenerateCardMeshContext& Context, int32 MaxLumenMeshCards, FCardRepresentationData& OutData)
{
	// Make sure BBox isn't empty and we can generate card representation for it. This handles e.g. infinitely thin planes.
	const FVector MeshCardsBoundsCenter = MeshBounds.GetCenter();
	const FVector MeshCardsBoundsExtent = FVector::Max(MeshBounds.GetExtent() + 1.0f, FVector(5.0f));
	const FBox MeshCardsBounds(MeshCardsBoundsCenter - MeshCardsBoundsExtent, MeshCardsBoundsCenter + MeshCardsBoundsExtent);

	// Prepare a list of surfels for cluster fitting
	FSurfelScene SurfelScene;
	FClusteringParams ClusteringParamsLOD0;
	FClusteringParams ClusteringParamsLOD1;
	InitSurfelScene(Context, MeshCardsBounds, MaxLumenMeshCards, SurfelScene, ClusteringParamsLOD0, ClusteringParamsLOD1);

	FMeshCardsLODLevel MeshCardsLOD0;
	BuildMeshCardsLOD0(MeshBounds, Context, SurfelScene, ClusteringParamsLOD0, MeshCardsLOD0);

	// Assume that two sided is foliage and revert to a simpler and more reliable box projection
	FMeshCardsLODLevel MeshCardsLOD1;
	if (!Context.EmbreeScene.bMostlyTwoSided)
	{
		BuildMeshCardsLOD1(MeshBounds, Context, SurfelScene, ClusteringParamsLOD1, MeshCardsLOD1);
	}

	OutData.MeshCardsBuildData.Bounds = MeshCardsBounds;
	OutData.MeshCardsBuildData.MaxLODLevel = 0;
	OutData.MeshCardsBuildData.CardBuildData.Reset();

	SerializeLOD(Context, ClusteringParamsLOD0, SurfelScene, MeshCardsLOD0, /*LODLevelIndex*/ 0, MeshCardsBounds, OutData.MeshCardsBuildData);

	// Optionally serialize LOD1 if it's of higher quality without exceeding the budget
	if (MeshCardsLOD1.NumClusters <= MeshCardGen::MaxCardsPerMesh
		&& MeshCardsLOD1.WeightedSurfaceCoverage > MeshCardsLOD0.WeightedSurfaceCoverage)
	{
		SerializeLOD(Context, ClusteringParamsLOD1, SurfelScene, MeshCardsLOD1, /*LODLevelIndex*/ 1, MeshCardsBounds, OutData.MeshCardsBuildData);
	}

	OutData.MeshCardsBuildData.DebugData.NumSurfels = 0;
	for (const FSurfelScenePerDirection& SurfelScenePerDirection : SurfelScene.Directions)
	{
		 OutData.MeshCardsBuildData.DebugData.NumSurfels += SurfelScenePerDirection.Surfels.Num();
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
	int32 MaxLumenMeshCards,
	bool bGenerateAsIfTwoSided,
	FCardRepresentationData& OutData)
{
#if USE_EMBREE
	TRACE_CPUPROFILER_EVENT_SCOPE(FMeshUtilities::GenerateCardRepresentationData);

	if (MaxLumenMeshCards > 0)
	{
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

		// Note: must operate on the SDF bounds when available, because SDF generation can expand the mesh's bounds
		const FBox BuildCardsBounds = DistanceFieldVolumeData && DistanceFieldVolumeData->LocalSpaceMeshBounds.IsValid ? DistanceFieldVolumeData->LocalSpaceMeshBounds : Bounds.GetBox();
		BuildMeshCards(BuildCardsBounds, Context, MaxLumenMeshCards, OutData);

		MeshRepresentation::DeleteEmbreeScene(EmbreeScene);

		const float TimeElapsed = (float)(FPlatformTime::Seconds() - StartTime);
		if (TimeElapsed > 1.0f)
		{
			UE_LOG(LogMeshUtilities, Log, TEXT("Finished mesh card build in %.1fs %s tris:%d surfels:%d"),
				TimeElapsed,
				*MeshName,
				EmbreeScene.NumIndices / 3,
				OutData.MeshCardsBuildData.DebugData.NumSurfels);
		}
	}

	return true;
#else
	UE_LOG(LogMeshUtilities, Warning, TEXT("Platform did not set USE_EMBREE, GenerateCardRepresentationData failed."));
	return false;
#endif
}
