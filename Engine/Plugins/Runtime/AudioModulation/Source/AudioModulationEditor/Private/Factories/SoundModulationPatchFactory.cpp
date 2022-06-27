// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundModulationPatchFactory.h"

#include "EngineAnalytics.h"
#include "SoundModulationPatch.h"


USoundModulationPatchFactory::USoundModulationPatchFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundModulationPatch::StaticClass();
	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* USoundModulationPatchFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Audio.Usage.AudioModulation.ParameterPatchCreated"));
	}
	return NewObject<USoundModulationPatch>(InParent, Name, Flags);
}
