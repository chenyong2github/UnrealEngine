// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosVDRecording.h"
#include "GameFramework/Actor.h"

#include "ChaosVDParticleActor.generated.h"

struct FChaosVDParticleDebugData;
class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;

/** Actor used to represent a Chaos Particle in the Visual Debugger's world */
UCLASS(HideCategories=("Transform"))
class AChaosVDParticleActor : public AActor
{
	GENERATED_BODY()
public:
	AChaosVDParticleActor(const FObjectInitializer& ObjectInitializer);

	void UpdateFromRecordedData(const FChaosVDParticleDebugData& RecordedData);

	void SetStaticMesh(UStaticMesh* NewStaticMesh);

protected:

	UPROPERTY(EditDefaultsOnly, Category= Debug)
	TObjectPtr<UStaticMeshComponent> StaticMeshComponent;

	UPROPERTY(VisibleAnywhere, Category="Chaos Visual Debugger Data")
	FChaosVDParticleDebugData RecordedDebugData;
};
