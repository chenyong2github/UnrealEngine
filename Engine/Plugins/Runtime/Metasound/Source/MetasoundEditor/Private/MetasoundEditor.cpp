// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditor.h"

#include "Components/AudioComponent.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IDetailsView.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MetasoundEditorCommands.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorTabFactory.h"
#include "Misc/Attribute.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "SMetasoundPalette.h"
#include "SNodePanel.h"
#include "Widgets/Docking/SDockTab.h"

#define LOCTEXT_NAMESPACE "MetasoundEditor"

namespace Metasound
{
	namespace Editor
	{
		void FEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
		{
			WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_MetasoundEditor", "Metasound Editor"));
			auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

			FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

			InTabManager->RegisterTabSpawner(TabFactory::Names::GraphCanvas, FOnSpawnTab::CreateLambda([InMetasoundGraphEditor = MetasoundGraphEditor](const FSpawnTabArgs& Args)
			{
				return TabFactory::CreateGraphCanvasTab(InMetasoundGraphEditor, Args);
			}))
			.SetDisplayName(LOCTEXT("GraphCanvasTab", "Viewport"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));

			InTabManager->RegisterTabSpawner(TabFactory::Names::Properties, FOnSpawnTab::CreateLambda([InMetasoundProperties = MetasoundProperties](const FSpawnTabArgs& Args)
			{
				return TabFactory::CreatePropertiesTab(InMetasoundProperties, Args);
			}))
			.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

			InTabManager->RegisterTabSpawner(TabFactory::Names::Palette, FOnSpawnTab::CreateLambda([InPalette = Palette](const FSpawnTabArgs& Args)
			{
				return TabFactory::CreatePaletteTab(InPalette, Args);
			}))
			.SetDisplayName(LOCTEXT("PaletteTab", "Palette"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.Palette"));
		}

		void FEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
		{
			using namespace Metasound::Editor;

			FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

			InTabManager->UnregisterTabSpawner(TabFactory::Names::GraphCanvas);
			InTabManager->UnregisterTabSpawner(TabFactory::Names::Palette);
			InTabManager->UnregisterTabSpawner(TabFactory::Names::Properties);
		}

		FEditor::~FEditor()
		{
			// Stop any playing sounds when the editor closes
			UAudioComponent* Component = GEditor->GetPreviewAudioComponent();
			if (Component && Component->IsPlaying())
			{
				Stop();
			}

			check(GEditor);
			GEditor->UnregisterForUndo(this);
		}

		void FEditor::InitMetasoundEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit)
		{

			Metasound = CastChecked<UMetasound>(ObjectToEdit);

			// Support undo/redo
			Metasound->SetFlags(RF_Transactional);

			if (!Metasound->GetGraph())
			{
				UMetasoundEditorGraph* Graph = NewObject<UMetasoundEditorGraph>(Metasound);
				Graph->ParentMetasound = Metasound;
				Graph->Schema = UMetasoundEditorGraphSchema::StaticClass();
				Metasound->SetGraph(Graph);
			}

			GEditor->RegisterForUndo(this);

			FGraphEditorCommands::Register();
			FEditorCommands::Register();

			BindGraphCommands();

			CreateInternalWidgets();

			const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_MetasoundEditor_Layout_v1")
				->AddArea
				(
					FTabManager::NewPrimaryArea()
					->SetOrientation(Orient_Vertical)
					->Split
					(
						FTabManager::NewStack()
						->SetSizeCoefficient(0.1f)
						->SetHideTabWell(true)
						->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
					)
					->Split(FTabManager::NewSplitter()
						->SetOrientation(Orient_Horizontal)
						->SetSizeCoefficient(0.9f)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.225f)
							->SetHideTabWell(true)
							->AddTab(TabFactory::Names::Properties, ETabState::OpenedTab)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.65f)
							->SetHideTabWell(true)
							->AddTab(TabFactory::Names::GraphCanvas, ETabState::OpenedTab)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.125f)
							->SetHideTabWell(true)
							->AddTab(TabFactory::Names::Palette, ETabState::OpenedTab)
						)
					)
				);

			const bool bCreateDefaultStandaloneMenu = true;
			const bool bCreateDefaultToolbar = true;
			FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, TEXT("MetasoundEditorApp"), StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectToEdit, false);

			ExtendToolbar();
			RegenerateMenusAndToolbars();
		}

		UMetasound* FEditor::GetMetasound() const
		{
			return Metasound;
		}

		void FEditor::SetSelection(const TArray<UObject*>& SelectedObjects)
		{
			if (MetasoundProperties.IsValid())
			{
				MetasoundProperties->SetObjects(SelectedObjects);
			}
		}

		bool FEditor::GetBoundsForSelectedNodes(FSlateRect& Rect, float Padding)
		{
			return MetasoundGraphEditor->GetBoundsForSelectedNodes(Rect, Padding);
		}

		FName FEditor::GetToolkitFName() const
		{
			return FName("MetasoundEditor");
		}

		FText FEditor::GetBaseToolkitName() const
		{
			return LOCTEXT("AppLabel", "Metasound Editor");
		}

		FString FEditor::GetWorldCentricTabPrefix() const
		{
			return LOCTEXT("WorldCentricTabPrefix", "Metasound ").ToString();
		}

		FLinearColor FEditor::GetWorldCentricTabColorScale() const
		{
			return FLinearColor(0.3f, 0.2f, 0.5f, 0.5f);
		}

		void FEditor::AddReferencedObjects(FReferenceCollector& Collector)
		{
			Collector.AddReferencedObject(Metasound);
		}

		void FEditor::PostUndo(bool bSuccess)
		{
			if (MetasoundGraphEditor.IsValid())
			{
				MetasoundGraphEditor->ClearSelectionSet();
				MetasoundGraphEditor->NotifyGraphChanged();
				FSlateApplication::Get().DismissAllMenus();
			}

		}

		void FEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
		{
			if (MetasoundGraphEditor.IsValid() && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
			{
				MetasoundGraphEditor->NotifyGraphChanged();
			}
		}

		void FEditor::CreateInternalWidgets()
		{
			MetasoundGraphEditor = CreateGraphEditorWidget();

			FDetailsViewArgs Args;
			Args.bHideSelectionTip = true;
			Args.NotifyHook = this;

			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			MetasoundProperties = PropertyModule.CreateDetailView(Args);
			MetasoundProperties->SetObject(Metasound);

			Palette = SNew(SMetasoundPalette);
		}

		void FEditor::ExtendToolbar()
		{
			TSharedPtr<FExtender> ToolbarExtender = MakeShared<FExtender>();
			ToolbarExtender->AddToolBarExtension
			(
				"Asset",
				EExtensionHook::After,
				GetToolkitCommands(),
				FToolBarExtensionDelegate::CreateLambda([this](FToolBarBuilder& ToolbarBuilder)
				{
					ToolbarBuilder.BeginSection("Utilities");
					{
						ToolbarBuilder.AddToolBarButton
						(
							FEditorCommands::Get().Import,
							NAME_None,
							TAttribute<FText>(),
							TAttribute<FText>(),
							TAttribute<FSlateIcon>::Create([this]() { return GetImportStatusImage(); }),
							"ImportMetasound"
						);

						ToolbarBuilder.AddToolBarButton
						(
							FEditorCommands::Get().Export,
							NAME_None,
							TAttribute<FText>(),
							TAttribute<FText>(),
							TAttribute<FSlateIcon>::Create([this]() { return GetExportStatusImage(); }),
							"ExportMetasound"
						);
					}
					ToolbarBuilder.EndSection();

					ToolbarBuilder.BeginSection("Audition");
					{
						ToolbarBuilder.AddToolBarButton(FEditorCommands::Get().Play);
						ToolbarBuilder.AddToolBarButton(FEditorCommands::Get().Stop);
					}
					ToolbarBuilder.EndSection();
				})
			);

			AddToolbarExtender(ToolbarExtender);
		}

		FSlateIcon FEditor::GetImportStatusImage() const
		{
			const FName IconName = "MetasoundEditor.Import";
			return FSlateIcon("MetasoundStyle", IconName);
		}

		FSlateIcon FEditor::GetExportStatusImage() const
		{
			FName IconName = "MetasoundEditor.Export";
			if (!bPassedValidation)
			{
				IconName = "MetasoundEditor.ExportError";
			}

			return FSlateIcon("MetasoundStyle", IconName);
		}

		void FEditor::BindGraphCommands()
		{
			const FEditorCommands& Commands = FEditorCommands::Get();

			ToolkitCommands->MapAction(
				Commands.Play,
				FExecuteAction::CreateSP(this, &FEditor::Play));

			ToolkitCommands->MapAction(
				Commands.Stop,
				FExecuteAction::CreateSP(this, &FEditor::Stop));

			ToolkitCommands->MapAction(
				Commands.Import,
				FExecuteAction::CreateSP(this, &FEditor::Import));

			ToolkitCommands->MapAction(
				Commands.Export,
				FExecuteAction::CreateSP(this, &FEditor::Export));

			ToolkitCommands->MapAction(
				Commands.TogglePlayback,
				FExecuteAction::CreateSP(this, &FEditor::TogglePlayback));

			ToolkitCommands->MapAction(
				FGenericCommands::Get().Undo,
				FExecuteAction::CreateSP(this, &FEditor::UndoGraphAction));

			ToolkitCommands->MapAction(
				FGenericCommands::Get().Redo,
				FExecuteAction::CreateSP(this, &FEditor::RedoGraphAction));
		}

		void FEditor::Import()
		{
			if (Metasound)
			{
				// TODO: Prompt OFD and provide path from user
				const FString Path = FPaths::ProjectIntermediateDir() / TEXT("Metasounds") + FPaths::ChangeExtension(Metasound->GetPathName(), FMetasoundAssetBase::FileExtension);
				Metasound->ImportFromJSON(Path);
			}
		}

		void FEditor::Export()
		{
			if (Metasound)
			{
				// TODO: Prompt OFD and provide path from user
				const FString Path = FPaths::ProjectIntermediateDir() / TEXT("Metasounds") + FPaths::ChangeExtension(Metasound->GetPathName(), FMetasoundAssetBase::FileExtension);
				Metasound->ExportToJSON(Path);
			}
		}

		void FEditor::Play()
		{
			// 	TODO: Implement play
			// 	check(GEditor);
			// 	GEditor->PlayPreviewSound(Metasound);
			// 
			// 	MetasoundGraphEditor->RegisterActiveTimer(0.0f, 
			// 		FWidgetActiveTimerDelegate::CreateLambda([](double InCurrentTime, float InDeltaTime)
			// 		{
			// 			UAudioComponent* PreviewComp = GEditor->GetPreviewAudioComponent();
			// 			if (PreviewComp && PreviewComp->IsPlaying())
			// 			{
			// 				return EActiveTimerReturnType::Continue;
			// 			}
			// 			else
			// 			{
			// 				return EActiveTimerReturnType::Stop;
			// 			}
			// 		})
			// 	);
		}

		void FEditor::PlayNode()
		{
			const FGraphPanelSelectionSet SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();
			for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
			{
				PlaySingleNode(CastChecked<UEdGraphNode>(*NodeIt));
			}
		}

		bool FEditor::CanPlayNode() const
		{
			// TODO: Implement node playback
			return false;
		}

		void FEditor::Stop()
		{
			check(GEditor);
			GEditor->ResetPreviewAudioComponent();
		}

		void FEditor::TogglePlayback()
		{
			check(GEditor);

			UAudioComponent* Component = GEditor->GetPreviewAudioComponent();
			if (Component && Component->IsPlaying())
			{
				Stop();
			}
			else
			{
				Play();
			}
		}

		void FEditor::PlaySingleNode(UEdGraphNode* Node)
		{
			// TODO: Implement? Will we support single node playback?
		}

		void FEditor::SyncInBrowser()
		{
			TArray<UObject*> ObjectsToSync;

			const FGraphPanelSelectionSet SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();
			for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
			{
				// TODO: Implement sync to referenced Metasound if selected node is a reference to another metasound
			}

			if (!ObjectsToSync.Num())
			{
				ObjectsToSync.Add(Metasound);
			}

			check(GEditor);
			GEditor->SyncBrowserToObjects(ObjectsToSync);
		}

		void FEditor::AddInput()
		{
		}

		bool FEditor::CanAddInput() const
		{
			return MetasoundGraphEditor->GetSelectedNodes().Num() == 1;
		}

		void FEditor::DeleteInput()
		{
		}

		bool FEditor::CanDeleteInput() const
		{
			return true;
		}

		void FEditor::OnCreateComment()
		{
		}

		TSharedRef<SGraphEditor> FEditor::CreateGraphEditorWidget()
		{
			if (!GraphEditorCommands.IsValid())
			{
				GraphEditorCommands = MakeShared<FUICommandList>();

				GraphEditorCommands->MapAction(FEditorCommands::Get().BrowserSync,
					FExecuteAction::CreateSP(this, &FEditor::SyncInBrowser));

				GraphEditorCommands->MapAction(FEditorCommands::Get().AddInput,
					FExecuteAction::CreateSP(this, &FEditor::AddInput),
					FCanExecuteAction::CreateSP(this, &FEditor::CanAddInput));

				GraphEditorCommands->MapAction(FEditorCommands::Get().DeleteInput,
					FExecuteAction::CreateSP(this, &FEditor::DeleteInput),
					FCanExecuteAction::CreateSP(this, &FEditor::CanDeleteInput));

				// Graph Editor Commands
				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().CreateComment,
					FExecuteAction::CreateSP(this, &FEditor::OnCreateComment));

				// Editing commands
				GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->SelectAllNodes(); }));

				GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
					FExecuteAction::CreateSP(this, &FEditor::DeleteSelectedNodes),
					FCanExecuteAction::CreateLambda([this]() { return MetasoundGraphEditor->GetSelectedNodes().Num() > 0; }));

				GraphEditorCommands->MapAction(FGenericCommands::Get().Copy,
					FExecuteAction::CreateSP(this, &FEditor::CopySelectedNodes),
					FCanExecuteAction::CreateSP(this, &FEditor::CanCopyNodes));

				GraphEditorCommands->MapAction(FGenericCommands::Get().Cut,
					FExecuteAction::CreateSP(this, &FEditor::CutSelectedNodes),
					FCanExecuteAction::CreateSP(this, &FEditor::CanCutNodes));

				GraphEditorCommands->MapAction(FGenericCommands::Get().Paste,
					FExecuteAction::CreateSP(this, &FEditor::PasteNodes),
					FCanExecuteAction::CreateSP(this, &FEditor::CanPasteNodes));

				GraphEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
					FExecuteAction::CreateSP(this, &FEditor::DuplicateNodes),
					FCanExecuteAction::CreateSP(this, &FEditor::CanDuplicateNodes));

				// Alignment Commands
				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesTop,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->OnAlignTop(); }));

				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesMiddle,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->OnAlignMiddle(); }));

				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesBottom,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->OnAlignBottom(); }));

				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesLeft,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->OnAlignLeft(); }));

				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesCenter,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->OnAlignCenter(); }));

				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().AlignNodesRight,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->OnAlignRight(); }));

				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().StraightenConnections,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->OnStraightenConnections(); }));

				// Distribution Commands
				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().DistributeNodesHorizontally,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->OnDistributeNodesH(); }));

				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().DistributeNodesVertically,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->OnDistributeNodesV(); }));
			}

			FGraphAppearanceInfo AppearanceInfo;
			AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_Metasound", "Metasound");

			SGraphEditor::FGraphEditorEvents InEvents;
			InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FEditor::OnSelectedNodesChanged);
			InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FEditor::OnNodeTitleCommitted);
			InEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &FEditor::PlaySingleNode);

			return SNew(SGraphEditor)
				.AdditionalCommands(GraphEditorCommands)
				.IsEditable(true)
				.Appearance(AppearanceInfo)
				.GraphToEdit(Metasound->GetGraph())
				.GraphEvents(InEvents)
				.AutoExpandActionMenu(true)
				.ShowGraphStateOverlay(false);
		}

		void FEditor::OnSelectedNodesChanged(const TSet<UObject*>& NewSelection)
		{
			TArray<UObject*> Selection;

			if (NewSelection.Num())
			{
				for (TSet<UObject*>::TConstIterator SetIt(NewSelection); SetIt; ++SetIt)
				{
					if (Cast<UMetasoundEditorGraphNode>(*SetIt))
					{
						Selection.Add(GetMetasound());
					}
					else
					{
						Selection.Add(*SetIt);
					}
				}
			}
			else
			{
				Selection.Add(GetMetasound());
			}

			SetSelection(Selection);
		}

		void FEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
		{
			if (NodeBeingChanged)
			{
				const FScopedTransaction Transaction(LOCTEXT("RenameNode", "Rename Node"));
				NodeBeingChanged->Modify();
				NodeBeingChanged->OnRenameNode(NewText.ToString());
			}
		}

		void FEditor::DeleteSelectedNodes()
		{
			using namespace Metasound::Frontend;

			const FScopedTransaction Transaction(LOCTEXT("MetasoundEditorDeleteSelectedNode", "Delete Selected Metasound Node(s)"));

			UMetasoundEditorGraph* Graph = CastChecked<UMetasoundEditorGraph>(MetasoundGraphEditor->GetCurrentGraph());
			Graph->Modify();

			const FGraphPanelSelectionSet SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();
			MetasoundGraphEditor->ClearSelectionSet();

			FGraphHandle GraphHandle = Graph->ParentMetasound->GetRootGraphHandle();
			for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
			{
				UMetasoundEditorGraphNode* Node = CastChecked<UMetasoundEditorGraphNode>(*NodeIt);
				FGraphBuilder::DeleteNode(*Node, false /* bInRecordTransaction */);
			}
		}

		void FEditor::DeleteSelectedDuplicatableNodes()
		{
			// Cache off the old selection
			const FGraphPanelSelectionSet OldSelectedNodes = MetasoundGraphEditor->GetSelectedNodes();

			// Clear the selection and only select the nodes that can be duplicated
			FGraphPanelSelectionSet RemainingNodes;
			MetasoundGraphEditor->ClearSelectionSet();

			for (FGraphPanelSelectionSet::TConstIterator SelectedIter(OldSelectedNodes); SelectedIter; ++SelectedIter)
			{
				UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
				if ((Node != nullptr) && Node->CanDuplicateNode())
				{
					MetasoundGraphEditor->SetNodeSelection(Node, true);
				}
				else
				{
					RemainingNodes.Add(Node);
				}
			}

			// Delete the duplicatable nodes
			DeleteSelectedNodes();

			// Reselect whatever's left from the original selection after the deletion
			MetasoundGraphEditor->ClearSelectionSet();

			for (FGraphPanelSelectionSet::TConstIterator SelectedIter(RemainingNodes); SelectedIter; ++SelectedIter)
			{
				if (UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter))
				{
					MetasoundGraphEditor->SetNodeSelection(Node, true);
				}
			}
		}

		void FEditor::CutSelectedNodes()
		{
			CopySelectedNodes();
			// Cut should only delete nodes that can be duplicated
			DeleteSelectedDuplicatableNodes();
		}

		bool FEditor::CanCutNodes() const
		{
			return CanCopyNodes();
		}

		void FEditor::CopySelectedNodes()
		{
			// Export the selected nodes and place the text on the clipboard
			const FGraphPanelSelectionSet SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();

			FString ExportedText;

			for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
			{
				if (UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(*SelectedIter))
				{
					Node->PrepareForCopying();
				}
			}

			FEdGraphUtilities::ExportNodesToText(SelectedNodes, /*out*/ ExportedText);
			FPlatformApplicationMisc::ClipboardCopy(*ExportedText);

			// Make sure Metasound remains the owner of the copied nodes
			for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
			{
				if (UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(*SelectedIter))
				{
					Node->PostCopyNode();
				}
			}
		}

		bool FEditor::CanCopyNodes() const
		{
			// If any of the nodes can be duplicated then allow copying
			const FGraphPanelSelectionSet& SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();
			for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
			{
				UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
				if (Node && Node->CanDuplicateNode())
				{
					return true;
				}
			}
			return false;
		}

		void FEditor::PasteNodes()
		{
			PasteNodesAtLocation(MetasoundGraphEditor->GetPasteLocation());
		}

		void FEditor::PasteNodesAtLocation(const FVector2D& Location)
		{
			UEdGraph* Graph = Metasound->GetGraph();
			if (!Graph)
			{
				return;
			}

			// Undo/Redo support
			const FScopedTransaction Transaction(NSLOCTEXT("UnrealEd", "MetasoundEditorPaste", "Paste Metasound Node"));
			Graph->Modify();
			Metasound->Modify();

			// Clear the selection set (newly pasted stuff will be selected)
			MetasoundGraphEditor->ClearSelectionSet();

			// Grab the text to paste from the clipboard.
			FString TextToImport;
			FPlatformApplicationMisc::ClipboardPaste(TextToImport);

			// Import the nodes
			TSet<UEdGraphNode*> PastedNodes;
			FEdGraphUtilities::ImportNodesFromText(Graph, TextToImport, /*out*/ PastedNodes);

			//Average position of nodes so we can move them while still maintaining relative distances to each other
			FVector2D AvgNodePosition(0.0f, 0.0f);

			for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
			{
				UEdGraphNode* Node = *It;
				AvgNodePosition.X += Node->NodePosX;
				AvgNodePosition.Y += Node->NodePosY;
			}

			if (PastedNodes.Num() > 0)
			{
				float InvNumNodes = 1.0f / float(PastedNodes.Num());
				AvgNodePosition.X *= InvNumNodes;
				AvgNodePosition.Y *= InvNumNodes;
			}

			for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
			{
				UEdGraphNode* Node = *It;

				if (UMetasoundEditorGraphNode* SoundGraphNode = Cast<UMetasoundEditorGraphNode>(Node))
				{
					// TODO: Add newly referenced nodes to MS reference list
				}

				// Select the newly pasted stuff
				MetasoundGraphEditor->SetNodeSelection(Node, true);

				Node->NodePosX = (Node->NodePosX - AvgNodePosition.X) + Location.X;
				Node->NodePosY = (Node->NodePosY - AvgNodePosition.Y) + Location.Y;

				Node->SnapToGrid(SNodePanel::GetSnapGridSize());

				// Give new node a different Guid from the old one
				Node->CreateNewGuid();
			}

			// Update UI
			MetasoundGraphEditor->NotifyGraphChanged();

			Metasound->PostEditChange();
			Metasound->MarkPackageDirty();
		}

		bool FEditor::CanPasteNodes() const
		{
			FString ClipboardContent;
			FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

			// TODO: Implement
		// 	const bool bCanPasteNodes = FEdGraphUtilities::CanImportNodesFromText(Metasound->MetasoundGraph, ClipboardContent);
		// 	return bCanPasteNodes;

			return false;
		}

		void FEditor::DuplicateNodes()
		{
			// Copy and paste current selection
			CopySelectedNodes();
			PasteNodes();
		}

		bool FEditor::CanDuplicateNodes() const
		{
			return CanCopyNodes();
		}

		void FEditor::UndoGraphAction()
		{
			check(GEditor);
			GEditor->UndoTransaction();
		}

		void FEditor::RedoGraphAction()
		{
			// Clear selection, to avoid holding refs to nodes that go away
			MetasoundGraphEditor->ClearSelectionSet();

			check(GEditor);
			GEditor->RedoTransaction();
		}
	}
}
#undef LOCTEXT_NAMESPACE
