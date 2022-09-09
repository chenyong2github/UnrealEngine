// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "IObjectChooser.generated.h"

UINTERFACE(NotBlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class CHOOSER_API UObjectChooser : public UInterface
{
	GENERATED_BODY()
};

class CHOOSER_API IObjectChooser
{
	GENERATED_BODY()

public:

	virtual UObject* ChooseObject(const UObject* ContextObject) const { return nullptr; };
};
