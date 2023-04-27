// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDParticleActor.h"

#include "ChaosVDGeometryBuilder.h"
#include "ChaosVDScene.h"
#include "ChaosVDRecording.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"

AChaosVDParticleActor::AChaosVDParticleActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent0"));
}

void AChaosVDParticleActor::UpdateFromRecordedData(const FChaosVDParticleDebugData& RecordedData, const Chaos::FRigidTransform3& SimulationTransform)
{
	SetActorLocationAndRotation(SimulationTransform.TransformPosition(RecordedData.Position), SimulationTransform.GetRotation() * RecordedData.Rotation, false);

	// TODO: We should store a ptr to the data and in our custom details panel draw it, but the current data format is just a test
	// So we should do this once it is final
	RecordedDebugData = RecordedData;
}

void AChaosVDParticleActor::UpdateGeometryData(const TSharedPtr<const Chaos::FImplicitObject>& ImplicitObject)
{
	if (bIsGeometryDataLoaded)
	{
		return;
	}

	TArray<UMeshComponent*> OutGeneratedMeshComponents;

	if (const TSharedPtr<FChaosVDScene>& ScenePtr = OwningScene.Pin())
	{
		if (const TSharedPtr<FChaosVDGeometryBuilder>& GeometryGenerator = ScenePtr->GetGeometryGenerator())
		{
			Chaos::FRigidTransform3 Transform;
			GeometryGenerator->CreateMeshComponentsFromImplicit<UStaticMesh, UInstancedStaticMeshComponent>(ImplicitObject.Get(), this, OutGeneratedMeshComponents, Transform);
		}
	}
	
	if (OutGeneratedMeshComponents.Num() > 0)
	{
		bIsGeometryDataLoaded = true;
	}
}

void AChaosVDParticleActor::SetScene(const TSharedPtr<FChaosVDScene>& InScene)
{
	OwningScene = InScene;

	if (const TSharedPtr<FChaosVDScene>& ScenePtr = OwningScene.Pin())
	{
		GeometryUpdatedDelegate = ScenePtr->OnNewGeometryAvailable().AddWeakLambda(this, [this](const TSharedPtr<const Chaos::FImplicitObject>& ImplicitObject, const int32 ID)
		{
			if (RecordedDebugData.ImplicitObjectID == ID)
			{
				UpdateGeometryData(ImplicitObject);
			}
		});
	}
}


void AChaosVDParticleActor::BeginDestroy()
{
	if (const TSharedPtr<FChaosVDScene>& ScenePtr = OwningScene.Pin())
	{
		ScenePtr->OnNewGeometryAvailable().Remove(GeometryUpdatedDelegate);
	}

	Super::BeginDestroy();
}
