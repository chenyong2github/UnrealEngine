// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "IOptimusEditor.h"

#include "Misc/NotifyHook.h"


class FUICommandList;
class IMessageLogListing;
class IOptimusNodeGraphCollectionOwner;
class IPersonaPreviewScene;
class IPersonaToolkit;
class IPersonaViewport;
class SGraphEditor;
class SOptimusEditorViewport;
class SOptimusGraphTitleBar;
class SOptimusNodePalette;
class UComputeGraphComponent;
class UDebugSkelMeshComponent;
class UOptimusActionStack;
class UOptimusDeformer;
class UOptimusEditorGraph;
class UOptimusNode;
class UOptimusNodeGraph;
class USkeletalMesh;
enum class EOptimusGlobalNotifyType;
struct FGraphAppearanceInfo;


class FOptimusEditor :
	public IOptimusEditor,
	public FGCObject,
	public FNotifyHook
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

	// FGCObject overrides
	void AddReferencedObjects( FReferenceCollector& Collector ) override;

private:
	// ----------------------------------------------------------------------------------------
	// Editor commands
	void Compile();

	bool CanCompile() const;

	void CompileBegin(UOptimusDeformer* InDeformer);
	void CompileEnd(UOptimusDeformer* InDeformer);

	void InstallDataProviders();
	void RemoveDataProviders();

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
	void HandlePreviewMeshChanged(USkeletalMesh* InOldPreviewMesh, USkeletalMesh* InNewPreviewMesh);
	void HandleDetailsCreated(const TSharedRef<IDetailsView>& InDetailsView);
	void HandleViewportCreated(const TSharedRef<IPersonaViewport>& InPersonaViewport);
	
	// KILL ME
	TSharedPtr<SGraphEditor> GetGraphEditorWidget() const { return GraphEditorWidget; }
	TSharedPtr<SWidget> GetCompilerResultsWidget() const { return CompilerResultsWidget; }

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

	// Called for every compilation result. There may be multiple calls for each Compile.
	void OnCompileResults(UOptimusNodeGraph const* InGraph, UOptimusNode const* InNode, FString const& InMessage);

private:
	// Persona toolkit for the skelmesh preview
	TSharedPtr<IPersonaToolkit> PersonaToolkit;
	
	// -- Widgets
	TSharedPtr<IPersonaViewport> ViewportWidget;
	TSharedPtr<SGraphEditor> GraphEditorWidget;
	TSharedPtr<IDetailsView> PropertyDetailsWidget;
	TSharedPtr<IDetailsView> PreviewDetailsWidget;
	TSharedPtr<SWidget> CompilerResultsWidget;
	TSharedPtr<IMessageLogListing> CompilerResultsListing;

	UOptimusDeformer* DeformerObject = nullptr;
	UOptimusEditorGraph* EditorGraph = nullptr;
	UOptimusNodeGraph* PreviousEditedNodeGraph = nullptr;
	UOptimusNodeGraph* UpdateGraph = nullptr;
	TSharedPtr<FUICommandList> GraphEditorCommands;

	// Compute Graph Component and data providers.
	UComputeGraphComponent* ComputeGraphComponent = nullptr;
	UDebugSkelMeshComponent* SkeletalMeshComponent = nullptr;
	
	FOnRefreshEvent RefreshEvent;
};
