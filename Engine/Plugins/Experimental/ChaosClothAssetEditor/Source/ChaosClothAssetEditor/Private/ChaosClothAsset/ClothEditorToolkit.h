// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "BaseCharacterFXEditorToolkit.h"
#include "Dataflow/DataflowObjectInterface.h"

class FAdvancedPreviewScene;
class FChaosClothAssetEditor3DViewportClient;
template<typename T> class SComboBox;
class SClothCollectionOutliner;
class UDataflow;
class UChaosClothAsset;
class SGraphEditor;
class IStructureDetailsView;
class UEdGraphNode;

namespace Dataflow
{
	class CHAOSCLOTHASSETEDITOR_API FClothAssetDataflowContext : public TEngineContext<FContextSingle>
	{
	public:
		DATAFLOW_CONTEXT_INTERNAL(TEngineContext<FContextSingle>, FClothAssetDataflowContext);

		FClothAssetDataflowContext(UObject* InOwner, UDataflow* InGraph, FTimestamp InTimestamp)
			: Super(InOwner, InGraph, InTimestamp)
		{}
	};
}

/**
 * The toolkit is supposed to act as the UI manager for the asset editor. It's responsible 
 * for setting up viewports and most toolbars, except for the internals of the mode panel.
 * However, because the toolkit also sets up the mode manager, and much of the important
 * state is held in the UChaosClothAssetEditorMode managed by the mode manager, the toolkit also ends up
 * initializing the Cloth mode.
 * Thus, the FChaosClothAssetEditorToolkit ends up being the central place for the Cloth Asset Editor setup.
 */
class CHAOSCLOTHASSETEDITOR_API FChaosClothAssetEditorToolkit : public FBaseCharacterFXEditorToolkit, public FTickableEditorObject
{
public:

	FChaosClothAssetEditorToolkit(UAssetEditor* InOwningAssetEditor);
	virtual ~FChaosClothAssetEditorToolkit();

	static const FName ClothPreviewTabID;
	static const FName OutlinerTabID;

	// FAssetEditorToolkit
	virtual void AddViewportOverlayWidget(TSharedRef<SWidget> InViewportOverlayWidget) override;
	virtual void RemoveViewportOverlayWidget(TSharedRef<SWidget> InViewportOverlayWidget) override;

	virtual FText GetToolkitName() const override;
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FText GetToolkitToolTipText() const override;
	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual bool OnRequestClose() override;

	// FBaseAssetToolkit
	virtual void CreateWidgets() override;

	// IAssetEditorInstance
	// This is important because if this returns true, attempting to edit a static mesh that is
	// open in the cloth editor may open the cloth editor instead of opening the static mesh editor.
	// TODO: Change this if we create a dedicated Cloth Asset
	virtual bool IsPrimaryEditor() const override { return false; };

	// FTickableEditorObject
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override { return true; }
	virtual TStatId GetStatId() const override;

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

	// Appearance customization points
	// TODO: Implement these when we have an FChaosClothAssetEditorStyle class
	//virtual const FSlateBrush* GetDefaultTabIcon() const override;
	//virtual FLinearColor GetDefaultTabColor() const override;

private:

	// Return the cloth asset held by the Cloth Editor
	UChaosClothAsset* GetAsset() const;

	TSharedRef<SDockTab> SpawnTab_ClothPreview(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Outliner(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_GraphCanvas(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_NodeDetails(const FSpawnTabArgs& Args);

	/** Scene in which the 3D sim space preview meshes live. */
	TUniquePtr<FAdvancedPreviewScene> ClothPreviewScene;

	TSharedPtr<class FEditorViewportTabContent> ClothPreviewTabContent;
	AssetEditorViewportFactoryFunction ClothPreviewViewportDelegate;
	TSharedPtr<FChaosClothAssetEditor3DViewportClient> ClothPreviewViewportClient;
	TSharedPtr<FAssetEditorModeManager> ClothPreviewEditorModeManager;

	TWeakPtr<SEditorViewport> RestSpaceViewport;

	void InitDetailsViewPanel();
	
	TSharedPtr<SClothCollectionOutliner> OutlinerView;

	TSharedPtr<SComboBox<FName>> SelectedGroupNameComboBox;
	TArray<FName> ClothCollectionGroupNames;		// Data source for SelectedGroupNameComboBox

	//~ Begin Dataflow support
	 
	UDataflow* Dataflow = nullptr;

	static const FName GraphCanvasTabId;
	TSharedPtr<SGraphEditor> GraphEditor;
	TSharedRef<SGraphEditor> CreateGraphEditorWidget(UDataflow* ObjectToEdit, TSharedPtr<IStructureDetailsView> PropertiesEditor);

	static const FName NodeDetailsTabId;
	TSharedPtr<IStructureDetailsView> NodeDetailsEditor;
	TSharedPtr<IStructureDetailsView> CreateNodeDetailsEditorWidget(UObject* ObjectToEdit);

	FString DataflowTerminalPath = "";
	TSharedPtr<Dataflow::FEngineContext> DataflowContext;
	Dataflow::FTimestamp LastDataflowNodeTimestamp = Dataflow::FTimestamp::Invalid;

	// DataflowEditorActions
	void OnPropertyValueChanged(const FPropertyChangedEvent& PropertyChangedEvent);
	bool OnNodeVerifyTitleCommit(const FText& NewText, UEdGraphNode* GraphNode, FText& OutErrorMessage) const;
	void OnNodeTitleCommitted(const FText& InNewText, ETextCommit::Type InCommitType, UEdGraphNode* GraphNode) const;

	//~ End Dataflow support

};

