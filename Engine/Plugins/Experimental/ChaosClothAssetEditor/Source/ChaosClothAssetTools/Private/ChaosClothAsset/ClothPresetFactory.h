// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"

#include "ClothPresetFactory.generated.h"

/**
 * Having a cloth factory allows the cloth preset to be created from the Editor's menus.
 */
UCLASS(Experimental)
class UChaosClothPresetFactory : public UFactory
{
	GENERATED_BODY()
public:
	UChaosClothPresetFactory(const FObjectInitializer& ObjectInitializer);

	/** UFactory Interface */
	virtual bool CanCreateNew() const override { return true; }
	virtual bool FactoryCanImport(const FString& Filename) override { return false; }
	virtual bool ShouldShowInNewMenu() const override { return true; }
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	/** End UFactory Interface */
};
