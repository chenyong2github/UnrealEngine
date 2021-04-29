// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/EngineSubsystem.h"

#include "LensDistortionModelHandlerBase.h"
#include "LensFile.h"
#include "UObject/ObjectKey.h"

#include "CameraCalibrationSubsystem.generated.h"

class UActorComponent;

UENUM(BlueprintType)
enum class EHandlerOverrideMode : uint8
{
	/** If a component already has a lens handler that supports a different lens model, do not override the existing handler */
	NoOverride,

	/** 
	 * If a component already has a lens handler that supports a different lens model, but that model is not authoritative,
	 * remove the existing handler, create a new handler for the new model, and mark that new model as the authoritative one for that component
	 */
	SoftOverride,

	/**
	 * If a component already has a lens handler that supports a different lens model, regardless of any previous authoritative models,
	 * remove the existing handler, create a new handler for the new model, and mark that new model as the authoritative one for that component
	 */
	 ForceOverride
};

/**
 * Camera Calibration subsystem
 */
UCLASS()
class CAMERACALIBRATION_API UCameraCalibrationSubsystem : public UEngineSubsystem
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

	/** Get the lens distortion model handler owned by the input actor component (returns the first instance if there are multiple) */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	ULensDistortionModelHandlerBase* GetDistortionModelHandler(UActorComponent* Component);

	/** 
	 * Get the lens distortion model handler owned by the input actor component that supports the input lens model.
	 * If no such handler exists, creates a new instance of one that supports the input lens model and gives ownership of the handler to the input component. 
	 * If the input component already has a model handler that does not support the input model, that handler is removed.
	 */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	ULensDistortionModelHandlerBase* FindOrCreateDistortionModelHandler(UActorComponent* Component, TSubclassOf<ULensModel> LensModelClass, EHandlerOverrideMode OverrideMode = EHandlerOverrideMode::NoOverride);

	/** Return the ULensModel subclass that was registered with the input model name */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	TSubclassOf<ULensModel> GetRegisteredLensModel(FName ModelName);

public:
	//~ Begin USubsystem interface
	virtual void Deinitialize() override;
	//~ End USubsystem interface

	/** Add a new Lens Model to the registered model map */
	void RegisterDistortionModel(TSubclassOf<ULensModel> LensModel);

	/** Remove a Lens Model from the registered model map */
	void UnregisterDistortionModel(TSubclassOf<ULensModel> LensModel);

private:
	/** Default lens file to use when no override has been provided */
	UPROPERTY(Transient)
	ULensFile* DefaultLensFile = nullptr;

	/** Map of model names to ULensModel subclasses */
	UPROPERTY(Transient)
	TMap<FName, TSubclassOf<ULensModel>> LensModelMap;

	/** Map of actor components to the authoritative lens model that should be used with that component */
	TMap<FObjectKey, TSubclassOf<ULensModel>> ComponentsWithAuthoritativeModels;
};
