// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusEditor.h"

#include "Framework/Docking/TabManager.h"
#include "Misc/NotifyHook.h"

class IPersonaPreviewScene;
class IPersonaViewport;
class IPersonaToolkit;
class FUICommandList;
class IOptimusNodeGraphCollectionOwner;
class SGraphEditor;
class SOptimusEditorViewport;
class SOptimusGraphTitleBar;
class SOptimusNodePalette;
class UOptimusActionStack;
class UOptimusDeformer;
class UOptimusEditorGraph;
class UOptimusNodeGraph;
enum class EOptimusGlobalNotifyType;
struct FGraphAppearanceInfo;

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
		return EditorGraph;
	}

	IOptimusNodeGraphCollectionOwner* GetGraphCollectionRoot() const;
	UOptimusDeformer* GetDeformer() const;

	TSharedRef<IPersonaToolkit> GetPersonaToolkit() const override
	{
		return PersonaToolkit.ToSharedRef();
	}
	
	FText GetGraphCollectionRootName() const;

	UOptimusActionStack* GetActionStack() const;

	/// @brief Set object to view in the details panel.
	/// @param InObject  The object to view and edit in the details panel.
	void InspectObject(UObject* InObject);

	/// @brief Set a group of object to view in the details panel.
	/// @param InObject  The objects to view and edit in the details panel.
	void InspectObjects(const TArray<UObject *> &InObjects);

	// IToolkit overrides
	FName GetToolkitFName() const override;				
	FText GetBaseToolkitName() const override;			
	FString GetWorldCentricTabPrefix() const override;	
	FLinearColor GetWorldCentricTabColorScale() const override;

	// --
	bool SetEditGraph(UOptimusNodeGraph *InNodeGraph);

	DECLARE_EVENT( FOptimusEditor, FOnRefreshEvent );
	FOnRefreshEvent& OnRefresh() { return RefreshEvent; }

private:
	// ----------------------------------------------------------------------------------------
	// Editor commands
	void Compile();

	bool CanCompile() const;

	void CompileBegin(UOptimusDeformer* InDeformer);
	void CompileEnd(UOptimusDeformer* InDeformer);
	
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
	// Handlers for created tabs
	void HandlePreviewSceneCreated(const TSharedRef<IPersonaPreviewScene>& InPreviewScene);
	void HandleDetailsCreated(const TSharedRef<IDetailsView>& InDetailsView);
	void HandleViewportCreated(const TSharedRef<IPersonaViewport>& InPersonaViewport);
	
	// KILL ME
	TSharedPtr<SGraphEditor> GetGraphEditorWidget() const { return GraphEditorWidget; }

private:
	void CreateWidgets();
	TSharedRef<SGraphEditor> CreateGraphEditorWidget();
	FGraphAppearanceInfo GetGraphAppearance() const;

	void OnDeformerModified(
		EOptimusGlobalNotifyType InNotifyType, 
		UObject *InModifiedObject
		);

	// Called when the inspector has changed a value.
	void OnFinishedChangingProperties(const FPropertyChangedEvent& PropertyChangedEvent);

private:
	// Persona toolkit for the skelmesh preview
	TSharedPtr<IPersonaToolkit> PersonaToolkit;
	
	// -- Widgets
	TSharedPtr<SGraphEditor> GraphEditorWidget;
	TSharedPtr<IDetailsView> PropertyDetailsWidget;
	TSharedPtr<IDetailsView> PreviewDetailsWidget;

	UOptimusDeformer* DeformerObject = nullptr;
	UOptimusEditorGraph* EditorGraph = nullptr;
	UOptimusNodeGraph* PreviousEditedNodeGraph = nullptr;
	UOptimusNodeGraph* UpdateGraph = nullptr;
	TSharedPtr<FUICommandList> GraphEditorCommands;

	FOnRefreshEvent RefreshEvent;
};
