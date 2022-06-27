// Copyright Epic Games, Inc. All Rights Reserved.

#include "SoundControlBusMixFactory.h"

#include "EngineAnalytics.h"
#include "SoundControlBusMix.h"


USoundControlBusMixFactory::USoundControlBusMixFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USoundControlBusMix::StaticClass();
	bCreateNew     = true;
	bEditorImport  = false;
	bEditAfterNew  = true;
}

UObject* USoundControlBusMixFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	if (FEngineAnalytics::IsAvailable())
	{
		FEngineAnalytics::GetProvider().RecordEvent(TEXT("Audio.Usage.AudioModulation.ControlBusMixCreated"));
	}
	return NewObject<USoundControlBusMix>(InParent, Name, Flags);
}
