// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "UObject/ObjectMacros.h"

#include "NiagaraStackSection.generated.h"

/* Defines data for sections visible in the stack view. */
USTRUCT()
struct FNiagaraStackSection 
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category=Section)
	FText SectionDisplayName;

	UPROPERTY(EditAnywhere, Category=Section)
	TArray<FText> Categories;
};