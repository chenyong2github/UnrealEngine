// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/DeveloperSettings.h"

#include "LensDistortionModelHandlerBase.h"
#include "LensFile.h"

#include "CameraCalibrationSettings.generated.h"

#if WITH_EDITOR
DECLARE_MULTICAST_DELEGATE_OneParam(FOnDisplacementMapResolutionChanged, const FIntPoint NewDisplacementMapResolution);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnCalibrationInputToleranceChanged, const float NewTolerance);
#endif

/**
 * Settings for the CameraCalibration plugin modules. 
 */
UCLASS(config=Game)
class CAMERACALIBRATIONCORE_API UCameraCalibrationSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UCameraCalibrationSettings();

	//~ Begin UDeveloperSettings interface
	virtual FName GetCategoryName() const;
#if WITH_EDITOR
	virtual FText GetSectionText() const override;
	virtual FName GetSectionName() const override;
#endif
	//~ End UDeveloperSettings interface

#if WITH_EDITOR
	virtual void PostEditChangeChainProperty(struct FPropertyChangedChainEvent& PropertyChangedEvent) override;

	/** Gets a multicast delegate which is called whenever the displacement map resolution project setting changes */
	FOnDisplacementMapResolutionChanged& OnDisplacementMapResolutionChanged();

	/** Gets a multicast delegate which is called whenever the displacement map resolution project setting changes */
	FOnCalibrationInputToleranceChanged& OnCalibrationInputToleranceChanged();
#endif

public:

	/**
	 * Get the default startup lens file.
	 *
	 * @return The lens file, or nullptr if not set.
	 */
	ULensFile* GetStartupLensFile() const;

	/** Get the resolution that should be used for distortion and undistortion displacement maps */
	FIntPoint GetDisplacementMapResolution() const { return DisplacementMapResolution; }

	/** Get the tolerance to use when adding or accessing data in a calibrated LensFile */
	float GetCalibrationInputTolerance() const { return CalibrationInputTolerance; }

	/** Get the default MaterialInterface used by the input Model Handler class to write the undistortion displacement map */
	UMaterialInterface* GetDefaultUndistortionDisplacementMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const;

	/** Get the default MaterialInterface used by the input Model Handler class to write the distortion displacement map */
	UMaterialInterface* GetDefaultDistortionDisplacementMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const;

	/** Get the default MaterialInterface used by the input Model Handler class to apply the post-process lens distortion effect */
	UMaterialInterface* GetDefaultDistortionMaterial(const TSubclassOf<ULensDistortionModelHandlerBase>& InModelHandler) const;

#if WITH_EDITOR
protected:
	FOnDisplacementMapResolutionChanged DisplacementMapResolutionChangedDelegate;
	FOnCalibrationInputToleranceChanged CalibrationInputToleranceChangedDelegate;
#endif // WITH_EDITOR

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

	/** Resolution used when creating new distortion and undistortion displacement maps */
	UPROPERTY(config, EditAnywhere, Category = "Settings")
	FIntPoint DisplacementMapResolution = FIntPoint(256, 256);

	/** Tolerance to use when adding or accessing data in a calibrated LensFile */
	UPROPERTY(config, EditAnywhere, Category = "Settings")
	float CalibrationInputTolerance = 0.001f;

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

	/**
	 * Time slider enable flag
	 */
	UPROPERTY(config, EditAnywhere, Category = "Settings", Meta = (ToolTip = "Enable or Disable Time input driven by Live Link."))
	bool bEnableTimeSlider = true;

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