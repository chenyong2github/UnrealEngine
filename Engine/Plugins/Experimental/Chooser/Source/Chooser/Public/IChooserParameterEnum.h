// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UObject/Interface.h"
#include "IChooserParameterEnum.generated.h"

UINTERFACE(NotBlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class CHOOSER_API UChooserParameterEnum : public UInterface
{
	GENERATED_BODY()
};

class CHOOSER_API IChooserParameterEnum
{
	GENERATED_BODY()

public:
	virtual bool GetValue(const UObject* ContextObject, uint8& OutResult) const { return false; }

#if WITH_EDITOR
	virtual const UEnum* GetEnum() const { return nullptr; }
	virtual FSimpleMulticastDelegate& OnEnumChanged() = 0;
#endif
};