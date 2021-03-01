// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "LensFile.h"

#include "LensDistortionSettings.generated.h"



/**
 * Settings for the Lensdistortion plugin modules. 
 */
UCLASS(config=Game)
class LENSDISTORTION_API ULensDistortionSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

	//~ Begin UDevelopperSettings interface
	virtual FName GetCategoryName() const;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
#endif
	//~ End UDevelopperSettings interface

public:

	/**
	 * Get the default startup lens file.
	 *
	 * @return The lens file, or nullptr if not set.
	 */
	ULensFile* GetStartupLensFile() const;

private:

	/** 
	 * Startup lens file for the project 
	 * Can be overriden. Priority of operation is
	 * 1. Apply startup lens file found in 'LensDistortion.StartupLensFile' cvar at launch
	 * 2. If none found, apply user startup file (only for editor runs)
	 * 3. If none found, apply projet startup file (this one)
	 */
	UPROPERTY(config, EditAnywhere, Category = "Settings", meta = (ConfigRestartRequired = true))
	TSoftObjectPtr<ULensFile> StartupLensFile;
};

/**
 * Settings for the lens distortion when in editor and standalone.
 * @note Cooked games don't use this setting.
 */
UCLASS(config = EditorPerProjectUserSettings)
class LENSDISTORTION_API ULensDistortionEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

		//~ Begin UDevelopperSettings interface
		virtual FName GetCategoryName() const;
#if WITH_EDITOR
		virtual FText GetSectionText() const override;
#endif
		//~ End UDevelopperSettings interface

public:

#if WITH_EDITORONLY_DATA

	/**
	 * True if a lens file button shortcut should be added to level editor toolbar.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Settings", Meta = (ConfigRestartRequired = true, DisplayName = "Enable Lens File Toolbar Button"))
	bool bShowEditorToolbarButton = false;

private:

	/** 
	 * Startup lens file per user in editor 
	 * Can be overridden. Priority of operation is
	 * 1. Apply startup lens file found in 'LensDistortion.StartupLensFile' cvar at launch
	 * 2. If none found, apply user startup file (this one)
	 * 3. If none found, apply project startup file
	 */
	UPROPERTY(config, EditAnywhere, Category = "Settings")
	TSoftObjectPtr<ULensFile> UserLensFile;
#endif

public:

	/**
	 * Get the lens file used by the engine when in the editor and standalone.
	 *
	 * @return The lens file, or nullptr if not set.
	 */
	ULensFile* GetUserLensFile() const;

	/** Set the lens file used by the engine when in the editor and standalone. */
	void SetUserLensFile(ULensFile* InLensFile);
};