// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Toolkits/AssetEditorToolkit.h"

class UPCGGraph;
class UPCGEditorGraph;
class SGraphEditor;
class IDetailsView;
class FUICommandList;

class FPCGEditor : public FAssetEditorToolkit
{
public:
	/** Edits the specified PCGGraph */
	void Initialize(const EToolkitMode::Type InMode, const TSharedPtr<class IToolkitHost>& InToolkitHost, UPCGGraph* InPCGGraph);

	// ~Begin IToolkit interface
	virtual void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	// ~End IToolkit interface

	// ~Begin FAssetEditorToolkit interface
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	// ~End FAssetEditorToolkit interface

private:
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

	void OnAlignTop();
	void OnAlignMiddle();
	void OnAlignBottom();
	void OnAlignLeft();
	void OnAlignCenter();
	void OnAlignRight();
	void OnStraightenConnections();
	void OnDistributeNodesH();
	void OnDistributeNodesV();

	/** Create new graph editor widget */
	TSharedRef<SGraphEditor> CreateGraphEditorWidget();

	/** Called when the selection changes in the GraphEditor */
	void OnSelectedNodesChanged(const TSet<UObject*>& NewSelection);

	/** Called when the title of a node is changed */
	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);

	TSharedRef<SDockTab> SpawnTab_GraphEditor(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PropertyDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Library(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Attributes(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);

	TSharedPtr<SGraphEditor> GraphEditorWidget;
	TSharedPtr<IDetailsView> PropertyDetailsWidget;

	TSharedPtr<FUICommandList> GraphEditorCommands;

	UPCGGraph* PCGGraphBeingEdited = nullptr;
	UPCGEditorGraph* PCGEditorGraph = nullptr;
};