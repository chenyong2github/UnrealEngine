// Copyright Epic Games, Inc. All Rights Reserved.

/*
 * WorldPartitionMiniMapHelper implementation
 */

#include "WorldPartition/WorldPartitionMiniMapHelper.h"

#if WITH_EDITOR
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/SceneCapture2D.h"
#include "Engine/World.h"
#include "ShaderCompiler.h"
#include "AssetCompilingManager.h"
#include "DistanceFieldAtlas.h"
#include "PackageSourceControlHelper.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "WorldPartition/WorldPartitionMiniMap.h"
#include "WorldPartition/WorldPartition.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionMiniMapHelper, All, All);

AWorldPartitionMiniMap* FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(UWorld* World, bool bCreateNewMiniMap)
{
	if (!World->IsPartitionedWorld())
	{
		UE_LOG(LogWorldPartitionMiniMapHelper, Error, TEXT("No WorldPartition Found. WorldPartition must exist to get WorldPartitionMiniMap"));
		return nullptr;
	}

	ULevel* PersistentLevel = World->PersistentLevel;
	check(PersistentLevel);

	for (auto Actor : PersistentLevel->Actors)
	{
		if (Actor && Actor->IsA<AWorldPartitionMiniMap>())
		{
			return Cast<AWorldPartitionMiniMap>(Actor);
		}
	}

	if (bCreateNewMiniMap)
	{
		TSubclassOf<AWorldPartitionMiniMap> MiniMap(AWorldPartitionMiniMap::StaticClass());
		FActorSpawnParameters SpawnInfo;
		SpawnInfo.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnInfo.OverrideLevel = PersistentLevel;
		return World->SpawnActor<AWorldPartitionMiniMap>(MiniMap, SpawnInfo);
	}
	
	return nullptr;
}

void FWorldPartitionMiniMapHelper::CaptureWorldMiniMapToTexture(UWorld* InWorld, UObject* InOuterForTexture, uint32 InMiniMapSize, UTexture2D*& InOutMiniMapTexture, const FString& InTextureName, FBox& OutWorldBounds)
{
	//Calculate bounds of the World
	OutWorldBounds = InWorld->GetWorldPartition()->GetEditorWorldBounds();

	CaptureBoundsMiniMapToTexture(InWorld, InOuterForTexture, InMiniMapSize, InOutMiniMapTexture, InTextureName, OutWorldBounds);
}

void FWorldPartitionMiniMapHelper::CaptureBoundsMiniMapToTexture(UWorld* InWorld, UObject* InOuterForTexture, uint32 InMiniMapSize, UTexture2D*& InOutMiniMapTexture, const FString& InTextureName, const FBox& InBounds)
{
	// Before capturing the scene, make sure all assets are finished compiling
	FAssetCompilingManager::Get().FinishAllCompilation();

	//Calculate Viewport size
	FBox2D WorldBounds2D(FVector2D(InBounds.Min), FVector2D(InBounds.Max));
	FVector2D ViewSize = WorldBounds2D.Max - WorldBounds2D.Min;
	float AspectRatio = FMath::Abs(ViewSize.X) / FMath::Abs(ViewSize.Y);
	uint32 ViewportWidth = InMiniMapSize * AspectRatio;
	uint32 ViewportHeight = InMiniMapSize;

	//Calculate Projection matrix based on world bounds.
	FMatrix ProjectionMatrix;
	FWorldPartitionMiniMapHelper::CalTopViewOfWorld(ProjectionMatrix, InBounds, ViewportWidth, ViewportHeight);

	//Using SceneCapture Actor capture the scene to buffer
	UTextureRenderTarget2D* RenderTargetTexture = NewObject<UTextureRenderTarget2D>();
	RenderTargetTexture->ClearColor = FLinearColor::Transparent;
	RenderTargetTexture->TargetGamma = 2.2f;
	RenderTargetTexture->InitCustomFormat(InMiniMapSize, InMiniMapSize, PF_B8G8R8A8, false);
	RenderTargetTexture->UpdateResourceImmediate(true);

	FActorSpawnParameters SpawnInfo;
	SpawnInfo.ObjectFlags |= RF_Transient;
	FVector CaptureActorLocation = FVector(InBounds.GetCenter().X, InBounds.GetCenter().Y, InBounds.GetCenter().Z + InBounds.GetExtent().Z);
	FRotator CaptureActorRotation = FRotator(-90.f, 0.f, -90.f);
	ASceneCapture2D* CaptureActor = InWorld->SpawnActor<ASceneCapture2D>(CaptureActorLocation, CaptureActorRotation, SpawnInfo);
	auto CaptureComponent = CaptureActor->GetCaptureComponent2D();
	CaptureComponent->TextureTarget = RenderTargetTexture;
	CaptureComponent->ProjectionType = ECameraProjectionMode::Orthographic;
	CaptureComponent->CaptureSource = SCS_BaseColor;
	CaptureComponent->OrthoWidth = ViewportWidth;
	CaptureComponent->bUseCustomProjectionMatrix = true;
	CaptureComponent->CustomProjectionMatrix = ProjectionMatrix;
	CaptureComponent->bCaptureEveryFrame = false;
	CaptureComponent->bCaptureOnMovement = false;
	CaptureComponent->CaptureScene();

	InWorld->DestroyActor(CaptureActor);
	CaptureActor = nullptr;	

	//Update the output texture
	if (!InOutMiniMapTexture)
	{
		InOutMiniMapTexture = RenderTargetTexture->ConstructTexture2D(InOuterForTexture, InTextureName, RF_NoFlags, CTF_Default, NULL);
	}
	else
	{
		RenderTargetTexture->UpdateTexture2D(InOutMiniMapTexture, TSF_BGRA8, CTF_Default);
	}

	InOutMiniMapTexture->AdjustMinAlpha = 1.f;
	InOutMiniMapTexture->LODGroup = TEXTUREGROUP_UI;
	InOutMiniMapTexture->UpdateResource();
}

void FWorldPartitionMiniMapHelper::CalTopViewOfWorld(FMatrix& OutProjectionMatrix, const FBox& WorldBox, uint32 ViewportWidth, uint32 ViewportHeight)
{
	const FVector Origin = WorldBox.GetCenter();

	FVector2D WorldSizeMin2D(WorldBox.Min.X, WorldBox.Min.Y);
	FVector2D WorldSizeMax2D(WorldBox.Max.X, WorldBox.Max.Y);

	FVector2D WorldSize2D = (WorldSizeMax2D - WorldSizeMin2D);
	WorldSize2D.X = FMath::Abs(WorldSize2D.X);
	WorldSize2D.Y = FMath::Abs(WorldSize2D.Y);
	const bool bUseXAxis = (WorldSize2D.X / WorldSize2D.Y) > 1.f;
	const float WorldAxisSize = bUseXAxis ? WorldSize2D.X : WorldSize2D.Y;
	const uint32 ViewportAxisSize = bUseXAxis ? ViewportWidth : ViewportHeight;
	const float OrthoZoom = WorldAxisSize / ViewportAxisSize / 2.f;
	const float OrthoWidth = FMath::Max(1.f, ViewportWidth * OrthoZoom);
	const float OrthoHeight = FMath::Max(1.f, ViewportHeight * OrthoZoom);

	const float ZOffset = WORLDPARTITION_MAX * 0.5;
	OutProjectionMatrix = FReversedZOrthoMatrix(
		OrthoWidth,
		OrthoHeight,
		0.5f / ZOffset,
		ZOffset
	);

	ensureMsgf(!OutProjectionMatrix.ContainsNaN(), TEXT("Nans found on ProjectionMatrix"));
	if (OutProjectionMatrix.ContainsNaN())
	{
		OutProjectionMatrix.SetIdentity();
	}
}
#endif