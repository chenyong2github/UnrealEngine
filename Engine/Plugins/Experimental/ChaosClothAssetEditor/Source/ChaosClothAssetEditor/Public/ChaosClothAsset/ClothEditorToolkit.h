// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseCharacterFXEditorToolkit.h"

class FAdvancedPreviewScene;
class FChaosClothAssetEditor3DViewportClient;

/**
 * The toolkit is supposed to act as the UI manager for the asset editor. It's responsible 
 * for setting up viewports and most toolbars, except for the internals of the mode panel.
 * However, because the toolkit also sets up the mode manager, and much of the important
 * state is held in the UChaosClothAssetEditorMode managed by the mode manager, the toolkit also ends up
 * initializing the Cloth mode.
 * Thus, the FChaosClothAssetEditorToolkit ends up being the central place for the Cloth Asset Editor setup.
 */
class CHAOSCLOTHASSETEDITOR_API FChaosClothAssetEditorToolkit : public FBaseCharacterFXEditorToolkit
{
public:

	FChaosClothAssetEditorToolkit(UAssetEditor* InOwningAssetEditor);
	virtual ~FChaosClothAssetEditorToolkit();

	static const FName ClothPreviewTabID;
	static const FName InteractiveToolsPanelTabID;

	// FAssetEditorToolkit
	virtual void AddViewportOverlayWidget(TSharedRef<SWidget> InViewportOverlayWidget) override;
	virtual void RemoveViewportOverlayWidget(TSharedRef<SWidget> InViewportOverlayWidget) override;

	virtual FText GetToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual bool OnRequestClose() override;

	// TODO: Implement this if we want to apply tool changes before saving an asset
	//virtual void SaveAsset_Execute() override;

	// IAssetEditorInstance
	// This is important because if this returns true, attempting to edit a static mesh that is
	// open in the cloth editor may open the cloth editor instead of opening the static mesh editor.
	// TODO: Change this if we create a dedicated Cloth Asset
	virtual bool IsPrimaryEditor() const override { return false; };

protected:

	// FBaseAssetToolkit
	virtual AssetEditorViewportFactoryFunction GetViewportDelegate() override;
	virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;

	// FAssetEditorToolkit
	virtual void PostInitAssetEditor() override;

	// FBaseCharacterFXEditorToolkit
	virtual FEditorModeID GetEditorModeId() const override;
	virtual void InitializeEdMode(UBaseCharacterFXEditorMode* EdMode) override;
	virtual void CreateEditorModeUILayer() override;

	TSharedRef<SDockTab> SpawnTab_ClothPreview(const FSpawnTabArgs& Args);

	// Appearance customization points
	// TODO: Implement these when we have an FChaosClothAssetEditorStyle class
	//virtual const FSlateBrush* GetDefaultTabIcon() const override;
	//virtual FLinearColor GetDefaultTabColor() const override;

	/** Scene in which the 3D sim space preview meshes live. */
	TUniquePtr<FAdvancedPreviewScene> ClothPreviewScene;

	TSharedPtr<class FEditorViewportTabContent> ClothPreviewTabContent;
	AssetEditorViewportFactoryFunction ClothPreviewViewportDelegate;
	TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothPreviewViewportClient;
	TSharedPtr<FAssetEditorModeManager> ClothPreviewEditorModeManager;

	TWeakPtr<SEditorViewport> RestSpaceViewport;

	// TODO as necessary:
	//TObjectPtr<UInputRouter> ClothPreviewInputRouter = nullptr;
	//UClothToolViewportButtonsAPI* ViewportButtonsAPI = nullptr;
};

