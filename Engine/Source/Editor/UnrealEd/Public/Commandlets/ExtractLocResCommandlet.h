// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "ExtractLocResCommandlet.generated.h"

/**
 * Commandlet to extract the localization data from a binary LocRes file and dump it as human readable CSV.
 */
UCLASS()
class UExtractLocResCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:

	virtual int32 Main(const FString& Params);
};
