// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "Texture2DArrayFactory.generated.h"

class UTexture2D;

/** Factory for creating volume texture */
UCLASS(hidecategories=Object, MinimalAPI)
class UTexture2DArrayFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	UPROPERTY()
	TArray<UTexture2D*> InitialTextures;

	//~ Begin UFactory Interface
	virtual FText GetDisplayName() const override;
	virtual bool ConfigureProperties() override;
	virtual UObject* FactoryCreateNew(UClass* Class,UObject* InParent,FName Name,EObjectFlags Flags,UObject* Context,FFeedbackContext* Warn) override;
	//~ Begin UFactory Interface

	bool CheckArrayTexturesCompatibility();
};



