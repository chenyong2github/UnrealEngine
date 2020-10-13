// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "ClassTemplateEditorSubsystem.h"
#include "Components/SynthComponent.h"
#include "CoreMinimal.h"
#include "Sound/SoundEffectSource.h"
#include "Sound/SoundEffectSubmix.h"

#include "SoundClassTemplates.generated.h"


UCLASS()
class USoundEffectSourcePresetClassTemplate : public UClassTemplate
{
	GENERATED_UCLASS_BODY()

public:
	FString GetFilename() const override
	{
		return TEXT("SoundEffectSourceClass");
	}
};

UCLASS()
class USoundEffectSubmixPresetClassTemplate : public UClassTemplate
{
	GENERATED_UCLASS_BODY()

public:
	FString GetFilename() const override
	{
		return TEXT("SoundEffectSubmixClass");
	}
};

UCLASS()
class USynthComponentClassTemplate : public UClassTemplate
{
	GENERATED_UCLASS_BODY()

public:
	FString GetFilename() const override
	{
		return TEXT("SynthComponentClass");
	}
};