// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterMeshComponent.h"
#include "WaterBodyActor.h"
#include "WaterSplineComponent.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"
#include "Engine/Classes/Materials/MaterialInstanceDynamic.h"
#include "Engine/Classes/Materials/MaterialParameterCollection.h"
#include "Engine/Classes/Materials/MaterialParameterCollectionInstance.h"
#include "Engine/Engine.h"
#include "WaterMeshSceneProxy.h"
#include "WaterSubsystem.h"
#include "WaterModule.h"
#include "Math/NumericLimits.h"

/** Scalability CVars*/
static TAutoConsoleVariable<int32> CVarWaterMeshLODCountBias(
	TEXT("r.Water.WaterMesh.LODCountBias"), 0,
	TEXT("This value is added to the LOD Count of each Water Mesh Component. Negative values will lower the quality(fewer and larger water tiles at the bottom level of the water quadtree), higher values will increase quality (more and smaller water tiles at the bottom level of the water quadtree)"),
	ECVF_Scalability);

static TAutoConsoleVariable<int32> CVarWaterMeshTessFactorBias(
	TEXT("r.Water.WaterMesh.TessFactorBias"), 0,
	TEXT("This value is added to the tessellation factor of each Mesh Component. Negative values will lower the overall density/resolution or the vertex grid, higher values will increase the density/resolution "),
	ECVF_Scalability);

static TAutoConsoleVariable<float> CVarWaterMeshLODScaleBias(
	TEXT("r.Water.WaterMesh.LODScaleBias"), 0.0f,
	TEXT("This value is added to the LOD Scale of each Mesh Component. Negative values will lower the overall density/resolution or the vertex grid and make the LODs smaller, higher values will increase the density/resolution and make the LODs larger. Smallest value is -0.5. That will make the inner LOD as tight and optimized as possible"),
	ECVF_Scalability);

/** Debug CVars */ 
static TAutoConsoleVariable<int32> CVarWaterMeshShowTileGenerationGeometry(
	TEXT("r.Water.WaterMesh.ShowTileGenerationGeometry"),
	0,
	TEXT("This debug option will display the geometry used for intersecting the water grid and generating tiles"),
	ECVF_Default
);

static TAutoConsoleVariable<int32> CVarWaterMeshForceRebuildMeshPerFrame(
	TEXT("r.Water.WaterMesh.ForceRebuildMeshPerFrame"),
	0,
	TEXT("Force rebuilding the entire mesh each frame"),
	ECVF_Default
);

static TAutoConsoleVariable<int32> CVarWaterMeshEnabled(
	TEXT("r.Water.WaterMesh.Enabled"),
	1,
	TEXT("If the water mesh is enabled or disabled. This affects both rendering and the water tile generation"),
	ECVF_RenderThreadSafe
);

// ----------------------------------------------------------------------------------

bool IsWaterMeshEnabled(bool bIsRenderThread)
{
	return IsWaterEnabled(bIsRenderThread) && !!(bIsRenderThread ? CVarWaterMeshEnabled.GetValueOnRenderThread() : CVarWaterMeshEnabled.GetValueOnGameThread());
}

UWaterMeshComponent::UWaterMeshComponent()
{
	bAutoActivate = true;
	bHasPerInstanceHitProxies = true;

	SetMobility(EComponentMobility::Static);
}

void UWaterMeshComponent::PostLoad()
{
	Super::PostLoad();
}

void UWaterMeshComponent::PostInitProperties()
{
	Super::PostInitProperties();

	UpdateBounds();
	MarkRenderTransformDirty();
}

FPrimitiveSceneProxy* UWaterMeshComponent::CreateSceneProxy()
{
	// Early out
	if (!bIsEnabled)
	{
		return nullptr;
	}

	return new FWaterMeshSceneProxy(this);
}

void UWaterMeshComponent::GetUsedMaterials(TArray<UMaterialInterface*>& OutMaterials, bool bGetDebugMaterials) const
{
	for (UMaterialInterface* Mat : UsedMaterials)
	{
		if (Mat)
		{
			OutMaterials.Add(Mat);
		}
	}
}

bool UWaterMeshComponent::ShouldRenderSelected() const
{
	if (bSelectable)
	{
		for (TActorIterator<AWaterBody> ActorItr(GetWorld()); ActorItr; ++ActorItr)
		{
			if (ActorItr->IsSelected())
			{
				return true;
			}
		}
	}
	return false;
}

FMaterialRelevance UWaterMeshComponent::GetWaterMaterialRelevance(ERHIFeatureLevel::Type InFeatureLevel) const
{
	// Combine the material relevance for all materials.
	FMaterialRelevance Result;
	for (UMaterialInterface* Mat : UsedMaterials)
	{
		Result |= Mat->GetRelevance_Concurrent(InFeatureLevel);
	}

	return Result;
}

FBoxSphereBounds UWaterMeshComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	// Always return valid bounds (tree is initialized with invalid bounds and if nothing is inserted, the tree bounds will stay invalid)
	FBox NewBounds = WaterQuadTree.GetBounds();

	if (NewBounds.Min.Z >= NewBounds.Max.Z)
	{
		NewBounds.Min.Z = 0.0f;
		NewBounds.Max.Z = 100.0f;
	}
	// Add the far distance to the bounds if it's valid
	if (FarDistanceMaterial)
	{
		NewBounds = NewBounds.ExpandBy(FVector(FarDistanceMeshExtent, FarDistanceMeshExtent, 0.0f));
	}
	return NewBounds;
}

static bool IsMaterialUsedWithWater(const UMaterialInterface* InMaterial)
{
	return (InMaterial && InMaterial->CheckMaterialUsage_Concurrent(EMaterialUsage::MATUSAGE_Water));
}

void UWaterMeshComponent::RebuildWaterMesh(float InTileSize, const FIntPoint& InExtentInTiles)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RebuildWaterMesh);

	// Position snapped to the grid
	const FVector2D GridPosition = FVector2D(FMath::GridSnap(GetComponentLocation().X, InTileSize), FMath::GridSnap(GetComponentLocation().Y, InTileSize)); 
	const FVector2D WorldExtent = FVector2D(InTileSize * InExtentInTiles.X, InTileSize * InExtentInTiles.Y);

	const FBox2D WaterWorldBox = FBox2D(-WorldExtent + GridPosition, WorldExtent + GridPosition);
	
	// This resets the tree to an initial state, ready for node insertion
	WaterQuadTree.InitTree(WaterWorldBox, InTileSize, InExtentInTiles);

	UsedMaterials.Empty();

	// Will be updated with the ocean min bound, to be used to place the far mesh just under the ocean to avoid seams
	float FarMeshHeight = GetComponentLocation().Z;

	const UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld());

	const float GlobalOceanHeight = WaterSubsystem ? WaterSubsystem->GetOceanTotalHeight() : TNumericLimits<float>::Lowest();
	const float OceanFlood = WaterSubsystem ? WaterSubsystem->GetOceanFloodHeight() : 0.0f;
	const bool bIsFlooded = OceanFlood > 0.0f;

	// Go through all water body actors to figure out bounds and water tiles (should only be done and cached at load and when water bodies change)
	for (TActorIterator<AWaterBody> ActorItr(GetWorld()); ActorItr; ++ActorItr)
	{
		AWaterBody* WaterBody = *ActorItr;
		
		const FBox SplineCompBounds = WaterBody->GetWaterSpline()->Bounds.GetBox();

		// Don't process water bodies that has their spline outside of this water mesh
		if (!SplineCompBounds.IntersectXY(FBox(FVector(WaterWorldBox.Min, 0.0f), FVector(WaterWorldBox.Max, 0.0f))))
		{
			continue;
		}

		FWaterBodyRenderData RenderData;

		EWaterBodyType WaterBodyType = WaterBody->GetWaterBodyType();

		// No need to generate anything in the case of a custom water
		if (!WaterBody->ShouldGenerateWaterMeshTile())
		{
			continue;
		}

		if (WaterBodyType != EWaterBodyType::Ocean)
		{
			if (bIsFlooded)
			{
				// If water body is below ocean height and not set to snap to the ocean height, skip it
				const float CompareHeight = (WaterBodyType == EWaterBodyType::River) ? WaterBody->GetComponentsBoundingBox().Max.Z : WaterBody->GetActorLocation().Z;
				if (CompareHeight <= GlobalOceanHeight)
				{
					continue;
				}
			}
		}

		// Assign material instance
		UMaterialInstanceDynamic* WaterMaterial = WaterBody->GetWaterMaterialInstance();
		RenderData.Material = WaterMaterial;
		if (!IsMaterialUsedWithWater(RenderData.Material))
		{
			RenderData.Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		else
		{
			// Add ocean height as a scalar parameter
			WaterBody->SetDynamicParametersOnMID(WaterMaterial);
		}

		// Add material so that the component keeps track of all potential materials used
		UsedMaterials.Add(RenderData.Material);
		
		// Min and max user defined priority range. (Input also clamped on OverlapMaterialPriority in AWaterBody)
		const int32 MinPriority = -8192;
		const int32 MaxPriority = 8191;
		RenderData.Priority = (int16)FMath::Clamp(WaterBody->GetOverlapMaterialPriority(), MinPriority, MaxPriority);
		RenderData.WaterBodyIndex = (int16)WaterBody->WaterBodyIndex;
		RenderData.SurfaceBaseHeight = WaterBody->GetActorLocation().Z;
		RenderData.WaterBodyType = (int8)WaterBodyType;
#if WITH_WATER_SELECTION_SUPPORT
		RenderData.HitProxy = new HActor(WaterBody, this);
		RenderData.bWaterBodySelected = WaterBody->IsSelected();
#endif // WITH_WATER_SELECTION_SUPPORT

		if (WaterBodyType == EWaterBodyType::Ocean && bIsFlooded)
		{
			RenderData.SurfaceBaseHeight += OceanFlood;
			RenderData.Priority -= 1;
		}

		// For rivers, set up transition materials if they exist
		if (WaterBodyType == EWaterBodyType::River)
		{
			UMaterialInstanceDynamic* RiverToLakeMaterial = WaterBody->GetRiverToLakeTransitionMaterialInstance();
			if (IsMaterialUsedWithWater(RiverToLakeMaterial))
			{
				RenderData.RiverToLakeMaterial = RiverToLakeMaterial;
				UsedMaterials.Add(RenderData.RiverToLakeMaterial);
				// Add ocean height as a scalar parameter
				WaterBody->SetDynamicParametersOnMID(RiverToLakeMaterial);
			}
			
			UMaterialInstanceDynamic* RiverToOceanMaterial = WaterBody->GetRiverToOceanTransitionMaterialInstance();
			if (IsMaterialUsedWithWater(RiverToOceanMaterial))
			{
				RenderData.RiverToOceanMaterial = RiverToOceanMaterial;
				UsedMaterials.Add(RenderData.RiverToOceanMaterial);
				// Add ocean height as a scalar parameter
				WaterBody->SetDynamicParametersOnMID(RiverToOceanMaterial);
			}
		}

		if (RenderData.RiverToLakeMaterial || RenderData.RiverToOceanMaterial)
		{
			// Move rivers up to it's own priority space, so that they always have precedence if they have transitions and that they only compare agains other rivers with transitions
			RenderData.Priority += (MaxPriority-MinPriority)+1;
		}

		uint32 WaterBodyRenderDataIndex = WaterQuadTree.AddWaterBodyRenderData(RenderData);

		switch (WaterBodyType)
		{
		case EWaterBodyType::River:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(River);

			TArray<FBox, TInlineAllocator<16>> Boxes;
			TArray<UPrimitiveComponent*> CollisionComponents = WaterBody->GetCollisionComponents();
			for (UPrimitiveComponent* Comp : CollisionComponents)
			{
				if (UBodySetup* BodySetup = (Comp != nullptr) ? Comp->GetBodySetup() : nullptr)
				{
					// Go through all sub shapes on the bodysetup to get a tight fit along water body
					for (const FKConvexElem& ConvElem : BodySetup->AggGeom.ConvexElems)
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(Add);

						FBox SubBox = ConvElem.ElemBox.TransformBy(Comp->GetComponentTransform().ToMatrixWithScale());
						SubBox.Max.Z += WaterBody->GetMaxWaveHeight();

						Boxes.Add(SubBox);
					}
				}
				else
				{
					// fallback on global AABB: 
					FVector Center;
					FVector Extent;
					WaterBody->GetActorBounds(false, Center, Extent);
					FBox Box(FBox::BuildAABB(Center, Extent));
					Box.Max.Z += WaterBody->GetMaxWaveHeight();
					Boxes.Add(Box);
				}
			}

			for (const FBox& Box : Boxes)
			{
				WaterQuadTree.AddWaterTilesInsideBounds(Box, WaterBodyRenderDataIndex);

	#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (!!CVarWaterMeshShowTileGenerationGeometry.GetValueOnGameThread())
				{
					DrawDebugBox(GetWorld(), Box.GetCenter(), Box.GetExtent(), FColor::Red);
				}
	#endif
			}
			break;
		}
		case EWaterBodyType::Lake:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Lake);

			const UWaterSplineComponent* SplineComp = WaterBody->GetWaterSpline();
			const int32 NumOriginaSplinePoints = SplineComp->GetNumberOfSplinePoints();
			
			// Skip lakes with less than 3 spline points
			if (NumOriginaSplinePoints < 3)
			{
				break;
			}

			const float SplineLength = SplineComp->GetSplineLength();
			// LeafSize * 1.5 is roughly the max distance along the spline we aim to sample at
			const int32 NumSamplePoints = FMath::Max(FMath::FloorToInt(SplineLength / (WaterQuadTree.GetLeafSize() * 1.5f)), NumOriginaSplinePoints);
			const float DistDelta = SplineLength / NumSamplePoints;

			TArray<FVector2D> SplinePoints;

			// Allocate the number of spline points we'll be using and fill it from the actual spline
			SplinePoints.SetNum(NumSamplePoints);
			for (int32 i = 0; i < NumSamplePoints; i++)
			{
				SplinePoints[i] = FVector2D(SplineComp->GetLocationAtDistanceAlongSpline(i*DistDelta, ESplineCoordinateSpace::World));
				
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
				if (!!CVarWaterMeshShowTileGenerationGeometry.GetValueOnGameThread())
				{
					FVector Point0 = SplineComp->GetLocationAtDistanceAlongSpline(i*DistDelta, ESplineCoordinateSpace::World);
					FVector Point1 = SplineComp->GetLocationAtDistanceAlongSpline(((i + 1) % NumSamplePoints)*DistDelta, ESplineCoordinateSpace::World);
					DrawDebugLine(GetWorld(), Point0, Point1, FColor::Green);
				}
#endif
			}

			FBox LakeBounds = WaterBody->GetComponentsBoundingBox();
			LakeBounds.Max.Z += WaterBody->GetMaxWaveHeight();

			WaterQuadTree.AddLake(SplinePoints, LakeBounds, WaterBodyRenderDataIndex);

			break;
		}
		case EWaterBodyType::Ocean:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(Ocean);

			// Add ocean based on the ocean spline when there is no flood. Otherwise add ocean everywhere
			if (bIsFlooded)
			{
				FBox OceanBounds = WaterBody->GetComponentsBoundingBox();
				OceanBounds.Max.Z += WaterBody->GetMaxWaveHeight() + OceanFlood;
				WaterQuadTree.AddWaterTilesInsideBounds(OceanBounds, WaterBodyRenderDataIndex);
			}
			else
			{
				const UWaterSplineComponent* SplineComp = WaterBody->GetWaterSpline();
				const int32 NumSamplePoints = SplineComp->GetNumberOfSplinePoints();

				// Skip oceans with less than 3 spline points
				if (NumSamplePoints < 3)
				{
					break;
				}

				TArray<FVector2D> SplinePoints;

				// Allocate the number of spline points we'll be using and fill it from the actual spline
				SplinePoints.SetNum(NumSamplePoints);
				for (int32 i = 0; i < NumSamplePoints; i++)
				{
					SplinePoints[i] = FVector2D(SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World));

#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
					if (!!CVarWaterMeshShowTileGenerationGeometry.GetValueOnGameThread())
					{
						FVector Point0 = SplineComp->GetLocationAtSplinePoint(i, ESplineCoordinateSpace::World);
						FVector Point1 = SplineComp->GetLocationAtSplinePoint(((i + 1) % NumSamplePoints), ESplineCoordinateSpace::World);
						DrawDebugLine(GetWorld(), Point0, Point1, FColor::Blue);
					}
#endif
				}

				const FBox OceanBounds = WaterBody->GetComponentsBoundingBox();

				WaterQuadTree.AddOcean(SplinePoints, FVector2D(OceanBounds.Min.Z, OceanBounds.Max.Z + WaterBody->GetMaxWaveHeight()), WaterBodyRenderDataIndex);
			}

			// Place far mesh height just below the ocean level
			FarMeshHeight = RenderData.SurfaceBaseHeight - WaterBody->GetMaxWaveHeight();

			break;
		}
		case EWaterBodyType::Transition:
			// Transitions dont require rendering
			break;
		default:
			ensureMsgf(false, TEXT("This water body type is not implemented and will not produce any water tiles. "));
		}
	}

	WaterQuadTree.Unlock(true);

	MarkRenderStateDirty();

	// Build the far distance mesh instances. These are 8 tiles around the water quad tree
	if (IsMaterialUsedWithWater(FarDistanceMaterial) && FarDistanceMeshExtent > 0.0f)
	{
		UsedMaterials.Add(FarDistanceMaterial);

		for (int32 StreamIdx = 0; StreamIdx < FWaterTileInstanceData::NumStreams; ++StreamIdx)
		{
			FarDistanceWaterInstanceData.Streams[StreamIdx].SetNum(8);
		}

		const FVector2D WaterCenter = WaterQuadTree.GetTileRegion().GetCenter();
		const FVector2D WaterExtents = WaterQuadTree.GetTileRegion().GetExtent();
		const FVector2D WaterSize = WaterQuadTree.GetTileRegion().GetSize();
		const FVector2D TileOffets[] = { {-1.0f, 1.0f}, {0.0f, 1.0f}, {1.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, -1.0f}, {0.0f, -1.0f}, {-1.0f, -1.0f}, {-1.0f, 0.0f} };
		for (int32 i = 0; i < 8; i++)
		{
			const FVector2D TilePos = WaterCenter + TileOffets[i] * (WaterExtents + 0.5f * FarDistanceMeshExtent);
			FVector2D TileScale;
			TileScale.X = (TileOffets[i].X == 0.0f) ? WaterSize.X : FarDistanceMeshExtent;
			TileScale.Y = (TileOffets[i].Y == 0.0f) ? WaterSize.Y : FarDistanceMeshExtent;

			// Build instance data
			FVector4 InstanceData[FWaterTileInstanceData::NumStreams];
			InstanceData[0] = FVector4(TilePos, FVector2D(FarMeshHeight, 0.0f));
			InstanceData[1] = FVector4(FVector2D::ZeroVector, TileScale);
#if WITH_WATER_SELECTION_SUPPORT
			InstanceData[2] = FHitProxyId::InvisibleHitProxyId.GetColor().ReinterpretAsLinear();
#endif // WITH_WATER_SELECTION_SUPPORT

			for (int32 StreamIdx = 0; StreamIdx < FWaterTileInstanceData::NumStreams; ++StreamIdx)
			{
				FarDistanceWaterInstanceData.Streams[StreamIdx][i] = InstanceData[StreamIdx];
			}
		}
	}
	else
	{
		for(int32 StreamIdx = 0; StreamIdx < FWaterTileInstanceData::NumStreams; ++StreamIdx)
		{
			FarDistanceWaterInstanceData.Streams[StreamIdx].Empty();
		}
	}
}

void UWaterMeshComponent::Update()
{
	// For now, the CVar determines the enabled state
	bIsEnabled = IsWaterEnabled(/*bIsRenderThread = */false) && IsWaterMeshEnabled(/*bIsRenderThread = */false);

	// Early out
	if (!bIsEnabled)
	{
		return;
	}

	const int32 NewLODCountBias = CVarWaterMeshLODCountBias.GetValueOnGameThread();
	const int32 NewTessFactorBias = CVarWaterMeshTessFactorBias.GetValueOnGameThread();
	const float NewLODScaleBias = CVarWaterMeshLODScaleBias.GetValueOnGameThread();
	if (bNeedsRebuild 
		|| !!CVarWaterMeshShowTileGenerationGeometry.GetValueOnGameThread() 
		|| !!CVarWaterMeshForceRebuildMeshPerFrame.GetValueOnGameThread() 
		|| (NewLODCountBias != LODCountBiasScalability)
		|| (NewTessFactorBias != TessFactorBiasScalability) 
		|| (NewLODScaleBias != LODScaleBiasScalability))
	{
		LODCountBiasScalability = NewLODCountBias;
		TessFactorBiasScalability = NewTessFactorBias;
		LODScaleBiasScalability = NewLODScaleBias;
		const float LODCountBiasFactor = FMath::Pow(2.0f, (float)LODCountBiasScalability);
		RebuildWaterMesh(TileSize / LODCountBiasFactor, FIntPoint(FMath::CeilToInt(ExtentInTiles.X * LODCountBiasFactor), FMath::CeilToInt(ExtentInTiles.Y * LODCountBiasFactor)));
		UpdateWaterMPC();
		bNeedsRebuild = false;
	}
}

void UWaterMeshComponent::SetLandscapeInfo(const FVector& InRTWorldLocation, const FVector& InRTWorldSizeVector)
{
	RTWorldLocation = InRTWorldLocation;
	RTWorldSizeVector = InRTWorldSizeVector;

	UpdateWaterMPC();
}

void UWaterMeshComponent::UpdateWaterMPC()
{
	if (const UWaterSubsystem* WaterSubsystem = UWaterSubsystem::GetWaterSubsystem(GetWorld()))
	{
		UMaterialParameterCollection* WaterCollection = WaterSubsystem->GetMaterialParameterCollection();
		if (WaterCollection == nullptr)
		{
			UE_LOG(LogWater, Error, TEXT("No Water MaterialParameterCollection Assigned"));
		}
		else
		{
			UMaterialParameterCollectionInstance* WaterCollectionInstance = GetWorld()->GetParameterCollectionInstance(CastChecked<UMaterialParameterCollection>(WaterCollection));
			check(WaterCollectionInstance != nullptr);
			if (!WaterCollectionInstance->SetVectorParameterValue(FName(TEXT("LandscapeWorldSize")), FLinearColor(RTWorldSizeVector)))
			{
				UE_LOG(LogWater, Error, TEXT("Failed to set \"LandscapeWorldSize\" on Water MaterialParameterCollection"));
			}
			if (!WaterCollectionInstance->SetVectorParameterValue(FName(TEXT("LandscapeLocation")), FLinearColor(RTWorldLocation)))
			{
				UE_LOG(LogWater, Error, TEXT("Failed to set \"LandscapeLocation\" on Water MaterialParameterCollection"));
			}
		}
	}
}

#if WITH_EDITOR
void UWaterMeshComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	FProperty* PropertyThatChanged = PropertyChangedEvent.MemberProperty;
	if (PropertyThatChanged)
	{
		const FName PropertyName = PropertyThatChanged->GetFName();

		// Properties that needs the scene proxy to be rebuilt
		if (PropertyName == GET_MEMBER_NAME_CHECKED(UWaterMeshComponent, LODScale)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UWaterMeshComponent, TessellationFactor)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UWaterMeshComponent, TileSize)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UWaterMeshComponent, ExtentInTiles)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UWaterMeshComponent, ForceCollapseDensityLevel)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UWaterMeshComponent, FarDistanceMaterial)
			|| PropertyName == GET_MEMBER_NAME_CHECKED(UWaterMeshComponent, FarDistanceMeshExtent))
		{
			MarkWaterMeshGridDirty();
			MarkRenderStateDirty();
		}
	}
}
#endif
