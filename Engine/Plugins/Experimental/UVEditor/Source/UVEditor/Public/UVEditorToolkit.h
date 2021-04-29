// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Tools/BaseAssetToolkit.h"

class FAdvancedPreviewScene;
class IDetailsView;
class SDockTab;
class SBorder;
class UInteractiveToolsContext;

/**
 * The toolkit is supposed to act as the UI manager for the asset editor. It's responsible 
 * for setting up viewports and most toolbars, except for the internals of the mode panel.
 * However, because the toolkit also sets up the mode manager, and much of the important
 * state is held in the UUVEditorMode managed by the mode manager, the toolkit also ends up
 * initializing the UV mode.
 * Thus, the FUVEdiotrToolkit ends up being the central place for the UV Asset editor setup.
 */
class UVEDITOR_API FUVEditorToolkit : public FBaseAssetToolkit
{
public:
	FUVEditorToolkit(UAssetEditor* InOwningAssetEditor);
	virtual ~FUVEditorToolkit();

	static const FName InteractiveToolsPanelTabID;

	FPreviewScene* GetPreviewScene() { return PreviewScene.Get(); }

	// FBaseAssetToolkit
	virtual void CreateWidgets() override;

	// FAssetEditorToolkit
	virtual void CreateEditorModeManager() override;
	virtual FText GetToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual bool OnRequestClose() override;
	virtual void SaveAsset_Execute() override;

	// IAssetEditorInstance
	// This is important because if this returns true, attempting to edit a static mesh that is
	// open in the UV editor may open the UV editor instead of opening the static mesh editor.
	virtual bool IsPrimaryEditor() const override { return false; };

protected:

	TSharedRef<SDockTab> SpawnTab_InteractiveToolsPanel(const FSpawnTabArgs& Args);

	// FBaseAssetToolkit
	virtual AssetEditorViewportFactoryFunction GetViewportDelegate() override;
	virtual TSharedPtr<FEditorViewportClient> CreateEditorViewportClient() const override;

	// FAssetEditorToolkit
	virtual void PostInitAssetEditor() override;

	/** Inline content area for the UV mode's content (gotten from UVEditorModeToolkit) */
	TSharedPtr<SDockTab> ToolsPanel;

	TUniquePtr<FPreviewScene> PreviewScene;
};