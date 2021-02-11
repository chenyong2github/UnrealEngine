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

FVector TransformFaceExtent(FVector Extent, int32 Orientation)
{
	if (Orientation / 2 == 2)
	{
		return FVector(Extent.Y, Extent.X, Extent.Z);
	}
	else if (Orientation / 2 == 1)
	{
		return FVector(Extent.Z, Extent.X, Extent.Y);
	}
	else
	{
		return FVector(Extent.Y, Extent.Z, Extent.X); 
	}
}

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
		EmbreeRay.org[0] = SurfacePoint.X;
		EmbreeRay.org[1] = SurfacePoint.Y;
		EmbreeRay.org[2] = SurfacePoint.Z;
		EmbreeRay.dir[0] = RayDirection.X;
		EmbreeRay.dir[1] = RayDirection.Y;
		EmbreeRay.dir[2] = RayDirection.Z;
		EmbreeRay.tnear = 0.1f;
		EmbreeRay.tfar = FLT_MAX;

		rtcIntersect(FullMeshEmbreeScene, EmbreeRay);

		if (EmbreeRay.geomID != -1 && EmbreeRay.primID != -1)
		{
			++NumHits;

			if (FVector::DotProduct(RayDirection, EmbreeRay.GetHitNormal()) > 0.0f && !EmbreeRay.IsHitTwoSided())
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

void SerializePlacedCards(TArray<FPlacedCard, TInlineAllocator<16>>& PlacedCards, 
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
			CardBuildData.Extent = TransformFaceExtent(CardBuildData.Extent, Orientation);
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
		
	for (int32 Orientation = 0; Orientation < 6; ++Orientation)
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
						EmbreeRay.org[0] = RayOrigin.X;
						EmbreeRay.org[1] = RayOrigin.Y;
						EmbreeRay.org[2] = RayOrigin.Z;
						EmbreeRay.dir[0] = RayDirection.X;
						EmbreeRay.dir[1] = RayDirection.Y;
						EmbreeRay.dir[2] = RayDirection.Z;
						EmbreeRay.tnear = StepTMin;
						EmbreeRay.tfar = FLT_MAX;
						rtcIntersect(Context.FullMeshEmbreeScene, EmbreeRay);

						if (EmbreeRay.geomID != -1 && EmbreeRay.primID != -1)
						{
							const FVector SurfacePoint = RayOrigin + RayDirection * EmbreeRay.tfar;
							const FVector SurfaceNormal = EmbreeRay.GetHitNormal();

							const float NdotD = FVector::DotProduct(RayDirection, SurfaceNormal);
							const bool bPassCullTest = EmbreeRay.IsHitTwoSided() || NdotD <= 0.0f;
							const bool bPassProjectionAngleTest = FMath::Abs(NdotD) >= FMath::Cos(75.0f * (PI / 180.0f));

							const float MinDistanceBetweenPoints = (MaxRayT / MeshSliceNum);
							const bool bPassDistanceToAnotherSurfaceTest = EmbreeRay.tnear <= 0.0f || (EmbreeRay.tfar - EmbreeRay.tnear > MinDistanceBetweenPoints);

							if (bPassCullTest && bPassProjectionAngleTest && bPassDistanceToAnotherSurfaceTest)
							{
								const bool bIsInsideMesh = IsSurfacePointInsideMesh(Context.FullMeshEmbreeScene, SurfacePoint, SurfaceNormal, RayDirectionsOverHemisphere);
								if (!bIsInsideMesh)
								{
									HeightfieldLayers[HeighfieldX + HeighfieldY * HeighfieldSize.X].Add(
										{ EmbreeRay.tnear, EmbreeRay.tfar }
									);
								}
							}

							StepTMin = EmbreeRay.tfar + 0.01f;
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


		TArray<FPlacedCard, TInlineAllocator<16>> PlacedCards;
		int32 PlacedCardsHits = 0;

		// Place a default card
		{
			FPlacedCard PlacedCard;
			PlacedCard.SliceMin = 0;
			PlacedCard.SliceMax = MeshSliceNum;
			PlacedCards.Add(PlacedCard);

			PlacedCardsHits = UpdatePlacedCards(PlacedCards,
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
				PlacedCards.Reset();
			}
		}

		SerializePlacedCards(PlacedCards, /*LOD level*/ 0, Orientation, MinCardHits, MeshCardsBounds, OutData);

		// Try to place more cards by splitting existing ones
		for (uint32 CardPlacementIteration = 0; CardPlacementIteration < 4; ++CardPlacementIteration)
		{
			TArray<FPlacedCard, TInlineAllocator<16>> BestPlacedCards;
			int32 BestPlacedCardHits = PlacedCardsHits;

			for (int32 PlacedCardIndex = 0; PlacedCardIndex < PlacedCards.Num(); ++PlacedCardIndex)
			{
				const FPlacedCard& PlacedCard = PlacedCards[PlacedCardIndex];
				for (int32 SliceIndex = PlacedCard.SliceMin + 2; SliceIndex < PlacedCard.SliceMax; ++SliceIndex)
				{
					TArray<FPlacedCard, TInlineAllocator<16>> TempPlacedCards(PlacedCards);

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
				PlacedCards = BestPlacedCards;
				PlacedCardsHits = BestPlacedCardHits;
			}
		}

		SerializePlacedCards(PlacedCards, /*LOD level*/ 1, Orientation, MinCardHits, MeshCardsBounds, OutData);
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

	BuildMeshCards(Bounds.GetBox(), Context, OutData);

	MeshRepresentation::DeleteEmbreeScene(EmbreeScene);

	UE_LOG(LogMeshUtilities, Log, TEXT("Finished mesh card build in %.1fs %s"),
		(float)(FPlatformTime::Seconds() - StartTime),
		*MeshName);

	return true;
#else
	UE_LOG(LogMeshUtilities, Warning, TEXT("Platform did not set USE_EMBREE, GenerateCardRepresentationData failed."));
	return false;
#endif
}
