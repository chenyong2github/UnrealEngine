// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * Factory for LandscapeLayerInfoObject assets
 */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "LandscapeLayerInfoObjectFactory.generated.h"

UCLASS()
class ULandscapeLayerInfoObjectFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	// UFactory interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	// End of UFactory interface
};
