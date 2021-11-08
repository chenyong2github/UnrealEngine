// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "GameFramework/Actor.h"
#include "WorldBuildingNonOFPATestActor.generated.h"

UCLASS()
class AWorldBuildingNonOFPATestActor : public AActor
{
	GENERATED_UCLASS_BODY()

#if WITH_EDITOR
	virtual bool SupportsExternalPackaging() const override { return false; }
#endif
};