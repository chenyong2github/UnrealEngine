// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Engine/World.h"
#include "CameraCalibrationStep.h"

#include "NodalOffsetTool.generated.h"

struct FGeometry;
struct FLensFileEvalData;
struct FPointerEvent;

class UCameraNodalOffsetAlgo;

/**
 * FNodalOffsetTool is the controller for the nodal offset tool panel.
 * It has the logic to bridge user input like selection of nodal offset algorithm or CG camera
 * with the actions that follow. It houses convenience functions used to generate the data
 * of what is presented to the user, and holds pointers to the relevant objects and structures.
 */
UCLASS()
class UNodalOffsetTool : public UCameraCalibrationStep
{
	GENERATED_BODY()

public:

	//~ Begin UCameraCalibrationStep interface
	virtual void Initialize(TWeakPtr<FCameraCalibrationStepsController> InCameraCalibrationStepController) override;
	virtual void Shutdown() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual TSharedRef<SWidget> BuildUI() override;
	virtual FName FriendlyName() const  override { return TEXT("Nodal Offset"); };
	virtual bool DependsOnStep(UCameraCalibrationStep* Step) const override;
	virtual void Activate() override;
	virtual void Deactivate() override;
	virtual FCameraCalibrationStepsController* GetCameraCalibrationStepsController() const override;
	virtual bool IsActive() const override;
	//~ End UCameraCalibrationStep interface

public:

	/** Selects the nodal offset algorithm by name */
	void SetNodalOffsetAlgo(const FName& AlgoName);

	/** Returns the currently selected nodal offset algorithm */
	UCameraNodalOffsetAlgo* GetNodalOffsetAlgo() const;

public:

	/** Called by the UI when the user wants to save the nodal offset that the current algorithm is providing */
	void OnSaveCurrentNodalOffset();

private:

	/** Pointer to the calibration steps controller */
	TWeakPtr<FCameraCalibrationStepsController> CameraCalibrationStepsController;

	/** The currently selected nodal offset algorithm */
	UPROPERTY()
	UCameraNodalOffsetAlgo* NodalOffsetAlgo;

	/** True if this tool is the active one in the panel */
	bool bIsActive = false;
};
