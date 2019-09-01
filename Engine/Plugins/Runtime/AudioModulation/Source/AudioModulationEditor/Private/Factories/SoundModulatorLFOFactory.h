// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

//=============================================================================
// USoundModulatorLFOFactory
//=============================================================================

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "SoundModulatorLFOFactory.generated.h"

UCLASS(hidecategories=Object, MinimalAPI)
class USoundModulatorLFOFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
};
