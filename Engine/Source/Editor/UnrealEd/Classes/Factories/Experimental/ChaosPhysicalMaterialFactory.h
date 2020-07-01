// Copyright Epic Games, Inc. All Rights Reserved.

/** Factory which allows import of an ChaosPhysicalMaterialAsset */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "Chaos/ChaosPhysicalMaterial.h"

#include "ChaosPhysicalMaterialFactory.generated.h"


/**
* Factory for Chaos Physical Material
*/

UCLASS()
class UNREALED_API UChaosPhysicalMaterialFactory : public UFactory
{
    GENERATED_UCLASS_BODY()

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	//~ Begin UFactory Interface	

	static UChaosPhysicalMaterial* StaticFactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn);

};
