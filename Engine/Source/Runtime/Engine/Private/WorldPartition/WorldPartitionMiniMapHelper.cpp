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
#include "TextureCompiler.h"
#include "DistanceFieldAtlas.h"
#include "PackageSourceControlHelper.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "WorldPartition/WorldPartitionMiniMap.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionMiniMapHelper, All, All);

AWorldPartitionMiniMap* FWorldPartitionMiniMapHelper::GetWorldPartitionMiniMap(UWorld* World, bool bCreateNewMiniMap)
{
	if (!World->GetWorldPartition())
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

void FWorldPartitionMiniMapHelper::CaptureWorldMiniMapToTexture(UWorld* InWorld, UObject* InOuterForTexture, uint32 InMiniMapSize, UTexture2D*& InOutMiniMapTexture, FBox& OutWorldBounds)
{
	auto WaitForShaderCompilation = [](){
		UE_SCOPED_TIMER(TEXT("Shader Compilation"), LogWorldPartitionMiniMapHelper, Display);
		auto RemainingShaders = GShaderCompilingManager->GetNumRemainingJobs();
		if (RemainingShaders > 0)
		{
			GShaderCompilingManager->FinishAllCompilation();
		}
	};
	auto WaitForTextureBuilding = []()
	{
		UE_SCOPED_TIMER(TEXT("Building Textures"), LogWorldPartitionMiniMapHelper, Display);
		auto RemainingTextures = FTextureCompilingManager::Get().GetNumRemainingTextures();
		if (RemainingTextures > 0)
		{
			FTextureCompilingManager::Get().FinishAllCompilation();
		}
	};
	auto WaitForDistanceMeshFieldBuilding = []() {
		UE_SCOPED_TIMER(TEXT("Building Mesh Distance Fields"), LogWorldPartitionMiniMapHelper, Display);
		if (GDistanceFieldAsyncQueue && GDistanceFieldAsyncQueue->GetNumOutstandingTasks() > 0)
		{
			GDistanceFieldAsyncQueue->BlockUntilAllBuildsComplete();
		}
	};

	WaitForDistanceMeshFieldBuilding();
	WaitForTextureBuilding();
	WaitForShaderCompilation();
	
	//Calculate bounds of the World
	OutWorldBounds = FWorldPartitionMiniMapHelper::GetWorldBounds(InWorld);
	
	//Calculate Viewport size
	FBox2D WorldBounds2D(FVector2D(OutWorldBounds.Min), FVector2D(OutWorldBounds.Max));
	FVector2D ViewSize = WorldBounds2D.Max - WorldBounds2D.Min;
	float AspectRatio = FMath::Abs(ViewSize.X) / FMath::Abs(ViewSize.Y);
	uint32 ViewportWidth = InMiniMapSize * AspectRatio;
	uint32 ViewportHeight = InMiniMapSize;

	//Calculate Projection matrix based on world bounds.
	FMatrix ProjectionMatrix;
	FWorldPartitionMiniMapHelper::CalTopViewOfWorld(ProjectionMatrix, OutWorldBounds, ViewportWidth, ViewportHeight);

	TArray<FColor> CapturedImage;
	//Using SceneCapture Actor capture the scene to buffer
	{
		UTextureRenderTarget2D* RenderTargetTexture = NewObject<UTextureRenderTarget2D>();
		RenderTargetTexture->AddToRoot();
		RenderTargetTexture->ClearColor = FLinearColor::Transparent;
		RenderTargetTexture->TargetGamma = 2.2f;
		RenderTargetTexture->InitCustomFormat(InMiniMapSize, InMiniMapSize, PF_B8G8R8A8, false);

		FActorSpawnParameters SpawnInfo;
		SpawnInfo.ObjectFlags |= RF_Transient;
		FVector CaptureActorLocation = FVector(OutWorldBounds.GetCenter().X, OutWorldBounds.GetCenter().Y, OutWorldBounds.GetCenter().Z + OutWorldBounds.GetExtent().Z);
		FRotator CaptureActorRotation = FRotator(-90.f, 0.f, -90.f);
		ASceneCapture2D* CaptureActor = InWorld->SpawnActor<ASceneCapture2D>(CaptureActorLocation, CaptureActorRotation, SpawnInfo);
		auto CaptureComponent = CaptureActor->GetCaptureComponent2D();
		CaptureComponent->TextureTarget = RenderTargetTexture;
		CaptureComponent->ProjectionType = ECameraProjectionMode::Orthographic;
		CaptureComponent->OrthoWidth = ViewportWidth;
		CaptureComponent->bUseCustomProjectionMatrix = true;
		CaptureComponent->CustomProjectionMatrix = ProjectionMatrix;
		CaptureComponent->CaptureScene();

		auto RenderTargetResource = RenderTargetTexture->GameThread_GetRenderTargetResource();
		CapturedImage.SetNumUninitialized(InMiniMapSize * InMiniMapSize);
		RenderTargetResource->ReadPixelsPtr(CapturedImage.GetData(), FReadSurfaceDataFlags(), FIntRect(0, 0, InMiniMapSize, InMiniMapSize));
		InWorld->DestroyActor(CaptureActor);
		CaptureActor = nullptr;
	}
	
	//Update the output texture
	if (!InOutMiniMapTexture)
	{
		InOutMiniMapTexture = NewObject<UTexture2D>(InOuterForTexture, "MiniMapTexture");
	}
	InOutMiniMapTexture->AdjustMinAlpha = 1.f;
	InOutMiniMapTexture->Source.Init(InMiniMapSize, InMiniMapSize, 1, 1, TSF_BGRA8, (uint8*)CapturedImage.GetData());
	InOutMiniMapTexture->UpdateResource();

}

bool FWorldPartitionMiniMapHelper::DoesActorContributeToBounds(AActor* Actor)
{
	return Actor && Actor->GridPlacement != EActorGridPlacement::AlwaysLoaded;
}

FBox FWorldPartitionMiniMapHelper::GetWorldBounds(UWorld* World)
{
	FBox WorldBox(ForceInit);

	TArray<ULevel*> LevelsToRender = World->GetLevels();
	for (ULevel* Level : LevelsToRender)
	{
		if (Level && Level->bIsVisible)
		{
			for (AActor* Actor : Level->Actors)
			{
				if ( Actor && DoesActorContributeToBounds(Actor))
				{
					FVector Origin, Extent;
					Actor->GetActorBounds(false, Origin, Extent);
					FBox ActorBounds(Origin - Extent, Origin + Extent);
					WorldBox += ActorBounds;
				}
			}
		}
	}

	return WorldBox;
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

	const float ZOffset = HALF_WORLD_MAX;
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