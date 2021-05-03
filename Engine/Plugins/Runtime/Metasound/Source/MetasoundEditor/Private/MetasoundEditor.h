// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "AudioMeterStyle.h"
#include "AudioSynesthesia/Classes/Meter.h"
#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Framework/Commands/UICommandList.h"
#include "GraphEditor.h"
#include "IDetailsView.h"
#include "IMetasoundEditor.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "Misc/NotifyHook.h"
#include "SAudioMeter.h"
#include "SGraphActionMenu.h"
#include "SMetasoundPalette.h"
#include "Sound/AudioBus.h"
#include "Textures/SlateIcon.h"
#include "Toolkits/IToolkitHost.h"
#include "UObject/GCObject.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SPanel.h"
#include "WorkflowOrientedApp/WorkflowTabFactory.h"
#include "WorkflowOrientedApp/WorkflowTabManager.h"


// Forward Declarations
class FTabManager;
class SDockableTab;
class SGraphEditor;
class SMetasoundPalette;
class FSlateRect;
class IDetailsView;
class IToolkitHost;
class SVerticalBox;
class UEdGraphNode;
class UMetaSound;
class UMetasoundEditorGraph;

struct FGraphActionNode;
struct FMeterResults;
struct FPropertyChangedEvent;


namespace Metasound
{
	namespace Editor
	{
		// Forward Declarations
		class FMetasoundGraphNodeSchemaAction;

		/* Enums to use when grouping the members in the list panel. Enum order dictates visible order. */
		enum class ENodeSection : uint8
		{
			None,
			Inputs,
			Outputs,

			COUNT
		};


		class FEditor : public IMetasoundEditor, public FGCObject, public FNotifyHook, public FEditorUndoClient
		{
		public:
			virtual ~FEditor();

			virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
			virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

			double GetPlayTime() const;

			/** Edits the specified Metasound object */
			void InitMetasoundEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit);

			/** IMetasoundEditor interface */
			virtual UObject* GetMetasoundObject() const override;
			virtual UObject* GetMetasoundAudioBusObject() const override;
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
			bool CanPasteNodes();

			/* Duplicates the selected node(s) in the graph */
			void DuplicateNodes();

			/* Pastes node(s) from the clipboard to the graph */
			void PasteNodes(const FVector2D* InLocation = nullptr);
			void PasteNodes(const FVector2D* InLocation, const FText& InTransactionText);

			int32 GetNumNodesSelected() const
			{
				return MetasoundGraphEditor->GetSelectedNodes().Num();
			}

			void OnInputNameChanged(FGuid InNodeID);
			void OnOutputNameChanged(FGuid InNodeID);

			void OnMeterOutput(UMeterAnalyzer* InMeterAnalyzer, int32 ChannelIndex, const FMeterResults& MeterResults);

		protected:
			// Callbacks for action tree
			bool CanRenameOnActionNode(TWeakPtr<FGraphActionNode> InSelectedNode) const;
			bool CanAddNewElementToSection(int32 InSectionID) const;
			void CollectAllActions(FGraphActionListBuilderBase& OutAllActions);
			void CollectStaticSections(TArray<int32>& StaticSectionIDs);
			TSharedRef<SWidget> CreateAddInputButton(int32 InSectionID, FText AddNewText, FName MetaDataTag);
			FText GetFilterText() const;
			bool HandleActionMatchesName(FEdGraphSchemaAction* InAction, const FName& InName) const;
			FReply OnActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent);
			void OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, ESelectInfo::Type InSelectionType);
			FReply OnAddButtonClickedOnSection(int32 InSectionID);
			TSharedRef<SWidget> OnGetMenuSectionWidget(TSharedRef<SWidget> RowWidget, int32 InSectionID);
			FText GetSectionTitle(ENodeSection InSection) const;
			FText OnGetSectionTitle(int32 InSectionID);
			TSharedRef<SWidget> OnCreateWidgetForAction(struct FCreateWidgetForActionData* const InCreateData);

			/** Called when the selection changes in the GraphEditor */
			void OnSelectedNodesChanged(const TSet<UObject*>& NewSelection);

			UMetasoundEditorGraph& GetMetaSoundGraphChecked();

			/**
			 * Called when a node's title is committed for a rename
			 *
			 * @param	NewText				New title text
			 * @param	CommitInfo			How text was committed
			 * @param	NodeBeingChanged	The node being changed
			 */
			void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);

			/** Deletes from the Metasound Menu (i.e. input or output) if in focus, or the currently selected nodes if the graph editor is in focus. */
			void DeleteSelected();

			void DeleteInterfaceItem(TSharedPtr<FMetasoundGraphNodeSchemaAction> ActionToDelete, UMetasoundEditorGraph* Graph);

			/** Delete the currently selected nodes */
			void DeleteSelectedNodes();

			/** Cut the currently selected nodes */
			void CutSelectedNodes();

			/** Copy the currently selected nodes to the given string */
			void CopySelectedNodes(FString& OutString) const;

			/** Copy the currently selected nodes */
			void CopySelectedNodes() const;

			/** Whether copying the currently selected node(s) is permissible */
			bool CanCopyNodes() const;

			/** Whether or not the currently selected node(s) can be duplicated */
			bool CanDuplicateNodes() const;

			/** Whether the currently selected node(s) can be deleted */
			bool CanDeleteNodes() const;

			/** Called to undo the last action */
			void UndoGraphAction();

			/** Called to redo the last undone action */
			void RedoGraphAction();

		private:
			void SetPreviewID(uint32 InPreviewID);

			/** FNotifyHook interface */
			virtual void NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged) override;

			/** Creates all internal widgets for the tabs to point at */
			void CreateInternalWidgets();

			/** Creates analyzers */
			void CreateAnalyzers();

			/** Builds the toolbar widget for the Metasound editor */
			void ExtendToolbar();

			/** Binds new graph commands to delegates */
			void BindGraphCommands();

			FSlateIcon GetImportStatusImage() const;

			FSlateIcon GetExportStatusImage() const;

			FSlateIcon GetSettingsImage() const;

			/** Toolbar command methods */
			void Import();
			void ExecuteNode();
			void Export();
			void Play();
			void Stop();

			/** Whether we can play the current selection of nodes */
			bool CanExecuteNode() const;

			/** Either play the Metasound or stop currently playing sound */
			void TogglePlayback();

			/** Executes specified node (If supported) */
			void ExecuteNode(UEdGraphNode* Node);

			/** Sync the content browser to the current selection of nodes */
			void SyncInBrowser();

			/** Show the Metasound object's General settings in the Inspector */
			void EditGeneralSettings();

			/** Show the Metasound object's Metasound-specific settings in the Inspector */
			void EditMetasoundSettings();

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
			void CreateGraphEditorWidget();

		private:
			void EditObjectSettings();

			void NotifyUserModifiedBySync();

			bool IsPlaying() const;

			/** List of open tool panels; used to ensure only one exists at any one time */
			TMap<FName, TWeakPtr<SDockableTab>> SpawnedToolPanels;

			/** New Graph Editor */
			TSharedPtr<SGraphEditor> MetasoundGraphEditor;

			/** Details tab */
			TSharedPtr<IDetailsView> MetasoundDetails;

			/** Metasound Interface menu */
			TSharedPtr<SGraphActionMenu> MetasoundInterfaceMenu;

			/** Metasound Output Meter widget */
			TSharedPtr<SAudioMeter> MetasoundMeter;

			/** Metasound channel info for the meter. */
			TArray<FMeterChannelInfo> MetasoundChannelInfo;

			/** Palette of Node types */
			TSharedPtr<SMetasoundPalette> Palette;

			/** Widget showing playtime that overlays the graph when previewing */
			TSharedPtr<STextBlock> PlayTimeWidget;
			double PlayTime = 0.0;

			/** Command list for this editor */
			TSharedPtr<FUICommandList> GraphEditorCommands;

			/** The Metasound asset being edited */
			UObject* Metasound = nullptr;

			/** The preview audio bus. Used for analysis. */
			TStrongObjectPtr<UAudioBus> MetasoundAudioBus;

			/** Metasound analyzer object. */
			TStrongObjectPtr<UMeterAnalyzer> MetasoundMeterAnalyzer;

			TStrongObjectPtr<UMeterSettings> MetasoundMeterAnalyzerSettings;

			/** Handle for results delegate for metasound meter analyzer. */
			FDelegateHandle ResultsDelegateHandle;

			TMap<FGuid, FDelegateHandle> NameChangeDelegateHandles;

			/** Whether or not metasound being edited is valid */
			bool bPassedValidation = true;

			/** Text content used when either duplicating or pasting from clipboard (avoids deserializing twice) */
			FString NodeTextToPaste;

			/** Boolean state for when selection change handle should not respond due to selection state
			  * being manually applied in code */
			bool bManuallyClearingGraphSelection = false;
		};
	} // namespace Editor
} // namespace Metasound
