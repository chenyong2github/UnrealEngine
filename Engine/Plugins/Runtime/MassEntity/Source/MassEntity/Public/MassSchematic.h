// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "MassEntityTypes.h"
#include "MassSchematic.generated.h"


class UPipeProcessor;

/**
 * Pipe Schematic asset.
 */
UCLASS(BlueprintType)
class MASSENTITY_API UPipeSchematic : public UDataAsset
{
	GENERATED_BODY()

public:
	TConstArrayView<const UPipeProcessor*> GetProcessors() const { return MakeArrayView(Processors);  }

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = Pipe, Instanced)
	TArray<UPipeProcessor*> Processors;
};
