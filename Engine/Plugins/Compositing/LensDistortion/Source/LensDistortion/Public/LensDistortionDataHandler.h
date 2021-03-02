// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Engine/AssetUserData.h"

#include "LensData.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "LensDistortionDataHandler.generated.h"

USTRUCT(BlueprintType)
struct LENSDISTORTION_API FLensDistortionState
{
	GENERATED_BODY()

public:
	/** Lens Model describing how to interpret the distortion parameters */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	ELensModel LensModel = ELensModel::Spherical;

	/** Coefficients of the distortion model */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	FDistortionParameters DistortionParameters;

	/** Normalized center of the image, in the range [0.0f, 1.0f] */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Distortion")
	FVector2D PrincipalPoint = FVector2D(0.5f, 0.5f);

	/** Width and height of the camera's sensor, in millimeters */
	UPROPERTY(BlueprintReadWrite, Category = "Distortion")
	FVector2D SensorDimensions = FVector2D(23.76f, 13.365f);

	/** Focal length of the camera, in millimeters */
	UPROPERTY(BlueprintReadWrite, Category = "Distortion")
	float FocalLength = 35.0f;

public:
	bool operator==(const FLensDistortionState& Other) const;
	bool operator!=(const FLensDistortionState& Other) const { return !(*this == Other); }
};

/** Asset user data that can be used on Camera Actors to manage lens distortion state and utilities  */
UCLASS(BlueprintType)
class LENSDISTORTION_API ULensDistortionDataHandler : public UAssetUserData
{
	GENERATED_BODY()

public:
	/** Get the current distortion state (the lens model and properties that mathematically represent the distortion characteristics */
	FLensDistortionState GetCurrentDistortionState() const { return CurrentState; }

	/** Get the computed overscan factor needed to scale the camera's sensor dimensions */
	float GetOverscanFactor() const { return OverscanFactor; }

	/** Get the post-process MID for the currently specified lens model */
	UMaterialInstanceDynamic* GetDistortionMID() const { return DistortionMID; }

	/** Get the specified lens model that characterizes the distortion effect */
	ELensModel GetLensModel() const { return CurrentState.LensModel; };

	/** Get the coefficients of the distortion model */
	FDistortionParameters GetDistortionParameters() const { return CurrentState.DistortionParameters; }

	/** Get the normalized center of projection of the image, in the range [0.0f, 1.0f] */
	FVector2D GetPrincipalPoint() const { return CurrentState.PrincipalPoint; }

	/** Get the width and height of the camera's sensor, in millimeters */
	FVector2D GetSensorDimensions() const { return CurrentState.SensorDimensions; }

	/** Get the focal length of the camera, in millimeters */
	float GetFocalLength() const { return CurrentState.FocalLength; }

public:
	/** Get the first instance of a LensDistortionDataHandler object belonging to the input component */
	UFUNCTION(BlueprintCallable, Category = "Distortion")
	static ULensDistortionDataHandler* GetLensDistortionDataHandler(UActorComponent* InComponentWithUserData);

	/** Update the lens distortion state, recompute the overscan factor, and set all material parameters */
	UFUNCTION(BlueprintCallable, Category = "Distortion")
	void Update(const FLensDistortionState& InNewState);

	/** Update the camera settings of the lens distortion state, recompute the overscan factor, and set all material parameters */
	UFUNCTION(BlueprintCallable, Category = "Distortion")
	void UpdateCameraSettings(FVector2D InSensorDimensions, float InFocalLength);

	//~ Begin UObject Interface
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif	
	//~ End UObject Interface

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Distortion", meta = (ShowOnlyInnerProperties))
	FLensDistortionState CurrentState;

	/** Computed overscan factor needed to scale the camera's sensor dimensions (read-only) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Distortion")
	float OverscanFactor = 1.0f;

	/** Dynamically created post-process material instance for the currently specified lens model */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Transient, Category = "Distortion")
	UMaterialInstanceDynamic* DistortionMID;

private:
	/** Use the current distortion state to compute the distortion position of an input UV coordinate */
	FVector2D ComputeDistortedUV(const FVector2D& InScreenUV) const;

	/** Use the current distortion state to compute the overscan factor needed such that all distorted UVs will fall into the valid range of [0,1] */
	float ComputeOverscanFactor() const;

	/** Update the lens distortion state, recompute the overscan factor, and set all material parameters */
	void UpdateInternal(const FLensDistortionState& InNewState);
};
