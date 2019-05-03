// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "TimeSynthClip.h"
#include "TimeSynthComponent.h"
#include "Sound/SoundWave.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"


UClass* FAssetTypeActions_TimeSynthClip::GetSupportedClass() const
{
	return UTimeSynthClip::StaticClass();
}

UTimeSynthClipFactory::UTimeSynthClipFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UTimeSynthClip::StaticClass();

	bCreateNew = true;
	bEditorImport = false;
	bEditAfterNew = true;
}

UObject* UTimeSynthClipFactory::FactoryCreateNew(UClass* Class, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn)
{
	UTimeSynthClip* NewTimeSynthClip = NewObject<UTimeSynthClip>(InParent, InName, Flags);

	for (TWeakObjectPtr<USoundWave>& SoundWave : SoundWaves)
	{
		if (SoundWave.IsValid())
		{
			FTimeSynthClipSound Sound;
			Sound.SoundWave = SoundWave.Get();
			NewTimeSynthClip->Sounds.Add(Sound);
		}
	}
	
	return NewTimeSynthClip;
}