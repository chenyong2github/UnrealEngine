// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SBoxPanel.h"

#include "CameraLensDistortionAlgo.generated.h"

struct FGeometry;
struct FPointerEvent;
struct FTransform;

struct FDistortionInfo;
struct FFocalLengthInfo;
struct FImageCenterInfo;

class ULensDistortionTool;
class SWidget;
class ULensModel;

/**
 * Defines the interface that any lens distortion algorithm should implement
 * in order to be used and listed by the Lens Distortion Tool. 
 */
UCLASS(Abstract)
class CAMERACALIBRATIONCORE_API UCameraLensDistortionAlgo : public UObject
{
	GENERATED_BODY()

public:

	/** Make sure you initialize before using the object */
	virtual void Initialize(ULensDistortionTool* InTool) {};

	/** Clean up resources */
	virtual void Shutdown() {};

	/** Called every frame */
	virtual void Tick(float DeltaTime) {};

	/** Callback when viewport is clicked. Returns false if the event was not handled. */
	virtual bool OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) { return false;  };

	/** Returns the UI of this calibrator. Expected to only be called once */
	virtual TSharedRef<SWidget> BuildUI() { return SNew(SVerticalBox); };

	/** Returns a descriptive name/title of this nodal offset algorithm */
	virtual FName FriendlyName() const { return TEXT("Invalid Name"); };

	/** Called when the data sample was saved to the lens file */
	virtual void OnDistortionSavedToLens() { };

	/** Returns the lens distortion calibration data */
	virtual bool GetLensDistortion(
		float& OutFocus,
		float& OutZoom,
		FDistortionInfo& OutDistortionInfo,
		FFocalLengthInfo& OutFocalLengthInfo,
		FImageCenterInfo& OutImageCenterInfo,
		TSubclassOf<ULensModel>& OutLensModel, 
		double& OutError, 
		FText& OutErrorMessage) 
	{ 
		return false; 
	};
};
