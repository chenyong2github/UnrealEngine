// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoundModulationGeneratorLFOFactory.h"
#include "SoundModulationGeneratorLFO.h"


USoundModulationGeneratorLFOFactory::USoundModulationGeneratorLFOFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundModulationGeneratorLFO::StaticClass();
	bCreateNew     = true;
	bEditorImport  = false;
	bEditAfterNew  = true;
}

UObject* USoundModulationGeneratorLFOFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	return NewObject<USoundModulationGeneratorLFO>(InParent, Name, Flags);
}
