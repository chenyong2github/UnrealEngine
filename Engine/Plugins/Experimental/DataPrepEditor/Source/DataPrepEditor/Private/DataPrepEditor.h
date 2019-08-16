// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "DataPrepAsset.h"

#include "EditorUndoClient.h"
#include "Engine/EngineBaseTypes.h"
#include "GraphEditor.h"
#include "Misc/NotifyHook.h"
#include "Toolkits/IToolkitHost.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/StrongObjectPtr.h"

class FTabManager;
class FSpawnTabArgs;
class FUICommandList;
class IMessageLogListing;
class IMessageToken;
class SDockableTab;
class SGraphNodeDetailsWidget;
class SInspectorView;
class SWidget;
class UWorld;
class UEdGraphNode;

namespace AssetPreviewWidget
{
	class SAssetsPreviewWidget;
}

/** Tuple linking an asset package path, a unique identifier and the UClass of the asset*/
typedef TTuple< FString, UClass*, EObjectFlags > FSnapshotDataEntry;

struct FDataprepSnapshot
{
	bool bIsValid;
	TArray<FSnapshotDataEntry> DataEntries;

	FDataprepSnapshot() : bIsValid(false) {}
};

typedef TTuple< UClass*, FText, FText > DataprepEditorClassDescription;

class FDataprepEditor : public FAssetEditorToolkit, public FEditorUndoClient, public FNotifyHook
{
public:
	FDataprepEditor();
	virtual ~FDataprepEditor();

	DECLARE_MULTICAST_DELEGATE(FOnDataprepAssetProducerChanged);

	FOnDataprepAssetProducerChanged& OnDataprepAssetProducerChanged()
	{
		return DataprepAssetProducerChangedDelegate;
	}

	DECLARE_MULTICAST_DELEGATE(FOnDataprepAssetConsumerChanged);

	FOnDataprepAssetConsumerChanged& OnDataprepAssetConsumerChanged()
	{
		return DataprepAssetConsumerChangedDelegate;
	}

	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	void InitDataprepEditor(const EToolkitMode::Type Mode, const TSharedPtr< IToolkitHost >& InitToolkitHost, UDataprepAsset* InDataprepAsset);

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;

	/** @return Returns the color and opacity to use for the color that appears behind the tab text for this toolkit's tab in world-centric mode. */
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	UDataprepAsset* GetDataprepAsset() const
	{
		return DataprepAssetPtr.IsValid() ? DataprepAssetPtr.Get() : nullptr;
	}

	/** Gets or sets the flag for context sensitivity in the graph action menu */
	bool& GetIsContextSensitive() { return bIsActionMenuContextSensitive; }

	/** Returns root package which all transient packages are created under */
	static const FString& GetRootPackagePath();

	/** Returns root directory which all transient directories and data are created under */
	static const FString& GetRootTemporaryDir();

private:
	void BindCommands();
	void OnSaveScene();
	void OnBuildWorld();
	void ResetBuildWorld();
	void CleanPreviewWorld();
	void OnExecutePipeline();
	void OnCommitWorld();

	/** Updates asset preview and scene outliner */
	void UpdatePreviewPanels();

	void CreateTabs();

	void CreateScenePreviewTab();

	TSharedRef<SDockTab> SpawnTabAssetPreview(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTabScenePreview(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTabPalette(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTabDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTabDataprep(const FSpawnTabArgs& Args);

	void TryInvokingDetailsTab(bool bFlash);

	/** Extends the toolbar menu to include static mesh editor options */
	void ExtendMenu();

	/** Builds the Data Prep Editor toolbar. */
	void ExtendToolBar();

	void CreateDetailsViews();

	/** Gets a multi-cast delegate which is called whenever the graph object is changed to a different graph. */
	//FOnGraphChanged& OnGraphChanged();

	/** Called whenever the selected nodes in the graph editor changes. */
	void OnPipelineEditorSelectionChanged(const TSet<UObject*>& SelectedNodes);

	/** 
	 * Change the objects display by the details panel
	 * @param bCanEditProperties Should the details view let the user modify the objects.
	 */
	void SetDetailsObjects(const TSet<UObject*>& Objects, bool bCanEditProperties);

	bool CanExecutePipeline();
	bool CanBuildWorld();
	bool CanCommitWorld();

	virtual bool OnRequestClose() override;

	/** Returns content folder under which all assets are stored after execution of all producers */
	FString GetTransientContentFolder();

	/** Create a snapshot of the world and tracked assets */
	void TakeSnapshot();

	/** Recreate preview world from snapshot */
	void RestoreFromSnapshot();

	/** Handles changes in the Dataprep asset */
	void OnDataprepAssetChanged(FDataprepAssetChangeType ChangeType, int32 Index);

	/** Handles change to the Dataprep pipeline */
	void OnDataprepPipelineChange(UObject* ChangedObject);

	/** Remove all temporary data remaining from previous runs of the Dataprep editor */
	void CleanUpTemporaryDirectories();

private:
	bool bWorldBuilt;
	bool bIsFirstRun;
	bool bPipelineChanged;
	TWeakObjectPtr<UDataprepAsset> DataprepAssetPtr;

	FOnDataprepAssetProducerChanged DataprepAssetProducerChangedDelegate;
	FOnDataprepAssetConsumerChanged DataprepAssetConsumerChangedDelegate;

	TWeakPtr<SDockTab> DetailsTabPtr;
	TSharedPtr<SWidget> ViewportView;
	TSharedPtr<AssetPreviewWidget::SAssetsPreviewWidget> AssetPreviewView;
	TSharedPtr<SWidget> ScenePreviewView;
	TSharedPtr<SGraphNodeDetailsWidget> DetailsView;
	TSharedPtr<class SDataprepAssetView > DataprepAssetView;
	TSharedPtr<class SGraphEditor> PipelineView;

	TSharedPtr<class ISceneOutliner> SceneOutliner;

	/** Command list for the pipeline editor */
	TSharedPtr<FUICommandList> PipelineEditorCommands;
	bool bIsActionMenuContextSensitive;
	bool bSaveIntermediateBuildProducts;

	/**
	 * All assets tracked for this editor.
	 */
	TArray<TWeakObjectPtr<UObject>> Assets;
	TSet<TWeakObjectPtr<UObject>> CachedAssets;

	/**
	 * The world used to preview the inputs
	 */
	TStrongObjectPtr<UWorld> PreviewWorld;

	TSet<class AActor*> DefaultActorsInPreviewWorld;

	/** flag raised to prevent this editor to be closed */
	bool bIgnoreCloseRequest;

	/** Array of UClasses deriving from UDataprepContentConsumer */
	TArray< DataprepEditorClassDescription > ConsumerDescriptions;

	/** Temporary folder used to store content from snapshot */
	FString TempDir;

	/** Unique identifier assigned to each opened Dataprep editor to avoid name collision on cached data */
	FString SessionID;

	/** Structure to hold on the content of the latest call to OnBuildWorld */
	FDataprepSnapshot ContentSnapshot;

	/** Helper member to record classes of assets' sub-objects */
	TMap<FString, UClass*> SnapshotClassesMap;

	/**	The tab ids for all the tabs used */
	static const FName ScenePreviewTabId;
	static const FName AssetPreviewTabId;
	static const FName PaletteTabId;
	static const FName DetailsTabId;
	static const FName DataprepAssetTabId;

//Temp Code to allow us to work on the nodes while we don't have our own graph.
public:
	UBlueprint* GetDataprepBlueprint()
	{
		return DataprepRecipeBPPtr.IsValid() ? DataprepRecipeBPPtr.Get() : nullptr;
	}

private:

	TSharedRef<SDockTab> SpawnTabPipelineGraph(const FSpawnTabArgs& Args);

	void CreatePipelineEditor();

	
	/** Called to create context menu when right-clicking on graph */
	FActionMenuContent OnCreatePipelineActionMenu(UEdGraph* InGraph, const FVector2D& InNodePosition, const TArray<UEdGraphPin*>& InDraggedPins, bool bAutoExpand, SGraphEditor::FActionMenuClosed InOnMenuClosed);

	/** Called when the Pipeline Blueprint has changed */
	void OnPipelineChanged(UBlueprint* InBlueprint);

	/** Called when the Pipeline Blueprint has been compiled */
	void OnPipelineCompiled(UBlueprint* InBlueprint);

	/** Callback when a token is clicked on in the compiler results log */
	void OnLogTokenClicked(const TSharedRef<IMessageToken>& Token);

	/** Callback when properties have finished being handled */
	//virtual void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);

	/** Called when Compile button is clicked */
	void OnCompile();

	/** Returns the current status icon for the blueprint being edited */
	FSlateIcon GetPipelineCompileButtonImage() const;
	FText GetPipelineCompileButtonTooltip() const;

	TSet<UObject*> GetSelectedPipelineNodes() const;
	UEdGraph* GetPipelineGraph() const;
	bool IsPipelineEditable() const;

	void OnRenameNode();
	bool CanRenameNode() const;

	void SelectAllNodes();
	bool CanSelectAllNodes() const;

	void DeleteSelectedPipelineNodes();
	bool CanDeletePipelineNodes() const;

	void CopySelectedNodes();
	bool CanCopyNodes() const;

	void CutSelectedNodes();
	bool CanCutNodes() const;

	void PasteNodes();
	bool CanPasteNodes() const;

	void DuplicateNodes();
	bool CanDuplicateNodes() const;

	void OnCreateComment();

	void DeleteSelectedDuplicatableNodes();
	void PasteNodesHere(class UEdGraph* DestinationGraph, const FVector2D& GraphLocation);

	bool OnNodeVerifyTitleCommit(const FText& NewText, UEdGraphNode* NodeBeingChanged, FText& OutErrorMessage);
	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);

	// Start of temp code
	TWeakObjectPtr<UBlueprint> DataprepRecipeBPPtr;

	/** Compiler results log, with the log listing that it reflects */
	TSharedPtr<SWidget> CompilerResults;
	TSharedPtr<IMessageLogListing> CompilerResultsListing;

	UEdGraphNode* StartNode;

	static const FName PipelineGraphTabId;
	// End of temp code
};
