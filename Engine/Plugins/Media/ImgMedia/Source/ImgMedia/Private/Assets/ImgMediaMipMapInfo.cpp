// Copyright Epic Games, Inc. All Rights Reserved.

#include "ImgMediaMipMapInfo.h"

#include "ImgMediaEngine.h"
#include "ImgMediaMipMapInfoManager.h"
#include "ImgMediaPrivate.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

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

		// If width < 0, then try to calculate it.
		if (Width < 0.0f)
		{
			Width = GetObjectWidth(InActor);
		}
		Info->Width = Width;

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
	FImgMediaEngine& ImgMediaEngine = FImgMediaEngine::Get();
	const TArray<TWeakPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe>>* ObjectInfos = ImgMediaEngine.GetObjects(InMediaTexture);
	if (ObjectInfos != nullptr)
	{
		for (TWeakPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe> ObjectInfoPtr : *ObjectInfos)
		{
			TSharedPtr<FImgMediaMipMapObjectInfo, ESPMode::ThreadSafe> ObjectInfo = ObjectInfoPtr.Pin();
			if (ObjectInfo.IsValid())
			{
				AActor* Owner = ObjectInfo->Object.Get();
				if (Owner != nullptr)
				{
					AddObject(Owner, ObjectInfo->Width, ObjectInfo->LODBias);
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

void FImgMediaMipMapInfo::SetTextureInfo(FName InSequenceName, int NumMipMaps, const FIntPoint& Dim)
{
	SequenceName = InSequenceName;
	MipLevelDistances.SetNum(NumMipMaps);

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
				// Get closest distance to camera.
				FVector ObjectLocation = Object->GetActorLocation();
				for (const FImgMediaMipMapCameraInfo& CameraInfo : CameraInfos)
				{
					float DistToCamera = GetObjectDistToCamera(CameraInfo.Location, ObjectLocation);
					float AdjustedDistToCamera = DistToCamera;

					// Account for object size and possible rotation.
					// Closest case is the object edge is pointing towards us.
					AdjustedDistToCamera = AdjustedDistToCamera - Info->Width;

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

float FImgMediaMipMapInfo::GetObjectWidth(const AActor* InActor)
{
	// Get box extent of actor to calculate width from.
	float Width = -1.0f;
	if (InActor != nullptr)
	{
		const USceneComponent* RootComponent = InActor->GetRootComponent();
		if (RootComponent != nullptr)
		{
			const UStaticMeshComponent* MeshComponent = Cast<const UStaticMeshComponent>(RootComponent);
			if (MeshComponent != nullptr)
			{
				const UStaticMesh* StaticMesh = MeshComponent->GetStaticMesh();
				const FBoxSphereBounds Bounds = StaticMesh->GetBounds();
				Width = Bounds.BoxExtent.X * 2.0f;
			}
		}
	}

	// Did we get anything?
	if (Width < 0.0f)
	{
		UE_LOG(LogImgMedia, Error, TEXT("FImgMediaMipMapInfo could not get size of %s."), InActor != nullptr ? *InActor->GetName() : TEXT("<nullptr>"));
		Width = 0.0f;
	}

	return Width;
}

