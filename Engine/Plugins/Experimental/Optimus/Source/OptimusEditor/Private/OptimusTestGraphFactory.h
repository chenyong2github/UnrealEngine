// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Factories/Factory.h"
#include "OptimusTestGraphFactory.generated.h"

UCLASS(hidecategories = Object)
class UOptimusTestGraphFactory : public UFactory
{
	GENERATED_BODY()

public:
	UOptimusTestGraphFactory();

	//~ Begin UFactory Interface.
	UObject* FactoryCreateNew(
		UClass* InClass, 
		UObject* InParent, 
		FName InName, 
		EObjectFlags Flags, 
		UObject* Context, 
		FFeedbackContext* Warn
		) override;
	//~ End UFactory Interface.
};
