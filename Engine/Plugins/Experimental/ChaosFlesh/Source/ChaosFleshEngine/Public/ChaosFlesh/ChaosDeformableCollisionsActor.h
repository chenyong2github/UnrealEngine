// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "ChaosFlesh/ChaosDeformableCollisionsComponent.h"
#include "ChaosFlesh/ChaosDeformableSolverActor.h"

#include "ChaosDeformableCollisionsActor.generated.h"

class AStaticMeshActor;


UCLASS()
class CHAOSFLESHENGINE_API ADeformableCollisionsActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Physics")
	void EnableSimulation(ADeformableSolverActor* Actor);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chaos Deformable")
	TObjectPtr<UDeformableCollisionsComponent> DeformableCollisionsComponent;
	UDeformableCollisionsComponent* GetCollisionsComponent() const { return DeformableCollisionsComponent; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Deformable")
	TObjectPtr<ADeformableSolverActor> PrimarySolver;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Deformable")
	TArray<TObjectPtr<AStaticMeshActor>> StaticCollisions;

#if WITH_EDITOR
	TArray < TObjectPtr<AStaticMeshActor> > AddedBodies;
	TArray < TObjectPtr<AStaticMeshActor> > RemovedBodies;
	TArray < TObjectPtr<AStaticMeshActor> > PreEditChangeCollisionBodies;
	ADeformableSolverActor* PreEditChangePrimarySolver = nullptr;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};
