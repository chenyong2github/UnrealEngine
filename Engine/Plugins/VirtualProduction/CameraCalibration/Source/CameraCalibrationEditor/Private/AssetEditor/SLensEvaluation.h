// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Widgets/SCompoundWidget.h"

#include "LiveLinkRole.h"
#include "SLensFilePanel.h"
#include "SLiveLinkSubjectRepresentationPicker.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "UObject/StrongObjectPtr.h"


class ULensFile;

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

	void Construct(const FArguments& InArgs, ULensFile* InLensFile);

	//~ Begin SCompoundWidget interface
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	//~ End SCompoundWidget interface

	/** Returns last evaluated FIZ data from LiveLink */
	FCachedFIZData GetLastEvaluatedData() const { return CachedLiveLinkData; }

private:

	/** Returns tracking checkbox state */
	ECheckBoxState IsTrackingActive() const;

	/** Triggered when tracking checkbox state changes */
	void OnTrackingStateChanged(ECheckBoxState NewState);

	/** Whether tracking should be active and source can be selected */
	bool CanSelectTrackingSource() const;

	/** Returns the currently selected LiveLink subject */
	SLiveLinkSubjectRepresentationPicker::FLiveLinkSourceSubjectRole GetTrackingSubject() const;

	/** Set LiveLink subject to use */
	void SetTrackingSubject(SLiveLinkSubjectRepresentationPicker::FLiveLinkSourceSubjectRole NewValue);

	/** Evaluates LiveLink subjects */
	void CacheLiveLinkData();

	/** Evaluates LensFile using tracking data */
	void CacheLensFileData();

	/** Whether it should evaluate LiveLink subject */
	bool ShouldUpdateTracking() const { return IsTrackingActive() == ECheckBoxState::Checked; }

	/** Make LiveLink subject selection widget */
	TSharedRef<SWidget> MakeTrackingWidget();
	
	/** Make FIZ widget evaluated from LiveLink subject */
	TSharedRef<SWidget> MakeFIZWidget() const;

	/** Make widget showing distortion data evaluated from LensFile */
	TSharedRef<SWidget> MakeDistortionWidget() const;

	/** Make widget showing Intrinsic data evaluated from LensFile */
	TSharedRef<SWidget> MakeIntrinsicsWidget() const;

	/** Make widget showing NodalOffset data evaluated from LensFile */
	TSharedRef<SWidget> MakeNodalOffsetWidget() const;

private:

	/** LensFile being edited */
	TStrongObjectPtr<ULensFile> LensFile;

	/** Whether LiveLink is used */
	bool bIsUsingLiveLinkTracking = true;

	/** Subject used to get tracking input */
	FLiveLinkSubjectRepresentation TrackingSource;

	//~ Cached data taken from LiveLink subject and LensFile
	FCachedFIZData CachedLiveLinkData;
	FDistortionInfo CachedDistortionInfo;
	FFocalLengthInfo CachedFocalLengthInfo;
	FImageCenterInfo CachedImageCenter;
	FNodalPointOffset CachedNodalOffset;
};