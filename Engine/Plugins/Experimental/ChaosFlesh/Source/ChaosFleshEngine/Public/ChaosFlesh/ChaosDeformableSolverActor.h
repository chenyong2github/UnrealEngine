// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/BillboardComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/ObjectMacros.h"
#include "UObject/ObjectPtr.h"

#include "ChaosDeformableSolverActor.generated.h"

class UDeformableSolverComponent;

UCLASS()
class CHAOSFLESHENGINE_API ADeformableSolverActor : public AActor
{
	GENERATED_UCLASS_BODY()

public:

	UDeformableSolverComponent* GetDeformableSolverComponent() { return SolverComponent; }
	const UDeformableSolverComponent* GetDeformableSolverComponent() const { return SolverComponent; }

	UPROPERTY(Category = "Chaos Deformable", VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UDeformableSolverComponent> SolverComponent = nullptr;

	/*
	* Display icon in the editor
	*/
	UPROPERTY()
	TObjectPtr<UBillboardComponent> SpriteComponent;

private:
	void CreateBillboardIcon(const FObjectInitializer& ObjectInitializer);

};