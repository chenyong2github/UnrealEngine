// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SoundModulatorLFOFactory.h"
#include "SoundModulatorLFO.h"


USoundModulatorLFOFactory::USoundModulatorLFOFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundBusModulatorLFO::StaticClass();
	bCreateNew     = true;
	bEditorImport  = false;
	bEditAfterNew  = true;
}

UObject* USoundModulatorLFOFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USoundBusModulatorLFO>(InParent, Name, Flags);
}
