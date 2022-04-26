// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaMipMapInfo.h"

#include "ImgMediaMipMapInfoManager.h"
#include "ImgMediaPrivate.h"

#include "Async/Async.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Math/UnrealMathUtility.h"
#include "MediaTextureTracker.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

DECLARE_CYCLE_STAT(TEXT("ImgMedia MipMap Update Cache"), STAT_ImgMedia_MipMapUpdateCache, STATGROUP_Media);

FImgMediaMipMapObjectInfo::FImgMediaMipMapObjectInfo(UMeshComponent* InMeshComponent, float InLODBias)
	: MeshComponent(InMeshComponent)
	, LODBias(InLODBias)
{

}

UMeshComponent* FImgMediaMipMapObjectInfo::GetMeshComponent() const
{
	return MeshComponent.Get();
}

bool FImgMediaMipMapObjectInfo::CalculateVisibleTiles(const TArray<FImgMediaMipMapCameraInfo>& InCameraInfos, const FSequenceInfo& InSequenceInfo, TMap<int32, FImgMediaTileSelection>& VisibleTiles) const
{
	for (int32 MipLevel = 0; MipLevel < InSequenceInfo.NumMipLevels; ++MipLevel)
	{
		FImgMediaTileSelection& Selection = VisibleTiles.FindOrAdd(MipLevel);

		const int MipLevelDiv = 1 << MipLevel;

		int32 NumTilesX = FMath::Max(1, FMath::CeilToInt(float(InSequenceInfo.NumTiles.X) / MipLevelDiv));
		int32 NumTilesY = FMath::Max(1, FMath::CeilToInt(float(InSequenceInfo.NumTiles.Y) / MipLevelDiv));
		Selection.SetVisibleRegion(0u, 0u, IntCastChecked<uint16>(NumTilesX), IntCastChecked<uint16>(NumTilesY));
	}
	return true;
}

namespace {
	// Minimalized version of FSceneView::ProjectWorldToScreen
	FORCEINLINE bool ProjectWorldToScreenFast(const FVector& WorldPosition, const FIntRect& ViewRect, const FMatrix& ViewProjectionMatrix, FVector2D& out_ScreenPos)
	{
		FPlane Result = ViewProjectionMatrix.TransformFVector4(FVector4(WorldPosition, 1.f));
		if (Result.W > 0.0f)
		{
			float NormalizedX = (Result.X / (Result.W * 2.f)) + 0.5f;
			float NormalizedY = 1.f - (Result.Y / (Result.W * 2.f)) - 0.5f;
			out_ScreenPos = FVector2D(NormalizedX * (float)ViewRect.Width(), NormalizedY * (float)ViewRect.Height());

			return true;
		}

		return false;
	}

	// Approximates hardware mip level selection.
	bool CalculateMipLevel(const FImgMediaMipMapCameraInfo& CameraInfo, const FVector& TexelWS, const FVector& OffsetXWS, const FVector& OffsetYWS, float& OutMipLevel)
	{
		FVector2D TexelScreenSpace[3];

		bool bValid = true;
		bValid &= ProjectWorldToScreenFast(TexelWS, CameraInfo.ViewportRect, CameraInfo.ViewProjectionMatrix, TexelScreenSpace[0]);
		bValid &= ProjectWorldToScreenFast(TexelWS + OffsetXWS, CameraInfo.ViewportRect, CameraInfo.ViewProjectionMatrix, TexelScreenSpace[1]);
		bValid &= ProjectWorldToScreenFast(TexelWS + OffsetYWS, CameraInfo.ViewportRect, CameraInfo.ViewProjectionMatrix, TexelScreenSpace[2]);

		if (bValid)
		{
			float DistX = FVector2D::DistSquared(TexelScreenSpace[0], TexelScreenSpace[1]);
			float DistY = FVector2D::DistSquared(TexelScreenSpace[0], TexelScreenSpace[2]);
			OutMipLevel = FMath::Max(0.5f * (float)FMath::Log2(1.0f / FMath::Max(DistX, DistY)), 0.0f); // ~ log2(sqrt(delta))
		}

		return bValid;
	}

	class FPlaneObjectInfo : public FImgMediaMipMapObjectInfo
	{
	public:
		FPlaneObjectInfo(UMeshComponent* InMeshComponent, float InLODBias = 0.0f)
			: FImgMediaMipMapObjectInfo(InMeshComponent, InLODBias)
			, PlaneSize(FVector::ZeroVector)
		{
			// Get size of object.
			if (MeshComponent != nullptr)
			{
				PlaneSize = 2.0f * MeshComponent->CalcLocalBounds().BoxExtent;

				PlaneVertices[0] = 0.5f * FVector(-PlaneSize.X, -PlaneSize.Y, 0);
				PlaneVertices[1] = 0.5f * FVector(-PlaneSize.X, PlaneSize.Y, 0);
				PlaneVertices[2] = 0.5f * FVector(PlaneSize.X, -PlaneSize.Y, 0);
				PlaneVertices[3] = 0.5f * FVector(PlaneSize.X, PlaneSize.Y, 0);
			}
			else
			{
				UE_LOG(LogImgMedia, Error, TEXT("FPlaneImgMediaMipMapObjectInfo is missing its plane mesh component."));
			}
		}

		bool CalculateVisibleTiles(const TArray<FImgMediaMipMapCameraInfo>& InCameraInfos, const FSequenceInfo& InSequenceInfo, TMap<int32, FImgMediaTileSelection>& VisibleTiles) const override
		{
			UMeshComponent* Mesh = MeshComponent.Get();
			if (Mesh == nullptr)
			{
				return false;
			}

			// To avoid calculating tile corner mip levels multiple times over, we cache them in this array.
			CornerMipLevelsCached.SetNum((InSequenceInfo.NumTiles.X + 1) * (InSequenceInfo.NumTiles.Y + 1));
			for (float& Level : CornerMipLevelsCached)
			{
				Level = -1.0f;
			}

			const FTransform MeshTransform = Mesh->GetComponentTransform();
			const FVector MeshScale = Mesh->GetComponentScale();

			FVector PlaneCornerWS = MeshTransform.TransformPosition(-0.5f * PlaneSize);
			FVector DirXWS = MeshTransform.TransformVector(FVector(PlaneSize.X, 0, 0));
			FVector DirYWS = MeshTransform.TransformVector(FVector(0, PlaneSize.Y, 0));
			FVector TexelOffsetXWS = MeshTransform.TransformVector(FVector(PlaneSize.X / InSequenceInfo.Dim.X, 0, 0));
			FVector TexelOffsetYWS = MeshTransform.TransformVector(FVector(0, PlaneSize.Y / InSequenceInfo.Dim.Y, 0));

			for (const FImgMediaMipMapCameraInfo& CameraInfo : InCameraInfos)
			{
				// Get frustum.
				FConvexVolume ViewFrustum;
				GetViewFrustumBounds(ViewFrustum, CameraInfo.ViewProjectionMatrix, false, false);

				int32 MaxLevel = InSequenceInfo.NumMipLevels - 1;
				int MipLevelDiv = 1 << MaxLevel;

				FIntPoint CurrentNumTiles;
				CurrentNumTiles.X = FMath::Max(1, FMath::CeilToInt(float(InSequenceInfo.NumTiles.X) / MipLevelDiv));
				CurrentNumTiles.Y = FMath::Max(1, FMath::CeilToInt(float(InSequenceInfo.NumTiles.Y) / MipLevelDiv));

				// Starting with tiles at the highest mip level
				TQueue<FIntVector> Tiles;
				for (int32 TileY = 0; TileY < CurrentNumTiles.Y; ++TileY)
				{
					for (int32 TileX = 0; TileX < CurrentNumTiles.X; ++TileX)
					{
						Tiles.Enqueue(FIntVector(TileX, TileY, MaxLevel));
					}
				}

				// Process all visible tiles with a (quadtree) breadth-first search
				while (!Tiles.IsEmpty())
				{
					FIntVector Tile;
					Tiles.Dequeue(Tile);

					int32 CurrentMipLevel = Tile.Z;
					MipLevelDiv = 1 << CurrentMipLevel;
					// Calculate the number of tiles at this mip level
					CurrentNumTiles.X = FMath::Max(1, FMath::CeilToInt(float(InSequenceInfo.NumTiles.X) / MipLevelDiv));
					CurrentNumTiles.Y = FMath::Max(1, FMath::CeilToInt(float(InSequenceInfo.NumTiles.Y) / MipLevelDiv));

					// Calculate the tile location in world-space
					float StepX = float(Tile.X + 0.5f) / CurrentNumTiles.X;
					float StepY = float(Tile.Y + 0.5f) / CurrentNumTiles.Y;
					FVector TileCenterWS = PlaneCornerWS + (DirXWS * StepX + DirYWS * StepY);

					// Calculate the tile radius in world space
					FVector TileSizeWS = (PlaneSize * MeshScale) / FVector(CurrentNumTiles.X, CurrentNumTiles.Y, 1);
					float TileRadiusInWS = 0.5f * (float)FMath::Sqrt(2 * FMath::Square(TileSizeWS.GetAbsMax()));

					// Now we check if tile spherical bounds are in view.
					if (ViewFrustum.IntersectSphere(TileCenterWS, TileRadiusInWS))
					{
						// Calculate the visible mip level range over all tile corners.
						int32 NumVisibleCorners = 0;
						FIntVector2 MipLevelRange = FIntVector2(TNumericLimits<int32>::Max(), 0);
						for (int32 CornerY = 0; CornerY < 2; ++CornerY)
						{
							for (int32 CornerX = 0; CornerX < 2; ++CornerX)
							{
								float CalculatedLevel;
								int32 TileCornerX = Tile.X + CornerX;
								int32 TileCornerY = Tile.Y + CornerY;

								// First we query the cached corner mip levels.
								int32 MaxCornerX = InSequenceInfo.NumTiles.X + 1;
								int32 MaxCornerY = InSequenceInfo.NumTiles.Y + 1;
								FIntPoint BaseLevelCorner;
								BaseLevelCorner.X = FMath::Clamp(TileCornerX << CurrentMipLevel, 0, InSequenceInfo.NumTiles.X);
								BaseLevelCorner.Y = FMath::Clamp(TileCornerY << CurrentMipLevel, 0, InSequenceInfo.NumTiles.Y);
								bool bValidLevel = GetCachedMipLevel(BaseLevelCorner.X, BaseLevelCorner.Y, MaxCornerX, CalculatedLevel);

								// If not found, calculate and cache it.
								if (!bValidLevel)
								{
									float CornerStepX = TileCornerX / (float)CurrentNumTiles.X;
									float CornerStepY = TileCornerY / (float)CurrentNumTiles.Y;
									FVector CornersWS = PlaneCornerWS + (DirXWS * CornerStepX + DirYWS * CornerStepY);

									if (CalculateMipLevel(CameraInfo, CornersWS, TexelOffsetXWS, TexelOffsetYWS, CalculatedLevel))
									{
										CalculatedLevel += LODBias + CameraInfo.MaterialTextureMipBias;

										SetCachedMipLevel(BaseLevelCorner.X, BaseLevelCorner.Y, MaxCornerX, CalculatedLevel);
										bValidLevel = true;
									}
								}
								
								if (bValidLevel)
								{
									MipLevelRange[0] = FMath::Min(MipLevelRange[0], FMath::Min((int32)CalculatedLevel, MaxLevel));
									MipLevelRange[1] = FMath::Max(MipLevelRange[1], FMath::Min(FMath::CeilToInt32(CalculatedLevel), MaxLevel));
									NumVisibleCorners++;
								}
							}
						}

						// As an approximation, we force the lowest mip to 0 if only some corners are behind camera.
						if (NumVisibleCorners > 0 && NumVisibleCorners < 4)
						{
							MipLevelRange[0] = 0;
						}

						// If the lowest (calculated) mip level is below our current mip level, enqueue all 4 sub-tiles for further processing.
						if (MipLevelRange[0] < CurrentMipLevel)
						{
							for (int32 SubY = 0; SubY < FMath::Min(InSequenceInfo.NumTiles.Y, 2); ++SubY)
							{
								for (int32 SubX = 0; SubX < FMath::Min(InSequenceInfo.NumTiles.X, 2); ++SubX)
								{
									FIntVector SubTile = FIntVector((Tile.X << 1) + SubX, (Tile.Y << 1) + SubY, CurrentMipLevel - 1);
									Tiles.Enqueue(SubTile);
								}
							}
						}

						// If the highest (calculated) mip level equals or exceeds our current mip level, we register the tile as visible.
						if (MipLevelRange[1] >= CurrentMipLevel)
						{
							VisibleTiles.FindOrAdd(CurrentMipLevel).Include(Tile.X, Tile.Y);
						}
#if false
#if WITH_EDITOR
						// Enable this to draw a sphere where each tile is.
						Async(EAsyncExecution::TaskGraphMainThread, [TileCenterWS, TileRadiusInWS]()
							{
								UWorld* World = GEditor->GetEditorWorldContext().World();
								DrawDebugSphere(World, TileCenterWS, TileRadiusInWS, 8, FColor::Red, false, 0.05f);
							});
#endif // WITH_EDITOR
#endif // false
					}
				}
			}
			return true;
		}

	private:

		/** Convenience function to get a cached calculated mip level (in mip0 tile address space). */
		FORCEINLINE bool GetCachedMipLevel(int32 Address0X, int32 Address0Y, int32 RowSize, float& OutCalculatedLevel) const
		{
			const int32 Index = Address0Y * RowSize + Address0X;

			if (CornerMipLevelsCached[Index] >= 0.0f)
			{
				OutCalculatedLevel = CornerMipLevelsCached[Index];
				
				return true;
			}

			return false;
		}

		/** Convenience function to cache a calculated mip level (in mip0 tile address space). */
		FORCEINLINE void SetCachedMipLevel(int32 Address0X, int32 Address0Y, int32 RowSize, float InCalculatedLevel) const
		{
			CornerMipLevelsCached[Address0Y * RowSize + Address0X] = InCalculatedLevel;
		}

		/** Local size of this mesh component. */
		FVector PlaneSize;

		/** Four plane vertices (in local space). */
		TStaticArray<FVector, 4> PlaneVertices;

		/** Cached calculating mip levels (at mip0). */
		mutable TArray<float> CornerMipLevelsCached;
	};

	// Reduce the selected tiles region for higher mip levels, always ensuring that all tiles at the base level are included.
	FImgMediaTileSelection DownscaleTileSelection(const FImgMediaTileSelection& InTopLevelSelection, int32 InMipLevel)
	{
		if (InMipLevel <= 0)
		{
			return InTopLevelSelection;
		}

		const int32 MipLevelDiv = 1 << InMipLevel;

		FImgMediaTileSelection OutSelection;
		OutSelection.TopLeftX = (uint16)(InTopLevelSelection.TopLeftX / MipLevelDiv);
		OutSelection.TopLeftY = (uint16)(InTopLevelSelection.TopLeftY / MipLevelDiv);
		OutSelection.BottomRightX = (uint16)FMath::CeilToInt(float(InTopLevelSelection.BottomRightX) / MipLevelDiv);
		OutSelection.BottomRightY = (uint16)FMath::CeilToInt(float(InTopLevelSelection.BottomRightY) / MipLevelDiv);

		return OutSelection;
	}

	class FSphereObjectInfo : public FImgMediaMipMapObjectInfo
	{
	public:
		using FImgMediaMipMapObjectInfo::FImgMediaMipMapObjectInfo;

		bool CalculateVisibleTiles(const TArray<FImgMediaMipMapCameraInfo>& InCameraInfos, const FSequenceInfo& InSequenceInfo, TMap<int32, FImgMediaTileSelection>& VisibleTiles) const override
		{
			UMeshComponent* Mesh = MeshComponent.Get();
			if (Mesh == nullptr)
			{
				return false;
			}

			const float DefaultSphereRadius = 160.0f;

			for (const FImgMediaMipMapCameraInfo& CameraInfo : InCameraInfos)
			{
				// Analytical derivation of visible tiles from the view frustum, given a sphere presumed to be infinitely large
				FConvexVolume ViewFrustum;
				GetViewFrustumBounds(ViewFrustum, CameraInfo.ViewProjectionMatrix, false, false);
				
				// Include all tiles containted in the visible UV region
				uint16 NumX = IntCastChecked<uint16>(InSequenceInfo.NumTiles.X);
				uint16 NumY = IntCastChecked<uint16>(InSequenceInfo.NumTiles.Y);
				for (uint16 TileY = 0; TileY < NumY; ++TileY)
				{
					for (uint16 TileX = 0; TileX < NumX; ++TileX)
					{
						FVector2D TileCornerUV = FVector2D((float)TileX / InSequenceInfo.NumTiles.X, (float)TileY / InSequenceInfo.NumTiles.Y);
						
						// Convert from latlong UV to spherical coordinates
						FVector2D TileCornerSpherical = FVector2D(UE_PI * TileCornerUV.Y, UE_TWO_PI * TileCornerUV.X);
						
						// Adjust spherical coordinates to default sphere UVs
						TileCornerSpherical.Y = -UE_HALF_PI - TileCornerSpherical.Y;

						FVector TileCorner = TileCornerSpherical.SphericalToUnitCartesian() * DefaultSphereRadius;
						TileCorner = Mesh->GetComponentTransform().TransformPosition(TileCorner);

						// For each tile corner, we include all adjacent tiles
						if (ViewFrustum.IntersectPoint(TileCorner))
						{
							VisibleTiles.FindOrAdd(0).Include(TileX, TileY);

							int AdjacentX = TileX > 0 ? TileX - 1 : NumX - 1;
							int AdjacentY = TileY > 0 ? TileY - 1 : NumY - 1;
							VisibleTiles[0].Include(AdjacentX, TileY);
							VisibleTiles[0].Include(TileX, AdjacentY);
							VisibleTiles[0].Include(AdjacentX, AdjacentY);
						}
					}
				}

				if (VisibleTiles.Contains(0))
				{
					for (int32 Level = 1; Level < InSequenceInfo.NumMipLevels; ++Level)
					{
						FImgMediaTileSelection& Selection = VisibleTiles.FindOrAdd(Level);
						Selection = DownscaleTileSelection(VisibleTiles[0], Level);
					}
				}
			}

			return true;
		}
	};

} //end anonymous namespace

FImgMediaMipMapInfo::FImgMediaMipMapInfo()
	: bIsCacheValid(false)
{
}

FImgMediaMipMapInfo::~FImgMediaMipMapInfo()
{
	ClearAllObjects();
}

void FImgMediaMipMapInfo::AddObject(AActor* InActor, float Width, float LODBias)
{
	if (InActor != nullptr)
	{
		UMeshComponent* MeshComponent = Cast<UMeshComponent>(InActor->FindComponentByClass(UMeshComponent::StaticClass()));
		if (MeshComponent != nullptr)
		{
			FImgMediaMipMapObjectInfo* Info = nullptr;

			if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshComponent))
			{
				FString StaticMeshName = StaticMeshComponent->GetStaticMesh()->GetName();
				if (StaticMeshName.Equals(TEXT("Plane")))
				{
					Info = new FPlaneObjectInfo(MeshComponent, LODBias);
				}
				else if (StaticMeshName.Equals(TEXT("Sphere")))
				{
					Info = new FSphereObjectInfo(MeshComponent, LODBias);
				}
			}

			if (Info == nullptr)
			{
				Info = new FImgMediaMipMapObjectInfo(MeshComponent, LODBias);
			}

			Objects.Add(Info);
		}
	}
}

void FImgMediaMipMapInfo::RemoveObject(AActor* InActor)
{
	if (InActor != nullptr)
	{
		for (int Index = 0; Index < Objects.Num(); ++Index)
		{
			FImgMediaMipMapObjectInfo* Info = Objects[Index];
			if (InActor == Info->GetMeshComponent()->GetOuter())
			{
				Objects.RemoveAtSwap(Index);
				delete Info;
				break;
			}
		}
	}
}

void FImgMediaMipMapInfo::AddObjectsUsingThisMediaTexture(UMediaTexture* InMediaTexture)
{
	// Get objects using this texture.
	FImgMediaMipMapInfoManager& InfoManager = FImgMediaMipMapInfoManager::Get();
	FMediaTextureTracker& TextureTracker = FMediaTextureTracker::Get();
	const TArray<TWeakPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe>>* ObjectInfos = TextureTracker.GetObjects(InMediaTexture);
	if (ObjectInfos != nullptr)
	{
		for (TWeakPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe> ObjectInfoPtr : *ObjectInfos)
		{
			TSharedPtr<FMediaTextureTrackerObject, ESPMode::ThreadSafe> ObjectInfo = ObjectInfoPtr.Pin();
			if (ObjectInfo.IsValid())
			{
				AActor* Owner = ObjectInfo->Object.Get();
				if (Owner != nullptr)
				{
					AddObject(Owner, 0.0f, ObjectInfo->MipMapLODBias);
				}
			}
		}
	}
}

void FImgMediaMipMapInfo::ClearAllObjects()
{
	for (FImgMediaMipMapObjectInfo* Info : Objects)
	{
		delete Info;
	}
	Objects.Empty();
}


void FImgMediaMipMapInfo::SetTextureInfo(FName InSequenceName, int32 InNumMipMaps,
	const FIntPoint& InNumTiles, const FIntPoint& InSequenceDim)
{
	SequenceInfo.Name = InSequenceName;
	SequenceInfo.Dim = InSequenceDim;

	// To simplify logic, we assume we always have at least one mip level and one tile.
	SequenceInfo.NumMipLevels = FMath::Max(1, InNumMipMaps);
	SequenceInfo.NumTiles.X = FMath::Max(1, InNumTiles.X);
	SequenceInfo.NumTiles.Y = FMath::Max(1, InNumTiles.Y);
}

TMap<int32, FImgMediaTileSelection> FImgMediaMipMapInfo::GetVisibleTiles()
{
	// This is called from the loader one thread at a time as the call is guarded by a critical section.
	// So no need for thread safety here with regards to this function.
	// However the Tick is called from a different thread so care must still be taken when
	// accessing things that are modified by code external to this function.
	
	// Do we need to update the cache?
	if (bIsCacheValid == false)
	{
		UpdateMipLevelCache();
	}

	return CachedVisibleTiles;
}

void FImgMediaMipMapInfo::UpdateMipLevelCache()
{
	SCOPE_CYCLE_COUNTER(STAT_ImgMedia_MipMapUpdateCache);

	{
		FScopeLock Lock(&InfoCriticalSection);

		CachedVisibleTiles.Reset();

		// Loop over all objects.
		for (FImgMediaMipMapObjectInfo* ObjectInfo : Objects)
		{
			ObjectInfo->CalculateVisibleTiles(CameraInfos, SequenceInfo, CachedVisibleTiles);
		}
	}
	
	// Mark cache as valid.
	bIsCacheValid = true;
}

void FImgMediaMipMapInfo::Tick(float DeltaTime)
{
	FScopeLock Lock(&InfoCriticalSection);

	// Get global info.
	FImgMediaMipMapInfoManager& InfoManager = FImgMediaMipMapInfoManager::Get();
	CameraInfos = InfoManager.GetCameraInfo();

	// Let the cache update this frame.
	bIsCacheValid = false;

	// Display debug?
	if (InfoManager.IsDebugEnabled())
	{
		if (GEngine != nullptr)
		{
			FIntVector2 MipLevelRange = FIntVector2(TNumericLimits<int32>::Max(), 0);
			int32 NumVisibleTiles = 0;
			for (const auto& MipTiles : CachedVisibleTiles)
			{
				MipLevelRange[0] = FMath::Min(MipLevelRange[0], MipTiles.Key);
				MipLevelRange[1] = FMath::Max(MipLevelRange[1], MipTiles.Key);

				const FImgMediaTileSelection& TileSelection = MipTiles.Value;
				NumVisibleTiles += (TileSelection.BottomRightX - TileSelection.TopLeftX) * (TileSelection.BottomRightY - TileSelection.TopLeftY);
			}

			if (MipLevelRange[0] <= MipLevelRange[1])
			{
				GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Yellow, *FString::Printf(TEXT("%s Mip Level: [%d, %d]"), *SequenceInfo.Name.ToString(), MipLevelRange[0], MipLevelRange[1]));
				GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Yellow, *FString::Printf(TEXT("%s Num Tiles: %d"), *SequenceInfo.Name.ToString(), NumVisibleTiles));
			}
		}
	}
}


