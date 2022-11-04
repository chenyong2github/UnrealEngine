// Copyright Epic Games, Inc. All Rights Reserved.

/** Factory which allows import of an ChaosSolverAsset */

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"
#include "ChaosFlesh/ChaosDeformableSolverAsset.h"

#include "ChaosAssetDeformableSolverFactory.generated.h"


/**
* Factory for Simple Cube
*/

UCLASS()
class CHAOSFLESHEDITOR_API UChaosDeformableSolverFactory : public UFactory
{
    GENERATED_UCLASS_BODY()

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	static UChaosDeformableSolver* StaticFactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn);
};


