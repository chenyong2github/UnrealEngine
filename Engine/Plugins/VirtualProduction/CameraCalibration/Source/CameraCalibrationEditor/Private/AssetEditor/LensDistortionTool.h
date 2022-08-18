// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/World.h"
#include "CameraCalibrationStep.h"

#include "LensDistortionTool.generated.h"

struct FGeometry;
struct FLensFileEvalData;
struct FPointerEvent;

class UCameraLensDistortionAlgo;

/**
 * ULensDistortionTool is the controller for the lens distortion panel.
 */
UCLASS()
class ULensDistortionTool : public UCameraCalibrationStep
{
	GENERATED_BODY()

public:

	//~ Begin UCameraCalibrationStep interface
	virtual void Initialize(TWeakPtr<FCameraCalibrationStepsController> InCameraCalibrationStepController) override;
	virtual void Shutdown() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual TSharedRef<SWidget> BuildUI() override;
	virtual FName FriendlyName() const  override { return TEXT("Lens Distortion"); };
	virtual bool DependsOnStep(UCameraCalibrationStep* Step) const override;
	virtual void Activate() override;
	virtual void Deactivate() override;
	virtual FCameraCalibrationStepsController* GetCameraCalibrationStepsController() const override;
	virtual bool IsActive() const override;
	virtual UMaterialInstanceDynamic* GetOverlayMID() const override;
	virtual bool IsOverlayEnabled() const override;
	//~ End UCameraCalibrationStep interface

public:

	/** Selects the algorithm by name */
	void SetAlgo(const FName& AlgoName);

	/** Returns the currently selected algorithm */
	UCameraLensDistortionAlgo* GetAlgo() const;

	/** Returns available algorithm names */
	TArray<FName> GetAlgos() const;

public:

	/** Called by the UI when the user wants to save the calibration data that the current algorithm is providing */
	void OnSaveCurrentCalibrationData();

private:

	/** Pointer to the calibration steps controller */
	TWeakPtr<FCameraCalibrationStepsController> CameraCalibrationStepsController;

	/** The currently selected algorithm */
	UPROPERTY(Transient)
	TObjectPtr<UCameraLensDistortionAlgo> CurrentAlgo;

	/** Holds the registered camera lens distortion algos */
	UPROPERTY(Transient)
	TMap<FName, TSubclassOf<UCameraLensDistortionAlgo>> AlgosMap;

	/** Map of algo names to overlay MIDs used by those algos */
	UPROPERTY(Transient)
	TMap<FName, TWeakObjectPtr<UMaterialInstanceDynamic>> AlgoOverlayMIDs;

	/** True if this tool is the active one in the panel */
	bool bIsActive = false;
};
