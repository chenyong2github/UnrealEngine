// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"
#include "InteractiveTool.h"
#include "ModelingToolsEditorMode.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/STextComboBox.h"
#include "Widgets/SBoxPanel.h"
#include "StatusBarSubsystem.h"


class IDetailsView;
class SButton;
class STextBlock;

class FModelingToolsEditorModeToolkit : public FModeToolkit
{
public:

	FModelingToolsEditorModeToolkit();
	~FModelingToolsEditorModeToolkit();
	
	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost, TWeakObjectPtr<UEdMode> InOwningMode) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ToolkitWidget; }

	// initialize toolkit widgets that need to wait until mode is initialized/entered
	virtual void InitializeAfterModeSetup();

	// set/clear notification message area
	virtual void PostNotification(const FText& Message);
	virtual void ClearNotification();

	// set/clear warning message area
	virtual void PostWarning(const FText& Message);
	virtual void ClearWarning();

	/** Returns the Mode specific tabs in the mode toolbar **/ 
	virtual void GetToolPaletteNames(TArray<FName>& InPaletteName) const;
	virtual FText GetToolPaletteDisplayName(FName PaletteName) const; 
	virtual void BuildToolPalette(FName PaletteName, class FToolBarBuilder& ToolbarBuilder);
	virtual void OnToolPaletteChanged(FName PaletteName) override;
	virtual bool HasIntegratedToolPalettes() const { return false; }
	virtual bool HasExclusiveToolPalettes() const { return false; }

	virtual FText GetActiveToolDisplayName() const override { return ActiveToolName; }
	virtual FText GetActiveToolMessage() const override { return ActiveToolMessage; }

	virtual void EnableShowRealtimeWarning(bool bEnable);

	virtual void OnToolStarted(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;
	virtual void OnToolEnded(UInteractiveToolManager* Manager, UInteractiveTool* Tool) override;

	virtual void CustomizeModeDetailsViewArgs(FDetailsViewArgs& ArgsInOut) override;

	void OnActiveViewportChanged(TSharedPtr<IAssetViewport>, TSharedPtr<IAssetViewport> );



	virtual void InvokeUI() override;

private:
	const static TArray<FName> PaletteNames_Standard;
	const static TArray<FName> PaletteNames_Experimental;

	FText ActiveToolName;
	FText ActiveToolMessage;
	FStatusBarMessageHandle ActiveToolMessageHandle;
	const FSlateBrush* ActiveToolIcon;

	TSharedPtr<SWidget> ToolkitWidget;
	void UpdateActiveToolProperties();
	void InvalidateCachedDetailPanelState(UObject* ChangedObject);

	TSharedPtr<SWidget> ViewportOverlayWidget;

	TSharedPtr<STextBlock> ModeWarningArea;
	TSharedPtr<STextBlock> ModeHeaderArea;
	TSharedPtr<STextBlock> ToolWarningArea;
	TSharedPtr<SButton> AcceptButton;
	TSharedPtr<SButton> CancelButton;
	TSharedPtr<SButton> CompletedButton;

	TSharedPtr<SWidget> MakeAssetConfigPanel();

	bool bShowRealtimeWarning = false;
	void UpdateShowWarnings();

	TArray<TSharedPtr<FString>> AssetLocationModes;
	TArray<TSharedPtr<FString>> AssetSaveModes;
	TSharedPtr<STextComboBox> AssetLocationMode;
	TSharedPtr<STextComboBox> AssetSaveMode;
	void UpdateAssetLocationMode(TSharedPtr<FString> NewString);
	void UpdateAssetSaveMode(TSharedPtr<FString> NewString);
	void UpdateAssetPanelFromSettings();
	void UpdateObjectCreationOptionsFromSettings();
	void OnAssetSettingsModified();
	FDelegateHandle AssetSettingsModifiedHandle;
	void OnShowAssetSettings();

	TArray<TSharedPtr<FString>> AssetLODModes;
	TSharedPtr<STextBlock> AssetLODModeLabel;
	TSharedPtr<STextComboBox> AssetLODMode;

	bool bFirstInitializeAfterModeSetup = true;

	// custom accept/cancel/complete handlers
};
