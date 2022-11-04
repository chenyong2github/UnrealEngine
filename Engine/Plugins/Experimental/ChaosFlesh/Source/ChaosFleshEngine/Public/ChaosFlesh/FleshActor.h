// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"
#include "ChaosFlesh/FleshComponent.h"

#include "FleshActor.generated.h"



UCLASS()
class CHAOSFLESHENGINE_API AFleshActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:

	/* FleshComponent */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = Destruction, meta = (ExposeFunctionCategories = "Components|Flesh", AllowPrivateAccess = "true"))
		TObjectPtr<UFleshComponent> FleshComponent;
	UFleshComponent* GetFleshComponent() const { return FleshComponent; }

	UFUNCTION(BlueprintCallable, Category = "Physics")
	void EnableSimulation(ADeformableSolverActor* Actor);

#if WITH_EDITOR
	virtual bool GetReferencedContentObjects(TArray<UObject*>& Objects) const override;
#endif
};
