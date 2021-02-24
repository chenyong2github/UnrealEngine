// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "EditorUndoClient.h"
#include "Framework/Commands/UICommandList.h"
#include "GraphEditor.h"
#include "IDetailsView.h"
#include "IMetasoundEditor.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendController.h"
#include "Misc/NotifyHook.h"
#include "SMetasoundPalette.h"
#include "Textures/SlateIcon.h"
#include "Toolkits/IToolkitHost.h"
#include "UObject/GCObject.h"
#include "Widgets/SPanel.h"
#include "Sound/AudioBus.h"
#include "SAudioMeter.h"
#include "AudioMeterStyle.h"
#include "AudioSynesthesia/Classes/Meter.h"
#include "UObject/StrongObjectPtr.h"

// Forward Declarations
class FSlateRect;
class FTabManager;
class IDetailsView;
class IToolkitHost;
class SDockableTab;
class SGraphEditor;
class SMetasoundPalette;
class SVerticalBox;
class UEdGraphNode;
class UMetasound;

struct FMeterResults;
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

			void PasteNodes(const FVector2D* Location = nullptr);

			int32 GetNumNodesSelected() const
			{
				return MetasoundGraphEditor->GetSelectedNodes().Num();
			}

			void OnMeterOutput(UMeterAnalyzer* InMeterAnalyzer, int32 ChannelIndex, const FMeterResults& MeterResults);

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

			/** Cut the currently selected nodes */
			void CutSelectedNodes();


			/** Copy the currently selected nodes */
			void CopySelectedNodes();

			/** Whether copying the currently selected nodes is permissible */
			bool CanCopyNodes() const;

			/** Whether the currently selected nodes can be deleted */
			bool CanDeleteNodes() const;

			/** Called to undo the last action */
			void UndoGraphAction();

			/** Called to redo the last undone action */
			void RedoGraphAction();

		private:
			static Frontend::FGraphHandle InitMetasound(UObject& InMetasound);

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

			/** Details tab */
			TSharedPtr<IDetailsView> MetasoundDetails;

			/** General tab */
			TSharedPtr<IDetailsView> MetasoundGeneral;

			/** Metasound Output Meter widget */
			TSharedPtr<SAudioMeter> MetasoundMeter;

			/** Metasound channel info for the meter. */
			TArray<FMeterChannelInfo> MetasoundChannelInfo;

			/** Palette of Node types */
			TSharedPtr<SMetasoundPalette> Palette;

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

			/** Whether or not metasound being edited is valid */
			bool bPassedValidation = true;

			/** Document used when pasting from clipboard to avoid deserializing twice */
			FMetasoundFrontendDocument PastedDocument;
		};
	} // namespace Editor
} // namespace Metasound
