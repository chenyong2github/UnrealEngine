// Copyright Epic Games, Inc. All Rights Reserved.

#include "WaterMeshActor.h"
#include "WaterSubsystem.h"
#include "WaterMeshComponent.h"
#include "WaterBodyActor.h"

#if	WITH_EDITOR
#include "Algo/Transform.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "WaterIconHelper.h"
#endif // WITH_EDITOR

AWaterMeshActor::AWaterMeshActor(const FObjectInitializer& Initializer)
{
	WaterMesh = CreateDefaultSubobject<UWaterMeshComponent>(TEXT("WaterMesh"));
	SetRootComponent(WaterMesh);

#if	WITH_EDITOR
	if (GIsEditor && !IsTemplate())
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
		LevelEditorModule.OnActorSelectionChanged().AddUObject(this, &AWaterMeshActor::OnActorSelectionChanged);
	}

	ActorIcon = FWaterIconHelper::EnsureSpriteComponentCreated(this, TEXT("/Water/Icons/WaterMeshActorSprite"));
#endif // WITH_EDITOR
}

void AWaterMeshActor::PostLoadSubobjects(FObjectInstancingGraph* OuterInstanceGraph)
{
	// Water mesh component was made new root component. Make sure it doesn't have a parent
	WaterMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	Super::PostLoadSubobjects(OuterInstanceGraph);
}

void AWaterMeshActor::MarkWaterMeshComponentForRebuild()
{
	WaterMesh->MarkWaterMeshGridDirty();
}

void AWaterMeshActor::Update()
{
	if (WaterMesh)
	{
		WaterMesh->Update();
	}
}

void AWaterMeshActor::SetLandscapeInfo(const FVector& InRTWorldLocation, const FVector& InRTWorldSizeVector)
{
	if (WaterMesh)
	{
		WaterMesh->SetLandscapeInfo(InRTWorldLocation, InRTWorldSizeVector);
	}
}

#if	WITH_EDITOR
void AWaterMeshActor::PostEditMove(bool bFinished)
{
	Super::PostEditMove(bFinished);

	// Ensure that the water mesh is rebuilt if it moves
	WaterMesh->MarkWaterMeshGridDirty();
}

void AWaterMeshActor::OnActorSelectionChanged(const TArray<UObject*>& NewSelection, bool bForceRefresh)
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
