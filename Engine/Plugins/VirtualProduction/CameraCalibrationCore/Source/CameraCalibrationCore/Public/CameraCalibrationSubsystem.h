// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/EngineSubsystem.h"

#include "CoreMinimal.h"

#include "CameraCalibrationTypes.h"
#include "CameraNodalOffsetAlgo.h"
#include "CameraCalibrationStep.h"
#include "Containers/ArrayView.h"
#include "LensDistortionModelHandlerBase.h"
#include "LensFile.h"
#include "UObject/ObjectKey.h"

#include "CameraCalibrationSubsystem.generated.h"

class UCineCameraComponent;

/** Utility struct to store the original and overscanned focal lengths of a camera */
struct FCachedFocalLength
{
public:
	float OriginalFocalLength = 0.0f;
	float OverscanFocalLength = 0.0f;
};

/**
 * Camera Calibration subsystem
 */
UCLASS()
class CAMERACALIBRATIONCORE_API UCameraCalibrationSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:

	/** Get the default lens file. */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	ULensFile* GetDefaultLensFile() const;

	/** Get the default lens file. */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	void SetDefaultLensFile(ULensFile* NewDefaultLensFile);

	/** Facilitator around the picker to get the desired lens file. */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	ULensFile* GetLensFile(const FLensFilePicker& Picker) const;

	/** Return all handlers associated with the input camera component */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	TArray<ULensDistortionModelHandlerBase*> GetDistortionModelHandlers(UCineCameraComponent* Component);

	/** 
	 * Return the handler associated with the input distortion source, if one exists 
	 * If bUpdatePicker is true, the input picker reference will be updated so that its properties match those of the found handler
	 */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	ULensDistortionModelHandlerBase* FindDistortionModelHandler(UPARAM(ref)FDistortionHandlerPicker& DistortionHandlerPicker, bool bUpdatePicker = true) const;

	/** Return the handler associated with the input distortion source, if one exists that also matches the input model. If none exist, create a new handler and return it. */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	ULensDistortionModelHandlerBase* FindOrCreateDistortionModelHandler(UPARAM(ref)FDistortionHandlerPicker& DistortionHandlerPicker, const TSubclassOf<ULensModel> LensModelClass);

	/** Disassociate the input handler from the input camera component in the subsystem's handler map */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	void UnregisterDistortionModelHandler(UCineCameraComponent* Component, ULensDistortionModelHandlerBase* Handler);

	/** Return the ULensModel subclass that was registered with the input model name */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	TSubclassOf<ULensModel> GetRegisteredLensModel(FName ModelName) const;

	/** Returns the nodal offset algorithm by name */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	TSubclassOf<UCameraNodalOffsetAlgo> GetCameraNodalOffsetAlgo(FName Name) const;

	/** Returns an array with the names of the available nodal offset algorithms */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	TArray<FName> GetCameraNodalOffsetAlgos() const;

	/** Returns an array with the names of the available camera calibration steps */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	TArray<FName> GetCameraCalibrationSteps() const;

	/** Returns the camera calibration step by name */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	TSubclassOf<UCameraCalibrationStep> GetCameraCalibrationStep(FName Name) const;

public:
	//~ Begin USubsystem interface
	virtual void Deinitialize() override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	//~ End USubsystem interface

	/** Add a new Lens Model to the registered model map */
	void RegisterDistortionModel(TSubclassOf<ULensModel> LensModel);

	/** Remove a Lens Model from the registered model map */
	void UnregisterDistortionModel(TSubclassOf<ULensModel> LensModel);

	/** Update the original focal length of the input camera componet */
	void UpdateOriginalFocalLength(UCineCameraComponent* Component, float InFocalLength);

	/** Update the overscanned focal length of the input camera component */
	void UpdateOverscanFocalLength(UCineCameraComponent* Component, float InFocalLength);

	/** 
	 * Get the original focal length of the input camera component, if it exists in the subsystems map. 
	 * Returns false and does not update the output parameter if the input camera is not in the map. 
	 */
	bool GetOriginalFocalLength(UCineCameraComponent* Component, float& OutFocalLength);

private:
	/** Default lens file to use when no override has been provided */
	UPROPERTY(Transient)
	ULensFile* DefaultLensFile = nullptr;

	/** Map of model names to ULensModel subclasses */
	UPROPERTY(Transient)
	TMap<FName, TSubclassOf<ULensModel>> LensModelMap;

	/** Holds the registered camera nodal offset algos */
	UPROPERTY(Transient)
	TMap<FName, TSubclassOf<UCameraNodalOffsetAlgo>> CameraNodalOffsetAlgosMap;

	/** Holds the registered camera calibration steps */
	UPROPERTY(Transient)
	TMap<FName, TSubclassOf<UCameraCalibrationStep>> CameraCalibrationStepsMap;

	/** Map of actor components to the authoritative lens model that should be used with that component */
	TMap<FObjectKey, TSubclassOf<ULensModel>> ComponentsWithAuthoritativeModels;

	/** Map of actor components to the authoritative lens model that should be used with that component */
	TMultiMap<FObjectKey, ULensDistortionModelHandlerBase*> LensDistortionHandlerMap;

	/** Map of camera components to a cached pair of focal lengths for that camera */
	TMap<FObjectKey, FCachedFocalLength> CachedFocalLengthMap;

private:

	FDelegateHandle PostEngineInitHandle;

};
