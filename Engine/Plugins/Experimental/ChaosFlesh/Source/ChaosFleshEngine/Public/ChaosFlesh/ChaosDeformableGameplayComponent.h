// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "ChaosFlesh/FleshComponent.h"
#include "UObject/ObjectMacros.h"


#include "ChaosDeformableGameplayComponent.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
*	UDeformableGameplayComponent
*/
UCLASS(meta = (BlueprintSpawnableComponent))
class CHAOSFLESHENGINE_API UDeformableGameplayComponent : public UFleshComponent
{
	GENERATED_UCLASS_BODY()

public:
	~UDeformableGameplayComponent();

};

