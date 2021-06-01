// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Toolkits/AssetEditorToolkit.h"

#include "CoreMinimal.h"
#include "LensFile.h"
#include "CameraCalibrationEditorCommon.h"


class IToolkitHost;
class SLensFilePanel;
class SNodalOffsetToolPanel;
class SLensEvaluation;
class ULensFile;
class FCameraCalibrationStepsController;

/** Toolkit to do camera calibration */
class FCameraCalibrationToolkit : public FAssetEditorToolkit
{
	using Super = FAssetEditorToolkit;

public:
	/** Creates an editor for LensFile assets */
	static TSharedRef<FCameraCalibrationToolkit> CreateEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULensFile* InLensFile);

	/**
	 * Edits the specified table
	 *
	 * @param	Mode					Asset editing mode for this editor (standalone or world-centric)
	 * @param	InitToolkitHost			When Mode is WorldCentric, this is the level editor instance to spawn this editor within
	 * @param	InLensFile				The LensFile asset to edit
	 */
	void InitCameraCalibrationTool(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, ULensFile* InLensFile);

protected:

	//~ Begin IToolkit interface
	virtual FText GetBaseToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual bool OnRequestClose() override;
	//~ End IToolkit interface

private:

	/** Handle spawning the tab that holds the LensFile panel tab. */
	TSharedRef<SDockTab> HandleSpawnLensEditorTab(const FSpawnTabArgs& Args) const;

	/** Handle spawning the tab that holds the NodalOffset panel tab. */
	TSharedRef<SDockTab> HandleSpawnNodalOffsetTab(const FSpawnTabArgs& Args) const;
	
	/** Handle spawning the tab that holds the current evaluated data */
	TSharedRef<SDockTab> HandleSpawnLensEvaluationTab(const FSpawnTabArgs& Args) const;

	/** Temporary tab to view the details panel of the lens file */
	TSharedRef<SDockTab> HandleSpawnLensDetailsPanelTab(const FSpawnTabArgs& Args) const;

	FCachedFIZData GetFIZData() const;


private:

	/** Toolkit is adding a reference to the editing object so a standard pointer is ok */
	ULensFile* LensFile = nullptr;

	/** Lens editor tab to do manual adjustments */
	TSharedPtr<SLensFilePanel> LensEditorTab;

	/** Camera calibration tools */
	TSharedPtr<SWidget> CalibrationStepsTab;

	/** Data displaying evaluated data based on LiveLink inputs */
	TSharedPtr<SLensEvaluation> LensEvaluationWidget;

	/** Calibration Steps Controller */
	TSharedPtr<FCameraCalibrationStepsController> CalibrationStepsController;
};