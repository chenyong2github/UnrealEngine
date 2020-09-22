// Copyright Epic Games, Inc. All Rights Reserved.
#include "SoundClassTemplates.h"

USoundEffectSourcePresetClassTemplate::USoundEffectSourcePresetClassTemplate(const FObjectInitializer& ObjectInitializer)
	: UClassTemplate(ObjectInitializer)
{
	SetGeneratedBaseClass(USoundEffectSourcePreset::StaticClass());
}

USoundEffectSubmixPresetClassTemplate::USoundEffectSubmixPresetClassTemplate(const FObjectInitializer& ObjectInitializer)
	: UClassTemplate(ObjectInitializer)
{
	SetGeneratedBaseClass(USoundEffectSubmixPreset::StaticClass());
}

USynthComponentClassTemplate::USynthComponentClassTemplate(const FObjectInitializer& ObjectInitializer)
	: UClassTemplate(ObjectInitializer)
{
	SetGeneratedBaseClass(USynthComponent::StaticClass());
}
