// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

//~=============================================================================
// LevelFactory
//~=============================================================================

#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"

#include "DataPrepFactories.generated.h"

UCLASS(MinimalAPI)
class UDataprepAssetFactory : public UFactory
{
	GENERATED_BODY()

public:
	UDataprepAssetFactory();

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	//~ Begin UFactory Interface	
};



