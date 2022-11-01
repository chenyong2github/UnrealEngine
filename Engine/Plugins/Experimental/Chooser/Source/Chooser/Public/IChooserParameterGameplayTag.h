// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "IChooserParameterGameplayTag.generated.h"

struct FGameplayTagContainer;

UINTERFACE(NotBlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class CHOOSER_API UChooserParameterGameplayTag : public UInterface
{
	GENERATED_BODY()
};

class CHOOSER_API IChooserParameterGameplayTag
{
	GENERATED_BODY()

public:
	virtual bool GetValue(const UObject* ContextObject, const FGameplayTagContainer*& OutResult) const { return false; }
};