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

FImgMediaMipMapInfo::FImgMediaMipMapInfo()
	: MipLevel0Distance(-1.0f)
	, bIsMipLevel0DistanceSetManually(false)
	, ViewportDistAdjust(0.0f)
	, CachedMipLevel(0)
	, bIsCachedMipLevelValid(false)
{
}

FImgMediaMipMapInfo::~FImgMediaMipMapInfo()
{
	for (FImgMediaMipMapObjectInfo* Info : Objects)
	{
		delete Info;
	}
	Objects.Empty();
}

void FImgMediaMipMapInfo::AddObject(AActor* InActor, float Width, float LODBias)
{
	if (InActor != nullptr)
	{
		FImgMediaMipMapObjectInfo* Info = new FImgMediaMipMapObjectInfo();
		Info->Object = InActor;

		// Calculate dist adjust from bias.
		Info->DistAdjust = FMath::Pow(2.0f, LODBias);

		// Get size of object.
		GetObjectSize(InActor, Info->Width, Info->Height);

		// Get dist adjust.
		if (Width > 0.0f)
		{
			FImgMediaMipMapInfoManager& InfoManager = FImgMediaMipMapInfoManager::Get();
			Info->DistAdjust *= InfoManager.GetRefObjectWidth() / Width;
		}

		Objects.Add(Info);
	}
}

void FImgMediaMipMapInfo::RemoveObject(AActor* InActor)
{
	if (InActor != nullptr)
	{
		for (int Index = 0; Index < Objects.Num(); ++Index)
		{
			FImgMediaMipMapObjectInfo* Info = Objects[Index];
			if (InActor == Info->Object.Get())
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
	Objects.Empty();
}

void FImgMediaMipMapInfo::SetMipLevelDistance(float Distance)
{
	MipLevel0Distance = Distance;
	bIsMipLevel0DistanceSetManually = Distance >= 0.0f;
	UpdateMipLevelDistances();
}

void FImgMediaMipMapInfo::SetTextureInfo(FName InSequenceName, int32 NumMipMaps,
	int32 InNumTilesX, int32 InNumTilesY, const FIntPoint& Dim)
{
	SequenceName = InSequenceName;
	MipLevelDistances.SetNum(NumMipMaps);
	NumTilesX = InNumTilesX;
	NumTilesY = InNumTilesY;

	// Set ideal distance for mip level 0 based on various factors.
	if (bIsMipLevel0DistanceSetManually == false)
	{
		// Get reference distance.
		FImgMediaMipMapInfoManager& InfoManager = FImgMediaMipMapInfoManager::Get();
		MipLevel0Distance = InfoManager.GetMipLevel0Distance();

		// Take our texture size into account.
		float TextureAdjust = InfoManager.GetRefObjectTextureWidth() / Dim.X;
		MipLevel0Distance *= TextureAdjust;
	}

	UpdateMipLevelDistances();
}

int32 FImgMediaMipMapInfo::GetDesiredMipLevel(FImgMediaTileSelection& OutTileSelection)
{
	// This is called from the loader one thread at a time as the call is guarded by a critical section.
	// So no need for thread safety here with regards to this function.
	// However the Tick is called from a different thread so care must still be taken when
	// accessing things that are modified by code external to this function.
	
	// Do we need to update the cache?
	if (bIsCachedMipLevelValid == false)
	{
		UpdateMipLevelCache();
	}

	// Get desired mip level from the cache.
	int32 MipLevel = CachedMipLevel;
	OutTileSelection = CachedTileSelection;

	return MipLevel;
}

void FImgMediaMipMapInfo::UpdateMipLevelCache()
{
	SCOPE_CYCLE_COUNTER(STAT_ImgMedia_MipMapUpdateCache);

	CachedMipLevel = 0;
	CachedTileSelection.SetAllVisible();
	bool bHasTiles = (NumTilesX > 1) || (NumTilesY > 1);

	// Loop over all objects.
	float ClosestDistToCamera = TNumericLimits<float>::Max();
	{
		FScopeLock Lock(&InfoCriticalSection);
		for (int Index = 0; Index < Objects.Num(); ++Index)
		{
			FImgMediaMipMapObjectInfo* Info = Objects[Index];
			AActor* Object = Info->Object.Get();
			if (Object != nullptr)
			{
				FVector ObjectLocation = Object->GetActorLocation();
				
				const FTransform ObjectTransform = Object->GetTransform();
				FVector ObjectScale = Object->GetActorScale();
				
				// Set up for tiles.
				float TileWidthInWorldSpace = 0.0f;
				float TileHeightInWorldSpace = 0.0f;
				float TileRadiusInWorldSpace = 0.0f;
				FVector NextTileXVector;
				FVector NextTileYVector;
				if (bHasTiles)
				{
					// Get size of tile in world space.
					TileWidthInWorldSpace = ObjectScale.X * Info->Width / ((float)NumTilesX);
					TileHeightInWorldSpace = ObjectScale.Y * Info->Height / ((float)NumTilesY);
					TileRadiusInWorldSpace = FMath::Max(TileWidthInWorldSpace, TileHeightInWorldSpace) * 0.5f;

					// Get vectors to go from one tile to the next in world space.
					FVector TilePosInObjectSpace(0.0f, TileWidthInWorldSpace, 0.0f);
					FVector TilePos = ObjectTransform.TransformPositionNoScale(TilePosInObjectSpace);
					NextTileXVector = TilePos - ObjectLocation;
					TilePosInObjectSpace.Set(0.0f, 0.0f, TileHeightInWorldSpace);
					TilePos = ObjectTransform.TransformPositionNoScale(TilePosInObjectSpace);
					NextTileYVector = TilePos - ObjectLocation;
				}

				for (const FImgMediaMipMapCameraInfo& CameraInfo : CameraInfos)
				{
					// Mips calculation.
					// Get closest distance to camera.
					float DistToCamera = GetObjectDistToCamera(CameraInfo.Location, ObjectLocation);
					float AdjustedDistToCamera = DistToCamera;

					// Account for object size and possible rotation.
					// Closest case is the object edge is pointing towards us.
					AdjustedDistToCamera = AdjustedDistToCamera - Info->Width * 0.5f;

					// If we are using our calculated distances,
					// then account for various settings.
					if (bIsMipLevel0DistanceSetManually == false)
					{
						AdjustedDistToCamera *= CameraInfo.DistAdjust;
						if (CameraInfo.ScreenSize == 0.0f)
						{
							AdjustedDistToCamera *= ViewportDistAdjust;
						}
						AdjustedDistToCamera *= Info->DistAdjust;
					}

					if (AdjustedDistToCamera < ClosestDistToCamera)
					{
						ClosestDistToCamera = AdjustedDistToCamera;
					}

					// See which tiles are visible.
					if (bHasTiles)
					{
						CalculateTileVisibility(CameraInfo, ObjectLocation, NextTileXVector,
							NextTileYVector, TileRadiusInWorldSpace);
					}
				}
			}
		}
	}

	// Did we get a distance?
	if (ClosestDistToCamera != TNumericLimits<float>::Max())
	{
		// Find the best mip level.
		CachedMipLevel = GetMipLevelForDistance(ClosestDistToCamera, MipLevelDistances);
	}
	
	// Mark cache as valid.
	bIsCachedMipLevelValid = true;
}

void FImgMediaMipMapInfo::CalculateTileVisibility(const FImgMediaMipMapCameraInfo& CameraInfo,
	const FVector& ObjectLocation, const FVector& NextTileXVector,
	const FVector& NextTileYVector, float TileRadiusInWorldSpace)
{
	// Get frustum.
	FConvexVolume ViewFrustum;
	GetViewFrustumBounds(ViewFrustum, CameraInfo.ViewMatrix, false);

	// This is the middle of the object.
	FVector TileStartPos = ObjectLocation;

	// Loop over Y.
	for (int32 TileY = 0; TileY < NumTilesY; ++TileY)
	{
		// Y needs to be flipped so that 0 is at the top.
		// - NumTiles/2 so we start at the corner.
		// + 0.5 so the position is the middle of the tile.
		float TileMultY = (NumTilesY - TileY - 1) - ((float)NumTilesY) / 2.0f + 0.5f;

		// Loop over X.
		for (int32 TileX = 0; TileX < NumTilesX; ++TileX)
		{
			float TileMultX = TileX - ((float)NumTilesX) / 2.0f + 0.5f;
			FVector TilePos = TileStartPos + NextTileXVector * TileMultX +
				NextTileYVector * TileMultY;

			// Is this tile visible?
			if (ViewFrustum.IntersectSphere(TilePos, TileRadiusInWorldSpace))
			{
				// Is the cache initialized to all visible?
				if (CachedTileSelection.BottomRightX != 0xffff)
				{
					// Nope.
					// Add this tile to the selection.
					CachedTileSelection.TopLeftX = FMath::Min(
						CachedTileSelection.TopLeftX, (uint16)TileX);
					CachedTileSelection.TopLeftY = FMath::Min(
						CachedTileSelection.TopLeftY, (uint16)TileY);
					CachedTileSelection.BottomRightX = FMath::Max(
						CachedTileSelection.BottomRightX, (uint16)(TileX + 1));
					CachedTileSelection.BottomRightY = FMath::Max(
						CachedTileSelection.BottomRightY, (uint16)(TileY + 1));
				}
				else
				{
					// Yes.
					// Set tile selection to this tile.
					CachedTileSelection.TopLeftX = TileX;
					CachedTileSelection.TopLeftY = TileY;
					CachedTileSelection.BottomRightX = TileX + 1;
					CachedTileSelection.BottomRightY = TileY + 1;
				}
				
				// Enable this to draw a sphere where each tile is.
#if false
#if WITH_EDITOR
				Async(EAsyncExecution::TaskGraphMainThread, [TilePos, TileRadiusInWorldSpace]()
				{
					UWorld* World = GEditor->GetEditorWorldContext().World();
					DrawDebugSphere(World, TilePos, TileRadiusInWorldSpace, 6, FColor::Red);
				});
#endif // WITH_EDITOR
#endif // false
			}
		}
	}
}

void FImgMediaMipMapInfo::Tick(float DeltaTime)
{
	FScopeLock Lock(&InfoCriticalSection);

	// Get global info.
	FImgMediaMipMapInfoManager& InfoManager = FImgMediaMipMapInfoManager::Get();
	ViewportDistAdjust = InfoManager.GetViewportInfo();
	CameraInfos = InfoManager.GetCameraInfo();

	// Let the cache update this frame.
	bIsCachedMipLevelValid = false;

	// Display debug?
	if (InfoManager.IsDebugEnabled())
	{
		if (GEngine != nullptr)
		{
			GEngine->AddOnScreenDebugMessage(-1, 0.0f, FColor::Yellow, *FString::Printf(TEXT("%s Mip Level: %d"), *SequenceName.ToString(), CachedMipLevel));
		}
	}
}

void FImgMediaMipMapInfo::UpdateMipLevelDistances()
{
	if ((MipLevelDistances.Num() > 0) && (MipLevel0Distance >= 0.0f))
	{
		MipLevelDistances[0] = MipLevel0Distance;

		// Objects will appear half the size when they are twice the distance away.
		// So we can approximate the mip level distance by each level being twice the distance
		// of the previous level, since the mip level is half the size of the previous level.
		for (int Level = 1; Level < MipLevelDistances.Num(); ++Level)
		{
			MipLevelDistances[Level] = MipLevelDistances[Level - 1] * 2.0f;
		}

		for (int Level = 0; Level < MipLevelDistances.Num(); ++Level)
			UE_LOG(LogImgMedia, VeryVerbose, TEXT("FImgMediaMipMapInfo MipLevel:%d Distance:%f"), Level, MipLevelDistances[Level]);
	}
}

float FImgMediaMipMapInfo::GetObjectDistToCamera(const FVector& InCameraLocation, const FVector& InObjectLocation)
{
	float DistToCamera = FVector::Dist(InCameraLocation, InObjectLocation);

	return DistToCamera;
}

int FImgMediaMipMapInfo::GetMipLevelForDistance(float InDistance, const TArray<float>& InMipLevelDistances)
{
	int MipLevel = 0;

	// Find the best mip level.
	for (MipLevel = 0; MipLevel < InMipLevelDistances.Num() - 1; ++MipLevel)
	{
		// If we are between levels 2 and 3, then return 2.
		if (InDistance <= InMipLevelDistances[MipLevel + 1])
		{
			break;
		}
	}
	
	return MipLevel;
}

void FImgMediaMipMapInfo::GetObjectSize(const AActor* InActor, float& Width, float& Height)
{
	// Get box extent of actor to calculate width from.
	Width = -1.0f;
	if (InActor != nullptr)
	{
		FVector BoundsOrigin;
		FVector BoxExtent;
		InActor->GetActorBounds(false, BoundsOrigin, BoxExtent);
		Width = BoxExtent.Z * 2.0f;
		Height = BoxExtent.Y * 2.0f;
	}

	// Did we get anything?
	if (Width < 0.0f)
	{
		UE_LOG(LogImgMedia, Error, TEXT("FImgMediaMipMapInfo could not get size of %s."), InActor != nullptr ? *InActor->GetName() : TEXT("<nullptr>"));
		Width = 0.0f;
		Height = 0.0f;
	}
}

