// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "ChaosVDRecording.h"
#include "Chaos/Core.h"
#include "GameFramework/Actor.h"

#include "ChaosVDParticleActor.generated.h"

class FChaosVDScene;
struct FChaosVDParticleDebugData;
class USceneComponent;
class UStaticMeshComponent;
class UStaticMesh;

/** Actor used to represent a Chaos Particle in the Visual Debugger's world */
UCLASS()
class AChaosVDParticleActor : public AActor
{
	GENERATED_BODY()
public:
	AChaosVDParticleActor(const FObjectInitializer& ObjectInitializer);

	void UpdateFromRecordedData(const FChaosVDParticleDebugData& RecordedData, const Chaos::FRigidTransform3& SimulationTransform);
	
	const FChaosVDParticleDebugData& GetDebugData() const { return RecordedDebugData; }

	void UpdateGeometryData(const TSharedPtr<const Chaos::FImplicitObject>& ImplicitObject);

	void SetScene(const TSharedPtr<FChaosVDScene>& InScene);

	virtual void BeginDestroy() override;

protected:

	UPROPERTY(VisibleAnywhere, Category="Chaos Visual Debugger Data")
	FChaosVDParticleDebugData RecordedDebugData;

	bool bIsGeometryDataGenerationStarted = false;

	TWeakPtr<FChaosVDScene> OwningScene;

	FDelegateHandle GeometryUpdatedDelegate;
};
