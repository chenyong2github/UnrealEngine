// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CameraNodalOffsetAlgo.h"

#include "LensFile.h"

#include "CameraNodalOffsetAlgoPoints.generated.h"

template <typename ItemType>
class SListView;

class UCalibrationPointComponent;

template<typename OptionType>
class SComboBox;

namespace CameraNodalOffsetAlgoPoints
{
	class SCalibrationRowGenerator;
};

/** 
 * Implements a nodal offset calibration algorithm. It uses 3d points (UCalibrationPointComponent) 
 * specified in a selectable calibrator with features that the user can identify by clicking on the 
 * simulcam viewport, and after enough points have been identified, it can calculate the nodal offset
 * that needs to be applied to the associated camera in order to align its CG with the live action media plate.
 * 
 * Limitations:
 * - Only supports Brown–Conrady model lens model (FSphericalDistortionParameters)
 * - The camera or the lens should not be moved during the calibration of each nodal offset sample.
 */
UCLASS()
class UCameraNodalOffsetAlgoPoints : public UCameraNodalOffsetAlgo
{
	GENERATED_BODY()
	
public:

	//~ Begin CalibPointsNodalOffsetAlgo
	virtual void Initialize(UNodalOffsetTool* InNodalOffsetTool) override;
	virtual void Shutdown() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool OnViewportClicked(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual TSharedRef<SWidget> BuildUI() override;
	virtual bool GetNodalOffset(FNodalPointOffset& OutNodalOffset, float& OutFocus, float& OutZoom, float& OutError, FText& OutErrorMessage) override;
	virtual FName FriendlyName() const override { return TEXT("Nodal Offset Points Method"); };
	virtual void OnSavedNodalOffset() override;
	//~ End CalibPointsNodalOffsetAlgo

private:

	// SCalibrationRowGenerator will need access to the row structures below.
	friend class CameraNodalOffsetAlgoPoints::SCalibrationRowGenerator;

	/** Holds item information a given calibrator point in the calibrator model */
	struct FCalibratorPointData
	{
		FCalibratorPointData(FString&& InName)
			: Name(InName)
		{}

		/** Name of the calibrator 3d point, as defined in the mesh */
		FString Name;
	};

	/** Holds information of the identified calibrator 2d point for a given sample of a 2d-3d correlation */
	struct FCalibratorPointCache
	{
		// True if the rest of the contents are valid.
		bool bIsValid;

		// The name of the 3d calibrator point, as defined in the mesh
		FString Name;

		// The world space 3d location of the point
		FVector Location;
	};

	/** Holds information of the camera pose for a given sample of a 2d-3d correlation */
	struct FCameraDataCache
	{
		// True if the rest of the contents are valid.
		bool bIsValid;

		// The unique id of the camera object. Used to detect camera selection changes during a calibration session.
		uint32 UniqueId;

		// The camera pose
		FTransform Pose;

		// The data used to evaluate the lens data in the camera for this sample
		FLensFileEvalData LensFileEvalData;
	};

	/** Holds information of the calibrator 3d point for a given sample of a 2d-3d correlation */
	struct FCalibrationRowData
	{
		// Normalized 0~1 2d location of the identified calibrator point in the media plate.
		FVector2D Point2D;

		// Holds information of the calibrator point data for this sample.
		FCalibratorPointCache CalibratorPointData;

		// Holds information of the camera data for this sample
		FCameraDataCache CameraData;
	};

private:

	/** The nodal offset tool controller */
	TWeakObjectPtr<UNodalOffsetTool> NodalOffsetTool;

	/** The currently selected calibrator object. It is expected to contain one or more UCalibrationPointComponent in it */
	TWeakObjectPtr<AActor> Calibrator;

	/** Options source for the CalibratorPointsComboBox. Lists the calibrator points found in the selected calibrator object */
	TArray<TSharedPtr<FCalibratorPointData>> CurrentCalibratorPoints;

	/** Allows the selection of calibrator point that will be visually identified in the simulcam viewport */
	TSharedPtr<SComboBox<TSharedPtr<FCalibratorPointData>>> CalibratorPointsComboBox;

	/** Rows source for the CalibrationListView */
	TArray<TSharedPtr<FCalibrationRowData>> CalibrationRows;

	/** Displays the list of calibration points that will be used to calculate the nodal offset */
	TSharedPtr<SListView<TSharedPtr<FCalibrationRowData>>> CalibrationListView;

	/** Caches the last calibrator point 3d location.  Will hold last value before the nodal offset tool is paused */
	TArray<FCalibratorPointCache> LastCalibratorPoints;

	/** Caches the last camera data.  Will hold last value before the nodal offset tool is paused */
	FCameraDataCache LastCameraData;

private:

	/** Builds the UI of the calibration device picker */
	TSharedRef<SWidget> BuildCalibrationDevicePickerWidget();

	/** Builds the UI of the calibration points table */
	TSharedRef<SWidget> BuildCalibrationPointsTable();

	/** Buils the UI for selecting the current calibrator point */
	TSharedRef<SWidget> BuildCalibrationPointsComboBox();

	/** Builds the UI for the action buttons (RemoveLast, ClearAll) */
	TSharedRef<SWidget> BuildCalibrationActionButtons();

private:

	/** Returns the first calibrator object in the scene that it can find */
	AActor* FindFirstCalibrator() const;

	/** Sets the calibrator object to be used. Updates the selection picker. */
	void SetCalibrator(AActor* InCalibrator);

	/** Returns the currently selected calibrator object. */
	AActor* GetCalibrator() const;

	/** Clears the list of calibration sample rows */
	void ClearCalibrationRows();

	/** Retrieves by name the UCalibrationPointComponent of the currently selected calibrator */
	const UCalibrationPointComponent* GetCalibrationPointComponentFromName(FString& Name) const;

	/** Returns the world 3d location of the currently selected */
	bool GetCurrentCalibratorPointLocation(FVector& OutLocation);

	/** Selects the next available UCalibrationPointComponent of the currently selected calibrator object. Returns true when it wraps around */
	bool AdvanceCalibratorPoint();

	/** Validates a new calibration point to determine if it should be added as a new sample row */
	bool ValidateNewRow(TSharedPtr<FCalibrationRowData>& Row, FText& OutErrorMessage) const;
};
