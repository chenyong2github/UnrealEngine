// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "LensDistortionModelHandlerBase.h"
#include "LensFile.h"

#include "CameraCalibrationSettings.generated.h"



/**
 * Settings for the CameraCalibration plugin modules. 
 */
UCLASS(config=Game)
class CAMERACALIBRATIONCORE_API UCameraCalibrationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCameraCalibrationSettings();

	//~ Begin UDevelopperSettings interface
	virtual FName GetCategoryName() const;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FName GetSectionName() const override;
#endif
	//~ End UDevelopperSettings interface

public:

	/**
	 * Get the default startup lens file.
	 *
	 * @return The lens file, or nullptr if not set.
	 */
	ULensFile* GetStartupLensFile() const;

	/** Get the default MaterialInterface used by the input Model Handler class to write the undistortion displacement map */
	UMaterialInterface* GetDefaultUndistortionDisplacementMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const;

	/** Get the default MaterialInterface used by the input Model Handler class to write the distortion displacement map */
	UMaterialInterface* GetDefaultDistortionDisplacementMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const;

	/** Get the default MaterialInterface used by the input Model Handler class to apply the post-process lens distortion effect */
	UMaterialInterface* GetDefaultDistortionMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const;

private:

	/** 
	 * Startup lens file for the project 
	 * Can be overriden. Priority of operation is
	 * 1. Apply startup lens file found in 'CameraCalibration.StartupLensFile' cvar at launch
	 * 2. If none found, apply user startup file (only for editor runs)
	 * 3. If none found, apply projet startup file (this one)
	 */
	UPROPERTY(config, EditAnywhere, Category = "Settings", meta = (ConfigRestartRequired = true))
	TSoftObjectPtr<ULensFile> StartupLensFile;

	/** Map of Lens Distortion Model Handler classes to the default displacement map material used by that class */
	UPROPERTY(config)
	TMap<TSubclassOf<ULensDistortionModelHandlerBase>, TSoftObjectPtr<UMaterialInterface>> DefaultUndistortionDisplacementMaterials;

	/** Map of Lens Distortion Model Handler classes to the default displacement map material used by that class */
	UPROPERTY(config)
	TMap<TSubclassOf<ULensDistortionModelHandlerBase>, TSoftObjectPtr<UMaterialInterface>> DefaultDistortionDisplacementMaterials;

	/** Map of Lens Distortion Model Handler classes to the default lens distortion post-process material used by that class */
	UPROPERTY(config)
	TMap<TSubclassOf<ULensDistortionModelHandlerBase>, TSoftObjectPtr<UMaterialInterface>> DefaultDistortionMaterials;
};

/**
* Lens Data Table Editor Category color. Using for the color of the curves
*/
USTRUCT()
struct FLensDataCategoryEditorColor
{
	GENERATED_BODY()

	/** Get the color for specific category */
	FColor GetColorForCategory(const ELensDataCategory InCategory) const
	{
		switch (InCategory) {
		case ELensDataCategory::Focus:
			return Focus;
		case ELensDataCategory::Iris:
			return Iris;
		case ELensDataCategory::Zoom:
			return Zoom;
		case ELensDataCategory::Distortion:
			return Distortion;
		case ELensDataCategory::ImageCenter:
			return ImageCenter;
		case ELensDataCategory::STMap:
			return STMap;
		case ELensDataCategory::NodalOffset:
			return NodalOffset;
		default:
			checkNoEntry();
			return FColor::Black;
		}
	}
	
	UPROPERTY(EditAnywhere, Category = "Settings")
	FColor Focus = FColor::Red;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FColor Iris = FColor::Green;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FColor Zoom = FColor::Blue;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FColor Distortion = FColor::Cyan;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FColor ImageCenter = FColor::Yellow;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FColor STMap = FColor::Orange;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FColor NodalOffset = FColor::Purple;
};

/**
 * Settings for the camera calibration when in editor and standalone.
 * @note Cooked games don't use this setting.
 */
UCLASS(config = EditorPerProjectUserSettings)
class CAMERACALIBRATIONCORE_API UCameraCalibrationEditorSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:

		//~ Begin UDevelopperSettings interface
		virtual FName GetCategoryName() const;
#if WITH_EDITOR
		virtual FText GetSectionText() const override;
		virtual FName GetSectionName() const override;
#endif
		//~ End UDevelopperSettings interface

public:

#if WITH_EDITORONLY_DATA

	/**
	 * True if a lens file button shortcut should be added to level editor toolbar.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Settings", Meta = (ConfigRestartRequired = true, DisplayName = "Enable Lens File Toolbar Button"))
	bool bShowEditorToolbarButton = false;

	/**
	 * Data Table category color settings
	 */
	UPROPERTY(config, EditAnywhere, Category = "Settings")
	FLensDataCategoryEditorColor CategoryColor;

private:

	/** 
	 * Startup lens file per user in editor 
	 * Can be overridden. Priority of operation is
	 * 1. Apply startup lens file found in 'CameraCalibration.StartupLensFile' cvar at launch
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