// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusEditor.h"

#include "Framework/Docking/TabManager.h"
#include "Misc/NotifyHook.h"

struct FGraphAppearanceInfo;
class FUICommandList;
class UOptimusDeformer;
class UOptimusEditorGraph;
class SGraphEditor;
class SOptimusEditorViewport;
class SOptimusNodePalette;

class FOptimusEditor
	: public IOptimusEditor
	, public FNotifyHook
{
public:
	FOptimusEditor();
	~FOptimusEditor();

	void Construct(
		const EToolkitMode::Type InMode,
		const TSharedPtr< class IToolkitHost >& InToolkitHost,
		UOptimusDeformer* InDeformerObject
	);

	/// @brief Returns the graph that this editor operates on.
	/// @return The graph that this editor operates on.
	UOptimusEditorGraph* GetGraph() const
	{
		return DeformerGraph;
	}

	// IToolkit overrides
	FName GetToolkitFName() const override;				
	FText GetBaseToolkitName() const override;			
	FString GetWorldCentricTabPrefix() const override;	
	FLinearColor GetWorldCentricTabColorScale() const override;

	// --

private:
	// ----------------------------------------------------------------------------------------
	// Graph commands

	/// Select all nodes in the visible graph
	void SelectAllNodes();

	/// Returns \c true if all the nodes can be selected.
	bool CanSelectAllNodes() const;

	/// Delete all selected nodes in the graph
	void DeleteSelectedNodes();

	/// Returns \c true if all the nodes can be selected.
	bool CanDeleteSelectedNodes() const;


	// ----------------------------------------------------------------------------------------
	// Graph event listeners
	void OnSelectedNodesChanged(const TSet<class UObject*>& NewSelection);
	void OnNodeDoubleClicked(class UEdGraphNode* Node);
	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);
	bool OnVerifyNodeTextCommit(const FText& NewText, UEdGraphNode* NodeBeingChanged, FText& OutErrorMessage);
	FReply OnSpawnGraphNodeByShortcut(FInputChord InChord, const FVector2D& InPosition, UEdGraph* InGraph);

private:
	// Toolbar and command helpers
	void RegisterToolbar();

	void BindCommands();

public:
	// IToolkit overrides
	void RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;
	void UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager) override;

private:
	TSharedRef<SDockTab> SpawnTab_Preview(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Palette(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_GraphArea(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_NodeDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PreviewDetails(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Output(const FSpawnTabArgs& Args);

	TSharedRef<FTabManager::FLayout> CreatePaneLayout() const;

	void CreateWidgets();
	TSharedRef<SGraphEditor> CreateGraphEditorWidget();
	FGraphAppearanceInfo GetGraphAppearance() const;

private:
	// Tab Ids for the Optimus editor
	static const FName PreviewTabId;
	static const FName PaletteTabId;
	static const FName GraphAreaTabId;
	static const FName NodeDetailsTabId;
	static const FName PreviewDetailsTabId;
	static const FName OutputTabId;

	// -- Widgets

	TSharedPtr<SOptimusEditorViewport> EditorViewportWidget;
	TSharedPtr<SOptimusNodePalette> NodePaletteWidget;
	TSharedPtr<SGraphEditor> GraphEditorWidget;
	TSharedPtr<IDetailsView> NodeDetailsWidget;
	TSharedPtr<IDetailsView> PreviewDetailsWidget;

	UOptimusDeformer* DeformerObject = nullptr;
	UOptimusEditorGraph *DeformerGraph = nullptr;
	TSharedPtr<FUICommandList> GraphEditorCommands;
};
