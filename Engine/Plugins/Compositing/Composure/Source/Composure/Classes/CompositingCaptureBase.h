// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CompositingElement.h"

#include "CameraCalibrationTypes.h"
#include "LensDistortionModelHandlerBase.h"

#include "CompositingCaptureBase.generated.h"

class USceneCaptureComponent2D;

/**
 * Base class for CG Compositing Elements
 */
UCLASS(BlueprintType)
class COMPOSURE_API ACompositingCaptureBase : public ACompositingElement
{
	GENERATED_BODY()

public:
	/** Component used to generate the scene capture for this CG Layer */
	UPROPERTY(VisibleDefaultsOnly, BlueprintReadOnly, Category = "SceneCapture")
	USceneCaptureComponent2D* SceneCaptureComponent2D = nullptr;

protected:
	/** Whether to apply distortion as a post-process effect on this CG Layer */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composure|LensDistortion")
	bool bApplyDistortion = false;

	/** Structure used to query the camera calibration subsystem for a lens distortion model handler */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Composure|LensDistortion")
	FDistortionHandlerPicker DistortionSource;

	/** Value used to augment the FOV of the scene capture to produce a CG image with enough data to distort */
	UPROPERTY(BlueprintReadOnly, Category = "Composure|LensDistortion")
	float OverscanFactor = 1.0f;

	/** Cached distortion MID produced by the Lens Distortion Handler, used to clean up the post-process materials in the case that the the MID changes */
	UPROPERTY()
	UMaterialInstanceDynamic* LastDistortionMID = nullptr;

public:
	/** Default constructor */
	ACompositingCaptureBase();

	/** Update the state of the Lens Distortion Data Handler, and updates or removes the Distortion MID from the SceneCaptureComponent's post process materials, depending on whether distortion should be applied*/
	UFUNCTION(BlueprintCallable, Category = "Composure|LensDistortion")
	void UpdateDistortion();

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif	
	//~ End UObject Interface
};