// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"
#include "WidgetBlueprint.h"

#include "AudioEditorSettings.generated.h"


UCLASS(config = EditorSettings, defaultconfig, meta = (DisplayName = "Submix Editor"))
class AUDIOEDITOR_API UAudioSubmixEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// Which UMG widget to use to draw the submix graph node
	UPROPERTY(config, EditAnywhere, Category = "Submix Graph", meta = (AllowedClasses = "WidgetBlueprint"))
	FSoftObjectPath SubmixNodeWidget;

	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("Audio"); }
	//~ End UDeveloperSettings

	//~ Begin UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject
};

UCLASS(config = EditorSettings, defaultconfig, meta = (DisplayName = "Metasound Editor"))
class AUDIOEDITOR_API UAudioMetasoundEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("Audio"); }
	//~ End UDeveloperSettings

	//~ Begin UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject
};

UCLASS(config = EditorSettings, defaultconfig, meta = (DisplayName = "SoundClass Editor"))
class AUDIOEDITOR_API UAudioSoundClassEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	//~ Begin UDeveloperSettings
	virtual FName GetCategoryName() const override { return TEXT("Audio"); }
	//~ End UDeveloperSettings

	//~ Begin UObject
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	//~ End UObject
};

