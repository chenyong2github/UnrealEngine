// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterZoneActor.h"
#include "WaterSubsystem.h"
#include "WaterMeshComponent.h"
#include "WaterBodyActor.h"
#include "Components/BoxComponent.h"

#if	WITH_EDITOR
#include "Algo/Transform.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "WaterIconHelper.h"
#endif // WITH_EDITOR

AWaterZone::AWaterZone(const FObjectInitializer& Initializer)
{
	WaterMesh = CreateDefaultSubobject<UWaterMeshComponent>(TEXT("WaterMesh"));
	SetRootComponent(WaterMesh);

	BoundsComponent = CreateDefaultSubobject<UBoxComponent>(TEXT("BoundsComponent"));
	BoundsComponent->SetCollisionObjectType(ECC_WorldStatic);
	BoundsComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	BoundsComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BoundsComponent->SetGenerateOverlapEvents(false);
	BoundsComponent->SetupAttachment(WaterMesh);
	BoundsComponent->SetBoxExtent(FVector(WaterMesh->GetExtentInTiles() * WaterMesh->GetTileSize(), 51200.f));

#if WITH_EDITOR
	if (GIsEditor && !IsTemplate())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.OnActorSelectionChanged().AddUObject(this, &AWaterZone::OnActorSelectionChanged);
	}

	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterZoneActorSprite"));
#endif
}

void AWaterZone::PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph)
{
	// Water mesh component was made new root component. Make sure it doesn't have a parent
	WaterMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	Super::PostLoadSubobjects(OuterInstanceGraph);
}

void AWaterZone::MarkWaterMeshComponentForRebuild()
{
	WaterMesh->MarkWaterMeshGridDirty();
}

void AWaterZone::Update()
{
	if (WaterMesh)
	{
		WaterMesh->Update();
	}
}

void AWaterZone::SetLandscapeInfo(const FVector& InRTWorldLocation, const FVector& InRTWorldSizeVector)
{
	if (WaterMesh)
	{
		WaterMesh->SetLandscapeInfo(InRTWorldLocation, InRTWorldSizeVector);
	}
}

#if	WITH_EDITOR
void AWaterZone::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Ensure that the water mesh is rebuilt if it moves
	MarkWaterMeshComponentForRebuild();
}

void AWaterZone::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName() == GET_MEMBER_NAME_CHECKED(AWaterZone, BoundsComponent))
	{
		OnBoundsChanged();
	}
}

void AWaterZone::OnActorSelectionChanged(const TArray<UObject*>& NewSelection, bool bForceRefresh)
{
	TArray<AWaterBody*> NewWaterBodiesSelection;
	Algo::TransformIf(NewSelection, NewWaterBodiesSelection, [](UObject* Obj) { return Obj->IsA<AWaterBody>(); }, [](UObject* Obj) { return static_cast<AWaterBody*>(Obj); });
	NewWaterBodiesSelection.Sort();
	TArray<TWeakObjectPtr<AWaterBody>> NewWeakWaterBodiesSelection;
	NewWeakWaterBodiesSelection.Reserve(NewWaterBodiesSelection.Num());
	Algo::Transform(NewWaterBodiesSelection, NewWeakWaterBodiesSelection, [](AWaterBody* Body) { return TWeakObjectPtr<AWaterBody>(Body); });

	// Ensure that the water mesh is rebuilt if water body selection changed
	if (SelectedWaterBodies != NewWeakWaterBodiesSelection)
	{
		SelectedWaterBodies = NewWeakWaterBodiesSelection;
		MarkWaterMeshComponentForRebuild();
	}
}
#endif // WITH_EDITOR

void AWaterZone::OnBoundsChanged()
{
	const float MeshTileSize = WaterMesh->GetTileSize();

	// Compute the new tile extent based on the new bounds
	const FVector NewBounds = BoundsComponent->GetUnscaledBoxExtent();

	const int32 NewTileExtentX = FMath::FloorToInt(NewBounds.X / MeshTileSize);
	const int32 NewTileExtentY = FMath::FloorToInt(NewBounds.Y / MeshTileSize);

	WaterMesh->SetExtentInTiles(FIntPoint(NewTileExtentX, NewTileExtentY));
	MarkWaterMeshComponentForRebuild();
}