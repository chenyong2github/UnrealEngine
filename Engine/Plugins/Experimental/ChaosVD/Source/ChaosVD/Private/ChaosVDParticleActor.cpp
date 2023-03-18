// Copyright Epic Games, Inc. All Rights Reserved.

#include "ChaosVDParticleActor.h"
#include "ChaosVDRecording.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"

AChaosVDParticleActor::AChaosVDParticleActor(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("SceneComponent0"));;
	
	//Note: This static mesh component is just a place holder for debugging purposes 
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMeshComponent0"));
	StaticMeshComponent->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	StaticMeshComponent->SetGenerateOverlapEvents(false);
	StaticMeshComponent->SetupAttachment(RootComponent);

	StaticMeshComponent->Mobility = EComponentMobility::Movable;
	StaticMeshComponent->bUseDefaultCollision = false;
	StaticMeshComponent->bCastStaticShadow = false;
	StaticMeshComponent->bCastDynamicShadow = false;
	StaticMeshComponent->bSelectable = true;
}

void AChaosVDParticleActor::UpdateFromRecordedData(const FChaosVDParticleDebugData& RecordedData)
{
	SetActorLocation(RecordedData.Position);
	SetActorRotation(RecordedData.Rotation);

	// TODO: We should store a ptr to the data and in our custom details panel draw it, but the current data format is just a test
	// So we should do this once it is final
	RecordedDebugData = RecordedData;
}

void AChaosVDParticleActor::SetStaticMesh(UStaticMesh* NewStaticMesh)
{
	if (ensure(StaticMeshComponent))
	{
		StaticMeshComponent->SetStaticMesh(NewStaticMesh);
	}
}
