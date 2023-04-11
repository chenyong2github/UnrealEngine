// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "UObject/Object.h"
#include "EditorConfigBase.h"
#include "Engine/DeveloperSettings.h"

#include "PresetSettings.generated.h"

/**
 * Implements the settings for the PresetEditor.
 */
UCLASS(EditorConfig = "UPresetUserSettings")
class PRESETEDITOR_API UPresetUserSettings : public UEditorConfigBase
{
public:
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category = "Interactive Tool Presets|Collections", meta = (EditorConfig, AllowedClasses = "/Script/PresetAsset.InteractiveToolsPresetCollectionAsset"))
	TSet<FSoftObjectPath> EnabledPresetCollections;// = { FSoftObjectPath(FTopLevelAssetPath("/ToolPresets/Presets/_DefaultCollection", "_DefaultCollection")) };

	// TODO: Ideally the above property would be able to store if the default collection was enabled or not.
	// However, currently the EditorConfig system has some trouble with defaults and array/set properties.
	// To overcome this, we store it separately which doesn't seem to break as badly.

	UPROPERTY(VisibleAnywhere, Category = "Interactive Tool Presets|Collections", meta = (EditorConfig, AllowedClasses = "/Script/PresetAsset.InteractiveToolsPresetCollectionAsset"))
	FSoftObjectPath DefaultCollectionPath = FSoftObjectPath(FTopLevelAssetPath("/ToolPresets/Presets/_DefaultCollection", "_DefaultCollection"));

	UPROPERTY(EditAnywhere, Category = "Interactive Tool Presets|Collections", meta = (EditorConfig))
	bool bDefaultCollectionEnabled = true;

	static void Initialize();
	static UPresetUserSettings* Get();

private:
	static TObjectPtr<UPresetUserSettings> Instance;
};


/**
 * Implements the settings for the PresetEditor.
 */
UCLASS(config = Editor)
class PRESETEDITOR_API UPresetProjectSettings
	: public UDeveloperSettings
{
public:

	// UDeveloperSettings overrides

	virtual FName GetContainerName() const override { return FName("Project"); }
	virtual FName GetCategoryName() const override { return FName("Plugins"); }
	virtual FName GetSectionName() const override { return FName("Interactive Tool Presets"); }

	virtual FText GetSectionText() const override { return NSLOCTEXT("PresetSettings", "SectionText", "Interactive Tool Presets"); };
	virtual FText GetSectionDescription() const override { return NSLOCTEXT("PresetSettings", "SectionDescription", "Manage preset settings at the project level."); };

public:
	GENERATED_BODY()

	/* Controls which preset collection assets are to be loaded for this project.  */
	UPROPERTY(config, EditAnywhere, Category = "Interactive Tool Presets|Collections", meta = (AllowedClasses = "/Script/PresetAsset.InteractiveToolsPresetCollectionAsset"))
	TSet<FSoftObjectPath> LoadedPresetCollections;
};