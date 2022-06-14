// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once
#include "Toolkits/BaseToolkit.h"
#include "EditorModeManager.h"
#include "StatusBarSubsystem.h"

namespace LidarEditorPalletes
{
	static const FName Manage(TEXT("ToolMode_Manage")); 
	static const FName Edit(TEXT("ToolMode_Edit"));
}

/**
 * Public interface to Lidar Edit mode.
 */
class FLidarPointCloudEdModeToolkit : public FModeToolkit
{
public:
	FLidarPointCloudEdModeToolkit(){}
	~FLidarPointCloudEdModeToolkit();
	
	/** Initializes the Lidar mode toolkit */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual void GetToolPaletteNames(TArray<FName>& InPaletteName) const override;
	virtual FText GetToolPaletteDisplayName(FName PaletteName) const override;
	virtual bool HasIntegratedToolPalettes() const override { return true; }	
	virtual FText GetActiveToolDisplayName() const override;
	virtual FText GetActiveToolMessage() const override;

	void SetActiveToolMessage(const FText& Message);

private:
	FStatusBarMessageHandle ActiveToolMessageHandle;
	FText ActiveToolMessageCache;
};