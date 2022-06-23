// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "PCGEditorModule.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "EditorUndoClient.h"

class FUICommandList;
class IDetailsView;
class SGraphEditor;
class SPCGEditorGraphDeterminismListView;
class SPCGEditorGraphFind;
class SPCGEditorGraphNodePalette;
class UPCGEditorGraph;
class UPCGGraph;

class FPCGEditor : public FAssetEditorToolkit, public FSelfRegisteringEditorUndoClient
{
public:
	/** Edits the specified PCGGraph */
	void Initialize(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UPCGGraph* InPCGGraph);

	/** Get the PCG graph being edited */
	UPCGEditorGraph* GetPCGEditorGraph();

	/** Focus the graph view on a specific node */
	void JumpToNode(const UEdGraphNode* InNode);

	// ~Begin IToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	// ~End IToolkit interface

	// ~Begin FEditorUndoClient interface
	virtual bool MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject *, FTransactionObjectEvent>>& TransactionObjectContexts) const override;
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	// ~End FEditorUndoClient interface

	// ~Begin FAssetEditorToolkit interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual void OnClose() override;
	// ~End FAssetEditorToolkit interface

protected:
	// ~Begin FAssetEditorToolkit interface
	/** Called when "Save" is clicked for this asset */
	virtual void SaveAsset_Execute() override;
	// ~End FAssetEditorToolkit interface

private:
	/** Bind commands to delegates */
	void BindCommands();

	/** Bring up the find tab */
	void OnFind();

	/** Can determinism be tested on this node */
	bool CanRunDeterminismTests() const;
	/** Summon the Determinism tab */
	void OnDeterminismTests();

	/** Select every node in the graph */
	void SelectAllNodes();
	/** Whether we can select every node */
	bool CanSelectAllNodes() const;

	/** Delete all selected nodes in the graph */
	void DeleteSelectedNodes();
	/** Whether we can delete all selected nodes */
	bool CanDeleteSelectedNodes() const;

	/** Copy all selected nodes in the graph */
	void CopySelectedNodes();
	/** Whether we can copy all selected nodes */
	bool CanCopySelectedNodes() const;

	/** Cut all selected nodes in the graph */
	void CutSelectedNodes();
	/** Whether we can cut all selected nodes */
	bool CanCutSelectedNodes() const;

	/** Paste nodes in the graph */
	void PasteNodes();
	/** Paste nodes in the graph at location*/
	void PasteNodesHere(const FVector2D& Location);
	/** Whether we can paste nodes */
	bool CanPasteNodes() const;

	/** Duplicate the currently selected nodes */
	void DuplicateNodes();
	/** Whether we are able to duplicate the currently selected nodes */
	bool CanDuplicateNodes() const;

	/** Collapse the currently selected nodes in a subgraph */
	void OnCollapseNodesInSubgraph();
	/** Whether we can collapse nodes in a subgraph */
	bool CanCollapseNodesInSubgraph();

	void OnAlignTop();
	void OnAlignMiddle();
	void OnAlignBottom();
	void OnAlignLeft();
	void OnAlignCenter();
	void OnAlignRight();
	void OnStraightenConnections();
	void OnDistributeNodesH();
	void OnDistributeNodesV();
	void OnCreateComment();

	/** Create new graph editor widget */
	TSharedRef<SGraphEditor> CreateGraphEditorWidget();

	/** Create new palette widget */
	TSharedRef<SPCGEditorGraphNodePalette> CreatePaletteWidget();

	/** Create new find widget */
	TSharedRef<SPCGEditorGraphFind> CreateFindWidget();

	/** Create a new determinism tab widget */
	TSharedRef<SPCGEditorGraphDeterminismListView> CreateDeterminismWidget();

	/** Called when the selection changes in the GraphEditor */
	void OnSelectedNodesChanged(const TSet<UObject*>& NewSelection);

	/** Called when the title of a node is changed */
	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);

	/**
	 * Called when a node is double clicked
	 *
	 * @param Node - The Node that was clicked
	 */
	void OnNodeDoubleClicked(UEdGraphNode* Node);

	/**
	 * Try to jump to a given class (if allowed)
	 *
	 * @param Class - The Class to jump to
	 */
	void JumpToDefinition(const UClass* Class) const;

	/** To be called everytime we need to replicate our extra nodes to the underlying PCGGraph */
	void ReplicateExtraNodes() const;

	TSharedRef<SDockTab> SpawnTab_GraphEditor(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PropertyDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Palette(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Attributes(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Find(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Determinism(const FSpawnTabArgs& Args);

	TSharedPtr<SGraphEditor> GraphEditorWidget;
	TSharedPtr<IDetailsView> PropertyDetailsWidget;
	TSharedPtr<SPCGEditorGraphNodePalette> PaletteWidget;
	TSharedPtr<SPCGEditorGraphFind> FindWidget;
	TSharedPtr<SPCGEditorGraphDeterminismListView> DeterminismWidget;

	TSharedPtr<FUICommandList> GraphEditorCommands;

	UPCGGraph* PCGGraphBeingEdited = nullptr;
	UPCGEditorGraph* PCGEditorGraph = nullptr;
};