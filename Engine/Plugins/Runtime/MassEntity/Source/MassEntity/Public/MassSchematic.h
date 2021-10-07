// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MassProcessingTypes.h"
#include "MassSchematic.generated.h"


class UMassProcessor;

/**
 * Mass Schematic asset.
 */
UCLASS(BlueprintType)
class MASSENTITY_API UMassSchematic : public UDataAsset
{
	GENERATED_BODY()

public:
	TConstArrayView<const UMassProcessor*> GetProcessors() const { return MakeArrayView(Processors);  }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Mass, Instanced)
	TArray<UMassProcessor*> Processors;
};
