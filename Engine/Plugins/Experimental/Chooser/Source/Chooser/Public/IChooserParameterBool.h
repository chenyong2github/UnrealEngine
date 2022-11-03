// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "IChooserParameterBool.generated.h"

UINTERFACE(NotBlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class CHOOSER_API UChooserParameterBool : public UInterface
{
	GENERATED_BODY()
};

class CHOOSER_API IChooserParameterBool
{
	GENERATED_BODY()

public:
	virtual bool GetValue(const UObject* ContextObject, bool& OutResult) const { return false; }
};
