// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "ChaosFlesh/FleshComponent.h"
#include "ChaosFlesh/ChaosDeformableSolverActor.h"

#include "FleshActor.generated.h"



UCLASS()
class CHAOSFLESHENGINE_API AFleshActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "Physics")
	void EnableSimulation(ADeformableSolverActor* Actor);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Chaos Deformable")
	TObjectPtr<UFleshComponent> FleshComponent;
	UFleshComponent* GetFleshComponent() const { return FleshComponent; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chaos Deformable")
	TObjectPtr<ADeformableSolverActor> PrimarySolver;

#if WITH_EDITOR
	ADeformableSolverActor* PreEditChangePrimarySolver = nullptr;
	virtual void PreEditChange(FProperty* PropertyThatWillChange) override;
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool GetReferencedContentObjects(TArray<UObject*>& Objects) const override;
#endif
};
