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

class FGenerateCardMeshContext
{
public:
	const FString& MeshName;
	RTCScene FullMeshEmbreeScene;
	RTCDevice EmbreeDevice;
	FCardRepresentationData& OutData;

	FGenerateCardMeshContext(const FString& InMeshName, RTCScene InEmbreeScene, RTCDevice InEmbreeDevice, FCardRepresentationData& InOutData) :
		MeshName(InMeshName),
		FullMeshEmbreeScene(InEmbreeScene),
		EmbreeDevice(InEmbreeDevice),
		OutData(InOutData)
	{}
};

class FPlacedCard
{
public:
	int32 SliceMin;
	int32 SliceMax;

	float NearPlane;
	float FarPlane;
	FBox Bounds;
	int32 NumHits;
};

#if USE_EMBREE

bool IsSurfacePointInsideMesh(const RTCScene& FullMeshEmbreeScene, FVector SurfacePoint, FVector SurfaceNormal, const TArray<FVector4>& RayDirectionsOverHemisphere)
{
	uint32 NumHits = 0;
	uint32 NumBackFaceHits = 0;

	const FMatrix SurfaceBasis = MeshRepresentation::GetTangentBasisFrisvad(SurfaceNormal);

	for (int32 SampleIndex = 0; SampleIndex < RayDirectionsOverHemisphere.Num(); ++SampleIndex)
	{
		FVector RayDirection = SurfaceBasis.TransformVector(RayDirectionsOverHemisphere[SampleIndex]);

		FEmbreeRay EmbreeRay;
		EmbreeRay.ray.org_x = SurfacePoint.X;
		EmbreeRay.ray.org_y = SurfacePoint.Y;
		EmbreeRay.ray.org_z = SurfacePoint.Z;
		EmbreeRay.ray.dir_x = RayDirection.X;
		EmbreeRay.ray.dir_y = RayDirection.Y;
		EmbreeRay.ray.dir_z = RayDirection.Z;
		EmbreeRay.ray.tnear = 0.1f;
		EmbreeRay.ray.tfar = FLT_MAX;

		FEmbreeIntersectionContext EmbreeContext;
		rtcInitIntersectContext(&EmbreeContext);
		rtcIntersect1(FullMeshEmbreeScene, &EmbreeContext, &EmbreeRay);

		if (EmbreeRay.hit.geomID != RTC_INVALID_GEOMETRY_ID && EmbreeRay.hit.primID != RTC_INVALID_GEOMETRY_ID)
		{
			++NumHits;

			if (FVector::DotProduct(RayDirection, EmbreeRay.GetHitNormal()) > 0.0f && !EmbreeContext.IsHitTwoSided())
			{
				++NumBackFaceHits;
			}
		}
	}

	if (NumHits > 0 && NumBackFaceHits > RayDirectionsOverHemisphere.Num() * 0.4f)
	{
		return true;
	}

	return false;
}

struct FSurfacePoint
{
	float MinT;
	float HitT;
};

int32 UpdatePlacedCards(TArray<FPlacedCard, TInlineAllocator<16>>& PlacedCards,
	FVector RayOriginFrame,
	FVector RayDirection,
	FVector HeighfieldStepX,
	FVector HeighfieldStepY,
	FIntPoint HeighfieldSize, 
	int32 MeshSliceNum,
	float MaxRayT,
	int32 MinCardHits,
	FVector VoxelExtent,
	const TArray<TArray<FSurfacePoint, TInlineAllocator<16>>>& HeightfieldLayers)
{
	for (int32 PlacedCardIndex = 0; PlacedCardIndex < PlacedCards.Num(); ++PlacedCardIndex)
	{
		FPlacedCard& PlacedCard = PlacedCards[PlacedCardIndex];
		PlacedCard.NearPlane = PlacedCard.SliceMin / float(MeshSliceNum) * MaxRayT;
		PlacedCard.FarPlane = (PlacedCard.SliceMax / float(MeshSliceNum)) * MaxRayT;
		PlacedCard.Bounds.Init();
		PlacedCard.NumHits = 0;
	}

	for (int32 HeighfieldY = 0; HeighfieldY < HeighfieldSize.Y; ++HeighfieldY)
	{
		for (int32 HeighfieldX = 0; HeighfieldX < HeighfieldSize.X; ++HeighfieldX)
		{
			const int32 HeightfieldLinearIndex = HeighfieldX + HeighfieldY * HeighfieldSize.X;

			FVector RayOrigin = RayOriginFrame;
			RayOrigin += (HeighfieldX + 0.5f) * HeighfieldStepX;
			RayOrigin += (HeighfieldY + 0.5f) * HeighfieldStepY;

			int32 LayerIndex = 0;
			int32 PlacedCardIndex = 0;

			while (LayerIndex < HeightfieldLayers[HeightfieldLinearIndex].Num() && PlacedCardIndex < PlacedCards.Num())
			{
				const FSurfacePoint& SurfacePoint = HeightfieldLayers[HeightfieldLinearIndex][LayerIndex];
				FPlacedCard& PlacedCard = PlacedCards[PlacedCardIndex];

				if (SurfacePoint.HitT >= PlacedCard.NearPlane && SurfacePoint.HitT <= PlacedCard.FarPlane
					&& SurfacePoint.MinT <= PlacedCard.NearPlane)
				{
					PlacedCard.NumHits += 1;
					PlacedCard.Bounds += RayOrigin + SurfacePoint.HitT * RayDirection - VoxelExtent;
					PlacedCard.Bounds += RayOrigin + SurfacePoint.HitT * RayDirection + VoxelExtent;

					++PlacedCardIndex;
					++LayerIndex;
				}
				else
				{
					if (SurfacePoint.HitT >= PlacedCard.FarPlane)
					{
						++PlacedCardIndex;
					}
					else
					{
						++LayerIndex;
					}
				}
			}
		}
	}

	int32 NumMeshHits = 0;
	for (int32 PlacedCardIndex = 0; PlacedCardIndex < PlacedCards.Num(); ++PlacedCardIndex)
	{
		const FPlacedCard& PlacedCard = PlacedCards[PlacedCardIndex];
		if (PlacedCard.NumHits >= MinCardHits)
		{
			NumMeshHits += PlacedCard.NumHits;
		}
	}
	return NumMeshHits;
}

void SerializePlacedCards(TArrayView<const FPlacedCard> PlacedCards,
	int32 LODLevel,
	int32 Orientation,
	int32 MinCardHits,
	const FBox& MeshCardsBounds,
	FCardRepresentationData& OutData)
{
	for (int32 PlacedCardIndex = 0; PlacedCardIndex < PlacedCards.Num(); ++PlacedCardIndex)
	{
		const FPlacedCard& PlacedCard = PlacedCards[PlacedCardIndex];
		if (PlacedCard.NumHits >= MinCardHits)
		{
			const FBox ClampedBox = PlacedCard.Bounds.Overlap(MeshCardsBounds);

			FLumenCardBuildData CardBuildData;
			CardBuildData.Center = ClampedBox.GetCenter();
			CardBuildData.Extent = ClampedBox.GetExtent();
			CardBuildData.Extent = FLumenCardBuildData::TransformFaceExtent(CardBuildData.Extent, Orientation);
			CardBuildData.Orientation = Orientation;
			CardBuildData.LODLevel = LODLevel;

			OutData.MeshCardsBuildData.CardBuildData.Add(CardBuildData);
		}
	}
}

void BuildMeshCards(const FBox& MeshBounds, const FGenerateCardMeshContext& Context, FCardRepresentationData& OutData)
{
	static const auto CVarMeshCardRepresentationMinSurface = IConsoleManager::Get().FindTConsoleVariableDataFloat(TEXT("r.MeshCardRepresentation.MinSurface"));
	const float MinSurfaceThreshold = CVarMeshCardRepresentationMinSurface->GetValueOnAnyThread();

	// Make sure BBox isn't empty and we can generate card representation for it. This handles e.g. infinitely thin planes.
	const FVector MeshCardsBoundsCenter = MeshBounds.GetCenter();
	const FVector MeshCardsBoundsExtent = FVector::Max(MeshBounds.GetExtent() + 1.0f, FVector(5.0f));
	const FBox MeshCardsBounds(MeshCardsBoundsCenter - MeshCardsBoundsExtent, MeshCardsBoundsCenter + MeshCardsBoundsExtent);

	OutData.MeshCardsBuildData.Bounds = MeshCardsBounds;
	OutData.MeshCardsBuildData.MaxLODLevel = 1;
	OutData.MeshCardsBuildData.CardBuildData.Reset();

	const float SamplesPerWorldUnit = 1.0f / 10.0f;
	const int32 MinSamplesPerAxis = 4;
	const int32 MaxSamplesPerAxis = 64;
	FIntVector VolumeSizeInVoxels;
	VolumeSizeInVoxels.X = FMath::Clamp<int32>(MeshCardsBounds.GetSize().X * SamplesPerWorldUnit, MinSamplesPerAxis, MaxSamplesPerAxis);
	VolumeSizeInVoxels.Y = FMath::Clamp<int32>(MeshCardsBounds.GetSize().Y * SamplesPerWorldUnit, MinSamplesPerAxis, MaxSamplesPerAxis);
	VolumeSizeInVoxels.Z = FMath::Clamp<int32>(MeshCardsBounds.GetSize().Z * SamplesPerWorldUnit, MinSamplesPerAxis, MaxSamplesPerAxis);

	const FVector VoxelExtent = MeshCardsBounds.GetSize() / FVector(VolumeSizeInVoxels);

	// Generate random ray directions over a hemisphere
	TArray<FVector4> RayDirectionsOverHemisphere;
	{
		FRandomStream RandomStream(0);
		MeshUtilities::GenerateStratifiedUniformHemisphereSamples(64, RandomStream, RayDirectionsOverHemisphere);
	}

	using FPlacedCardArray = TArray<FPlacedCard, TInlineAllocator<16>>;
	struct FTaskOutputs
	{
		FPlacedCardArray PlacedCardsPerLod[2];
		float MinCardHitsPerLod[2] = {};
	};
	
	FTaskOutputs TaskOutputsPerOrientation[6];

	ParallelFor(6, [VolumeSizeInVoxels, MinSurfaceThreshold, VoxelExtent,
		&Context = AsConst(Context),
		&RayDirectionsOverHemisphere = AsConst(RayDirectionsOverHemisphere),
		&MeshCardsBounds = AsConst(MeshCardsBounds),
		&TaskOutputsPerOrientation
	] (int32 Orientation)
	{
		FIntPoint HeighfieldSize(0, 0);
		FVector RayDirection(0.0f, 0.0f, 0.0f);
		FVector RayOriginFrame = MeshCardsBounds.Min;
		FVector HeighfieldStepX(0.0f, 0.0f, 0.0f);
		FVector HeighfieldStepY(0.0f, 0.0f, 0.0f);
		float MaxRayT = 0.0f;
		int32 MeshSliceNum = 0;

		switch (Orientation / 2)
		{
			case 0:
				MaxRayT = MeshCardsBounds.GetSize().X + 0.1f;
				MeshSliceNum = VolumeSizeInVoxels.X;
				HeighfieldSize.X = VolumeSizeInVoxels.Y;
				HeighfieldSize.Y = VolumeSizeInVoxels.Z;
				HeighfieldStepX = FVector(0.0f, MeshCardsBounds.GetSize().Y / HeighfieldSize.X, 0.0f);
				HeighfieldStepY = FVector(0.0f, 0.0f, MeshCardsBounds.GetSize().Z / HeighfieldSize.Y);
				break;

			case 1:
				MaxRayT = MeshCardsBounds.GetSize().Y + 0.1f;
				MeshSliceNum = VolumeSizeInVoxels.Y;
				HeighfieldSize.X = VolumeSizeInVoxels.X;
				HeighfieldSize.Y = VolumeSizeInVoxels.Z;
				HeighfieldStepX = FVector(MeshCardsBounds.GetSize().X / HeighfieldSize.X, 0.0f, 0.0f);
				HeighfieldStepY = FVector(0.0f, 0.0f, MeshCardsBounds.GetSize().Z / HeighfieldSize.Y);
				break;

			case 2:
				MaxRayT = MeshCardsBounds.GetSize().Z + 0.1f;
				MeshSliceNum = VolumeSizeInVoxels.Z;
				HeighfieldSize.X = VolumeSizeInVoxels.X;
				HeighfieldSize.Y = VolumeSizeInVoxels.Y;
				HeighfieldStepX = FVector(MeshCardsBounds.GetSize().X / HeighfieldSize.X, 0.0f, 0.0f);
				HeighfieldStepY = FVector(0.0f, MeshCardsBounds.GetSize().Y / HeighfieldSize.Y, 0.0f);
				break;
		}

		switch (Orientation)
		{
			case 0: 
				RayDirection.X = +1.0f; 
				break;

			case 1: 
				RayDirection.X = -1.0f; 
				RayOriginFrame.X = MeshCardsBounds.Max.X;
				break;

			case 2: 
				RayDirection.Y = +1.0f; 
				break;

			case 3: 
				RayDirection.Y = -1.0f; 
				RayOriginFrame.Y = MeshCardsBounds.Max.Y;
				break;

			case 4: 
				RayDirection.Z = +1.0f; 
				break;

			case 5: 
				RayDirection.Z = -1.0f; 
				RayOriginFrame.Z = MeshCardsBounds.Max.Z;
				break;

			default: 
				check(false);
		};

		TArray<TArray<FSurfacePoint, TInlineAllocator<16>>> HeightfieldLayers;
		HeightfieldLayers.SetNum(HeighfieldSize.X * HeighfieldSize.Y);

		// Fill surface points
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(FillSurfacePoints);

			TArray<float> Heightfield;
			Heightfield.SetNum(HeighfieldSize.X * HeighfieldSize.Y);
			for (int32 HeighfieldY = 0; HeighfieldY < HeighfieldSize.Y; ++HeighfieldY)
			{
				for (int32 HeighfieldX = 0; HeighfieldX < HeighfieldSize.X; ++HeighfieldX)
				{
					Heightfield[HeighfieldX + HeighfieldY * HeighfieldSize.X] = -1.0f;
				}
			}

			for (int32 HeighfieldY = 0; HeighfieldY < HeighfieldSize.Y; ++HeighfieldY)
			{
				for (int32 HeighfieldX = 0; HeighfieldX < HeighfieldSize.X; ++HeighfieldX)
				{
					FVector RayOrigin = RayOriginFrame;
					RayOrigin += (HeighfieldX + 0.5f) * HeighfieldStepX;
					RayOrigin += (HeighfieldY + 0.5f) * HeighfieldStepY;

					float StepTMin = 0.0f;

					for (int32 StepIndex = 0; StepIndex < 64; ++StepIndex)
					{
						FEmbreeRay EmbreeRay;
						EmbreeRay.ray.org_x = RayOrigin.X;
						EmbreeRay.ray.org_y = RayOrigin.Y;
						EmbreeRay.ray.org_z = RayOrigin.Z;
						EmbreeRay.ray.dir_x = RayDirection.X;
						EmbreeRay.ray.dir_y = RayDirection.Y;
						EmbreeRay.ray.dir_z = RayDirection.Z;
						EmbreeRay.ray.tnear = StepTMin;
						EmbreeRay.ray.tfar = FLT_MAX;

						FEmbreeIntersectionContext EmbreeContext;
						rtcInitIntersectContext(&EmbreeContext);
						rtcIntersect1(Context.FullMeshEmbreeScene, &EmbreeContext, &EmbreeRay);

						if (EmbreeRay.hit.geomID != RTC_INVALID_GEOMETRY_ID && EmbreeRay.hit.primID != RTC_INVALID_GEOMETRY_ID)
						{
							const FVector SurfacePoint = RayOrigin + RayDirection * EmbreeRay.ray.tfar;
							const FVector SurfaceNormal = EmbreeRay.GetHitNormal();

							const float NdotD = FVector::DotProduct(RayDirection, SurfaceNormal);
							const bool bPassCullTest = EmbreeContext.IsHitTwoSided() || NdotD <= 0.0f;
							const bool bPassProjectionAngleTest = FMath::Abs(NdotD) >= FMath::Cos(75.0f * (PI / 180.0f));

							const float MinDistanceBetweenPoints = (MaxRayT / MeshSliceNum);
							const bool bPassDistanceToAnotherSurfaceTest = EmbreeRay.ray.tnear <= 0.0f || (EmbreeRay.ray.tfar - EmbreeRay.ray.tnear > MinDistanceBetweenPoints);

							if (bPassCullTest && bPassProjectionAngleTest && bPassDistanceToAnotherSurfaceTest)
							{
								const bool bIsInsideMesh = IsSurfacePointInsideMesh(Context.FullMeshEmbreeScene, SurfacePoint, SurfaceNormal, RayDirectionsOverHemisphere);
								if (!bIsInsideMesh)
								{
									HeightfieldLayers[HeighfieldX + HeighfieldY * HeighfieldSize.X].Add(
										{ EmbreeRay.ray.tnear, EmbreeRay.ray.tfar }
									);
								}
							}

							StepTMin = EmbreeRay.ray.tfar + 0.01f;
						}
						else
						{
							break;
						}
					}
				}
			}
		}

		const int32 MinCardHits = FMath::Floor(HeighfieldSize.X * HeighfieldSize.Y * MinSurfaceThreshold);

		FPlacedCardArray& PlacedCardsLod0 = TaskOutputsPerOrientation[Orientation].PlacedCardsPerLod[0];
		int32 PlacedCardsHits = 0;

		// Place a default card
		{
			FPlacedCard PlacedCard;
			PlacedCard.SliceMin = 0;
			PlacedCard.SliceMax = MeshSliceNum;
			PlacedCardsLod0.Add(PlacedCard);

			PlacedCardsHits = UpdatePlacedCards(PlacedCardsLod0,
				RayOriginFrame,
				RayDirection,
				HeighfieldStepX,
				HeighfieldStepY,
				HeighfieldSize,
				MeshSliceNum,
				MaxRayT,
				MinCardHits,
				VoxelExtent,
				HeightfieldLayers);

			if (PlacedCardsHits < MinCardHits)
			{
				PlacedCardsLod0.Reset();
			}
		}

		TaskOutputsPerOrientation[Orientation].MinCardHitsPerLod[0] = MinCardHits;

		FPlacedCardArray& PlacedCardsLod1 = TaskOutputsPerOrientation[Orientation].PlacedCardsPerLod[1];
		PlacedCardsLod1 = PlacedCardsLod0;

		// Try to place more cards by splitting existing ones
		for (uint32 CardPlacementIteration = 0; CardPlacementIteration < 4; ++CardPlacementIteration)
		{
			TArray<FPlacedCard, TInlineAllocator<16>> BestPlacedCards;
			int32 BestPlacedCardHits = PlacedCardsHits;

			for (int32 PlacedCardIndex = 0; PlacedCardIndex < PlacedCardsLod1.Num(); ++PlacedCardIndex)
			{
				const FPlacedCard& PlacedCard = PlacedCardsLod1[PlacedCardIndex];
				for (int32 SliceIndex = PlacedCard.SliceMin + 2; SliceIndex < PlacedCard.SliceMax; ++SliceIndex)
				{
					TArray<FPlacedCard, TInlineAllocator<16>> TempPlacedCards(PlacedCardsLod1);

					FPlacedCard NewPlacedCard;
					NewPlacedCard.SliceMin = SliceIndex;
					NewPlacedCard.SliceMax = PlacedCard.SliceMax;

					TempPlacedCards[PlacedCardIndex].SliceMax = SliceIndex - 1;
					TempPlacedCards.Insert(NewPlacedCard, PlacedCardIndex + 1);

					const int32 NumHits = UpdatePlacedCards(
						TempPlacedCards,
						RayOriginFrame,
						RayDirection,
						HeighfieldStepX,
						HeighfieldStepY,
						HeighfieldSize,
						MeshSliceNum,
						MaxRayT,
						MinCardHits,
						VoxelExtent,
						HeightfieldLayers);

					if (NumHits > BestPlacedCardHits)
					{
						BestPlacedCards = TempPlacedCards;
						BestPlacedCardHits = NumHits;
					}
				}
			}

			if (BestPlacedCardHits >= PlacedCardsHits + MinCardHits)
			{
				PlacedCardsLod1 = BestPlacedCards;
				PlacedCardsHits = BestPlacedCardHits;
			}
		}

		TaskOutputsPerOrientation[Orientation].MinCardHitsPerLod[1] = MinCardHits;
	});

	for (int32 Orientation = 0; Orientation < 6; ++Orientation)
	{
		const FTaskOutputs& TaskOutputs = TaskOutputsPerOrientation[Orientation];
		SerializePlacedCards(TaskOutputs.PlacedCardsPerLod[0], /*LOD level*/ 0, Orientation, TaskOutputs.MinCardHitsPerLod[0], MeshCardsBounds, OutData);
		SerializePlacedCards(TaskOutputs.PlacedCardsPerLod[1], /*LOD level*/ 1, Orientation, TaskOutputs.MinCardHitsPerLod[1], MeshCardsBounds, OutData);
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

	FGenerateCardMeshContext Context(MeshName, EmbreeScene.EmbreeScene, EmbreeScene.EmbreeDevice, OutData);

	// Note: must operate on the SDF bounds because SDF generation can expand the mesh's bounds
	BuildMeshCards(DistanceFieldVolumeData ? DistanceFieldVolumeData->LocalSpaceMeshBounds : Bounds.GetBox(), Context, OutData);

	MeshRepresentation::DeleteEmbreeScene(EmbreeScene);

	const float TimeElapsed = (float)(FPlatformTime::Seconds() - StartTime);

	if (TimeElapsed > 1.0f)
	{
		UE_LOG(LogMeshUtilities, Log, TEXT("Finished mesh card build in %.1fs %s"),
			TimeElapsed,
			*MeshName);
	}

	return true;
#else
	UE_LOG(LogMeshUtilities, Warning, TEXT("Platform did not set USE_EMBREE, GenerateCardRepresentationData failed."));
	return false;
#endif
}
