// Copyright 2022 (c) Microsoft. All rights reserved.
// Licensed under the MIT License.

#pragma once

#include "CoreMinimal.h"
#include "VisualStudioToolsCommandletBase.h"

#include "VisualStudioToolsCommandlet.generated.h"

UCLASS()
class UVisualStudioToolsCommandlet
	: public UVisualStudioToolsCommandletBase
{
	GENERATED_BODY()

public:
	UVisualStudioToolsCommandlet();
	int32 Run(
		TArray<FString>& Tokens,
		TArray<FString>& Switches,
		TMap<FString, FString>& ParamVals,
		FArchive& OutArchive) override;
};
