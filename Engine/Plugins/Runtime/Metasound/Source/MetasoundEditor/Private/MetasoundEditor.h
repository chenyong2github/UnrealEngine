// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Framework/Commands/UICommandList.h"
#include "GraphEditor.h"
#include "IDetailsView.h"
#include "IMetasoundEditor.h"
#include "Misc/NotifyHook.h"
#include "SMetasoundPalette.h"
#include "Textures/SlateIcon.h"
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

namespace Metasound
{
	namespace Editor
	{
		class FEditor : public IMetasoundEditor, public FGCObject, public FNotifyHook, public FEditorUndoClient
		{
		public:
			virtual ~FEditor();

			virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
			virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

			/** Edits the specified Metasound object */
			void InitMetasoundEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit);

			/** IMetasoundEditor interface */
			virtual UMetasound* GetMetasound() const override;
			virtual void SetSelection(const TArray<UObject*>& SelectedObjects) override;
			virtual bool GetBoundsForSelectedNodes(FSlateRect& Rect, float Padding) override;

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

			int32 GetNumNodesSelected() const
			{
				return MetasoundGraphEditor->GetSelectedNodes().Num();
			}

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

			/** Delete the currently selected nodes */
			void DeleteSelectedNodes();

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

		private:
			/** FNotifyHook interface */
			virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

			/** Creates all internal widgets for the tabs to point at */
			void CreateInternalWidgets();

			/** Builds the toolbar widget for the Metasound editor */
			void ExtendToolbar();

			/** Binds new graph commands to delegates */
			void BindGraphCommands();

			FSlateIcon GetImportStatusImage() const;

			FSlateIcon GetExportStatusImage() const;

			/** Toolbar command methods */
			void Import();
			void Export();
			void Play();
			void PlayNode();
			void Stop();

			/** Whether we can play the current selection of nodes */
			bool CanPlayNode() const;

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

			/** The Metasound asset being edited */
			UMetasound* Metasound = nullptr;

			/** Whether or not metasound being edited is valid */
			bool bPassedValidation = true;
		};
	} // namespace Editor
} // namespace Metasound
