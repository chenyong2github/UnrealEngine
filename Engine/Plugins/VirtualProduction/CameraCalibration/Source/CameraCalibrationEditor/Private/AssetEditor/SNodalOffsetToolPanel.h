// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "Templates/SharedPointer.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"
#include "UObject/StrongObjectPtr.h"

class FString;
class ULensFile;

template<typename OptionType>
class SComboBox;

struct FArguments;

/**
 * UI for the nodal offset calibration.
 * It also holds the UI given by the selected nodal offset algorithm.
 */
class SNodalOffsetToolPanel : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNodalOffsetToolPanel) {}
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, ULensFile* InLensFile);

private:

	/** Builds the UI used to pick the camera used for the CG layer of the comp */
	TSharedRef<SWidget> BuildCameraPickerWidget();

	/** Builds the UI for the simulcam wiper */
	TSharedRef<SWidget> BuildSimulcamWiperWidget();

	/** Builds the wrapper for the currently selected nodal offset UI */
	TSharedRef<SWidget> BuildNodalOffsetUIWrapper();

	/** Builds the UI for the nodal offset algorithm picker */
	TSharedRef<SWidget> BuildNodalOffsetAlgoPickerWidget();

	/** Builds the UI for the media source picker */
	TSharedRef<SWidget> BuildMediaSourceWidget();

	/** Updates the UI so that it matches the selected nodal offset algorithm (if necessary) */
	void UpdateNodalOffsetUI();

	/** Refreshes the list of available nodal offset algorithms shown in the AlgosComboBox */
	void UpdateAlgosOptions();

	/** Refreshes the list of available media sources shown in the MediaSourcesComboBox */
	void UpdateMediaSourcesOptions();

private:

	/** The lens asset */
	TStrongObjectPtr<class ULensFile> LensFile;

	/** The nodal offset tool controller object */
	TSharedPtr<class FNodalOffsetTool> NodalOffsetTool;

	/** The box containing the UI given by the selected nodal offset algorithm */
	TSharedPtr<class SVerticalBox> NodalOffsetUI;

	/** Displays the title of the nodal offset UI */
	TSharedPtr<class STextBlock> NodalOffsetUITitle;
	
private:

	/** Options source for the AlgosComboBox. Lists the currently available nodal offset algorithms */
	TArray<TSharedPtr<FString>> CurrentAlgos;

	/** The combobox that presents the available nodal offset algorithms */
	TSharedPtr<SComboBox<TSharedPtr<FString>>> AlgosComboBox;

	/** Options source for the MediaSourcesComboBox. Lists the currently available media sources */
	TArray<TSharedPtr<FString>> CurrentMediaSources;

	/** The combobox that presents the available media sources */
	TSharedPtr<SComboBox<TSharedPtr<FString>>> MediaSourcesComboBox;
};
