// Copyright Epic Games, Inc. All Rights Reserved.

#include "MetasoundEditor.h"

#include "AudioDevice.h"
#include "AudioMeterStyle.h"
#include "Components/AudioComponent.h"
#include "DetailLayoutBuilder.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "EditorStyleSet.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GraphEditor.h"
#include "GraphEditorActions.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformApplicationMisc.h"
#include "IAudioExtensionPlugin.h"
#include "IDetailsView.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Logging/TokenizedMessage.h"
#include "Metasound.h"
#include "MetasoundEditorCommands.h"
#include "MetasoundEditorGraph.h"
#include "MetasoundEditorGraphBuilder.h"
#include "MetasoundEditorGraphNode.h"
#include "MetasoundEditorGraphSchema.h"
#include "MetasoundEditorGraphValidation.h"
#include "MetasoundEditorModule.h"
#include "MetasoundEditorSettings.h"
#include "MetasoundEditorTabFactory.h"
#include "MetasoundFrontend.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundFrontendRegistries.h"
#include "MetasoundFrontendSearchEngine.h"
#include "MetasoundLog.h"
#include "MetasoundUObjectRegistry.h"
#include "Misc/Attribute.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "SMetasoundPalette.h"
#include "SNodePanel.h"
#include "Templates/SharedPointer.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SBoxPanel.h"

// This needs to be moved to public directory
#include "../../GraphEditor/Private/GraphActionNode.h"


#define LOCTEXT_NAMESPACE "MetaSoundEditor"

static int32 ShowLiteralMetasoundInputsInEditorCVar = 0;
// FAutoConsoleVariableRef CVarShowLiteralMetasoundInputsInEditor(
// 	TEXT("au.Debug.Editor.Metasounds.ShowLiteralInputs"),
// 	ShowLiteralMetasoundInputsInEditorCVar,
// 	TEXT("Show literal inputs in the Metasound Editor.\n")
// 	TEXT("0: Disabled (default), !0: Enabled"),
// 	ECVF_Default);


namespace Metasound
{
	namespace Editor
	{
		namespace Private
		{
			void ValidateAndUpdate(UMetasoundEditorGraph& Graph)
			{
				using namespace Frontend;

				FGraphValidationResults Results;
				Graph.Validate(Results);

				// Only auto-update minor upgrades
				for (const FGraphNodeValidationResult& Result : Results.GetResults())
				{
					FMetasoundFrontendVersionNumber MinorUpdateVersion = Result.Node->GetMinorUpdateAvailable();
					if (MinorUpdateVersion.IsValid())
					{
						Result.Node->UpdateToVersion(MinorUpdateVersion, true /* bInPropagateErrorMessages */);
					}
				}
			}
		}

		static const TArray<FText> NodeSectionNames
		{
			LOCTEXT("NodeSectionName_Invalid", "INVALID"),
			LOCTEXT("NodeSectionName_Inputs", "Inputs"),
			LOCTEXT("NodeSectionName_Outputs", "Outputs")
		};

		class FMetasoundGraphNodeSchemaAction : public FEdGraphSchemaAction
		{
		public:
			UEdGraph* Graph = nullptr;
			FGuid NodeID;

			FMetasoundGraphNodeSchemaAction()
				: FEdGraphSchemaAction()
			{
			}

			FMetasoundGraphNodeSchemaAction(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping, const ENodeSection InSectionID)
				: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping, FText(), static_cast<int32>(InSectionID))
			{
			}

			FMetasoundAssetBase& GetMetasoundAssetChecked() const
			{
				UObject* Object = CastChecked<UMetasoundEditorGraph>(Graph)->GetMetasound();
				FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Object);
				check(MetasoundAsset);
				return *MetasoundAsset;
			}

			UMetasoundEditorGraphVariable* GetVariable() const
			{
				UMetasoundEditorGraph* MetasoundGraph = CastChecked<UMetasoundEditorGraph>(Graph);
				return MetasoundGraph->FindVariable(NodeID);
			}

			Frontend::FGraphHandle GetGraphHandle() const
			{
				return GetMetasoundAssetChecked().GetRootGraphHandle();
			}

			Frontend::FNodeHandle GetNodeHandle() const
			{
				return GetGraphHandle()->GetNodeWithID(NodeID);
			}

			bool IsRequired() const
			{
				return GetNodeHandle()->IsRequired();
			}

			// FEdGraphSchemaAction interface
			virtual bool IsParentable() const override
			{
				return true;
			}

			virtual void MovePersistentItemToCategory(const FText& NewCategoryName) override
			{
				checkNoEntry();
			}

			virtual int32 GetReorderIndexInContainer() const override
			{
				TArray<Frontend::FConstNodeHandle> InputHandles = GetGraphHandle()->GetConstInputNodes();
				return InputHandles.IndexOfByPredicate([=](const Frontend::FConstNodeHandle& NodeHandle)
				{
					return NodeHandle->GetID() == NodeID;
				});
			}

			virtual bool ReorderToBeforeAction(TSharedRef<FEdGraphSchemaAction> OtherAction) override
			{
				// TODO: Implement reordering
				checkNoEntry();

				return false;
			}
		};

		class SMetaSoundGraphPaletteItem : public SGraphPaletteItem
		{
		private:
			TSharedPtr<FMetasoundGraphNodeSchemaAction> MetasoundAction;
			bool bIsNameInvalid = false;

		public:
			SLATE_BEGIN_ARGS(SMetaSoundGraphPaletteItem)
			{
			}

			SLATE_END_ARGS()

			void Construct(const FArguments& InArgs, FCreateWidgetForActionData* const InCreateData)
			{
				TSharedPtr<FEdGraphSchemaAction> Action = InCreateData->Action;
				MetasoundAction = StaticCastSharedPtr<FMetasoundGraphNodeSchemaAction>(Action);

				SGraphPaletteItem::Construct(SGraphPaletteItem::FArguments(), InCreateData);
			}

		protected:
			virtual void OnNameTextCommitted(const FText& InNewText, ETextCommit::Type InTextCommit) override
			{
				using namespace Frontend;

				if (MetasoundAction.IsValid())
				{
					if (UMetasoundEditorGraphVariable* GraphVariable = MetasoundAction->GetVariable())
					{
						GraphVariable->SetDisplayName(InNewText);
					}
				}
			}

			virtual bool OnNameTextVerifyChanged(const FText& InNewText, FText& OutErrorMessage) override
			{
				if (MetasoundAction.IsValid())
				{
					if (UMetasoundEditorGraphVariable* GraphVariable = MetasoundAction->GetVariable())
					{
						return GraphVariable->CanRename(InNewText, OutErrorMessage);
					}
				}

				return false;
			}
		};

		void FEditor::RegisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
		{
			WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_MetasoundEditor", "MetaSound Editor"));
			auto WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

			FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

			InTabManager->RegisterTabSpawner(TabFactory::Names::GraphCanvas, FOnSpawnTab::CreateLambda([InPlayTimeWidget = PlayTimeWidget, InMetasoundGraphEditor = MetasoundGraphEditor](const FSpawnTabArgs& Args)
			{
				return TabFactory::CreateGraphCanvasTab(SNew(SOverlay)
					+ SOverlay::Slot()
					[
						InMetasoundGraphEditor.ToSharedRef()
					]
					+ SOverlay::Slot()
					[
						InPlayTimeWidget.ToSharedRef()
					]
					.Padding(5.0f, 5.0f)
				, Args);
			}))
			.SetDisplayName(LOCTEXT("GraphCanvasTab", "Viewport"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "GraphEditor.EventGraph_16x"));

			InTabManager->RegisterTabSpawner(TabFactory::Names::Inspector, FOnSpawnTab::CreateLambda([InMetasoundDetails = MetasoundDetails](const FSpawnTabArgs& Args)
			{
				return TabFactory::CreateInspectorTab(InMetasoundDetails, Args);
			}))
			.SetDisplayName(LOCTEXT("InspectorTab", "Inspector"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

			InTabManager->RegisterTabSpawner(TabFactory::Names::Metasound, FOnSpawnTab::CreateLambda([InMetasoundMenu = MetasoundInterfaceMenu](const FSpawnTabArgs& Args)
			{
				return TabFactory::CreateMetasoundTab(InMetasoundMenu, Args);
			}))
			.SetDisplayName(LOCTEXT("MetasoundTab", "MetaSound"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Settings"));


			InTabManager->RegisterTabSpawner(TabFactory::Names::Analyzers, FOnSpawnTab::CreateLambda([InMetasoundMeter = MetasoundMeter](const FSpawnTabArgs& Args)
			{
				return TabFactory::CreateAnalyzersTab(InMetasoundMeter, Args);
			}))
			.SetDisplayName(LOCTEXT("AnalyzersTab", "Analyzers"))
			.SetGroup(WorkspaceMenuCategoryRef)
			.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "Kismet.Tabs.Palette"));
		}

		void FEditor::UnregisterTabSpawners(const TSharedRef<FTabManager>& InTabManager)
		{
			using namespace Metasound::Editor;

			FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

			InTabManager->UnregisterTabSpawner(TabFactory::Names::Analyzers);
			InTabManager->UnregisterTabSpawner(TabFactory::Names::GraphCanvas);
			InTabManager->UnregisterTabSpawner(TabFactory::Names::Inspector);
			InTabManager->UnregisterTabSpawner(TabFactory::Names::Metasound);
		}

		bool FEditor::IsPlaying() const
		{
			if (Metasound)
			{
				FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
				check(MetasoundAsset);

				if (UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(MetasoundAsset->GetGraph()))
				{
					return Graph->IsPreviewing();
				}
			}

			return false;
		}

		FEditor::~FEditor()
		{
			if (IsPlaying())
			{
				Stop();
			}

			if (Metasound)
			{
				FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
				check(MetasoundAsset);

				if(UMetasoundEditorGraph* Graph = Cast<UMetasoundEditorGraph>(MetasoundAsset->GetGraph()))
				{
					for (TPair<FGuid, FDelegateHandle>& Pair : NameChangeDelegateHandles)
					{
						if (UMetasoundEditorGraphInput* Input = Graph->FindInput(Pair.Key))
						{
							Input->NameChanged.Remove(Pair.Value);
						}
						if (UMetasoundEditorGraphOutput* Output = Graph->FindOutput(Pair.Key))
						{
							Output->NameChanged.Remove(Pair.Value);
						}
					}
				}
				NameChangeDelegateHandles.Reset();
			}

			if (MetasoundMeterAnalyzer.IsValid() && ResultsDelegateHandle.IsValid())
			{
				MetasoundMeterAnalyzer->OnLatestPerChannelMeterResultsNative.Remove(ResultsDelegateHandle);
			}

			check(GEditor);
			GEditor->UnregisterForUndo(this);
		}

		void FEditor::InitMetasoundEditor(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, UObject* ObjectToEdit)
		{
			check(ObjectToEdit);
			checkf(IMetasoundUObjectRegistry::Get().IsRegisteredClass(ObjectToEdit), TEXT("Object passed in was not registered as a valid metasound archetype!"));
			
			// Support undo/redo
			ObjectToEdit->SetFlags(RF_Transactional);

			Metasound = ObjectToEdit;

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
			if (!MetasoundAsset->GetGraph())
			{
				UMetasoundEditorGraph* Graph = NewObject<UMetasoundEditorGraph>(Metasound, FName(), RF_Transactional);
				Graph->Schema = UMetasoundEditorGraphSchema::StaticClass();
				MetasoundAsset->SetGraph(Graph);
				FGraphBuilder::SynchronizeGraph(*Metasound);
			}

			GEditor->RegisterForUndo(this);

			FGraphEditorCommands::Register();
			FEditorCommands::Register();

			BindGraphCommands();

			CreateInternalWidgets();

			CreateAnalyzers();

			const TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_MetasoundEditor_Layout_v8")
				->AddArea
				(
					FTabManager::NewPrimaryArea()
					->SetOrientation(Orient_Vertical)
					->Split(FTabManager::NewSplitter()
						->SetOrientation(Orient_Horizontal)
						->Split
						(
							FTabManager::NewSplitter()
							->SetSizeCoefficient(0.15f)
							->SetOrientation(Orient_Vertical)
							->Split
							(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.25f)
								->SetHideTabWell(false)
								->AddTab(TabFactory::Names::Metasound, ETabState::OpenedTab)
							)
							->Split
							(
								FTabManager::NewStack()
								->SetSizeCoefficient(0.50f)
								->SetHideTabWell(false)
								->AddTab(TabFactory::Names::Inspector, ETabState::OpenedTab)
							)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.80f)
							->SetHideTabWell(true)
							->AddTab(TabFactory::Names::GraphCanvas, ETabState::OpenedTab)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.05f)
							->SetHideTabWell(true)
							->AddTab(TabFactory::Names::Analyzers, ETabState::OpenedTab)
						)
					)
				);

			const bool bCreateDefaultStandaloneMenu = true;
			const bool bCreateDefaultToolbar = true;
			FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, TEXT("MetasoundEditorApp"), StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ObjectToEdit, false);

			ExtendToolbar();
			RegenerateMenusAndToolbars();

			Private::ValidateAndUpdate(GetMetaSoundGraphChecked());
		}

		UObject* FEditor::GetMetasoundObject() const
		{
			return Metasound;
		}

		UObject* FEditor::GetMetasoundAudioBusObject() const
		{
			return MetasoundAudioBus.Get();
		}

		void FEditor::SetSelection(const TArray<UObject*>& SelectedObjects)
		{
			if (MetasoundDetails.IsValid())
			{
				MetasoundDetails->SetObjects(SelectedObjects);
				MetasoundDetails->HideFilterArea(true);
			}
		}

		bool FEditor::GetBoundsForSelectedNodes(FSlateRect& Rect, float Padding)
		{
			return MetasoundGraphEditor->GetBoundsForSelectedNodes(Rect, Padding);
		}

		FName FEditor::GetToolkitFName() const
		{
			return "MetaSoundEditor";
		}

		FText FEditor::GetBaseToolkitName() const
		{
			return LOCTEXT("AppLabel", "MetaSound Editor");
		}

		FString FEditor::GetWorldCentricTabPrefix() const
		{
			return LOCTEXT("WorldCentricTabPrefix", "MetaSound ").ToString();
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
			}

			if (MetasoundInterfaceMenu.IsValid())
			{
				MetasoundInterfaceMenu->RefreshAllActions(true /* bPreserveExpansion */);
			}

			FSlateApplication::Get().DismissAllMenus();
		}

		void FEditor::NotifyUserModifiedBySync()
		{
			FNotificationInfo Info(LOCTEXT("SynchronizationRequired", "Operation modified pin(s), connection(s), and/or node(s).  Please refer to graph."));
			Info.bFireAndForget = true;
			Info.bUseSuccessFailIcons = false;
			Info.ExpireDuration = 5.0f;

			MetasoundGraphEditor->AddNotification(Info, false /* bSuccess */);
		}

		void FEditor::NotifyPostChange(const FPropertyChangedEvent& PropertyChangedEvent, FProperty* PropertyThatChanged)
		{	
			if (MetasoundGraphEditor.IsValid() && PropertyChangedEvent.ChangeType != EPropertyChangeType::Interactive)
			{
				// If a property change event occurs outside of the metasound UEdGraph and results in the metasound document changing, 
				// then the document and the UEdGraph need to be synchronized. There may be a better trigger for this call to reduce
				// the number of times the graph is synchronized. 
				UObject* MetasoundObj = GetMetasoundObject();
				if (nullptr != MetasoundObj)
				{
					if (IMetasoundUObjectRegistry::Get().IsRegisteredClass(MetasoundObj))
					{
						if (FGraphBuilder::SynchronizeGraph(*MetasoundObj))
						{
							NotifyUserModifiedBySync();
							MetasoundObj->MarkPackageDirty();
						}
						MetasoundGraphEditor->NotifyGraphChanged();
						MetasoundInterfaceMenu->RefreshAllActions(true /* bPreserveExpansion */);
					}
				}
			}
		}

		void FEditor::CreateInternalWidgets()
		{
			CreateGraphEditorWidget();

			FDetailsViewArgs Args;
			Args.bHideSelectionTip = true;
			Args.NotifyHook = this;

			SAssignNew(MetasoundInterfaceMenu, SGraphActionMenu, false)
				.AlphaSortItems(false)
// 				.OnActionDoubleClicked(this, &FEditor::OnActionDoubleClicked)
				.OnActionDragged(this, &FEditor::OnActionDragged)
				.OnActionMatchesName(this, &FEditor::HandleActionMatchesName)
				.OnActionSelected(this, &FEditor::OnActionSelected)
// 				.OnCategoryTextCommitted(this, &FEditor::OnCategoryNameCommitted)
				.OnCollectAllActions(this, &FEditor::CollectAllActions)
				.OnCollectStaticSections(this, &FEditor::CollectStaticSections)
// 				.OnContextMenuOpening(this, &FEditor::OnContextMenuOpening)
				.OnCreateWidgetForAction(this, &FEditor::OnCreateWidgetForAction)
  				.OnCanRenameSelectedAction(this, &FEditor::CanRenameOnActionNode)
				.OnGetFilterText(this, &FEditor::GetFilterText)
				.OnGetSectionTitle(this, &FEditor::OnGetSectionTitle)
				.OnGetSectionWidget(this, &FEditor::OnGetMenuSectionWidget)
				.UseSectionStyling(true);

			FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
			MetasoundDetails = PropertyModule.CreateDetailView(Args);
			Palette = SNew(SMetasoundPalette);
		}

		// TODO: Tie in rename on GraphActionMenu.  For now, just renameable via field in inspector
		bool FEditor::CanRenameOnActionNode(TWeakPtr<FGraphActionNode> InSelectedNode) const
		{
			return false;
		}

		void FEditor::OnMeterOutput(UMeterAnalyzer* InMeterAnalyzer, int32 ChannelIndex, const FMeterResults& InMeterResults)
		{
			if (InMeterAnalyzer && MetasoundMeter.IsValid())
			{
				FMeterChannelInfo ChannelInfo;
				ChannelInfo.MeterValue = InMeterResults.MeterValue;
				ChannelInfo.PeakValue = InMeterResults.PeakValue;

				check(ChannelIndex < MetasoundChannelInfo.Num());
				MetasoundChannelInfo[ChannelIndex] = ChannelInfo;

				// If it's the last channel, update the widget
				if (ChannelIndex == MetasoundChannelInfo.Num() - 1)
				{
					MetasoundMeter->SetMeterChannelInfo(MetasoundChannelInfo);
				}
			}
		}

		void FEditor::CreateAnalyzers()
		{
			MetasoundAudioBus = TStrongObjectPtr<UAudioBus>(NewObject<UAudioBus>());
			MetasoundAudioBus->AudioBusChannels = EAudioBusChannels::Stereo;

			MetasoundMeterAnalyzer = TStrongObjectPtr<UMeterAnalyzer>(NewObject<UMeterAnalyzer>());

			ResultsDelegateHandle = MetasoundMeterAnalyzer->OnLatestPerChannelMeterResultsNative.AddRaw(this, &FEditor::OnMeterOutput);

			MetasoundMeterAnalyzerSettings = TStrongObjectPtr<UMeterSettings>(NewObject<UMeterSettings>());
			MetasoundMeterAnalyzerSettings->PeakHoldTime = 4000.0f;

			MetasoundMeterAnalyzer->Settings = MetasoundMeterAnalyzerSettings.Get();

			UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
			MetasoundMeterAnalyzer->StartAnalyzing(EditorWorld, MetasoundAudioBus.Get());

			// Create the audio meter
			MetasoundMeter = SNew(SAudioMeter);

			// TODO: THIS IS TEMP, WIP
			MetasoundMeter->SetOrientation(EOrientation::Orient_Vertical);
			MetasoundMeter->SetBackgroundColor(FLinearColor(0.0075f, 0.0075f, 0.0075, 1.0f));
			MetasoundMeter->SetMeterBackgroundColor(FLinearColor(0.031f, 0.031f, 0.031f, 1.0f));
			MetasoundMeter->SetMeterValueColor(FLinearColor(0.025719f, 0.208333f, 0.069907f, 1.0f));
			MetasoundMeter->SetMeterPeakColor(FLinearColor(0.24349f, 0.708333f, 0.357002f, 1.0f));
			MetasoundMeter->SetMeterClippingColor(FLinearColor(1.0f, 0.0f, 0.112334f, 1.0f));
			MetasoundMeter->SetMeterScaleColor(FLinearColor(0.017642f, 0.017642f, 0.017642f, 1.0f));
			MetasoundMeter->SetMeterScaleLabelColor(FLinearColor(0.442708f, 0.442708f, 0.442708f, 1.0f));

			FMeterChannelInfo DefaultInfo;
			DefaultInfo.MeterValue = -60.0f;
			DefaultInfo.PeakValue = -60.0f;
			MetasoundChannelInfo.Add(DefaultInfo);
			MetasoundChannelInfo.Add(DefaultInfo);
			MetasoundMeter->SetMeterChannelInfo(MetasoundChannelInfo);

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
				// TODO: Add OS SVD and clean this up post UE5.0 - Early Access
// 					ToolbarBuilder.BeginSection("Utilities");
// 					{
// 						ToolbarBuilder.AddToolBarButton
// 						(
// 							FEditorCommands::Get().Import,
// 							NAME_None,
// 							TAttribute<FText>(),
// 							TAttribute<FText>(),
// 							TAttribute<FSlateIcon>::Create([this]() { return GetImportStatusImage(); }),
// 							"ImportMetasound"
// 						);
// 
// 						ToolbarBuilder.AddToolBarButton
// 						(
// 							FEditorCommands::Get().Export,
// 							NAME_None,
// 							TAttribute<FText>(),
// 							TAttribute<FText>(),
// 							TAttribute<FSlateIcon>::Create([this]() { return GetExportStatusImage(); }),
// 							"ExportMetasound"
// 						);
// 					}
// 					ToolbarBuilder.EndSection();

					ToolbarBuilder.BeginSection("Audition");
					{
						ToolbarBuilder.AddToolBarButton(FEditorCommands::Get().Play);
						ToolbarBuilder.AddToolBarButton(FEditorCommands::Get().Stop);
					}
					ToolbarBuilder.EndSection();

					ToolbarBuilder.BeginSection("Utilities");
					{
						ToolbarBuilder.AddToolBarButton(
							FEditorCommands::Get().EditGeneralSettings,
							NAME_None,
							TAttribute<FText>(),
							TAttribute<FText>(),
							TAttribute<FSlateIcon>::Create([this]() { return GetSettingsImage(); }),
							"EditGeneralSettings"
						);

						ToolbarBuilder.AddToolBarButton(
							FEditorCommands::Get().EditMetasoundSettings,
							NAME_None,
							TAttribute<FText>(),
							TAttribute<FText>(),
							TAttribute<FSlateIcon>::Create([this]() { return GetSettingsImage(); }),
							"EditMetasoundSettings"
						);
					}
					ToolbarBuilder.EndSection();
				})
			);

			AddToolbarExtender(ToolbarExtender);
		}

		FSlateIcon FEditor::GetImportStatusImage() const
		{
			const FName IconName = "MetasoundEditor.Import";
			return FSlateIcon("MetaSoundStyle", IconName);
		}

		FSlateIcon FEditor::GetSettingsImage() const
		{
			const FName IconName = "MetasoundEditor.Settings";
			return FSlateIcon("MetaSoundStyle", IconName);
		}

		FSlateIcon FEditor::GetExportStatusImage() const
		{
			FName IconName = "MetasoundEditor.Export";
			if (!bPassedValidation)
			{
				IconName = "MetasoundEditor.ExportError";
			}

			return FSlateIcon("MetaSoundStyle", IconName);
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

			ToolkitCommands->MapAction(
				Commands.EditMetasoundSettings,
				FExecuteAction::CreateSP(this, &FEditor::EditMetasoundSettings));

			ToolkitCommands->MapAction(
				Commands.EditGeneralSettings,
				FExecuteAction::CreateSP(this, &FEditor::EditGeneralSettings));

			ToolkitCommands->MapAction(
				FGenericCommands::Get().Delete,
				FExecuteAction::CreateSP(this, &FEditor::DeleteSelected));
		}

		void FEditor::Import()
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
			if (MetasoundAsset)
			{
				// TODO: Prompt OFD and provide path from user
				const FString InputPath = FPaths::ProjectIntermediateDir() / TEXT("MetaSounds") + FPaths::ChangeExtension(Metasound->GetPathName(), FMetasoundAssetBase::FileExtension);
				
				// TODO: use the same directory as the currently open MetaSound
				const FString OutputPath = FString("/Game/ImportedMetaSound/GeneratedMetaSound");

				FMetasoundFrontendDocument MetasoundDoc;

				if (Frontend::ImportJSONAssetToMetasound(InputPath, MetasoundDoc))
				{
					TArray<UClass*> ImportClasses = IMetasoundUObjectRegistry::Get().GetUClassesForArchetype(MetasoundDoc.Archetype.Name);

					if (ImportClasses.Num() < 1)
					{
						UE_LOG(LogMetaSound, Warning, TEXT("Cannot create UObject from MetaSound document. No UClass supports archetype \"%s\""), *MetasoundDoc.Archetype.Name.ToString());
					}
					else
					{
						if (ImportClasses.Num() > 1)
						{
							for (UClass* Cls : ImportClasses)
							{
								// TODO: could do a modal dialog to give user choice of import type.
								UE_LOG(LogMetaSound, Warning, TEXT("Duplicate UClass support archetype \"%s\" with UClass \"%s\""), *MetasoundDoc.Archetype.Name.ToString(), *Cls->GetName());
							}
						}

						IMetasoundUObjectRegistry::Get().NewObject(ImportClasses[0], MetasoundDoc, OutputPath);
					}
				}
				else
				{
					UE_LOG(LogMetaSound, Warning, TEXT("Could not import MetaSound at path: %s"), *InputPath);
				}
			}
		}

		void FEditor::Export()
		{
			FMetasoundAssetBase* InMetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
			check(InMetasoundAsset);

			if (!InMetasoundAsset)
			{
				return;
			}

			static const FString MetasoundExtension(TEXT(".metasound"));

			// TODO: We could just make this an object.
			const FString Path = FPaths::ProjectSavedDir() / TEXT("MetaSounds") + FPaths::ChangeExtension(Metasound->GetPathName(), MetasoundExtension);
			InMetasoundAsset->GetDocumentHandle()->ExportToJSONAsset(Path);
		}

		void FEditor::Play()
		{
			if (USoundBase* MetasoundToPlay = Cast<USoundBase>(Metasound))
			{
				// Set the send to the audio bus that is used for analyzing the metasound output
				check(GEditor);
				if (UAudioComponent* PreviewComp = GEditor->PlayPreviewSound(MetasoundToPlay))
				{
					PlayTime = 0.0;

					UObject* ParamInterfaceObject = PreviewComp->GetParameterInterface().GetObject();
					if (ensure(ParamInterfaceObject))
					{
						SetPreviewID(ParamInterfaceObject->GetUniqueID());
					}

					if (MetasoundAudioBus.IsValid())
					{
						PreviewComp->SetAudioBusSendPostEffect(MetasoundAudioBus.Get(), 1.0f);
					}
				}

				MetasoundGraphEditor->RegisterActiveTimer(0.0f,
					FWidgetActiveTimerDelegate::CreateLambda([this](double InCurrentTime, float InDeltaTime)
					{
						if (IsPlaying())
						{
							if (PlayTimeWidget.IsValid())
							{
								PlayTime += InDeltaTime;
								FString PlayTimeString = FTimespan::FromSeconds(PlayTime).ToString();

								// Remove leading '+'
								PlayTimeString.ReplaceInline(TEXT("+"), TEXT(""));
								PlayTimeWidget->SetText(FText::FromString(PlayTimeString));
							}
							return EActiveTimerReturnType::Continue;
						}
						else
						{
							SetPreviewID(INDEX_NONE);
							PlayTime = 0.0;
							PlayTimeWidget->SetText(FText::GetEmpty());
							return EActiveTimerReturnType::Stop;
						}
					})
				);
			}
		}

		void FEditor::SetPreviewID(uint32 InPreviewID)
		{
			if (!Metasound)
			{
				return;
			}

			GetMetaSoundGraphChecked().SetPreviewID(InPreviewID);
		}

		UMetasoundEditorGraph& FEditor::GetMetaSoundGraphChecked()
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
			check(MetasoundAsset);

			UEdGraph* Graph = MetasoundAsset->GetGraph();
			check(Graph);

			return *CastChecked<UMetasoundEditorGraph>(MetasoundAsset->GetGraph());
		}

		void FEditor::ExecuteNode()
		{
			const FGraphPanelSelectionSet SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();
			for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
			{
				ExecuteNode(CastChecked<UEdGraphNode>(*NodeIt));
			}
		}

		bool FEditor::CanExecuteNode() const
		{
			return true;
		}

		double FEditor::GetPlayTime() const
		{
			return PlayTime;
		}

		void FEditor::Stop()
		{
			check(GEditor);
			GEditor->ResetPreviewAudioComponent();
			SetPreviewID(INDEX_NONE);
		}

		void FEditor::TogglePlayback()
		{
			check(GEditor);

			if (IsPlaying())
			{
				Stop();
			}
			else
			{
				Play();
			}
		}

		void FEditor::ExecuteNode(UEdGraphNode* Node)
		{
			if (!IsPlaying())
			{
				return;
			}

			if (UMetasoundEditorGraphInputNode* InputNode = Cast<UMetasoundEditorGraphInputNode>(Node))
			{
				if (UAudioComponent* PreviewComponent = GEditor->GetPreviewAudioComponent())
				{
					// TODO: fix how identifying the parameter to update is determined. It should not be done
					// with a "DisplayName" but rather the vertex Guid.
					if (TScriptInterface<IAudioParameterInterface> ParamInterface = PreviewComponent->GetParameterInterface())
					{
						Metasound::Frontend::FConstNodeHandle NodeHandle = InputNode->GetConstNodeHandle();
						Metasound::FVertexKey VertexKey = Metasound::FVertexKey(NodeHandle->GetDisplayName().ToString());
						ParamInterface->Trigger(*VertexKey);
					}
				}
			}
		}

		void FEditor::EditObjectSettings()
		{
			if (MetasoundInterfaceMenu.IsValid())
			{
				MetasoundInterfaceMenu->SelectItemByName(FName());
			}

			if (MetasoundGraphEditor.IsValid())
			{
				bManuallyClearingGraphSelection = true;
				MetasoundGraphEditor->ClearSelectionSet();
				bManuallyClearingGraphSelection = false;
			}

			// Clear selection first to force refresh of customization
			// if swapping from one object-level edit mode to the other
			// (ex. Metasound Settings to General Settings)
			SetSelection({ });
			SetSelection({ Metasound });
		}

		void FEditor::EditGeneralSettings()
		{
			if (UMetasoundEditorSettings* EditorSettings = GetMutableDefault<UMetasoundEditorSettings>())
			{
				EditorSettings->DetailView = EMetasoundActiveDetailView::General;
			}

			EditObjectSettings();
		}

		void FEditor::EditMetasoundSettings()
		{
			if (UMetasoundEditorSettings* EditorSettings = GetMutableDefault<UMetasoundEditorSettings>())
			{
				EditorSettings->DetailView = EMetasoundActiveDetailView::Metasound;
			}

			EditObjectSettings();
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
			if (MetasoundGraphEditor.IsValid())
			{
				if (UEdGraph* Graph = MetasoundGraphEditor->GetCurrentGraph())
				{
					FMetasoundGraphSchemaAction_NewComment CommentAction;
					CommentAction.PerformAction(Graph, nullptr, MetasoundGraphEditor->GetPasteLocation());
				}
			}
		}

		void FEditor::CreateGraphEditorWidget()
		{
			if (!GraphEditorCommands.IsValid())
			{
				GraphEditorCommands = MakeShared<FUICommandList>();

				GraphEditorCommands->MapAction(FEditorCommands::Get().BrowserSync,
					FExecuteAction::CreateSP(this, &FEditor::SyncInBrowser));

				GraphEditorCommands->MapAction(FEditorCommands::Get().EditMetasoundSettings,
					FExecuteAction::CreateSP(this, &FEditor::EditMetasoundSettings));

				GraphEditorCommands->MapAction(FEditorCommands::Get().EditGeneralSettings,
					FExecuteAction::CreateSP(this, &FEditor::EditGeneralSettings));

				GraphEditorCommands->MapAction(FEditorCommands::Get().AddInput,
					FExecuteAction::CreateSP(this, &FEditor::AddInput),
					FCanExecuteAction::CreateSP(this, &FEditor::CanAddInput));

				GraphEditorCommands->MapAction(FEditorCommands::Get().DeleteInput,
					FExecuteAction::CreateSP(this, &FEditor::DeleteInput),
					FCanExecuteAction::CreateSP(this, &FEditor::CanDeleteInput));

				// Editing Commands
				GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
					FExecuteAction::CreateLambda([this]() { MetasoundGraphEditor->SelectAllNodes(); }));

				GraphEditorCommands->MapAction(FGenericCommands::Get().Copy,
					FExecuteAction::CreateSP(this, &FEditor::CopySelectedNodes),
					FCanExecuteAction::CreateSP(this, &FEditor::CanCopyNodes));

				GraphEditorCommands->MapAction(FGenericCommands::Get().Cut,
					FExecuteAction::CreateSP(this, &FEditor::CutSelectedNodes),
					FCanExecuteAction::CreateLambda([this]() { return CanCopyNodes() && CanDeleteNodes(); }));

				GraphEditorCommands->MapAction(FGenericCommands::Get().Paste,
					FExecuteAction::CreateLambda([this]() { PasteNodes(); }),
					FCanExecuteAction::CreateSP(this, &FEditor::CanPasteNodes));

				GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
					FExecuteAction::CreateSP(this, &FEditor::DeleteSelectedNodes),
					FCanExecuteAction::CreateLambda([this]() { return CanDeleteNodes(); }));

				GraphEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
					FExecuteAction::CreateLambda([this] { DuplicateNodes(); }),
					FCanExecuteAction::CreateLambda([this]() { return CanDuplicateNodes(); }));

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

				// Node Commands
				GraphEditorCommands->MapAction(FGraphEditorCommands::Get().CreateComment,
					FExecuteAction::CreateSP(this, &FEditor::OnCreateComment));

				GraphEditorCommands->MapAction(FEditorCommands::Get().UpdateNodes,
					FExecuteAction::CreateLambda([this]()
					{
						const FScopedTransaction Transaction(LOCTEXT("NodeVersionUpgrade", "Upgrade MetaSound Node(s)"));
						check(Metasound);
						Metasound->Modify();

						UMetasoundEditorGraph& Graph = GetMetaSoundGraphChecked();
						Graph.Modify();

						const FGraphPanelSelectionSet Selection = MetasoundGraphEditor->GetSelectedNodes();
						for (UObject* Object : Selection)
						{
							if (UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(Object))
							{
								FMetasoundFrontendVersionNumber MajorUpdateVersion = ExternalNode->GetMajorUpdateAvailable();
								if (MajorUpdateVersion.IsValid())
								{
									ExternalNode->UpdateToVersion(MajorUpdateVersion, false /* bInPropagateErrorMessages */);
								}
							}
						}
					}));
			}

			FGraphAppearanceInfo AppearanceInfo;
			AppearanceInfo.CornerText = LOCTEXT("AppearanceCornerText_MetaSound", "MetaSound");

			SGraphEditor::FGraphEditorEvents InEvents;
			InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FEditor::OnSelectedNodesChanged);
			InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FEditor::OnNodeTitleCommitted);
			InEvents.OnNodeDoubleClicked = FSingleNodeEvent::CreateSP(this, &FEditor::ExecuteNode);

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
			check(MetasoundAsset);

			SAssignNew(MetasoundGraphEditor, SGraphEditor)
				.AdditionalCommands(GraphEditorCommands)
				.IsEditable(true)
				.Appearance(AppearanceInfo)
				.GraphToEdit(MetasoundAsset->GetGraph())
				.GraphEvents(InEvents)
				.AutoExpandActionMenu(true)
				.ShowGraphStateOverlay(false);

			SAssignNew(PlayTimeWidget, STextBlock)
				.Visibility(EVisibility::HitTestInvisible)
				.TextStyle(FEditorStyle::Get(), "Graph.ZoomText")
				.ColorAndOpacity(FLinearColor(1, 1, 1, 0.30f));
		}

		void FEditor::OnSelectedNodesChanged(const TSet<UObject*>& InSelectedNodes)
		{
			TArray<UObject*> Selection;
			for (UObject* NodeObject : InSelectedNodes)
			{
				if (UMetasoundEditorGraphInputNode* InputNode = Cast<UMetasoundEditorGraphInputNode>(NodeObject))
				{
					Selection.Add(InputNode->Input);
				}
				else if (UMetasoundEditorGraphOutputNode* OutputNode = Cast<UMetasoundEditorGraphOutputNode>(NodeObject))
				{
					Selection.Add(OutputNode->Output);
				}
				else
				{
					Selection.Add(NodeObject);
				}
			}

			if (MetasoundInterfaceMenu.IsValid() && !bManuallyClearingGraphSelection)
			{
				MetasoundInterfaceMenu->SelectItemByName(FName());
			}
			SetSelection(Selection);
		}

		void FEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
		{
			if (NodeBeingChanged)
			{
				const FScopedTransaction Transaction(TEXT(""), LOCTEXT("RenameNode", "Rename Node"), NodeBeingChanged);
				NodeBeingChanged->Modify();
				NodeBeingChanged->OnRenameNode(NewText.ToString());
			}
		}

		void FEditor::DeleteInterfaceItem(TSharedPtr<FMetasoundGraphNodeSchemaAction> ActionToDelete, UMetasoundEditorGraph* Graph)
		{
			using namespace Metasound::Frontend;

			UMetasoundEditorGraphVariable* Variable = ActionToDelete->GetVariable();

			FNodeHandle VariableHandle = Variable->GetNodeHandle();
			const FGuid IDToDelete = VariableHandle->GetID();

			struct FNameNodeIDPair
			{
				FName Name;
				FGuid ID;
			};

			auto GetNameToSelect = [IDToDelete](const TArray<FConstNodeHandle>& Handles)
			{
				int32 IndexToDelete = -1;
				bool bGetNextValidName = false;
				for (int32 i = 0; i < Handles.Num(); ++i)
				{
					if (Handles[i]->GetID() == IDToDelete)
					{
						IndexToDelete = i;
						break;
					}
				}

				if (IndexToDelete >= 0)
				{
					for (int32 i = IndexToDelete + 1; i < Handles.Num(); ++i)
					{
						if (Handles[i]->GetNodeStyle().Display.Visibility != EMetasoundFrontendNodeStyleDisplayVisibility::Hidden)
						{
							const FName NameToSelect(*(Handles[i]->GetDisplayName().ToString()));
							return FNameNodeIDPair { NameToSelect, Handles[i]->GetID() };
						}
					}

					for (int32 i = IndexToDelete - 1; i >= 0; --i)
					{
						if (Handles[i]->GetNodeStyle().Display.Visibility != EMetasoundFrontendNodeStyleDisplayVisibility::Hidden)
						{
							FName NameToSelect = (*(Handles[i]->GetDisplayName().ToString()));
							return FNameNodeIDPair { NameToSelect, Handles[i]->GetID() };
						}
					}
				}

				return FNameNodeIDPair();
			};

			const TArray<FConstNodeHandle> InputHandles = VariableHandle->GetOwningGraph()->GetConstInputNodes();
			FNameNodeIDPair NameIDPair = GetNameToSelect(InputHandles);
			int32 SectionId = static_cast<int32>(ENodeSection::Inputs);
			if (NameIDPair.Name.IsNone())
			{
				SectionId = static_cast<int32>(ENodeSection::Outputs);
				const TArray<FConstNodeHandle> OutputHandles = VariableHandle->GetOwningGraph()->GetConstOutputNodes();
				NameIDPair = GetNameToSelect(OutputHandles);
			}

			FGraphBuilder::DeleteVariableNodeHandle(*Variable);
			Graph->RemoveVariable(*Variable);
			MetasoundInterfaceMenu->RefreshAllActions(true /* bPreserveExpansion */);

			if (!NameIDPair.Name.IsNone())
			{
				if (MetasoundInterfaceMenu->SelectItemByName(NameIDPair.Name, ESelectInfo::Direct, SectionId))
				{
					if (UMetasoundEditorGraphVariable* VariableToSelect = Graph->FindVariable(NameIDPair.ID))
					{
						const TArray<UObject*> VariablesToSelect { VariableToSelect };
						SetSelection(VariablesToSelect);
					}
				}
			}
		}

		void FEditor::DeleteSelected()
		{
			using namespace Frontend;

			if (MetasoundInterfaceMenu.IsValid())
			{
				TArray<TSharedPtr<FEdGraphSchemaAction>> Actions;
				MetasoundInterfaceMenu->GetSelectedActions(Actions);

				if (!Actions.IsEmpty())
				{
					check(Metasound);
					const FScopedTransaction Transaction(LOCTEXT("MetaSoundEditorDeleteSelectedNode", "Delete MetaSound Variable"));
					Metasound->Modify();

					UMetasoundEditorGraph& Graph = GetMetaSoundGraphChecked();
					Graph.Modify();

					TSharedPtr<FMetasoundGraphNodeSchemaAction> ActionToDelete;
					for (const TSharedPtr<FEdGraphSchemaAction>& Action : Actions)
					{
						TSharedPtr<FMetasoundGraphNodeSchemaAction> MetasoundAction = StaticCastSharedPtr<FMetasoundGraphNodeSchemaAction>(Action);
						if (MetasoundAction.IsValid())
						{
							Frontend::FNodeHandle NodeHandle = MetasoundAction->GetNodeHandle();
							if (ensure(NodeHandle->IsValid()))
							{
								if (NodeHandle->IsRequired())
								{
									if (MetasoundGraphEditor.IsValid())
									{
										FNotificationInfo Info(LOCTEXT("CannotDelete_RequiredVariable", "Delete failed: Input/Output is required."));
										Info.bFireAndForget = true;
										Info.bUseSuccessFailIcons = false;
										Info.ExpireDuration = 5.0f;

										MetasoundGraphEditor->AddNotification(Info, false /* bSuccess */);
									}
								}
								else
								{
									ActionToDelete = MetasoundAction;
									break;
								}
							}
						}
					}

					if (ActionToDelete.IsValid())
					{
						DeleteInterfaceItem(ActionToDelete, &Graph);
					}
					return;
				}
			}

			DeleteSelectedNodes();
		}

		void FEditor::DeleteSelectedNodes()
		{
			using namespace Metasound::Frontend;

			const FGraphPanelSelectionSet SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();
			MetasoundGraphEditor->ClearSelectionSet();

			const FScopedTransaction Transaction(LOCTEXT("MetaSoundEditorDeleteSelectedNode", "Delete Selected MetaSound Node(s)"));
			check(Metasound);
			Metasound->Modify();
			UEdGraph* Graph = MetasoundGraphEditor->GetCurrentGraph();
			check(Graph);
			Graph->Modify();
			for (UObject* NodeObj : SelectedNodes)
			{
				// Some nodes may not be metasound nodes (ex. comments and perhaps aliases eventually), but can be safely deleted.
				if (UEdGraphNode* Node = Cast<UEdGraphNode>(NodeObj))
				{
					if (!FGraphBuilder::DeleteNode(*Node))
					{
						MetasoundGraphEditor->SetNodeSelection(Node, true /* bSelect */);
					}
				}
			}
		}

		void FEditor::CutSelectedNodes()
		{
			CopySelectedNodes();

			// Cache off the old selection
			const FGraphPanelSelectionSet OldSelectedNodes = MetasoundGraphEditor->GetSelectedNodes();

			// Clear the selection and only select the nodes that can be duplicated
			FGraphPanelSelectionSet RemainingNodes;
			MetasoundGraphEditor->ClearSelectionSet();

			for (FGraphPanelSelectionSet::TConstIterator SelectedIter(OldSelectedNodes); SelectedIter; ++SelectedIter)
			{
				UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
				if (Node && Node->CanUserDeleteNode())
				{
					MetasoundGraphEditor->SetNodeSelection(Node, true);
				}
				else
				{
					RemainingNodes.Add(Node);
				}
			}

			// Delete the deletable nodes
			DeleteSelectedNodes();

			// Clear deleted, and reselect remaining nodes from original selection
			MetasoundGraphEditor->ClearSelectionSet();
			for (UObject* RemainingNode : RemainingNodes)
			{
				if (UEdGraphNode* Node = Cast<UEdGraphNode>(RemainingNode))
				{
					MetasoundGraphEditor->SetNodeSelection(Node, true);
				}
			}
		}

		void FEditor::CopySelectedNodes() const
		{
			FString NodeString;
			const FGraphPanelSelectionSet SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();
			FEdGraphUtilities::ExportNodesToText(SelectedNodes, NodeString);
			FPlatformApplicationMisc::ClipboardCopy(*NodeString);
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

		bool FEditor::CanDuplicateNodes() const
		{
			// If any of the nodes can be duplicated then allow copying
			const FGraphPanelSelectionSet& SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();
			for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
			{
				UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
				if (!Node || !Node->CanDuplicateNode())
				{
					return false;
				}
			}

			FString NodeString;
			FEdGraphUtilities::ExportNodesToText(SelectedNodes, NodeString);

			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
			check(MetasoundAsset);

			UEdGraph* Graph = MetasoundAsset->GetGraph();
			if (!Graph)
			{
				return false;
			}

			return FEdGraphUtilities::CanImportNodesFromText(Graph, NodeString);
		}

		bool FEditor::CanDeleteNodes() const
		{
			if (MetasoundGraphEditor->GetSelectedNodes().IsEmpty())
			{
				return false;
			}

			const FGraphPanelSelectionSet& SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();
			for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
			{
				UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
				if (Node && Node->CanUserDeleteNode())
				{
					return true;
				}
			}
			return false;
		}

		void FEditor::DuplicateNodes()
		{
			const FGraphPanelSelectionSet SelectedNodes = MetasoundGraphEditor->GetSelectedNodes();
			FEdGraphUtilities::ExportNodesToText(SelectedNodes, NodeTextToPaste);
			PasteNodes(nullptr, LOCTEXT("MetaSoundEditorDuplicate", "Duplicate MetaSound Node(s)"));
		}

		void FEditor::PasteNodes(const FVector2D* InLocation)
		{
			PasteNodes(InLocation, LOCTEXT("MetaSoundEditorPaste", "Paste MetaSound Node(s)"));
		}

		void FEditor::PasteNodes(const FVector2D* InLocation, const FText& InTransactionText)
		{
			using namespace Frontend;

			FVector2D Location;
			if (InLocation)
			{
				Location = *InLocation;
			}
			else
			{
				check(MetasoundGraphEditor);
				Location = MetasoundGraphEditor->GetPasteLocation();
			}

			UMetasoundEditorGraph& Graph = GetMetaSoundGraphChecked();

			const FScopedTransaction Transaction(InTransactionText);
			Metasound->Modify();
			Graph.Modify();

			// Clear the selection set (newly pasted stuff will be selected)
			MetasoundGraphEditor->ClearSelectionSet();

			TSet<UEdGraphNode*> PastedGraphNodes;
			FEdGraphUtilities::ImportNodesFromText(&Graph, NodeTextToPaste, PastedGraphNodes);

			NodeTextToPaste.Empty();

			TArray<UEdGraphNode*> NodesToRemove;
			for (UEdGraphNode* GraphNode : PastedGraphNodes)
			{
				GraphNode->CreateNewGuid();
				if (UMetasoundEditorGraphExternalNode* ExternalNode = Cast<UMetasoundEditorGraphExternalNode>(GraphNode))
				{
					FNodeHandle NewHandle = FGraphBuilder::AddNodeHandle(*Metasound, *ExternalNode);
					if (!NewHandle->IsValid())
					{
						NodesToRemove.Add(GraphNode);
					}
				}
				else if (UMetasoundEditorGraphInputNode* InputNode = Cast<UMetasoundEditorGraphInputNode>(GraphNode))
				{
					if (!Graph.ContainsInput(InputNode->Input))
					{
						NodesToRemove.Add(GraphNode);
					}
				}
				else if (UMetasoundEditorGraphOutputNode* OutputNode = Cast<UMetasoundEditorGraphOutputNode>(GraphNode))
				{
					if (!Graph.ContainsOutput(OutputNode->Output))
					{
						NodesToRemove.Add(GraphNode);
					}

					auto NodeMatches = [OutputNodeID = OutputNode->GetNodeID()](const TObjectPtr<UEdGraphNode>& EdNode)
					{
						if (UMetasoundEditorGraphOutputNode* OutputNode = Cast<UMetasoundEditorGraphOutputNode>(EdNode))
						{
							return OutputNodeID == OutputNode->GetNodeID();
						}
						return false;
					};

					// Can only have one output reference node
					if (Graph.Nodes.ContainsByPredicate(NodeMatches))
					{
						NodesToRemove.Add(GraphNode);
					}
				}
				else if (!GraphNode->IsA<UEdGraphNode_Comment>())
				{
					checkNoEntry();
				}
			}

			// Remove nodes failed to import before attempting to connect/place
			// in frontend graph.
			for (UEdGraphNode* Node : NodesToRemove)
			{
				Graph.RemoveNode(Node);
				PastedGraphNodes.Remove(Node);
			}

			// Find average midpoint of nodes and offset subgraph accordingly
			FVector2D AvgNodePosition = FVector2D::ZeroVector;
			for (UEdGraphNode* Node : PastedGraphNodes)
			{
				AvgNodePosition.X += Node->NodePosX;
				AvgNodePosition.Y += Node->NodePosY;
			}

			if (!PastedGraphNodes.IsEmpty())
			{
				float InvNumNodes = 1.0f / PastedGraphNodes.Num();
				AvgNodePosition.X *= InvNumNodes;
				AvgNodePosition.Y *= InvNumNodes;
			}

			for (UEdGraphNode* GraphNode : PastedGraphNodes)
			{
				GraphNode->NodePosX = (GraphNode->NodePosX - AvgNodePosition.X) + Location.X;
				GraphNode->NodePosY = (GraphNode->NodePosY - AvgNodePosition.Y) + Location.Y;

				GraphNode->SnapToGrid(SNodePanel::GetSnapGridSize());
				if (UMetasoundEditorGraphNode* MetasoundGraphNode = Cast<UMetasoundEditorGraphNode>(GraphNode))
				{
					FNodeHandle NodeHandle = MetasoundGraphNode->GetNodeHandle();
					if (ensure(NodeHandle->IsValid()))
					{
						const FVector2D NewNodeLocation = FVector2D(GraphNode->NodePosX, GraphNode->NodePosY);
						FMetasoundFrontendNodeStyle NodeStyle = NodeHandle->GetNodeStyle();
						NodeStyle.Display.Locations.FindOrAdd(NodeHandle->GetID()) = NewNodeLocation;
						NodeHandle->SetNodeStyle(NodeStyle);
					}
				}
			}

			for (UEdGraphNode* GraphNode : PastedGraphNodes)
			{
				UMetasoundEditorGraphNode* MetasoundNode = Cast<UMetasoundEditorGraphNode>(GraphNode);
				if (!MetasoundNode)
				{
					continue;
				}

				for (UEdGraphPin* Pin : GraphNode->Pins)
				{
					if (Pin->Direction != EGPD_Input)
					{
						continue;
					}

					if (Pin->LinkedTo.IsEmpty())
					{
						FGraphBuilder::AddOrUpdateLiteralInput(*Metasound, MetasoundNode->GetNodeHandle(), *Pin, true /* bForcePinValueAsDefault */);
					}
					else
					{
						for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
						{
							if (UMetasoundEditorGraphNode* Node = Cast<UMetasoundEditorGraphNode>(LinkedPin->GetOwningNode()))
							{
								FGraphBuilder::ConnectNodes(*Pin, *LinkedPin, false /* bConnectEdPins */);
							}
						}
					}
				}
			}

			// Select the newly pasted stuff
			for (UEdGraphNode* GraphNode : PastedGraphNodes)
			{
				MetasoundGraphEditor->SetNodeSelection(GraphNode, true);
			}

			if (FGraphBuilder::SynchronizeGraph(*Metasound))
			{
				NotifyUserModifiedBySync();
			}

			MetasoundGraphEditor->NotifyGraphChanged();
		}

		bool FEditor::CanPasteNodes()
		{
			UMetasoundEditorGraph& Graph = GetMetaSoundGraphChecked();
			FPlatformApplicationMisc::ClipboardPaste(NodeTextToPaste);
			if (FEdGraphUtilities::CanImportNodesFromText(&Graph, NodeTextToPaste))
			{
				return true;
			}

			NodeTextToPaste.Empty();
			return false;
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

		void FEditor::OnInputNameChanged(FGuid InNodeID)
		{
			if (!MetasoundInterfaceMenu.IsValid())
			{
				return;
			}

			TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
			MetasoundInterfaceMenu->GetSelectedActions(SelectedActions);
			MetasoundInterfaceMenu->RefreshAllActions(/* bPreserveExpansion */ true);

			for(const TSharedPtr<FEdGraphSchemaAction>& Action : SelectedActions)
			{
				TSharedPtr<FMetasoundGraphNodeSchemaAction> MetasoundAction = StaticCastSharedPtr<FMetasoundGraphNodeSchemaAction>(Action);
				if (MetasoundAction.IsValid())
				{
					Frontend::FNodeHandle NodeHandle = MetasoundAction->GetNodeHandle();
					if (InNodeID == NodeHandle->GetID())
					{
						TArray<Frontend::FConstOutputHandle> OutputHandles = NodeHandle->GetConstOutputs();
						if (ensure(OutputHandles.Num() == 1))
						{
							const FName ActionName = *OutputHandles[0]->GetDisplayName().ToString();
							MetasoundInterfaceMenu->SelectItemByName(ActionName, ESelectInfo::Direct, Action->GetSectionID());
						}
						break;
					}
				}
			}
		}

		void FEditor::OnOutputNameChanged(FGuid InNodeID)
		{
			if (!MetasoundInterfaceMenu.IsValid())
			{
				return;
			}

			TArray<TSharedPtr<FEdGraphSchemaAction>> SelectedActions;
			MetasoundInterfaceMenu->GetSelectedActions(SelectedActions);
			MetasoundInterfaceMenu->RefreshAllActions(/* bPreserveExpansion */ true);

			for (const TSharedPtr<FEdGraphSchemaAction>& Action : SelectedActions)
			{
				TSharedPtr<FMetasoundGraphNodeSchemaAction> MetasoundAction = StaticCastSharedPtr<FMetasoundGraphNodeSchemaAction>(Action);
				if (MetasoundAction.IsValid())
				{
					Frontend::FNodeHandle NodeHandle = MetasoundAction->GetNodeHandle();
					if (InNodeID == NodeHandle->GetID())
					{
						TArray<Frontend::FConstInputHandle> InputHandles = NodeHandle->GetConstInputs();
						if (ensure(InputHandles.Num() == 1))
						{
							const FName ActionName = *InputHandles[0]->GetDisplayName().ToString();
							MetasoundInterfaceMenu->SelectItemByName(ActionName, ESelectInfo::Direct, Action->GetSectionID());
						}
						break;
					}
				}
			}
		}

		void FEditor::CollectAllActions(FGraphActionListBuilderBase& OutAllActions)
		{
			FMetasoundAssetBase* MetasoundAsset = IMetasoundUObjectRegistry::Get().GetObjectAsAssetBase(Metasound);
			check(MetasoundAsset);

			MetasoundAsset->GetRootGraphHandle()->IterateConstNodes([this, ActionList = &OutAllActions](const Frontend::FConstNodeHandle& Input)
			{
				const bool bIsLiteralInput = Input->GetNodeStyle().Display.Visibility == EMetasoundFrontendNodeStyleDisplayVisibility::Hidden;
				if (!ShowLiteralMetasoundInputsInEditorCVar && bIsLiteralInput)
				{
					return;
				}

				const FText Tooltip = Input->GetDescription();
				const FText InputName = Input->GetDisplayName();
				UMetasoundEditorGraph& Graph = GetMetaSoundGraphChecked();

				TSharedPtr<FMetasoundGraphNodeSchemaAction> NewFuncAction = MakeShared<FMetasoundGraphNodeSchemaAction>(FText::GetEmpty(), InputName, Tooltip, 1, ENodeSection::Inputs);
				NewFuncAction->Graph = &Graph;
				ActionList->AddAction(NewFuncAction);

				const FGuid NodeID = Input->GetID();
				NewFuncAction->NodeID = NodeID;
				UMetasoundEditorGraphInput * EdGraphInput = Graph.FindInput(NodeID);
				if (ensure(EdGraphInput))
				{
					if (FDelegateHandle* NameChangeDelegate = NameChangeDelegateHandles.Find(NodeID))
					{
						EdGraphInput->NameChanged.Remove(*NameChangeDelegate);
					}
					NameChangeDelegateHandles.FindOrAdd(NodeID) = EdGraphInput->NameChanged.AddSP(this, &FEditor::OnInputNameChanged);
				}
			}, EMetasoundFrontendClassType::Input);

			MetasoundAsset->GetRootGraphHandle()->IterateConstNodes([this, ActionList = &OutAllActions](const Frontend::FConstNodeHandle& Output)
			{
				const FText Tooltip = Output->GetDescription();
				const FText OutputName = Output->GetDisplayName();
				UMetasoundEditorGraph& Graph = GetMetaSoundGraphChecked();

				TSharedPtr<FMetasoundGraphNodeSchemaAction> NewFuncAction = MakeShared<FMetasoundGraphNodeSchemaAction>(FText::GetEmpty(), OutputName, Tooltip, 1, ENodeSection::Outputs);
				NewFuncAction->Graph = &Graph;
				NewFuncAction->NodeID = Output->GetID();
				ActionList->AddAction(NewFuncAction);

				const FGuid NodeID = Output->GetID();
				NewFuncAction->NodeID = NodeID;
				UMetasoundEditorGraphOutput* EdGraphOutput = Graph.FindOutput(NodeID);
				if (ensure(EdGraphOutput))
				{
					if (FDelegateHandle* NameChangeDelegate = NameChangeDelegateHandles.Find(NodeID))
					{
						EdGraphOutput->NameChanged.Remove(*NameChangeDelegate);
					}
					NameChangeDelegateHandles.FindOrAdd(NodeID) = EdGraphOutput->NameChanged.AddSP(this, &FEditor::OnOutputNameChanged);
				}
			}, EMetasoundFrontendClassType::Output);
		}

		void FEditor::CollectStaticSections(TArray<int32>& StaticSectionIDs)
		{
			for (int32 i = 0; i < static_cast<int32>(ENodeSection::COUNT); ++i)
			{
				if (static_cast<ENodeSection>(i) != ENodeSection::None)
				{
					StaticSectionIDs.Add(i);
				}
			}
		}

		bool FEditor::HandleActionMatchesName(FEdGraphSchemaAction* InAction, const FName& InName) const
		{
			return false;
		}

		// TODO: Implement
		FReply FEditor::OnActionDragged(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, const FPointerEvent& MouseEvent)
		{
			return FReply::Unhandled();
		}

		void FEditor::OnActionSelected(const TArray<TSharedPtr<FEdGraphSchemaAction>>& InActions, ESelectInfo::Type InSelectionType)
		{
			if (InSelectionType == ESelectInfo::OnMouseClick || InSelectionType == ESelectInfo::OnKeyPress || InSelectionType == ESelectInfo::OnNavigation || InActions.IsEmpty())
			{
				TArray<UObject*> SelectedObjects;
				for (const TSharedPtr<FEdGraphSchemaAction>& Action : InActions)
				{
					TSharedPtr<FMetasoundGraphNodeSchemaAction> MetasoundNodeAction = StaticCastSharedPtr<FMetasoundGraphNodeSchemaAction>(Action);
					if (MetasoundNodeAction.IsValid())
					{
						SelectedObjects.Add(MetasoundNodeAction->GetVariable());
					}
				}

				if (InSelectionType != ESelectInfo::Direct && !InActions.IsEmpty())
				{
					if (MetasoundGraphEditor.IsValid())
					{
						bManuallyClearingGraphSelection = true;
						MetasoundGraphEditor->ClearSelectionSet();
						bManuallyClearingGraphSelection = false;
					}
					SetSelection(SelectedObjects);
				}
			}
		}

		// TODO: Add ability to filter inputs/outputs in "MetaSound" Tab
		FText FEditor::GetFilterText() const
		{
			return FText::GetEmpty();
		}

		TSharedRef<SWidget> FEditor::OnCreateWidgetForAction(FCreateWidgetForActionData* const InCreateData)
		{
			return SNew(SMetaSoundGraphPaletteItem, InCreateData);
		}

		FText FEditor::GetSectionTitle(ENodeSection InSection) const
		{
			const int32 SectionIndex = static_cast<int32>(InSection);
			if (ensure(NodeSectionNames.IsValidIndex(SectionIndex)))
			{
				return NodeSectionNames[SectionIndex];
			}

			return FText::GetEmpty();
		}

		FText FEditor::OnGetSectionTitle(int32 InSectionID)
		{
			if (ensure(NodeSectionNames.IsValidIndex(InSectionID)))
			{
				return NodeSectionNames[InSectionID];
			}

			return FText::GetEmpty();
		}

		TSharedRef<SWidget> FEditor::OnGetMenuSectionWidget(TSharedRef<SWidget> RowWidget, int32 InSectionID)
		{
			TWeakPtr<SWidget> WeakRowWidget = RowWidget;

			FText AddNewText;
			FName MetaDataTag;

			switch (static_cast<ENodeSection>(InSectionID))
			{
				case ENodeSection::Inputs:
				{
					AddNewText = LOCTEXT("AddNewInput", "Input");
					MetaDataTag = "AddNewInput";

					// TODO: Add back for outputs once reading outputs/Metasound
					// composition is supported.
					return CreateAddInputButton(InSectionID, AddNewText, MetaDataTag);
				}
				break;

				case ENodeSection::Outputs:
				{
					AddNewText = LOCTEXT("AddNewOutput", "Output");
					MetaDataTag = "AddNewOutput";
				}
				break;

				default:
				break;
			}

			return SNullWidget::NullWidget;
		}

		bool FEditor::CanAddNewElementToSection(int32 InSectionID) const
		{
			return true;
		}

		FReply FEditor::OnAddButtonClickedOnSection(int32 InSectionID)
		{
			if (!Metasound)
			{
				return FReply::Unhandled();
			}

			UMetasoundEditorGraph& Graph = GetMetaSoundGraphChecked();

			FName NameToSelect;
			switch (static_cast<ENodeSection>(InSectionID))
			{
				case ENodeSection::Inputs:
				{
					const FScopedTransaction Transaction(LOCTEXT("AddInputNode", "Add MetaSound Input"));
					Metasound->Modify();

					const FName DataTypeName = "Bool";
					FString NewNodeName = FGraphBuilder::GenerateUniqueInputName(*Metasound);
					NameToSelect = FName(*NewNodeName);

					Frontend::FNodeHandle NodeHandle = FGraphBuilder::AddInputNodeHandle(*Metasound, NewNodeName, DataTypeName, FText::GetEmpty());
					if (ensure(NodeHandle->IsValid()))
					{
						TObjectPtr<UMetasoundEditorGraphInput> Input = Graph.FindOrAddInput(NodeHandle);
						if (ensure(Input))
						{
							FGuid NodeID = NodeHandle->GetID();
							if (FDelegateHandle* NameChangeDelegate = NameChangeDelegateHandles.Find(NodeID))
							{
								Input->NameChanged.Remove(*NameChangeDelegate);
							}
							NameChangeDelegateHandles.FindOrAdd(NodeID) = Input->NameChanged.AddSP(this, &FEditor::OnInputNameChanged);
						}
					}
				}
				break;

				case ENodeSection::Outputs:
				{
					const FScopedTransaction Transaction(TEXT(""), LOCTEXT("AddOutputNode", "Add MetaSound Output"), Metasound);
					Metasound->Modify();

					const FName DataTypeName = "Bool";
					FString NewNodeName = FGraphBuilder::GenerateUniqueOutputName(*Metasound);
					NameToSelect = FName(*NewNodeName);

					Frontend::FNodeHandle NodeHandle = FGraphBuilder::AddOutputNodeHandle(*Metasound, NewNodeName, DataTypeName, FText::GetEmpty());
					if (ensure(NodeHandle->IsValid()))
					{
						TObjectPtr<UMetasoundEditorGraphOutput> Output = Graph.FindOrAddOutput(NodeHandle);
						if (ensure(Output))
						{
							FGuid NodeID = NodeHandle->GetID();
							if (FDelegateHandle* NameChangeDelegate = NameChangeDelegateHandles.Find(NodeID))
							{
								Output->NameChanged.Remove(*NameChangeDelegate);
							}
							NameChangeDelegateHandles.FindOrAdd(NodeID) = Output->NameChanged.AddSP(this, &FEditor::OnInputNameChanged);
						}
					}
				}
				break;

				default:
				return FReply::Unhandled();
			}

			if (MetasoundInterfaceMenu.IsValid())
			{
				MetasoundInterfaceMenu->RefreshAllActions(/* bPreserveExpansion */ true);
				if (NameToSelect.IsNone())
				{
					MetasoundInterfaceMenu->SelectItemByName(NameToSelect);
				}
			}
			return FReply::Handled();
		}

		TSharedRef<SWidget> FEditor::CreateAddInputButton(int32 InSectionID, FText AddNewText, FName MetaDataTag)
		{
			return
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "SimpleButton")
				.OnClicked(this, &FEditor::OnAddButtonClickedOnSection, InSectionID)
				.IsEnabled(this, &FEditor::CanAddNewElementToSection, InSectionID)
				.ContentPadding(FMargin(1, 0))
				.AddMetaData<FTagMetaData>(FTagMetaData(MetaDataTag))
				.ToolTipText(AddNewText)
				[
					SNew(SImage)
					.Image(FAppStyle::Get().GetBrush("Icons.PlusCircle"))
				.ColorAndOpacity(FSlateColor::UseForeground())
				];
		}
	}
}
#undef LOCTEXT_NAMESPACE
