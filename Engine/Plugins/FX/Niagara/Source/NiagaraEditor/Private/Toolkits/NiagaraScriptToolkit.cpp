// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraScriptToolkit.h"
#include "NiagaraEditorModule.h"
#include "NiagaraEditorUtilities.h"
#include "NiagaraScript.h"
#include "NiagaraScriptViewModel.h"
#include "NiagaraScriptGraphViewModel.h"
#include "NiagaraObjectSelection.h"
#include "NiagaraEditorCommands.h"
#include "NiagaraScriptInputCollectionViewModel.h"
#include "NiagaraScriptOutputCollectionViewModel.h"
#include "SNiagaraScriptGraph.h"
#include "SNiagaraSelectedObjectsDetails.h"
#include "SNiagaraParameterCollection.h"
#include "UObject/Package.h"
#include "SNiagaraParameterMapView.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraSystem.h"
#include "BusyCursor.h"
#include "EngineGlobals.h"
#include "Misc/FeedbackContext.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "UObject/UObjectIterator.h"
#include "NiagaraScriptSource.h"
#include "NiagaraNode.h"
#include "NiagaraGraph.h"
#include "NiagaraEmitter.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraModule.h"

#include "Modules/ModuleManager.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Docking/SDockTab.h"
#include "Misc/MessageDialog.h"
#include "Customizations/NiagaraScriptDetails.h"
#include "PropertyEditorModule.h"

#include "Developer/MessageLog/Public/IMessageLogListing.h"
#include "Developer/MessageLog/Public/MessageLogInitializationOptions.h"
#include "Developer/MessageLog/Public/MessageLogModule.h"

#include "AssetTypeActions/AssetTypeActions_NiagaraScript.h"
#include "NiagaraMessageManager.h"

#define LOCTEXT_NAMESPACE "NiagaraScriptToolkit"

DECLARE_CYCLE_STAT(TEXT("Niagara - ScriptToolkit - OnApply"), STAT_NiagaraEditor_ScriptToolkit_OnApply, STATGROUP_NiagaraEditor);

static TAutoConsoleVariable<int32> CVarDevDetails(
	TEXT("fx.DevDetailsPanels"),
	1,
	TEXT("Whether to enable the development details panels inside Niagara."));


const FName FNiagaraScriptToolkit::NodeGraphTabId(TEXT("NiagaraEditor_NodeGraph")); 
const FName FNiagaraScriptToolkit::DetailsTabId(TEXT("NiagaraEditor_Details"));
const FName FNiagaraScriptToolkit::ParametersTabId(TEXT("NiagaraEditor_Parameters"));
const FName FNiagaraScriptToolkit::StatsTabId(TEXT("NiagaraEditor_Stats"));
const FName FNiagaraScriptToolkit::MessageLogTabID(TEXT("NiagaraEditor_MessageLog"));

FNiagaraScriptToolkit::FNiagaraScriptToolkit()
{
}

FNiagaraScriptToolkit::~FNiagaraScriptToolkit()
{
	EditedNiagaraScript->OnVMScriptCompiled().RemoveAll(this);
	ScriptViewModel->GetGraphViewModel()->GetGraph()->RemoveOnGraphNeedsRecompileHandler(OnEditedScriptGraphChangedHandle);

	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	NiagaraEditorModule.GetOnScriptToolkitsShouldFocusGraphElement().RemoveAll(this);
}

void FNiagaraScriptToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_NiagaraEditor", "Niagara"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);
	
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	InTabManager->RegisterTabSpawner(NodeGraphTabId, FOnSpawnTab::CreateSP(this, &FNiagaraScriptToolkit::SpawnTabNodeGraph))
		.SetDisplayName( LOCTEXT("NodeGraph", "Node Graph") )
		.SetGroup(WorkspaceMenuCategoryRef); 

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FNiagaraScriptToolkit::SpawnTabNodeDetails))
		.SetDisplayName(LOCTEXT("DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(ParametersTabId, FOnSpawnTab::CreateSP(this, &FNiagaraScriptToolkit::SpawnTabScriptParameters))
		.SetDisplayName(LOCTEXT("ParametersTab", "Parameters"))
		.SetGroup(WorkspaceMenuCategoryRef);
		
	InTabManager->RegisterTabSpawner(StatsTabId, FOnSpawnTab::CreateSP(this, &FNiagaraScriptToolkit::SpawnTabStats))
		.SetDisplayName(LOCTEXT("StatsTab", "Stats"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(MessageLogTabID, FOnSpawnTab::CreateSP(this, &FNiagaraScriptToolkit::SpawnTabMessageLog))
		.SetDisplayName(LOCTEXT("NiagaraMessageLogTab", "Niagara Message Log"))
		.SetGroup(WorkspaceMenuCategoryRef);
}

void FNiagaraScriptToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(NodeGraphTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
	InTabManager->UnregisterTabSpawner(ParametersTabId);
	InTabManager->UnregisterTabSpawner(StatsTabId);
}

void FNiagaraScriptToolkit::Initialize( const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UNiagaraScript* InputScript )
{	
	check(InputScript != NULL);
	OriginalNiagaraScript = InputScript;
	ResetLoaders(GetTransientPackage()); // Make sure that we're not going to get invalid version number linkers into the package we are going into. 
	GetTransientPackage()->LinkerCustomVersion.Empty();
	EditedNiagaraScript = (UNiagaraScript*)StaticDuplicateObject(OriginalNiagaraScript, GetTransientPackage(), NAME_None, ~RF_Standalone, UNiagaraScript::StaticClass());
	EditedNiagaraScript->OnVMScriptCompiled().AddSP(this, &FNiagaraScriptToolkit::OnVMScriptCompiled);
	bEditedScriptHasPendingChanges = false;

	// Determine display name for panel heading based on asset usage type
	FText DisplayName = LOCTEXT("NiagaraScriptDisplayName", "Niagara Script");
	if (EditedNiagaraScript->GetUsage() == ENiagaraScriptUsage::Function)
	{
		DisplayName = FAssetTypeActions_NiagaraScriptFunctions::GetFormattedName();
	}
	else if (EditedNiagaraScript->GetUsage() == ENiagaraScriptUsage::Module)
	{
		DisplayName = FAssetTypeActions_NiagaraScriptModules::GetFormattedName();
	}
	else if (EditedNiagaraScript->GetUsage() == ENiagaraScriptUsage::DynamicInput)
	{
		DisplayName = FAssetTypeActions_NiagaraScriptDynamicInputs::GetFormattedName();
	}
	ScriptViewModel = MakeShareable(new FNiagaraScriptViewModel(EditedNiagaraScript, DisplayName, ENiagaraParameterEditMode::EditAll));

	OnEditedScriptGraphChangedHandle = ScriptViewModel->GetGraphViewModel()->GetGraph()->AddOnGraphNeedsRecompileHandler(
		FOnGraphChanged::FDelegate::CreateRaw(this, &FNiagaraScriptToolkit::OnEditedScriptGraphChanged));

	DetailsSelection = MakeShareable(new FNiagaraObjectSelection());
	DetailsSelection->SetSelectedObject(EditedNiagaraScript);
	
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions LogOptions;
	// Show Pages so that user is never allowed to clear log messages
	LogOptions.bShowPages = false;
	LogOptions.bShowFilters = false;
	LogOptions.bAllowClear = false;
	LogOptions.MaxPageCount = 1;

	// Reuse any existing log, or create a new one (that is not held onto by the message log system)
	auto CreateMessageLogListing = [&MessageLogModule, &LogOptions](const FName& LogName)->TSharedRef<IMessageLogListing> {
		if (MessageLogModule.IsRegisteredLogListing(LogName))
		{
			return MessageLogModule.GetLogListing(LogName);
		}
		else
		{
			return  MessageLogModule.CreateLogListing(LogName, LogOptions);
		}
	};

	StatsListing = MessageLogModule.CreateLogListing("MaterialEditorStats", LogOptions);
	Stats = MessageLogModule.CreateLogListingWidget(StatsListing.ToSharedRef());
	NiagaraMessageLogListing = CreateMessageLogListing(GetNiagaraScriptMessageLogName(EditedNiagaraScript));
	NiagaraMessageLog = MessageLogModule.CreateLogListingWidget(NiagaraMessageLogListing.ToSharedRef());

	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_Niagara_Layout_v8")
	->AddArea
	(
		FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
		->Split
		(
			FTabManager::NewStack()
			->SetSizeCoefficient(0.1f)
			->AddTab(GetToolbarTabId(), ETabState::OpenedTab)
			->SetHideTabWell(true)
		)
		->Split
		(
			FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)->SetSizeCoefficient(0.9f)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
				->SetSizeCoefficient(0.2f)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.5f)
					->AddTab(DetailsTabId, ETabState::OpenedTab)
					->SetForegroundTab(DetailsTabId)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.4f)
					->AddTab(ParametersTabId, ETabState::OpenedTab)
					->SetForegroundTab(ParametersTabId)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.1f)
					->AddTab(StatsTabId, ETabState::OpenedTab)
				)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)->SetSizeCoefficient(0.8f)
				->Split
				(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.8f)
				->AddTab(NodeGraphTabId, ETabState::OpenedTab)
				)
				->Split
				(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.2f)
				->AddTab(MessageLogTabID, ETabState::OpenedTab)
				)
			)
		)
	);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	FAssetEditorToolkit::InitAssetEditor( Mode, InitToolkitHost, FNiagaraEditorModule::NiagaraEditorAppIdentifier, StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, InputScript );

	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>( "NiagaraEditor" );
	AddMenuExtender(NiagaraEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
	NiagaraEditorModule.GetOnScriptToolkitsShouldFocusGraphElement().AddSP(this, &FNiagaraScriptToolkit::FocusGraphElement);

	SetupCommands();
	ExtendToolbar();
	RegenerateMenusAndToolbars();
	UpdateMessageLog();
	UpdateModuleStats();

	// @todo toolkit world centric editing
	/*// Setup our tool's layout
	if( IsWorldCentricAssetEditor() )
	{
		const FString TabInitializationPayload(TEXT(""));		// NOTE: Payload not currently used for table properties
		SpawnToolkitTab( NodeGraphTabId, TabInitializationPayload, EToolkitTabSpot::Details );
	}*/

	bChangesDiscarded = false;
}

FName FNiagaraScriptToolkit::GetToolkitFName() const
{
	return FName("Niagara");
}

FText FNiagaraScriptToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Niagara");
}

FString FNiagaraScriptToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Niagara ").ToString();
}


FLinearColor FNiagaraScriptToolkit::GetWorldCentricTabColorScale() const
{
	return FNiagaraEditorModule::WorldCentricTabColorScale;
}

TSharedRef<SDockTab> FNiagaraScriptToolkit::SpawnTabNodeGraph( const FSpawnTabArgs& Args )
{
	checkf(Args.GetTabId().TabType == NodeGraphTabId, TEXT("Wrong tab ID in NiagaraScriptToolkit"));
	checkf(ScriptViewModel.IsValid(), TEXT("NiagaraScriptToolkit - Script editor view model is invalid"));

	return
		SNew(SDockTab)
		[
			SAssignNew(NiagaraScriptGraphWidget, SNiagaraScriptGraph, ScriptViewModel->GetGraphViewModel())
			.GraphTitle(LOCTEXT("SpawnGraphTitle", "Script"))
		];
}

void FNiagaraScriptToolkit::OnEditedScriptPropertyFinishedChanging(const FPropertyChangedEvent& InEvent)
{
	// We need to synchronize the Usage field in the property editor with the actual node
	// in the graph.
	if (InEvent.Property != nullptr && InEvent.Property->GetName().Equals(TEXT("Usage")))
	{
		if (EditedNiagaraScript != nullptr && EditedNiagaraScript->GetSource() != nullptr)
		{
			UNiagaraScriptSource* ScriptSource = Cast<UNiagaraScriptSource>(EditedNiagaraScript->GetSource());
			if (ScriptSource != nullptr)
			{
				TArray<UNiagaraNodeOutput*> OutputNodes;
				ScriptSource->NodeGraph->FindOutputNodes(OutputNodes);

				bool bChanged = false;
				for (UNiagaraNodeOutput* Output : OutputNodes)
				{
					if (Output->GetUsage() != EditedNiagaraScript->GetUsage())
					{
						Output->Modify();
						Output->SetUsage(EditedNiagaraScript->GetUsage());
						bChanged = true;
					}
				}

				if (bChanged)
				{
					ScriptSource->NodeGraph->NotifyGraphChanged();
				}
			}
		}
	}
	bEditedScriptHasPendingChanges = true;
}

void FNiagaraScriptToolkit::OnVMScriptCompiled(UNiagaraScript* InScript)
{
	UpdateMessageLog();
	UpdateModuleStats();
}

TSharedRef<SDockTab> FNiagaraScriptToolkit::SpawnTabNodeDetails(const FSpawnTabArgs& Args)
{
	checkf(Args.GetTabId().TabType == DetailsTabId, TEXT("Wrong tab ID in NiagaraScriptToolkit"));
	checkf(ScriptViewModel.IsValid(), TEXT("NiagaraScriptToolkit - Script editor view model is invalid"));

	IConsoleVariable* DevDetailsCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.DevDetailsPanels"));

	TWeakPtr<FNiagaraScriptViewModel> ScriptViewModelWeakPtr = ScriptViewModel;

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs(false, false, true, FDetailsViewArgs::HideNameArea, true);
	TSharedRef<IDetailsView> DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);

	DetailsView->OnFinishedChangingProperties().AddRaw(this, &FNiagaraScriptToolkit::OnEditedScriptPropertyFinishedChanging);
	DetailsView->RegisterInstancedCustomPropertyLayout(
		UNiagaraScript::StaticClass(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraScriptDetails::MakeInstance, ScriptViewModelWeakPtr)
	);

	DetailsView->SetObjects(DetailsSelection->GetSelectedObjects().Array());

	if (DevDetailsCVar && DevDetailsCVar->GetInt() != 0)
	{
		return SNew(SDockTab)
			.Label(LOCTEXT("ScriptNodeDetailsTabLabel", "Details"))
			.TabColorScale(GetTabColorScale())
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				.Padding(0, 3, 0, 0)
				[
					DetailsView
				]
				+ SScrollBox::Slot()
				.Padding(0, 3, 0, 0)
				[
					SNew(SNiagaraSelectedObjectsDetails, ScriptViewModel->GetGraphViewModel()->GetSelection())
				]
			];
	}
	else
	{
		return SNew(SDockTab)
			.Label(LOCTEXT("ScriptNodeDetailsTabLabel", "Details"))
			.TabColorScale(GetTabColorScale())
			[
				DetailsView
			];
	}
}

TSharedRef<SDockTab> FNiagaraScriptToolkit::SpawnTabScriptParameters(const FSpawnTabArgs& Args)
{
	checkf(Args.GetTabId().TabType == ParametersTabId, TEXT("Wrong tab ID in NiagaraScriptToolkit"));

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SNew(SNiagaraParameterMapView, DetailsSelection.ToSharedRef(), SNiagaraParameterMapView::EToolkitType::SCRIPT, GetToolkitCommands())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraScriptToolkit::SpawnTabStats(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == StatsTabId);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("ModuleStatsTitle", "Stats"))
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ModuleStats")))
			[
				Stats.ToSharedRef()
			]
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraScriptToolkit::SpawnTabMessageLog(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == MessageLogTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("NiagaraMessageLogTitle", "Niagara Log"))
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("NiagaraLog")))
			[
				NiagaraMessageLog.ToSharedRef()
			]
		];

	return SpawnedTab;
}

void FNiagaraScriptToolkit::SetupCommands()
{
	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().Apply,
		FExecuteAction::CreateSP(this, &FNiagaraScriptToolkit::OnApply),
		FCanExecuteAction::CreateSP(this, &FNiagaraScriptToolkit::OnApplyEnabled));
	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().Compile,
		FExecuteAction::CreateRaw(this, &FNiagaraScriptToolkit::CompileScript, true));
	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().RefreshNodes,
		FExecuteAction::CreateRaw(this, &FNiagaraScriptToolkit::RefreshNodes));
}

const FName FNiagaraScriptToolkit::GetNiagaraScriptMessageLogName(UNiagaraScript* InScript) const
{
	checkf(InScript, TEXT("Tried to get MessageLog name for NiagaraScript but InScript was null!"));
	FName LogListingName = *FString::Printf(TEXT("%s_%s_MessageLog"), *InScript->GetBaseChangeID().ToString(), *InScript->GetName());
	return LogListingName;
}

void FNiagaraScriptToolkit::ExtendToolbar()
{
	struct Local
	{
		static void FillToolbar(FToolBarBuilder& ToolbarBuilder, FNiagaraScriptToolkit* ScriptToolkit)
		{
			ToolbarBuilder.BeginSection("Apply");
			ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().Apply,
				NAME_None, TAttribute<FText>(), TAttribute<FText>(), 
				FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.Apply"),
				FName(TEXT("ApplyNiagaraScript")));
			ToolbarBuilder.EndSection();

			ToolbarBuilder.BeginSection("Compile");
			ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().Compile,
				NAME_None,
				TAttribute<FText>(),
				TAttribute<FText>(ScriptToolkit, &FNiagaraScriptToolkit::GetCompileStatusTooltip),
				TAttribute<FSlateIcon>(ScriptToolkit, &FNiagaraScriptToolkit::GetCompileStatusImage),
				FName(TEXT("CompileNiagaraScript")));
			// removed this for UE-58554 ahead of 4.20. Functionality code should also be removed if this becomes permanent.
			/*ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().RefreshNodes,
				NAME_None,
				TAttribute<FText>(),
				TAttribute<FText>(ScriptToolkit, &FNiagaraScriptToolkit::GetRefreshStatusTooltip),
				TAttribute<FSlateIcon>(ScriptToolkit, &FNiagaraScriptToolkit::GetRefreshStatusImage),
				FName(TEXT("RefreshScriptReferences")));*/
			ToolbarBuilder.EndSection();
		}
	};

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateStatic(&Local::FillToolbar, this));

	AddToolbarExtender(ToolbarExtender);

	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>( "NiagaraEditor" );
	AddToolbarExtender(NiagaraEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

FSlateIcon FNiagaraScriptToolkit::GetCompileStatusImage() const
{
	ENiagaraScriptCompileStatus Status = ScriptViewModel->GetLatestCompileStatus();

	switch (Status)
	{
	default:
	case ENiagaraScriptCompileStatus::NCS_Unknown:
	case ENiagaraScriptCompileStatus::NCS_Dirty:
		return FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "Niagara.CompileStatus.Unknown");
	case ENiagaraScriptCompileStatus::NCS_Error:
		return FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "Niagara.CompileStatus.Error");
	case ENiagaraScriptCompileStatus::NCS_UpToDate:
		return FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "Niagara.CompileStatus.Good");
	case ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings:
		return FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "Niagara.CompileStatus.Warning");
	}
}

FText FNiagaraScriptToolkit::GetCompileStatusTooltip() const
{
	ENiagaraScriptCompileStatus Status = ScriptViewModel->GetLatestCompileStatus();
	return FNiagaraEditorUtilities::StatusToText(Status);
}

FSlateIcon FNiagaraScriptToolkit::GetRefreshStatusImage() const
{
	return FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "Niagara.Asset.ReimportAsset.Default");
}

FText FNiagaraScriptToolkit::GetRefreshStatusTooltip() const
{
	return LOCTEXT("Refresh_Status", "Currently dependencies up-to-date. Consider refreshing if status isn't accurate.");
}

void FNiagaraScriptToolkit::CompileScript(bool bForce)
{
	ScriptViewModel->CompileStandaloneScript();
	ScriptViewModel->RefreshMetadataCollection();
}

void FNiagaraScriptToolkit::RefreshNodes()
{
	ScriptViewModel->RefreshNodes();
}

bool FNiagaraScriptToolkit::IsEditScriptDifferentFromOriginalScript() const
{
	return bEditedScriptHasPendingChanges;
}

void FNiagaraScriptToolkit::OnApply()
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_ScriptToolkit_OnApply);
	UE_LOG(LogNiagaraEditor, Log, TEXT("Applying Niagara Script %s"), *GetEditingObjects()[0]->GetName());
	UpdateOriginalNiagaraScript();
}

bool FNiagaraScriptToolkit::OnApplyEnabled() const
{
	return IsEditScriptDifferentFromOriginalScript();
}

void FNiagaraScriptToolkit::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(OriginalNiagaraScript);
	Collector.AddReferencedObject(EditedNiagaraScript);
}



void FNiagaraScriptToolkit::UpdateMessageLog()
{
	TArray<TSharedPtr<INiagaraMessage>> NiagaraMessages;
	uint32 ErrorCount = 0;
	uint32 WarningCount = 0;
	for (const FNiagaraCompileEvent CompileEvent : EditedNiagaraScript->GetVMExecutableData().LastCompileEvents)
	{
		NiagaraMessages.Add(FNiagaraMessageManager::Get()->QueueMessageJob(MakeShared<FNiagaraMessageJobCompileEvent>(CompileEvent, TWeakObjectPtr<UNiagaraScript>(OriginalNiagaraScript), true)));
		if (CompileEvent.Severity == FNiagaraCompileEventSeverity::Error)
		{
			ErrorCount++;
		}
		else if (CompileEvent.Severity == FNiagaraCompileEventSeverity::Warning)
		{
			WarningCount++;
		}
	}

	TArray<TSharedRef<FTokenizedMessage>> TokenizedMessages;
	for (const TSharedPtr<INiagaraMessage> Message : NiagaraMessages)
	{
		TokenizedMessages.Add(Message->GenerateTokenizedMessage());
	}

	const auto GetCompileCompleteMessageText = [&ErrorCount, &WarningCount](ENiagaraScriptCompileStatus Status)->const FText {
		FText MessageText;
		switch (Status)
		{
		default:
		case ENiagaraScriptCompileStatus::NCS_Unknown:
		case ENiagaraScriptCompileStatus::NCS_Dirty:
			MessageText = LOCTEXT("NiagaraScriptCompileStatusUnknownInfo", "Script compile status unknown with {0} warning(s) and {1} error(s).");
			return FText::Format(MessageText, FText::FromString(FString::FromInt(WarningCount)), FText::FromString(FString::FromInt(ErrorCount)));
		case ENiagaraScriptCompileStatus::NCS_Error:
			MessageText = LOCTEXT("NiagaraScriptCompileStatusErrorInfo", "Script failed to compile with {0} warning(s) and {1} error(s).");
			return FText::Format(MessageText, FText::FromString(FString::FromInt(WarningCount)), FText::FromString(FString::FromInt(ErrorCount)));
		case ENiagaraScriptCompileStatus::NCS_UpToDate:
			MessageText = LOCTEXT("NiagaraScriptCompileStatusSuccessInfo", "Script successfully compiled.");
			return MessageText;
		case ENiagaraScriptCompileStatus::NCS_UpToDateWithWarnings:
			MessageText = LOCTEXT("NiagaraScriptCompileStatusWarningInfo", "Script successfully compiled with {0} warning(s).");
			return FText::Format(MessageText, FText::FromString(FString::FromInt(WarningCount)));
		}
	};

	const FText CompileCompleteMessageText = GetCompileCompleteMessageText(ScriptViewModel->GetLatestCompileStatus());
	TokenizedMessages.Add(FTokenizedMessage::Create(EMessageSeverity::Info, CompileCompleteMessageText));

	NiagaraMessageLogListing->ClearMessages();
 	NiagaraMessageLogListing->AddMessages(TokenizedMessages);
}

void FNiagaraScriptToolkit::UpdateModuleStats()
{
	TArray< TSharedRef<class FTokenizedMessage> > Messages;

	TSharedRef<FTokenizedMessage> Line = FTokenizedMessage::Create(EMessageSeverity::Info);
	uint32 LastOpCount = EditedNiagaraScript->GetVMExecutableData().LastOpCount;
	Line->AddToken(FTextToken::Create(FText::Format(FText::FromString("LastOpCount: {0}"), LastOpCount)));
	Messages.Add(Line);
	
	StatsListing->ClearMessages();
	StatsListing->AddMessages(Messages);
}

void FNiagaraScriptToolkit::GetSaveableObjects(TArray<UObject*>& OutObjects) const
{
	OutObjects.Add(OriginalNiagaraScript);
}


void FNiagaraScriptToolkit::SaveAsset_Execute()
{
	UE_LOG(LogNiagaraEditor, Log, TEXT("Saving and Compiling NiagaraScript %s"), *GetEditingObjects()[0]->GetName());

	UpdateOriginalNiagaraScript();

	FAssetEditorToolkit::SaveAsset_Execute();
}

void FNiagaraScriptToolkit::SaveAssetAs_Execute()
{
	UE_LOG(LogNiagaraEditor, Log, TEXT("Saving and Compiling NiagaraScript %s"), *GetEditingObjects()[0]->GetName());

	UpdateOriginalNiagaraScript();

	FAssetEditorToolkit::SaveAssetAs_Execute();
}

void FNiagaraScriptToolkit::UpdateOriginalNiagaraScript()
{
	const FScopedBusyCursor BusyCursor;

	const FText LocalizedScriptEditorApply = NSLOCTEXT("UnrealEd", "ToolTip_NiagaraScriptEditorApply", "Apply changes to original script and its use in the world.");
	GWarn->BeginSlowTask(LocalizedScriptEditorApply, true);
	GWarn->StatusUpdate(1, 1, LocalizedScriptEditorApply);

	if (OriginalNiagaraScript->IsSelected())
	{
		GEditor->GetSelectedObjects()->Deselect(OriginalNiagaraScript);
	}

	ResetLoaders(OriginalNiagaraScript->GetOutermost()); // Make sure that we're not going to get invalid version number linkers into the package we are going into. 
	OriginalNiagaraScript->GetOutermost()->LinkerCustomVersion.Empty();

	// Compile and then overwrite the original script in place by constructing a new one with the same name
	ScriptViewModel->CompileStandaloneScript();
	ScriptViewModel->RefreshMetadataCollection();
	OriginalNiagaraScript = (UNiagaraScript*)StaticDuplicateObject(EditedNiagaraScript, OriginalNiagaraScript->GetOuter(), OriginalNiagaraScript->GetFName(),
		RF_AllFlags,
		OriginalNiagaraScript->GetClass());

	// Restore RF_Standalone on the original material, as it had been removed from the preview material so that it could be GC'd.
	OriginalNiagaraScript->SetFlags(RF_Standalone);

	// Now there might be other Scripts with functions that referenced this script. So let's update them. They'll need a recompile.
	// Note that we don't discriminate between the version that are open in transient packages (likely duplicates for editing) and the
	// original in-scene versions.
	TArray<UNiagaraScript*> AffectedScripts;
	UNiagaraGraph* OriginalGraph = CastChecked<UNiagaraScriptSource>(OriginalNiagaraScript->GetSource())->NodeGraph;
	for (TObjectIterator<UNiagaraScript> It; It; ++It)
	{
		if (*It == OriginalNiagaraScript || It->IsPendingKillOrUnreachable())
		{
			continue;
		}

		// First see if it is directly called, as this will force a need to refresh from external changes...
		UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(It->GetSource());
		TArray<UNiagaraNode*> NiagaraNodes;
		Source->NodeGraph->GetNodesOfClass<UNiagaraNode>(NiagaraNodes);
		bool bRefreshed = false;
		for (UNiagaraNode* NiagaraNode : NiagaraNodes)
		{
			UObject* ReferencedAsset = NiagaraNode->GetReferencedAsset();
			if (ReferencedAsset == OriginalNiagaraScript)
			{
				NiagaraNode->RefreshFromExternalChanges();
				bRefreshed = true;
			}
		}

		if (bRefreshed)
		{
			//Source->NodeGraph->NotifyGraphNeedsRecompile();
			AffectedScripts.AddUnique(*It);
		}
		else
		{
			// Now check to see if our graph is anywhere in the dependency chain for a given graph. If it is, 
			// then it will need to be recompiled against the latest version.
			TArray<const UNiagaraGraph*> ReferencedGraphs;
			Source->NodeGraph->GetAllReferencedGraphs(ReferencedGraphs);
			for (const UNiagaraGraph* Graph : ReferencedGraphs)
			{
				if (Graph == OriginalGraph)
				{
					//Source->NodeGraph->NotifyGraphNeedsRecompile();
					AffectedScripts.AddUnique(*It);
					break;
				}
			}
		}
	}

	// Now determine if any of these scripts were in Emitters. If so, those emitters should be compiled together. If not, go ahead and compile individually.
	// Use the existing view models if they exist,as they are already wired into the correct UI.
	TArray<UNiagaraEmitter*> AffectedEmitters;
	for (UNiagaraScript* Script : AffectedScripts)
	{
		if (Script->IsParticleScript() || Script->IsEmitterSpawnScript() || Script->IsEmitterUpdateScript())
		{
			UNiagaraEmitter* Emitter = Cast<UNiagaraEmitter>(Script->GetOuter());
			if (Emitter)
			{
				AffectedEmitters.AddUnique(Emitter);
			}
		}
		else if (Script->IsSystemSpawnScript() || Script->IsSystemUpdateScript())
		{
			UNiagaraSystem* System = Cast<UNiagaraSystem>(Script->GetOuter());
			if (System)
			{
				for (int32 i = 0; i < System->GetNumEmitters(); i++)
				{
					AffectedEmitters.AddUnique(System->GetEmitterHandle(i).GetInstance());
					AffectedEmitters.AddUnique((UNiagaraEmitter*)System->GetEmitterHandle(i).GetSource());
				}
			}
		}
		else
		{
			TSharedPtr<FNiagaraScriptViewModel> AffectedScriptViewModel = FNiagaraScriptViewModel::GetExistingViewModelForObject(Script);
			if (!AffectedScriptViewModel.IsValid())
			{
				AffectedScriptViewModel = MakeShareable(new FNiagaraScriptViewModel(Script, FText::FromString(Script->GetName()), ENiagaraParameterEditMode::EditValueOnly));
			}
			AffectedScriptViewModel->CompileStandaloneScript();
		}
	}

	FNiagaraEditorUtilities::CompileExistingEmitters(AffectedEmitters);

	GWarn->EndSlowTask();
	bEditedScriptHasPendingChanges = false;
}


bool FNiagaraScriptToolkit::OnRequestClose()
{
	if (bChangesDiscarded == false && IsEditScriptDifferentFromOriginalScript())
	{
		// find out the user wants to do with this dirty NiagaraScript
		EAppReturnType::Type YesNoCancelReply = FMessageDialog::Open(EAppMsgType::YesNoCancel,
			FText::Format(
				NSLOCTEXT("UnrealEd", "Prompt_NiagaraScriptEditorClose", "Would you like to apply changes to this NiagaraScript to the original NiagaraScript?\n{0}\n(No will lose all changes!)"),
				FText::FromString(OriginalNiagaraScript->GetPathName())));

		// act on it
		switch (YesNoCancelReply)
		{
		case EAppReturnType::Yes:
			// update NiagaraScript and exit
			UpdateOriginalNiagaraScript();
			break;

		case EAppReturnType::No:
			// Set the changes discarded to avoid showing the dialog multiple times when request close is called multiple times on shut down.
			bChangesDiscarded = true;
			break;

		case EAppReturnType::Cancel:
			// don't exit
			return false;
		}
	}

	return true;
}

void FNiagaraScriptToolkit::OnEditedScriptGraphChanged(const FEdGraphEditAction& InAction)
{
	bEditedScriptHasPendingChanges = true;
}

void FNiagaraScriptToolkit::FocusGraphElement(const INiagaraScriptGraphFocusInfo* FocusInfo)
{
	checkf(FocusInfo->GetFocusType() != INiagaraScriptGraphFocusInfo::ENiagaraScriptGraphFocusInfoType::None, TEXT("Failed to assign focus type to FocusInfo parameter!"));
	
	if (FocusInfo->GetScriptUniqueAssetID() != OriginalNiagaraScript->GetUniqueID())
	{
		return;
	}

	if (FocusInfo->GetFocusType() == INiagaraScriptGraphFocusInfo::ENiagaraScriptGraphFocusInfoType::Node)
	{
		const FNiagaraScriptGraphNodeToFocusInfo* NodeFocusInfo = static_cast<const FNiagaraScriptGraphNodeToFocusInfo*>(FocusInfo);
		const FGuid& NodeGuidToMatch = NodeFocusInfo->GetNodeGuidToFocus();
		UEdGraphNode* const* NodeToFocus = ScriptViewModel->GetGraphViewModel()->GetGraph()->Nodes.FindByPredicate([&NodeGuidToMatch](const UEdGraphNode* Node) {return Node->NodeGuid == NodeGuidToMatch; });
		if (NodeToFocus != nullptr && *NodeToFocus != nullptr)
		{
			NiagaraScriptGraphWidget->GetGraphEditor()->JumpToNode(*NodeToFocus);
			return;
		}
		ensureMsgf(false, TEXT("Failed to find Node with matching GUID when focusing graph element. Was the graph edited out from underneath us?"));
		return;
	}
	else if (FocusInfo->GetFocusType() == INiagaraScriptGraphFocusInfo::ENiagaraScriptGraphFocusInfoType::Pin)
	{
		const FNiagaraScriptGraphPinToFocusInfo* PinFocusInfo = static_cast<const FNiagaraScriptGraphPinToFocusInfo*>(FocusInfo);
		const FGuid& PinGuidToMatch = PinFocusInfo->GetPinGuidToFocus();
		for (const UEdGraphNode* Node : ScriptViewModel->GetGraphViewModel()->GetGraph()->Nodes)
		{
			const UEdGraphPin* const* PinToFocus = Node->Pins.FindByPredicate([&PinGuidToMatch](const UEdGraphPin* Pin) {return Pin->PersistentGuid == PinGuidToMatch; });
			if (PinToFocus != nullptr && *PinToFocus != nullptr)
			{
				NiagaraScriptGraphWidget->GetGraphEditor()->JumpToPin(*PinToFocus);
				return;
			}
		}
		ensureMsgf(false, TEXT("Failed to find Pin with matching GUID when focusing graph element. Was the graph edited out from underneath us?"));
		return;
	}
	checkf(false, TEXT("Requested focus for a graph element without specifying a Node or Pin to focus!"));
}

#undef LOCTEXT_NAMESPACE
