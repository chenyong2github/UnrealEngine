// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CameraLensDistortionAlgo.h"

#include "LensFile.h"

#include "CameraLensDistortionAlgoCheckerboard.generated.h"

struct FGeometry;
struct FPointerEvent;

class ACameraCalibrationCheckerboard;
class FCameraCalibrationStepsController;
class ULensDistortionTool;
class SSimulcamViewport;

template <typename ItemType>
class SListView;

class SWidget;
class UMediaTexture;
class UWorld;

template<typename OptionType>
class SComboBox;

namespace CameraLensDistortionAlgoCheckerboard
{
	class SCalibrationRowGenerator;
}


/** 
 * Implements a lens distortion calibration algorithm. It requires a checkerboard pattern
 */
UCLASS()
class UCameraLensDistortionAlgoCheckerboard : public UCameraLensDistortionAlgo
{
	GENERATED_BODY()
	
public:

	//~ Begin CameraLensDistortionAlgo
	virtual void Initialize(ULensDistortionTool* InTool) override;
	virtual void Shutdown() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual TSharedRef<SWidget> BuildUI() override;
	virtual FName FriendlyName() const override { return TEXT("Lens Distortion Checkerboard"); };
	virtual void OnDistortionSavedToLens() override;
	virtual bool GetLensDistortion(
		float& OutFocus,
		float& OutZoom,
		FDistortionInfo& OutDistortionInfo,
		FFocalLengthInfo& OutFocalLengthInfo,
		FImageCenterInfo& OutImageCenterInfo,
		TSubclassOf<ULensModel>& OutLensModel,
		double& OutError,
		FText& OutErrorMessage
	) override;
	virtual TSharedRef<SWidget> BuildHelpWidget() override;
	//~ End CameraLensDistortionAlgo

private:

	// SCalibrationRowGenerator will need access to the row structures below.
	friend class CameraLensDistortionAlgoCheckerboard::SCalibrationRowGenerator;

	/** Holds camera information that can be used to add the samples */
	struct FCameraDataCache
	{
		// True if the rest of the contents are valid.
		bool bIsValid = false;

		// The data used to evaluate the lens data in the camera for this sample
		FLensFileEvalData LensFileEvalData;
	};

	/** Holds information of the calibration row */
	struct FCalibrationRowData
	{
		// Thumbnail to display in list
		TSharedPtr<SSimulcamViewport> Thumbnail;

		// Index to display in list
		int32 Index = -1;

		// Checkerboard corners in 2d image pixel coordinates.
		TArray<FVector2D> Points2d;

		// Checkerboard corners in 3d local space.
		TArray<FVector> Points3d;

		// Which calibrator was used
		FString CalibratorName;

		// Number of corner rows in pattern
		int32 NumCornerRows = 0;

		// Number of corner columns in pattern
		int32 NumCornerCols = 0;

		// Side dimension in cm
		float SquareSideInCm = 0;

		// Width of image
		int32 ImageWidth = 0;

		// Height of image
		int32 ImageHeight = 0;

		// Holds information of the camera data for this sample
		FCameraDataCache CameraData;
	};

private:

	/** The lens distortion tool controller */
	TWeakObjectPtr<ULensDistortionTool> Tool;

	/** The currently selected checkerboard object. */
	TWeakObjectPtr<ACameraCalibrationCheckerboard> Calibrator;

	/** Rows source for the CalibrationListView */
	TArray<TSharedPtr<FCalibrationRowData>> CalibrationRows;

	/** Displays the list of calibration points that will be used to calculate the lens distortion */
	TSharedPtr<SListView<TSharedPtr<FCalibrationRowData>>> CalibrationListView;

	/** Caches the last camera data.  Will hold last value before the media is paused */
	FCameraDataCache LastCameraData;

	/** True if a detection window should be shown after every capture */
	bool bShouldShowDetectionWindow = false;

private:

	/** Builds the UI of the calibration device picker */
	TSharedRef<SWidget> BuildCalibrationDevicePickerWidget();

	/** Builds the UI for the user to select if they want a corner detection window to be shown after every capture */
	TSharedRef<SWidget> BuildShowDetectionWidget();

	/** Builds the UI of the calibration points table */
	TSharedRef<SWidget> BuildCalibrationPointsTable();

	/** Builds the UI for the action buttons (RemoveLast, ClearAll) */
	TSharedRef<SWidget> BuildCalibrationActionButtons();

private:

	/** Returns the first checkerboard object in the scene that it can find */
	ACameraCalibrationCheckerboard* FindFirstCalibrator() const;

	/** Sets the calibrator object to be used. Updates the selection picker. */
	void SetCalibrator(ACameraCalibrationCheckerboard* InCalibrator);

	/** Returns the currently selected calibrator object. */
	ACameraCalibrationCheckerboard* GetCalibrator() const;

	/** Clears the list of calibration sample rows */
	void ClearCalibrationRows();

	/** Validates a new calibration point to determine if it should be added as a new sample row */
	bool ValidateNewRow(TSharedPtr<FCalibrationRowData>& Row, FText& OutErrorMessage) const;

	/** Add a new calibration row from media texture and camera data */
	bool AddCalibrationRow(FText& OutErrorMessage);

	/** Returns the steps controller */
	FCameraCalibrationStepsController* GetStepsController() const;
};
