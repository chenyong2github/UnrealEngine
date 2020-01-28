// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Toolkits/BaseToolkit.h"
#include "InteractiveTool.h"
#include "ModelingToolsEditorMode.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/STextComboBox.h"


#include "Widgets/SBoxPanel.h"

class IDetailsView;
class SButton;
class STextBlock;

class FModelingToolsEditorModeToolkit : public FModeToolkit
{
public:

	FModelingToolsEditorModeToolkit();
	~FModelingToolsEditorModeToolkit();
	
	/** FModeToolkit interface */
	virtual void Init(const TSharedPtr<IToolkitHost>& InitToolkitHost) override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual class FEdMode* GetEditorMode() const override;
	virtual TSharedPtr<class SWidget> GetInlineContent() const override { return ToolkitWidget; }

	virtual FModelingToolsEditorMode* GetToolsEditorMode() const;
	virtual UEdModeInteractiveToolsContext* GetToolsContext() const;

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

	virtual FText GetActiveToolDisplayName() const override { return ActiveToolName; }
	virtual FText GetActiveToolMessage() const override { return ActiveToolMessage; }

	virtual void BuildToolPalette_Standard(FName PaletteName, class FToolBarBuilder& ToolbarBuilder);
	virtual void BuildToolPalette_Experimental(FName PaletteName, class FToolBarBuilder& ToolbarBuilder);

	virtual void EnableShowRealtimeWarning(bool bEnable);




private:
	const static TArray<FName> PaletteNames_Standard;
	const static TArray<FName> PaletteNames_Experimental;

	FText ActiveToolName;
	FText ActiveToolMessage;

	TSharedPtr<SWidget> ToolkitWidget;
	TSharedPtr<IDetailsView> DetailsView;
	void UpdateActiveToolProperties();


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
	void OnAssetSettingsModified();
	FDelegateHandle AssetSettingsModifiedHandle;
	void OnShowAssetSettings();
};
