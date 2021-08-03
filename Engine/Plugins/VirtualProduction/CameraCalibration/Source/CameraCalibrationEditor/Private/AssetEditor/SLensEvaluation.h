// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#include "Camera/CameraActor.h"
#include "LiveLinkRole.h"
#include "SLensFilePanel.h"
#include "SLiveLinkSubjectRepresentationPicker.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/DeclarativeSyntaxSupport.h"


class ULensFile;
class FCameraCalibrationStepsController;

/**
 * Widget using LiveLink subject input to evaluate lens file and show resulting data
 */
class SLensEvaluation : public SCompoundWidget
{
	using Super = SCompoundWidget;
public:
	SLATE_BEGIN_ARGS(SLensEvaluation)
	{}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TWeakPtr<FCameraCalibrationStepsController> StepsController, ULensFile* InLensFile);

	//~ Begin SCompoundWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End SCompoundWidget interface

	/** Returns last evaluated FIZ data from LiveLink */
	FCachedFIZData GetLastEvaluatedData() const { return CachedLiveLinkData; }

private:

	/** Evaluates LiveLink subjects */
	void CacheLiveLinkData();

	/** Evaluates LensFile using tracking data */
	void CacheLensFileData();

	/** Get the name of the selected camera actor */
	FText GetTrackedCameraLabel() const;

	/** Get the text color for the tracked camera text */
	FSlateColor GetTrackedCameraLabelColor() const;

	/** Get the name of the livelink camera controller driving the selected camera */
	FText GetLiveLinkCameraControllerLabel() const;

	/** Get the text color for the livelink camera controller text */
	FSlateColor GetLiveLinkCameraControllerLabelColor() const;

	/** Make LiveLink subject selection widget */
	TSharedRef<SWidget> MakeTrackingWidget();

	/** Make FIZ widget evaluated from LiveLink subject */
	TSharedRef<SWidget> MakeRawInputFIZWidget() const;

	/** Make FIZ widget evaluated from LiveLink subject */
	TSharedRef<SWidget> MakeEvaluatedFIZWidget() const;

	/** Make widget showing distortion data evaluated from LensFile */
	TSharedRef<SWidget> MakeDistortionWidget() const;

	/** Make widget showing Intrinsic data evaluated from LensFile */
	TSharedRef<SWidget> MakeIntrinsicsWidget() const;

	/** Make widget showing NodalOffset data evaluated from LensFile */
	TSharedRef<SWidget> MakeNodalOffsetWidget() const;

private:

	/** LensFile being edited */
	TStrongObjectPtr<ULensFile> LensFile;

	/** Whether the tracked camera has a LiveLink Camera Controller */
	bool bCameraControllerExists = false;

	/** Flags used to know what data is valid or not and adjust UI */
	bool bCouldEvaluateDistortion = false;
	bool bCouldEvaluateFocalLength = false;
	bool bCouldEvaluateImageCenter = false;
	bool bCouldEvaluateNodalOffset = false;

	/** Calibration steps controller, which provides a selected target camera and livelink camera controller */
	TWeakPtr<FCameraCalibrationStepsController> WeakStepsController;

	//~ Cached data taken from LiveLink subject and LensFile
	FCachedFIZData CachedLiveLinkData;
	FDistortionInfo CachedDistortionInfo;
	FFocalLengthInfo CachedFocalLengthInfo;
	FImageCenterInfo CachedImageCenter;
	FNodalPointOffset CachedNodalOffset;
};