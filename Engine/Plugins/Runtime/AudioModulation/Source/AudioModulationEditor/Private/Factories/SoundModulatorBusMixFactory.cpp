// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SoundModulatorBusMixFactory.h"
#include "SoundModulatorBusMix.h"


USoundModulatorBusMixFactory::USoundModulatorBusMixFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundModulatorBusMix::StaticClass();
	bCreateNew     = true;
	bEditorImport  = false;
	bEditAfterNew  = true;
}

UObject* USoundModulatorBusMixFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USoundModulatorBusMix>(InParent, Name, Flags);
}
