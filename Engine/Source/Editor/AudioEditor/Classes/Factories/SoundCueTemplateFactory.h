// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Factories/Factory.h"

#include "SoundCueTemplateFactory.generated.h"

// Forward Declarations
class USoundCue;
class USoundCueTemplate;
class USoundWave;

UCLASS(hidecategories = Object, MinimalAPI)
class USoundCueTemplateCopyFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	//~ Begin UFactory Interface

public:
	UPROPERTY()
	TWeakObjectPtr<USoundCueTemplate> SoundCueTemplate;
};

UCLASS(hidecategories = Object, MinimalAPI)
class USoundCueTemplateFactory : public UFactory
{
	GENERATED_UCLASS_BODY()

	//~ Begin UFactory Interface
	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn) override;
	//~ Begin UFactory Interface
};