// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "IMetasoundEditor.h"
#include "Misc/NotifyHook.h"
#include "Toolkits/IToolkitHost.h"
#include "UObject/GCObject.h"


// Forward Declarations
class FSlateRect;
class FTabManager;
class IDetailsView;
class IToolkitHost;
class SDockableTab;
class SGraphEditor;
class SMetasoundPalette;
class UEdGraphNode;
class UMetasound;

struct FPropertyChangedEvent;


class FMetasoundEditor : public IMetasoundEditor, public FGCObject, public FNotifyHook, public FEditorUndoClient
{
public:
	FMetasoundEditor();

	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	/** Destructor */
	virtual ~FMetasoundEditor();

	/** Edits the specified Metasound object */
	void InitMetasoundEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost >& InitToolkitHost, UObject* ObjectToEdit);

	/** IMetasoundEditor interface */
	virtual UMetasound* GetMetasound() const override;
	virtual void SetSelection(TArray<UObject*> SelectedObjects) override;
	virtual bool GetBoundsForSelectedNodes(FSlateRect& Rect, float Padding) override;
	virtual int32 GetNumberOfSelectedNodes() const override;
	virtual TSet<UObject*> GetSelectedNodes() const override;

	/** IToolkit interface */
	virtual FName GetToolkitFName() const override;
	virtual FText GetBaseToolkitName() const override;
	virtual FString GetWorldCentricTabPrefix() const override;
	virtual FLinearColor GetWorldCentricTabColorScale() const override;

	virtual FString GetDocumentationLink() const override
	{
		return FString(TEXT("Engine/Audio/Metasounds/Editor"));
	}

	/** FGCObject interface */
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override { PostUndo(bSuccess); }
	// End of FEditorUndoClient

	/** Whether pasting the currently selected nodes is permissible */
	bool CanPasteNodes() const;

	/** Paste the contents of the clipboard at the provided location */
	void PasteNodesAtLocation(const FVector2D& Location);

private:
	TSharedRef<SDockTab> SpawnTab_GraphCanvas(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Properties(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Palette(const FSpawnTabArgs& Args);

protected:
	/** Called when the selection changes in the GraphEditor */
	void OnSelectedNodesChanged(const TSet<UObject*>& NewSelection);

	/**
	 * Called when a node's title is committed for a rename
	 *
	 * @param	NewText				New title text
	 * @param	CommitInfo			How text was committed
	 * @param	NodeBeingChanged	The node being changed
	 */
	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);

	/** Select every node in the graph */
	void SelectAllNodes();
	/** Whether we can select every node */
	bool CanSelectAllNodes() const;

	/** Delete the currently selected nodes */
	void DeleteSelectedNodes();

	/** Whether we are able to delete the currently selected nodes */
	bool CanDeleteNodes() const;

	/** Delete only the currently selected nodes that can be duplicated */
	void DeleteSelectedDuplicatableNodes();

	/** Cut the currently selected nodes */
	void CutSelectedNodes();

	/** Whether we are able to cut the currently selected nodes */
	bool CanCutNodes() const;

	/** Copy the currently selected nodes */
	void CopySelectedNodes();

	/** Whether copying the currently selected nodes is permissible */
	bool CanCopyNodes() const;

	/** Paste the contents of the clipboard */
	void PasteNodes();

	/** Duplicate the currently selected nodes */
	void DuplicateNodes();

	/** Whether we are able to duplicate the currently selected nodes */
	bool CanDuplicateNodes() const;

	/** Called to undo the last action */
	void UndoGraphAction();

	/** Called to redo the last undone action */
	void RedoGraphAction();

	void OnAlignTop();
	void OnAlignMiddle();
	void OnAlignBottom();
	void OnAlignLeft();
	void OnAlignCenter();
	void OnAlignRight();

	void OnStraightenConnections();

	void OnDistributeNodesH();
	void OnDistributeNodesV();

private:
	/** FNotifyHook interface */
	virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

	/** Creates all internal widgets for the tabs to point at */
	void CreateInternalWidgets();

	/** Builds the toolbar widget for the Metasound editor */
	void ExtendToolbar();

	/** Binds new graph commands to delegates */
	void BindGraphCommands();

	/** Toolbar command methods */
	void Play();
	void PlayNode();
	

	/** Whether we can play the current selection of nodes */
	bool CanPlayNode() const;
	void Stop();

	/** Either play the Metasound or stop currently playing sound */
	void TogglePlayback();
	
	/** Plays a single specified node */
	void PlaySingleNode(UEdGraphNode* Node);

	/** Sync the content browser to the current selection of nodes */
	void SyncInBrowser();

	/** Add an input to the currently selected node */
	void AddInput();
	/** Whether we can add an input to the currently selected node */
	bool CanAddInput() const;

	/** Delete an input from the currently selected node */
	void DeleteInput();
	/** Whether we can delete an input from the currently selected node */
	bool CanDeleteInput() const;

	/* Create comment node on graph */
	void OnCreateComment();

	/** Create new graph editor widget */
	TSharedRef<SGraphEditor> CreateGraphEditorWidget();

private:
	/** The Metasound asset being inspected */
	UMetasound* Metasound;

	/** List of open tool panels; used to ensure only one exists at any one time */
	TMap<FName, TWeakPtr<SDockableTab>> SpawnedToolPanels;

	/** New Graph Editor */
	TSharedPtr<SGraphEditor> MetasoundGraphEditor;

	/** Properties tab */
	TSharedPtr<IDetailsView> MetasoundProperties;

	/** Palette of Sound Node types */
	TSharedPtr<SMetasoundPalette> Palette;

	/** Command list for this editor */
	TSharedPtr<FUICommandList> GraphEditorCommands;

	/**	The tab ids for all the tabs used */
	static const FName GraphCanvasTabId;
	static const FName PropertiesTabId;
	static const FName PaletteTabId;
};
