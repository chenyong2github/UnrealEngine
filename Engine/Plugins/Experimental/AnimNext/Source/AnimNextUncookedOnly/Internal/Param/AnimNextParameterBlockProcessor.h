
#pragma once

#include "CoreMinimal.h"
#include "AnimNextParameterBlockEntry.h"
#include "AnimNextParameterBlockProcessor.generated.h"

/** Block entry that performs arbitrary logic at the point the block is applied */
UCLASS(MinimalAPI)
class UAnimNextParameterBlockProcessor : public UAnimNextParameterBlockEntry
{
	GENERATED_BODY()
};