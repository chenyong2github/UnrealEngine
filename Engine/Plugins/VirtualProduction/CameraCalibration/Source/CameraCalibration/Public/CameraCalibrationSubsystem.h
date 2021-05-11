// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Subsystems/EngineSubsystem.h"

#include "CameraCalibrationTypes.h"
#include "LensDistortionModelHandlerBase.h"
#include "LensFile.h"
#include "UObject/ObjectKey.h"

#include "CameraCalibrationSubsystem.generated.h"

class UCineCameraComponent;

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

	/** Return all handlers associated with the input camera component */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	TArray<ULensDistortionModelHandlerBase*> GetDistortionModelHandlers(UCineCameraComponent* Component);

	/** 
	 * Return the handler associated with the input distortion source, if one exists 
	 * If bUpdatePicker is true, the input picker reference will be updated so that its properties match those of the found handler
	 */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	ULensDistortionModelHandlerBase* FindDistortionModelHandler(FDistortionHandlerPicker& DistortionHandlerPicker, bool bUpdatePicker = true) const;

	/** Return the handler associated with the input distortion source, if one exists that also matches the input model. If none exist, create a new handler and return it. */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	ULensDistortionModelHandlerBase* FindOrCreateDistortionModelHandler(FDistortionHandlerPicker& DistortionHandlerPicker, const TSubclassOf<ULensModel> LensModelClass);

	/** Disassociate the input handler from the input camera component in the subsystem's handler map */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	void UnregisterDistortionModelHandler(UCineCameraComponent* Component, ULensDistortionModelHandlerBase* Handler);

	/** Return the ULensModel subclass that was registered with the input model name */
	UFUNCTION(BlueprintCallable, Category = "Lens Distortion")
	TSubclassOf<ULensModel> GetRegisteredLensModel(FName ModelName) const;

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

	/** Map of actor components to the authoritative lens model that should be used with that component */
	TMultiMap<FObjectKey, ULensDistortionModelHandlerBase*> LensDistortionHandlerMap;

};
