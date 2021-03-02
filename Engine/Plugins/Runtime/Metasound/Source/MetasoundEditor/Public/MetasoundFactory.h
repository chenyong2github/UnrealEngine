// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "UObject/ObjectMacros.h"

#include "MetasoundFactory.generated.h"

// TODO: Re-enable and potentially rename once composition is supported
// UCLASS(hidecategories=Object, MinimalAPI)
// class UMetasoundFactory : public UFactory
// {
// 	GENERATED_UCLASS_BODY()
// 
// 	//~ Begin UFactory Interface
// 	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName Name, EObjectFlags Flags, UObject* InContext, FFeedbackContext* InFeedbackContext) override;
// 	//~ Begin UFactory Interface
// };

UCLASS(hidecategories = Object, MinimalAPI)
class UMetasoundSourceFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName Name, EObjectFlags Flags, UObject* InContext, FFeedbackContext* InFeedbackContext) override;
	//~ Begin UFactory Interface
};
