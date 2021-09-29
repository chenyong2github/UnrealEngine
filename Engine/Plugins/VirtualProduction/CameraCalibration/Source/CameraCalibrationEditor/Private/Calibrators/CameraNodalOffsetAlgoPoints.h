// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CameraNodalOffsetAlgo.h"

#include "LensFile.h"

#include "CameraNodalOffsetAlgoPoints.generated.h"

class FCameraCalibrationStepsController;

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
	virtual TSharedRef<SWidget> BuildHelpWidget() override;
	//~ End CalibPointsNodalOffsetAlgo

protected:

	// SCalibrationRowGenerator will need access to the row structures below.
	friend class CameraNodalOffsetAlgoPoints::SCalibrationRowGenerator;

	/** Holds item information a given calibrator point in the calibrator model */
	struct FCalibratorPointData
	{
		FCalibratorPointData(FString& InName)
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

		// The parent pose (expected to be the tracker origin)
		FTransform ParentPose;

		// The parent unique id
		uint32 ParentUniqueId;

		// Calibrator Pose
		FTransform CalibratorPose;

		// Calibrator ParentPose
		FTransform CalibratorParentPose;

		// Calibrator ComponentPose
		TMap<uint32, FTransform> CalibratorComponentPoses;

		// Calibrator unique id
		uint32 CalibratorUniqueId;

		// Calibrator parent unique id
		uint32 CalibratorParentUniqueId;
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

protected:

	/** The nodal offset tool controller */
	TWeakObjectPtr<UNodalOffsetTool> NodalOffsetTool;

	/** The currently selected calibrator object. It is expected to contain one or more UCalibrationPointComponent in it */
	TWeakObjectPtr<AActor> Calibrator;

	/** Container for the set of calibrator components selected in the component combobox */
	TArray<TWeakObjectPtr<const UCalibrationPointComponent>> ActiveCalibratorComponents;

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

	/** Instructs the inline allocation policy how many elements to allocate in a single allocation */
	static constexpr uint32 NumInlineAllocations = 32;

protected:

	/** Builds the UI of the calibration device picker */
	TSharedRef<SWidget> BuildCalibrationDevicePickerWidget();

	/** Builds the UI of the calibration component picker */
	TSharedRef<SWidget> BuildCalibrationComponentPickerWidget();

	/** Builds the UI of the calibration points table */
	TSharedRef<SWidget> BuildCalibrationPointsTable();

	/** Buils the UI for selecting the current calibrator point */
	TSharedRef<SWidget> BuildCalibrationPointsComboBox();

	/** Builds the UI for the action buttons (RemoveLast, ClearAll) */
	TSharedRef<SWidget> BuildCalibrationActionButtons();

	/** Builds the UI of the calibration component picker */
	TSharedRef<SWidget> BuildCalibrationComponentMenu();

protected:

	/** Returns the first calibrator object in the scene that it can find */
	virtual AActor* FindFirstCalibrator() const;

	/** Sets the calibrator object to be used. Updates the selection picker. */
	void SetCalibrator(AActor* InCalibrator);

	/** Returns the currently selected calibrator object. */
	AActor* GetCalibrator() const;

	/** Clears the list of calibration sample rows */
	void ClearCalibrationRows();

	/** 
	 * Retrieves by name the calibration point data of the currently selected calibrator.
	 * 
	 * @param Name The name of the point (namespaced if it is a subpoint).
	 * @param CalibratorPointCache This data structure will be populated with the information found.
	 * 
	 * @return True if successful.
	 */
	bool CalibratorPointCacheFromName(const FString& Name, FCalibratorPointCache& CalibratorPointCache) const;

	/** Returns the world 3d location of the currently selected calibrator */
	bool GetCurrentCalibratorPointLocation(FVector& OutLocation);

	/** Selects the next available UCalibrationPointComponent of the currently selected calibrator object. Returns true when it wraps around */
	bool AdvanceCalibratorPoint();

	/** Add or remove the selected component from the set of active components and refresh the calibrator point combobox */
	void OnCalibrationComponentSelected(const UCalibrationPointComponent* const SelectedComponent);

	/** Returns true if the input component is in the set of active calibration components */
	bool IsCalibrationComponentSelected(const UCalibrationPointComponent* const SelectedComponent) const;

	/** Update any calibration components in the set of active components if they were replaced by an in-editor event */
	void OnObjectsReplaced(const TMap<UObject*, UObject*>& OldToNewInstanceMap);

	/** Validates a new calibration point to determine if it should be added as a new sample row */
	virtual bool ValidateNewRow(TSharedPtr<FCalibrationRowData>& Row, FText& OutErrorMessage) const;

	/** Applies the nodal offset to the calibrator */
	bool ApplyNodalOffsetToCalibrator();

	/** Applies the nodal offset to the tracker origin (normally the camera parent) */
	bool ApplyNodalOffsetToTrackingOrigin();

	/** Applies the nodal offset to the parent of the calibrator */
	bool ApplyNodalOffsetToCalibratorParent();

	/** Applies the nodal offset to the parent of the calibrator */
	bool ApplyNodalOffsetToCalibratorComponents();

	/** Does basic checks on the data before performing the actual calibration */
	bool BasicCalibrationChecksPass(const TArray<TSharedPtr<FCalibrationRowData>>& Rows, FText& OutErrorMessage) const;

	/** 
	 * Gets the step controller and the lens file. 
	 *
	 * @param OutStepsController Steps Controller
	 * @param OutLensFile Lens File
	 * 
	 * @return True if successful
	 */
	bool GetStepsControllerAndLensFile(const FCameraCalibrationStepsController** OutStepsController, const ULensFile** OutLensFile) const;

	/** 
	 * Calculates the optimal camera component pose that minimizes the reprojection error 
	 * 
	 * @param OutDesiredCameraTransform The camera transform in world coordinates that minimizes the reprojection error.
	 * @param Rows Calibration samples.
	 * @param OutErrorMessage Documents any error that may have happened.
	 * 
	 * @return True if successful.
	 */
	bool CalculatedOptimalCameraComponentPose(
		FTransform& OutDesiredCameraTransform, 
		const TArray<TSharedPtr<FCalibrationRowData>>& Rows, 
		FText& OutErrorMessage) const;

	/**
	 * Calculates nodal offset based on a single camera pose 
	 * 
	 * @param OutNodalOffset Camera nodal offset
	 * @param OutFocus Focus distance associated with this nodal offset
	 * @param OutZoom Focal length associated with this nodal offset
	 * @param OutError Reprojection error
	 * @param Rows Calibration samples
	 * @param OutErrorMessage Describes any error that may have happened
	 * 
	 * @return True if successful
	 */
	bool GetNodalOffsetSinglePose(
		FNodalPointOffset& OutNodalOffset, 
		float& OutFocus, 
		float& OutZoom, 
		float& OutError, 
		const TArray<TSharedPtr<FCalibrationRowData>>& Rows, 
		FText& OutErrorMessage) const;

	/**
	 * Groups the calibration samples by camera pose.
	 *
	 * @param OutSamePoseRowGroups Output array of arrays of samples, where the samples in each group have the same camera pose.
	 * @param Rows Ungrouped calibration samples.
	 */
	void GroupRowsByCameraPose(
		TArray<TSharedPtr<TArray<TSharedPtr<FCalibrationRowData>>>>& OutSamePoseRowGroups, 
		const TArray<TSharedPtr<FCalibrationRowData>>& Rows) const;

	/** 
	 * Detects if the calibrator moved significantly in the given sample points 
	 * 
	 * @param OutSamePoseRowGroups Array of array of calibration samples.
	 * 
	 * @return True if the calibrator moved significantly across all the samples.
	 */
	bool CalibratorMovedAcrossGroups(const TArray<TSharedPtr<TArray<TSharedPtr<FCalibrationRowData>>>>& OutSamePoseRowGroups) const;

	/**
	 * Detects if the calibrator moved significantly in the given sample points
	 *
	 * @param Rows Array of calibration samples.
	 *
	 * @return True if the calibrator moved significantly across all the samples.
	 */
	bool CalibratorMovedInAnyRow(const TArray<TSharedPtr<FCalibrationRowData>>& Rows) const;

	/**
	 * Calculates the optimal camera parent transform that minimizes the reprojection of the sample points.
	 * 
	 * @param Rows The calibration points
	 * @param OutTransform The optimal camera parent transform
	 * @param OutErrorMessage Describes any errors encountered
	 * 
	 * @return True if successful
	 */
	bool CalcTrackingOriginPoseForSingleCamPose(
		const TArray<TSharedPtr<FCalibrationRowData>>& Rows,
		FTransform& OutTransform,
		FText& OutErrorMessage);

	/**
	 * Calculates the optimal calibrator transform that minimizes the reprojection of the sample points.
	 *
	 * @param Rows The calibration points
	 * @param OutTransform The optimal calibrator transform
	 * @param OutErrorMessage Describes any errors encountered
	 *
	 * @return True if successful
	 */
	bool CalcCalibratorPoseForSingleCamPose(
		const TArray<TSharedPtr<FCalibrationRowData>>& Rows,
		FTransform& OutTransform,
		FText& OutErrorMessage);
};
