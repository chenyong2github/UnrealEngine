// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraSystemToolkit.h"
#include "NiagaraEditorModule.h"
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScriptSource.h"
#include "NiagaraObjectSelection.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraEmitterViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/NiagaraScratchPadScriptViewModel.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"
#include "ViewModels/NiagaraScriptGraphViewModel.h"
#include "NiagaraSystemScriptViewModel.h"
#include "Widgets/SNiagaraSystemScript.h"
#include "Widgets/SNiagaraSystemViewport.h"
#include "Widgets/SNiagaraSelectedObjectsDetails.h"
#include "Widgets/SNiagaraParameterMapView.h"
#include "Widgets/SNiagaraParameterPanel.h"
#include "Widgets/SNiagaraSpreadsheetView.h"
#include "Widgets/SNiagaraGeneratedCodeView.h"
#include "Widgets/SNiagaraScriptGraph.h"
#include "Widgets/SNiagaraDebugger.h"
#include "NiagaraEditorCommands.h"
#include "NiagaraEditorStyle.h"
#include "NiagaraEditorSettings.h"
#include "NiagaraSystemFactoryNew.h"
#include "NiagaraComponent.h"
#include "NiagaraSystemEditorData.h"
#include "NiagaraScriptStatsViewModel.h"
#include "NiagaraBakerViewModel.h"
#include "NiagaraToolkitCommon.h"

#include "IContentBrowserSingleton.h"
#include "ContentBrowserModule.h"

#include "EditorStyleSet.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "ScopedTransaction.h"

#include "Framework/Application/SlateApplication.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Docking/SDockTab.h"
#include "AdvancedPreviewSceneModule.h"
#include "BusyCursor.h"
#include "Misc/FeedbackContext.h"
#include "Editor.h"
#include "LevelEditor.h"
#include "LevelEditorActions.h"
#include "Engine/Selection.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "NiagaraMessageLogViewModel.h"
#include "NiagaraScriptStatsViewModel.h"
#include "Widgets/Input/SCheckBox.h"
#include "ViewModels/NiagaraParameterPanelViewModel.h"
#include "NiagaraEditor/Private/SNiagaraAssetPickerList.h"
#include "Widgets/SNiagaraParameterDefinitionsPanel.h"
#include "ViewModels/NiagaraParameterDefinitionsPanelViewModel.h"


#define LOCTEXT_NAMESPACE "NiagaraSystemEditor"

DECLARE_CYCLE_STAT(TEXT("Niagara - SystemToolkit - OnApply"), STAT_NiagaraEditor_SystemToolkit_OnApply, STATGROUP_NiagaraEditor);

const FName FNiagaraSystemToolkit::ViewportTabID(TEXT("NiagaraSystemEditor_Viewport"));
const FName FNiagaraSystemToolkit::CurveEditorTabID(TEXT("NiagaraSystemEditor_CurveEditor"));
const FName FNiagaraSystemToolkit::SequencerTabID(TEXT("NiagaraSystemEditor_Sequencer"));
const FName FNiagaraSystemToolkit::SystemScriptTabID(TEXT("NiagaraSystemEditor_SystemScript"));
const FName FNiagaraSystemToolkit::SystemDetailsTabID(TEXT("NiagaraSystemEditor_SystemDetails"));
const FName FNiagaraSystemToolkit::SystemParametersTabID(TEXT("NiagaraSystemEditor_SystemParameters"));
const FName FNiagaraSystemToolkit::SystemParametersTabID2(TEXT("NiagaraSystemEditor_SystemParameters2"));
const FName FNiagaraSystemToolkit::SystemParameterDefinitionsTabID(TEXT("NiagaraSystemEditor_SystemParameterDefinitions"));
const FName FNiagaraSystemToolkit::SelectedEmitterStackTabID(TEXT("NiagaraSystemEditor_SelectedEmitterStack"));
const FName FNiagaraSystemToolkit::SelectedEmitterGraphTabID(TEXT("NiagaraSystemEditor_SelectedEmitterGraph"));
const FName FNiagaraSystemToolkit::DebugSpreadsheetTabID(TEXT("NiagaraSystemEditor_DebugAttributeSpreadsheet"));
const FName FNiagaraSystemToolkit::PreviewSettingsTabId(TEXT("NiagaraSystemEditor_PreviewSettings"));
const FName FNiagaraSystemToolkit::GeneratedCodeTabID(TEXT("NiagaraSystemEditor_GeneratedCode"));
const FName FNiagaraSystemToolkit::MessageLogTabID(TEXT("NiagaraSystemEditor_MessageLog"));
const FName FNiagaraSystemToolkit::SystemOverviewTabID(TEXT("NiagaraSystemEditor_SystemOverview"));
const FName FNiagaraSystemToolkit::ScratchPadTabID(TEXT("NiagaraSystemEditor_ScratchPad"));
const FName FNiagaraSystemToolkit::ScriptStatsTabID(TEXT("NiagaraSystemEditor_ScriptStats"));
const FName FNiagaraSystemToolkit::BakerTabID(TEXT("NiagaraSystemEditor_Baker"));
IConsoleVariable* FNiagaraSystemToolkit::VmStatEnabledVar = IConsoleManager::Get().FindConsoleVariable(TEXT("vm.DetailedVMScriptStats"));
IConsoleVariable* FNiagaraSystemToolkit::GpuStatEnabledVar = IConsoleManager::Get().FindConsoleVariable(TEXT("fx.NiagaraGpuProfilingEnabled"));

static int32 GbLogNiagaraSystemChanges = 0;
static FAutoConsoleVariableRef CVarSuppressNiagaraSystems(
	TEXT("fx.LogNiagaraSystemChanges"),
	GbLogNiagaraSystemChanges,
	TEXT("If > 0 Niagara Systems will be written to a text format when opened and closed in the editor. \n"),
	ECVF_Default
);

void FNiagaraSystemToolkit::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("WorkspaceMenu_NiagaraSystemEditor", "Niagara System"));

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(ViewportTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_Viewport))
		.SetDisplayName(LOCTEXT("Preview", "Preview"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Viewports"));

	InTabManager->RegisterTabSpawner(CurveEditorTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_CurveEd))
		.SetDisplayName(LOCTEXT("Curves", "Curves"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(SequencerTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_Sequencer))
		.SetDisplayName(LOCTEXT("Timeline", "Timeline"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(SystemScriptTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_SystemScript))
		.SetDisplayName(LOCTEXT("SystemScript", "System Script"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetAutoGenerateMenuEntry(GbShowNiagaraDeveloperWindows != 0);

	InTabManager->RegisterTabSpawner(SystemParametersTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_SystemParameters))
		.SetDisplayName(LOCTEXT("SystemParameters", "Parameters"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(SystemParametersTabID2, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_SystemParameters2))
		.SetDisplayName(LOCTEXT("SystemParameters2", "Legacy Parameters"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

//@todo(ng) disable parameter definitions panel pending bug fixes
// 	InTabManager->RegisterTabSpawner(SystemParameterDefinitionsTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_SystemParameterDefinitions)) 
// 		.SetDisplayName(LOCTEXT("SystemParameterDefinitions", "Parameter Definitions"))
// 		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(SelectedEmitterStackTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_SelectedEmitterStack))
		.SetDisplayName(LOCTEXT("SelectedEmitterStacks", "Selected Emitters"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(SelectedEmitterGraphTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_SelectedEmitterGraph))
		.SetDisplayName(LOCTEXT("SelectedEmitterGraph", "Selected Emitter Graph"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetAutoGenerateMenuEntry(GbShowNiagaraDeveloperWindows != 0);

	InTabManager->RegisterTabSpawner(DebugSpreadsheetTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_DebugSpreadsheet))
		.SetDisplayName(LOCTEXT("DebugSpreadsheet", "Attribute Spreadsheet"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(PreviewSettingsTabId, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_PreviewSettings))
		.SetDisplayName(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef())
		.SetIcon(FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(GeneratedCodeTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_GeneratedCode))
		.SetDisplayName(LOCTEXT("GeneratedCode", "Generated Code"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(MessageLogTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_MessageLog))
		.SetDisplayName(LOCTEXT("NiagaraMessageLog", "Niagara Log"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(SystemOverviewTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_SystemOverview))
		.SetDisplayName(LOCTEXT("SystemOverviewTabName", "System Overview"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(ScratchPadTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_ScratchPad))
		.SetDisplayName(LOCTEXT("ScratchPadTabName", "Scratch Pad"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	InTabManager->RegisterTabSpawner(ScriptStatsTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_ScriptStats))
		.SetDisplayName(LOCTEXT("NiagaraScriptsStatsTab", "Script Stats"))
		.SetGroup(WorkspaceMenuCategory.ToSharedRef());

	if (GetDefault<UNiagaraEditorSettings>()->bEnableBaker)
	{
		InTabManager->RegisterTabSpawner(BakerTabID, FOnSpawnTab::CreateSP(this, &FNiagaraSystemToolkit::SpawnTab_Baker))
			.SetDisplayName(LOCTEXT("NiagaraBakerTab", "Baker"))
			.SetGroup(WorkspaceMenuCategory.ToSharedRef());
	}
}

void FNiagaraSystemToolkit::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(ViewportTabID);
	InTabManager->UnregisterTabSpawner(CurveEditorTabID);
	InTabManager->UnregisterTabSpawner(SequencerTabID);
	InTabManager->UnregisterTabSpawner(SystemScriptTabID);
	InTabManager->UnregisterTabSpawner(SystemDetailsTabID);
	InTabManager->UnregisterTabSpawner(SystemParametersTabID);
	InTabManager->UnregisterTabSpawner(SystemParametersTabID2);
	//@todo(ng) disable parameter definitions panel pending bug fixes
	//InTabManager->UnregisterTabSpawner(SystemParameterDefinitionsTabID);
	InTabManager->UnregisterTabSpawner(SelectedEmitterStackTabID);
	InTabManager->UnregisterTabSpawner(SelectedEmitterGraphTabID);
	InTabManager->UnregisterTabSpawner(DebugSpreadsheetTabID);
	InTabManager->UnregisterTabSpawner(PreviewSettingsTabId);
	InTabManager->UnregisterTabSpawner(GeneratedCodeTabID);
	InTabManager->UnregisterTabSpawner(SystemOverviewTabID);
	InTabManager->UnregisterTabSpawner(ScratchPadTabID);
	InTabManager->UnregisterTabSpawner(ScriptStatsTabID);
	InTabManager->UnregisterTabSpawner(BakerTabID);
}

FNiagaraSystemToolkit::~FNiagaraSystemToolkit()
{
	// Cleanup viewmodels that use the system viewmodel before cleaning up the system viewmodel itself.
	if (ParameterPanelViewModel.IsValid())
	{
		ParameterPanelViewModel->Cleanup();
	}
	if (ParameterDefinitionsPanelViewModel.IsValid())
	{
		ParameterDefinitionsPanelViewModel->Cleanup();
	}

	if (SystemViewModel.IsValid())
	{
		if (SystemViewModel->GetSelectionViewModel() != nullptr)
		{
			SystemViewModel->GetSelectionViewModel()->OnSystemIsSelectedChanged().RemoveAll(this);
			SystemViewModel->GetSelectionViewModel()->OnEmitterHandleIdSelectionChanged().RemoveAll(this);
		}
		SystemViewModel->Cleanup();
	}
	SystemViewModel.Reset();
}

void FNiagaraSystemToolkit::AddReferencedObjects(FReferenceCollector& Collector) 
{
	Collector.AddReferencedObject(System);
}

void FNiagaraSystemToolkit::InitializeWithSystem(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UNiagaraSystem& InSystem)
{
	System = &InSystem;
	Emitter = nullptr;
	System->EnsureFullyLoaded();

	FNiagaraSystemViewModelOptions SystemOptions;
	SystemOptions.bCanModifyEmittersFromTimeline = true;
	SystemOptions.EditMode = ENiagaraSystemViewModelEditMode::SystemAsset;
	SystemOptions.OnGetSequencerAddMenuContent.BindSP(this, &FNiagaraSystemToolkit::GetSequencerAddMenuContent);
	SystemOptions.MessageLogGuid = InSystem.GetAssetGuid();

	SystemViewModel = MakeShared<FNiagaraSystemViewModel>();
	SystemViewModel->Initialize(*System, SystemOptions);
	SystemGraphSelectionViewModel = MakeShared<FNiagaraSystemGraphSelectionViewModel>();
	SystemGraphSelectionViewModel->Init(SystemViewModel);
	ParameterPanelViewModel = MakeShared<FNiagaraSystemToolkitParameterPanelViewModel>(SystemViewModel, TWeakPtr<FNiagaraSystemGraphSelectionViewModel>(SystemGraphSelectionViewModel));
	ParameterDefinitionsPanelViewModel = MakeShared<FNiagaraSystemToolkitParameterDefinitionsPanelViewModel>(SystemViewModel, SystemGraphSelectionViewModel);
	FSystemToolkitUIContext UIContext = FSystemToolkitUIContext(
		FSimpleDelegate::CreateSP(ParameterPanelViewModel.ToSharedRef(), &INiagaraImmutableParameterPanelViewModel::Refresh),
		FSimpleDelegate::CreateSP(ParameterDefinitionsPanelViewModel.ToSharedRef(), &INiagaraImmutableParameterPanelViewModel::Refresh)
	);
	ParameterPanelViewModel->Init(UIContext);
	ParameterDefinitionsPanelViewModel->Init(UIContext);
	
	SystemViewModel->SetToolkitCommands(GetToolkitCommands());
	SystemToolkitMode = ESystemToolkitMode::System;

	if (GbLogNiagaraSystemChanges > 0)
	{
		FString ExportText;
		SystemViewModel->DumpToText(ExportText);
		FString FilePath = System->GetOutermost()->FileName.ToString();
		FString PathPart, FilenamePart, ExtensionPart;
		FPaths::Split(FilePath, PathPart, FilenamePart, ExtensionPart);

		FNiagaraEditorUtilities::WriteTextFileToDisk(FPaths::ProjectLogDir(), FilenamePart + TEXT(".onLoad.txt"), ExportText, true);
	}

	InitializeInternal(Mode, InitToolkitHost, SystemOptions.MessageLogGuid.GetValue());
}

void FNiagaraSystemToolkit::InitializeWithEmitter(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UNiagaraEmitter& InEmitter)
{
	System = NewObject<UNiagaraSystem>(GetTransientPackage(), NAME_None, RF_Transient | RF_Transactional);
	UNiagaraSystemFactoryNew::InitializeSystem(System, true);
	System->EnsureFullyLoaded();

	Emitter = &InEmitter;
	Emitter->UpdateEmitterAfterLoad();

	// Before copying the emitter prepare the rapid iteration parameters so that the post compile prepare doesn't
	// cause the change ids to become out of sync.
	TArray<UNiagaraScript*> Scripts;
	TMap<UNiagaraScript*, UNiagaraScript*> ScriptDependencyMap;
	TMap<UNiagaraScript*, const UNiagaraEmitter*> ScriptToEmitterMap;

	Scripts.Add(Emitter->EmitterSpawnScriptProps.Script);
	ScriptToEmitterMap.Add(Emitter->EmitterSpawnScriptProps.Script, Emitter);

	Scripts.Add(Emitter->EmitterUpdateScriptProps.Script);
	ScriptToEmitterMap.Add(Emitter->EmitterUpdateScriptProps.Script, Emitter);

	Scripts.Add(Emitter->SpawnScriptProps.Script);
	ScriptToEmitterMap.Add(Emitter->SpawnScriptProps.Script, Emitter);

	Scripts.Add(Emitter->UpdateScriptProps.Script);
	ScriptToEmitterMap.Add(Emitter->UpdateScriptProps.Script, Emitter);

	if (Emitter->SimTarget == ENiagaraSimTarget::GPUComputeSim)
	{
		Scripts.Add(Emitter->GetGPUComputeScript());
		ScriptToEmitterMap.Add(Emitter->GetGPUComputeScript(), Emitter);
		ScriptDependencyMap.Add(Emitter->SpawnScriptProps.Script, Emitter->GetGPUComputeScript());
		ScriptDependencyMap.Add(Emitter->UpdateScriptProps.Script, Emitter->GetGPUComputeScript());
	} 
	else if (Emitter->bInterpolatedSpawning)
	{
		ScriptDependencyMap.Add(Emitter->UpdateScriptProps.Script, Emitter->SpawnScriptProps.Script);
	}

	FNiagaraUtilities::PrepareRapidIterationParameters(Scripts, ScriptDependencyMap, ScriptToEmitterMap);

	ResetLoaders(GetTransientPackage()); // Make sure that we're not going to get invalid version number linkers into the package we are going into. 
	GetTransientPackage()->LinkerCustomVersion.Empty();
	
	bEmitterThumbnailUpdated = false;

	FNiagaraSystemViewModelOptions SystemOptions;
	SystemOptions.bCanModifyEmittersFromTimeline = false;
	SystemOptions.EditMode = ENiagaraSystemViewModelEditMode::EmitterAsset;
	SystemOptions.MessageLogGuid = System->GetAssetGuid();

	SystemViewModel = MakeShared<FNiagaraSystemViewModel>();
	SystemViewModel->Initialize(*System, SystemOptions);
	SystemViewModel->GetEditorData().SetOwningSystemIsPlaceholder(true, *System);
	SystemViewModel->SetToolkitCommands(GetToolkitCommands());
	SystemViewModel->AddEmitter(*Emitter);

	ParameterPanelViewModel = MakeShared<FNiagaraSystemToolkitParameterPanelViewModel>(SystemViewModel);
	ParameterDefinitionsPanelViewModel = MakeShared<FNiagaraSystemToolkitParameterDefinitionsPanelViewModel>(SystemViewModel);
	FSystemToolkitUIContext UIContext = FSystemToolkitUIContext(
		FSimpleDelegate::CreateSP(ParameterPanelViewModel.ToSharedRef(), &INiagaraImmutableParameterPanelViewModel::Refresh),
		FSimpleDelegate::CreateSP(ParameterDefinitionsPanelViewModel.ToSharedRef(), &INiagaraImmutableParameterPanelViewModel::Refresh)
	);
	ParameterPanelViewModel->Init(UIContext);
	ParameterDefinitionsPanelViewModel->Init(UIContext);

	// Adding the emitter to the system has made a copy of it and we set this to the copy's change id here instead of the original emitter's change 
	// id because the copy's change id may have been updated from the original as part of post load and we use this id to detect if the editable 
	// emitter has been changed.
	LastSyncedEmitterChangeId = SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterViewModel()->GetEmitter()->GetChangeId();
	SystemToolkitMode = ESystemToolkitMode::Emitter;

	if (GbLogNiagaraSystemChanges > 0)
	{
		FString ExportText;
		SystemViewModel->DumpToText(ExportText);
		FString FilePath = Emitter->GetOutermost()->FileName.ToString();
		FString PathPart, FilenamePart, ExtensionPart;
		FPaths::Split(FilePath, PathPart, FilenamePart, ExtensionPart);

		FNiagaraEditorUtilities::WriteTextFileToDisk(FPaths::ProjectLogDir(), FilenamePart + TEXT(".onLoad.txt"), ExportText, true);
	}

	InitializeInternal(Mode, InitToolkitHost, SystemOptions.MessageLogGuid.GetValue());
}

void FNiagaraSystemToolkit::InitializeInternal(const EToolkitMode::Type Mode, const TSharedPtr<IToolkitHost>& InitToolkitHost, const FGuid& MessageLogGuid)
{
	NiagaraMessageLogViewModel = MakeShared<FNiagaraMessageLogViewModel>(GetNiagaraSystemMessageLogName(System), MessageLogGuid, NiagaraMessageLog);
	ObjectSelectionForParameterMapView = MakeShared<FNiagaraObjectSelection>();
	ScriptStats = MakeShared<FNiagaraScriptStatsViewModel>();
	ScriptStats->Initialize(SystemViewModel);
	BakerViewModel = MakeShared<FNiagaraBakerViewModel>();
	BakerViewModel->Initialize(SystemViewModel);

	SystemViewModel->OnEmitterHandleViewModelsChanged().AddSP(this, &FNiagaraSystemToolkit::RefreshParameters);
	SystemViewModel->GetSelectionViewModel()->OnSystemIsSelectedChanged().AddSP(this, &FNiagaraSystemToolkit::OnSystemSelectionChanged);
	SystemViewModel->GetSelectionViewModel()->OnEmitterHandleIdSelectionChanged().AddSP(this, &FNiagaraSystemToolkit::OnSystemSelectionChanged);
	SystemViewModel->GetOnPinnedEmittersChanged().AddSP(this, &FNiagaraSystemToolkit::RefreshParameters);
	SystemViewModel->OnRequestFocusTab().AddSP(this, &FNiagaraSystemToolkit::OnViewModelRequestFocusTab);
	
	const float InTime = -0.02f;
	const float OutTime = 3.2f;

	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("Standalone_Niagara_System_Layout_v24")
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
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split
				(
					// Top Level Left
					FTabManager::NewSplitter()->SetOrientation(Orient_Vertical)
					->SetSizeCoefficient(.75f)
					->Split
					(
						// Inner Left Top
						FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
						->SetSizeCoefficient(0.75f)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(.25f)
							->AddTab(ViewportTabID, ETabState::OpenedTab)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.15f)
							->AddTab(SystemParametersTabID, ETabState::OpenedTab)
						)
						->Split
						(
							FTabManager::NewStack()
							->SetSizeCoefficient(0.6f)
							->AddTab(SystemOverviewTabID, ETabState::OpenedTab)
							->AddTab(ScratchPadTabID, ETabState::OpenedTab)
							->AddTab(BakerTabID, ETabState::ClosedTab)
							->SetForegroundTab(SystemOverviewTabID)
						)
					)
					->Split
					(
						// Inner Left Bottom
						FTabManager::NewStack()
						->SetSizeCoefficient(0.25f)
						->AddTab(CurveEditorTabID, ETabState::OpenedTab)
						->AddTab(MessageLogTabID, ETabState::OpenedTab)
						->AddTab(SequencerTabID, ETabState::OpenedTab)
						->AddTab(ScriptStatsTabID, ETabState::ClosedTab)
					)
				)
				->Split
				(
					// Top Level Right
					FTabManager::NewStack()
					->SetSizeCoefficient(0.25f)
					->AddTab(SelectedEmitterStackTabID, ETabState::OpenedTab)
					->AddTab(SelectedEmitterGraphTabID, ETabState::ClosedTab)
					->AddTab(SystemScriptTabID, ETabState::ClosedTab)
					->AddTab(SystemDetailsTabID, ETabState::ClosedTab)
					->AddTab(DebugSpreadsheetTabID, ETabState::ClosedTab)
					->AddTab(PreviewSettingsTabId, ETabState::ClosedTab)
					->AddTab(GeneratedCodeTabID, ETabState::ClosedTab)
				)
			)
		);

	const bool bCreateDefaultStandaloneMenu = true;
	const bool bCreateDefaultToolbar = true;
	UObject* ToolkitObject = SystemToolkitMode == ESystemToolkitMode::System ? (UObject*)System : (UObject*)Emitter;
	// order of registering commands matters. SetupCommands before InitAssetEditor will make the toolkit prioritize niagara commands
	SetupCommands();
	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, FNiagaraEditorModule::NiagaraEditorAppIdentifier,
		StandaloneDefaultLayout, bCreateDefaultStandaloneMenu, bCreateDefaultToolbar, ToolkitObject);
	
	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	AddMenuExtender(NiagaraEditorModule.GetMenuExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));

	ExtendToolbar();
	RegenerateMenusAndToolbars();

	bChangesDiscarded = false;
	bScratchPadChangesDiscarded = false;


	GEditor->RegisterForUndo(this);
}

FName FNiagaraSystemToolkit::GetToolkitFName() const
{
	return FName("Niagara");
}

FText FNiagaraSystemToolkit::GetBaseToolkitName() const
{
	return LOCTEXT("AppLabel", "Niagara");
}

FString FNiagaraSystemToolkit::GetWorldCentricTabPrefix() const
{
	return LOCTEXT("WorldCentricTabPrefix", "Niagara ").ToString();
}


FLinearColor FNiagaraSystemToolkit::GetWorldCentricTabColorScale() const
{
	return FNiagaraEditorModule::WorldCentricTabColorScale;
}

bool FNiagaraSystemToolkit::MatchesContext(const FTransactionContext& InContext, const TArray<TPair<UObject*, FTransactionObjectEvent>>& TransactionObjects) const
{
	TArray<UNiagaraGraph*> Graphs;

	for (TSharedRef<FNiagaraScratchPadScriptViewModel> ScratchPadViewModel : SystemViewModel->GetScriptScratchPadViewModel()->GetScriptViewModels())
	{
		Graphs.AddUnique(ScratchPadViewModel->GetGraphViewModel()->GetGraph());
	}

	LastUndoGraphs.Empty();

	if (Graphs.Num() > 0)
	{
		for (const TPair<UObject*, FTransactionObjectEvent>& TransactionObjectPair : TransactionObjects)
		{
			UObject* Object = TransactionObjectPair.Key;
			while (Object != nullptr)
			{
				if (Graphs.Contains(Object))
				{
					LastUndoGraphs.AddUnique(Cast<UNiagaraGraph>(Object));
					return true;
				}
				Object = Object->GetOuter();
			}
		}
	}
	return LastUndoGraphs.Num() > 0;
}

void FNiagaraSystemToolkit::PostUndo(bool bSuccess)
{
	for (TWeakObjectPtr<UNiagaraGraph> Graph : LastUndoGraphs)
	{
		if (Graph.IsValid())
		{
			Graph->NotifyGraphNeedsRecompile();
		}
	}

	LastUndoGraphs.Empty();
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_Viewport(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == ViewportTabID);

	Viewport = SNew(SNiagaraSystemViewport)
		.OnThumbnailCaptured(this, &FNiagaraSystemToolkit::OnThumbnailCaptured)
		.Sequencer(SystemViewModel->GetSequencer());

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			Viewport.ToSharedRef()
		];

	Viewport->SetPreviewComponent(SystemViewModel->GetPreviewComponent());
	Viewport->OnAddedToTab(SpawnedTab);

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_PreviewSettings(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == PreviewSettingsTabId);

	TSharedRef<SWidget> InWidget = SNullWidget::NullWidget;
	if (Viewport.IsValid())
	{
		FAdvancedPreviewSceneModule& AdvancedPreviewSceneModule = FModuleManager::LoadModuleChecked<FAdvancedPreviewSceneModule>("AdvancedPreviewScene");
		InWidget = AdvancedPreviewSceneModule.CreateAdvancedPreviewSceneSettingsWidget(Viewport->GetPreviewScene());
	}

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Icon(FEditorStyle::GetBrush("LevelEditor.Tabs.Details"))
		.Label(LOCTEXT("PreviewSceneSettingsTab", "Preview Scene Settings"))
		[
			InWidget
		];

	return SpawnedTab;
}


TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_CurveEd(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == CurveEditorTabID);

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			FNiagaraEditorModule::Get().GetWidgetProvider()->CreateCurveOverview(SystemViewModel.ToSharedRef())
		];

	return SpawnedTab;
}


TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_Sequencer(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SequencerTabID);

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SystemViewModel->GetSequencer()->GetSequencerWidget()
		];

	return SpawnedTab;
}


TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_SystemScript(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SystemScriptTabID);

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SNew(SNiagaraSystemScript, SystemViewModel.ToSharedRef())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_SystemParameters(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SystemParametersTabID);


	TArray<TSharedRef<FNiagaraObjectSelection>> ObjectSelections;
	ObjectSelections.Add(ObjectSelectionForParameterMapView.ToSharedRef());

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SAssignNew(ParameterPanel, SNiagaraParameterPanel, ParameterPanelViewModel, GetToolkitCommands())
			.ShowParameterSynchronizingWithLibraryIconExternallyReferenced(false)
		];
	RefreshParameters();

	return SpawnedTab;
}

//@todo(ng) cleanup
TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_SystemParameters2(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SystemParametersTabID2);

	TArray<TSharedRef<FNiagaraObjectSelection>> ObjectSelections;
	ObjectSelections.Add(ObjectSelectionForParameterMapView.ToSharedRef());

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SAssignNew(ParameterMapView, SNiagaraParameterMapView, ObjectSelections, SNiagaraParameterMapView::EToolkitType::SYSTEM, GetToolkitCommands())
		];
	RefreshParameters();

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_SystemParameterDefinitions(const FSpawnTabArgs& Args)
{
	checkf(Args.GetTabId().TabType == SystemParameterDefinitionsTabID, TEXT("Wrong tab ID in NiagaraScriptToolkit"));

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SNew(SNiagaraParameterDefinitionsPanel, ParameterDefinitionsPanelViewModel, GetToolkitCommands())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_SelectedEmitterStack(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SelectedEmitterStackTabID);

	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("SystemOverviewSelection", "Selection"))
		[
			NiagaraEditorModule.GetWidgetProvider()->CreateStackView(*SystemViewModel->GetSelectionViewModel()->GetSelectionStackViewModel())
		];

	return SpawnedTab;
}

class SNiagaraSelectedEmitterGraph : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SNiagaraSelectedEmitterGraph)
	{}
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, TSharedRef<FNiagaraSystemViewModel> InSystemViewModel)
	{
		SystemViewModel = InSystemViewModel;
		SystemViewModel->GetSelectionViewModel()->OnEmitterHandleIdSelectionChanged().AddSP(this, &SNiagaraSelectedEmitterGraph::SystemSelectionChanged);
		ChildSlot
		[
			SAssignNew(GraphWidgetContainer, SBox)
		];
		UpdateGraphWidget();
	}

	~SNiagaraSelectedEmitterGraph()
	{
		if (SystemViewModel.IsValid() && SystemViewModel->GetSelectionViewModel())
		{
			SystemViewModel->GetSelectionViewModel()->OnEmitterHandleIdSelectionChanged().RemoveAll(this);
		}
	}

private:
	void SystemSelectionChanged()
	{
		UpdateGraphWidget();
	}

	void UpdateGraphWidget()
	{
		TArray<FGuid> SelectedEmitterHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();
		if (SelectedEmitterHandleIds.Num() == 1)
		{
			TSharedPtr<FNiagaraEmitterHandleViewModel> SelectedEmitterHandle = SystemViewModel->GetEmitterHandleViewModelById(SelectedEmitterHandleIds[0]);
			TSharedRef<SWidget> EmitterWidget = 
				SNew(SSplitter)
				+ SSplitter::Slot()
				.Value(.25f)
				[
					SNew(SNiagaraSelectedObjectsDetails, SelectedEmitterHandle->GetEmitterViewModel()->GetSharedScriptViewModel()->GetGraphViewModel()->GetNodeSelection())
				]
				+ SSplitter::Slot()
				.Value(.75f)
				[
					SNew(SNiagaraScriptGraph, SelectedEmitterHandle->GetEmitterViewModel()->GetSharedScriptViewModel()->GetGraphViewModel())
				];

			UNiagaraEmitter* LastMergedEmitter = SelectedEmitterHandle->GetEmitterViewModel()->GetEmitter()->GetParentAtLastMerge();
			if (LastMergedEmitter != nullptr)
			{
				UNiagaraScriptSource* LastMergedScriptSource = CastChecked<UNiagaraScriptSource>(LastMergedEmitter->GraphSource);
				bool bIsForDataProcessingOnly = false;
				TSharedRef<FNiagaraScriptGraphViewModel> LastMergedScriptGraphViewModel = MakeShared<FNiagaraScriptGraphViewModel>(FText(), bIsForDataProcessingOnly);
				LastMergedScriptGraphViewModel->SetScriptSource(LastMergedScriptSource);
				TSharedRef<SWidget> LastMergedEmitterWidget = 
					SNew(SSplitter)
					+ SSplitter::Slot()
					.Value(.25f)
					[
						SNew(SNiagaraSelectedObjectsDetails, LastMergedScriptGraphViewModel->GetNodeSelection())
					]
					+ SSplitter::Slot()
					.Value(.75f)
					[
						SNew(SNiagaraScriptGraph, LastMergedScriptGraphViewModel)
					];

				GraphWidgetContainer->SetContent
				(
					SNew(SSplitter)
					.Orientation(Orient_Vertical)
					+ SSplitter::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Emitter")))
						]
						+ SVerticalBox::Slot()
						[
							EmitterWidget
						]
					]
					+ SSplitter::Slot()
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Text(FText::FromString(TEXT("Last Merged Emitter")))
						]
						+ SVerticalBox::Slot()
						[
							LastMergedEmitterWidget
						]
					]
				);
			}
			else
			{
				GraphWidgetContainer->SetContent(EmitterWidget);
			}
		}
		else
		{
			GraphWidgetContainer->SetContent(SNullWidget::NullWidget);
		}
	}

private:
	TSharedPtr<FNiagaraSystemViewModel> SystemViewModel;
	TSharedPtr<SBox> GraphWidgetContainer;
};

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_SelectedEmitterGraph(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == SelectedEmitterGraphTabID);

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SNew(SNiagaraSelectedEmitterGraph, SystemViewModel.ToSharedRef())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_DebugSpreadsheet(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == DebugSpreadsheetTabID);

	TSharedRef<SDockTab> SpawnedTab =
		SNew(SDockTab)
		[
			SNew(SNiagaraSpreadsheetView, SystemViewModel.ToSharedRef())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_GeneratedCode(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == GeneratedCodeTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab);
	SpawnedTab->SetContent(SNew(SNiagaraGeneratedCodeView, SystemViewModel.ToSharedRef(), SpawnedTab));
	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_MessageLog(const FSpawnTabArgs& Args)
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

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_SystemOverview(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("SystemOverviewTabLabel", "System Overview"))
		[
			FNiagaraEditorModule::Get().GetWidgetProvider()->CreateSystemOverview(SystemViewModel.ToSharedRef())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_ScratchPad(const FSpawnTabArgs& Args)
{
	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("ScratchPadTabLabel", "Scratch Pad"))
		.Icon(FNiagaraEditorStyle::Get().GetBrush("NiagaraEditor.Scratch"))
		[
			FNiagaraEditorModule::Get().GetWidgetProvider()->CreateScriptScratchPad(*SystemViewModel->GetScriptScratchPadViewModel())
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_ScriptStats(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == ScriptStatsTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("NiagaraScriptStatsTitle", "Script Stats"))
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("ScriptStats")))
			[
				ScriptStats->GetWidget().ToSharedRef()
			]
		];

	return SpawnedTab;
}

TSharedRef<SDockTab> FNiagaraSystemToolkit::SpawnTab_Baker(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == BakerTabID);

	TSharedRef<SDockTab> SpawnedTab = SNew(SDockTab)
		.Label(LOCTEXT("NiagaraBakerTitle", "Baker"))
		[
			SNew(SBox)
			.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Baker")))
			[
				BakerViewModel->GetWidget().ToSharedRef()
			]
		];

	return SpawnedTab;
}

void FNiagaraSystemToolkit::SetupCommands()
{
	FLevelEditorModule& Module = FModuleManager::Get().LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
	const FLevelEditorCommands& LevelEditorCommands = Module.GetLevelEditorCommands();
	
	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().Compile,
		FExecuteAction::CreateRaw(this, &FNiagaraSystemToolkit::CompileSystem, false));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().ResetSimulation,
		FExecuteAction::CreateRaw(this, &FNiagaraSystemToolkit::ResetSimulation)); 

	GetToolkitCommands()->MapAction(
        FNiagaraEditorCommands::Get().ToggleStatPerformance,
        FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::ToggleStatPerformance),
        FCanExecuteAction(),
        FIsActionChecked::CreateSP(this, &FNiagaraSystemToolkit::IsStatPerformanceChecked));
	GetToolkitCommands()->MapAction(
        FNiagaraEditorCommands::Get().ClearStatPerformance,
        FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::ClearStatPerformance));
	GetToolkitCommands()->MapAction(
        FNiagaraEditorCommands::Get().ToggleStatPerformanceGPU,
        FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::ToggleStatPerformanceGPU),
        FCanExecuteAction(),
        FIsActionChecked::CreateSP(this, &FNiagaraSystemToolkit::IsStatPerformanceGPUChecked));
	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().ToggleStatPerformanceTypeAvg,
		FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::ToggleStatPerformanceTypeAvg),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FNiagaraSystemToolkit::IsStatPerformanceTypeAvg));
	GetToolkitCommands()->MapAction(
        FNiagaraEditorCommands::Get().ToggleStatPerformanceTypeMax,
        FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::ToggleStatPerformanceTypeMax),
        FCanExecuteAction(),
        FIsActionChecked::CreateSP(this, &FNiagaraSystemToolkit::IsStatPerformanceTypeMax));
	GetToolkitCommands()->MapAction(
        FNiagaraEditorCommands::Get().ToggleStatPerformanceModeAbsolute,
        FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::ToggleStatPerformanceModeAbsolute),
        FCanExecuteAction(),
        FIsActionChecked::CreateSP(this, &FNiagaraSystemToolkit::IsStatPerformanceModeAbsolute));
	GetToolkitCommands()->MapAction(
        FNiagaraEditorCommands::Get().ToggleStatPerformanceModePercent,
        FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::ToggleStatPerformanceModePercent),
        FCanExecuteAction(),
        FIsActionChecked::CreateSP(this, &FNiagaraSystemToolkit::IsStatPerformanceModePercent));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().ToggleBounds,
		FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OnToggleBounds),
		FCanExecuteAction(),
		FIsActionChecked::CreateSP(this, &FNiagaraSystemToolkit::IsToggleBoundsChecked));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().ToggleBounds_SetFixedBounds_SelectedEmitters,
		FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OnToggleBoundsSetFixedBounds_Emitters),
		FCanExecuteAction::CreateLambda([this]()
		{
			return (this->SystemToolkitMode == ESystemToolkitMode::System && this->SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds().Num() > 0) ||
				this->SystemToolkitMode == ESystemToolkitMode::Emitter;				
		}));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().ToggleBounds_SetFixedBounds_System,
		FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OnToggleBoundsSetFixedBounds_System));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().SaveThumbnailImage,
		FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OnSaveThumbnailImage));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().Apply,
		FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OnApply),
		FCanExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OnApplyEnabled));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().ApplyScratchPadChanges,
		FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OnApplyScratchPadChanges),
		FCanExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OnApplyScratchPadChangesEnabled));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().ToggleAutoPlay,
		FExecuteAction::CreateLambda([]()
		{
			UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
			Settings->SetAutoPlay(!Settings->GetAutoPlay());
		}),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([]() { return GetDefault<UNiagaraEditorSettings>()->GetAutoPlay(); }));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().ToggleResetSimulationOnChange,
		FExecuteAction::CreateLambda([]()
		{
			UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
			Settings->SetResetSimulationOnChange(!Settings->GetResetSimulationOnChange());
		}),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([]() { return GetDefault<UNiagaraEditorSettings>()->GetResetSimulationOnChange(); }));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().ToggleResimulateOnChangeWhilePaused,
		FExecuteAction::CreateLambda([]()
		{
			UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
			Settings->SetResimulateOnChangeWhilePaused(!Settings->GetResimulateOnChangeWhilePaused());
		}),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([]() { return GetDefault<UNiagaraEditorSettings>()->GetResimulateOnChangeWhilePaused(); }));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().ToggleResetDependentSystems,
		FExecuteAction::CreateLambda([]()
		{
			UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
			Settings->SetResetDependentSystemsWhenEditingEmitters(!Settings->GetResetDependentSystemsWhenEditingEmitters());
		}),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([]() { return GetDefault<UNiagaraEditorSettings>()->GetResetDependentSystemsWhenEditingEmitters(); }),
		FIsActionButtonVisible::CreateLambda([this]() { return SystemViewModel->GetEditMode() == ENiagaraSystemViewModelEditMode::EmitterAsset; }));

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().IsolateSelectedEmitters,
		FExecuteAction::CreateLambda([=]()
		{
			SystemViewModel->IsolateSelectedEmitters();
		}),
		FCanExecuteAction());

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().DisableSelectedEmitters,
		FExecuteAction::CreateLambda([=]()
		{
			SystemViewModel->DisableSelectedEmitters();
		}),
		FCanExecuteAction());

	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().OpenDebugHUD,
		FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OpenDebugHUD));
	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().OpenDebugOutliner,
		FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OpenDebugOutliner));	
	GetToolkitCommands()->MapAction(
		FNiagaraEditorCommands::Get().OpenAttributeSpreadsheet,
		FExecuteAction::CreateSP(this, &FNiagaraSystemToolkit::OpenAttributeSpreadsheet));
	
	// appending the sequencer commands will make the toolkit also check for sequencer commands (last)
	GetToolkitCommands()->Append(SystemViewModel->GetSequencer()->GetCommandBindings(ESequencerCommandBindings::Sequencer).ToSharedRef());
	SystemViewModel->GetSequencer()->GetCommandBindings(ESequencerCommandBindings::Sequencer)->Append(GetToolkitCommands());
}

void FNiagaraSystemToolkit::OnSaveThumbnailImage()
{
	if (Viewport.IsValid())
	{
		Viewport->CreateThumbnail(SystemToolkitMode == ESystemToolkitMode::System ? (UObject*)System : Emitter);
	}
}

void FNiagaraSystemToolkit::OnThumbnailCaptured(UTexture2D* Thumbnail)
{
	if (SystemToolkitMode == ESystemToolkitMode::System)
	{
		System->MarkPackageDirty();
		System->ThumbnailImage = Thumbnail;
		// Broadcast an object property changed event to update the content browser
		FPropertyChangedEvent EmptyPropertyChangedEvent(nullptr);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(System, EmptyPropertyChangedEvent);
	}
	else if (SystemToolkitMode == ESystemToolkitMode::Emitter) 
	{
		TSharedPtr<FNiagaraEmitterViewModel> EditableEmitterViewModel = SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterViewModel();
		UNiagaraEmitter* EditableEmitter = EditableEmitterViewModel->GetEmitter();
		EditableEmitter->ThumbnailImage = Thumbnail;
		bEmitterThumbnailUpdated = true;
		// Broadcast an object property changed event to update the content browser
		FPropertyChangedEvent EmptyPropertyChangedEvent(nullptr);
		FCoreUObjectDelegates::OnObjectPropertyChanged.Broadcast(EditableEmitter, EmptyPropertyChangedEvent);
	}
}

void FNiagaraSystemToolkit::ResetSimulation()
{
	SystemViewModel->ResetSystem(FNiagaraSystemViewModel::ETimeResetMode::AllowResetTime, FNiagaraSystemViewModel::EMultiResetMode::AllowResetAllInstances, FNiagaraSystemViewModel::EReinitMode::ReinitializeSystem);
}

void FNiagaraSystemToolkit::ExtendToolbar()
{
	struct Local
	{
		static TSharedRef<SWidget> FillSimulationOptionsMenu(FNiagaraSystemToolkit* Toolkit)
		{
			FMenuBuilder MenuBuilder(true, Toolkit->GetToolkitCommands());
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleAutoPlay);
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleResetSimulationOnChange);
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleResimulateOnChangeWhilePaused);
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleResetDependentSystems);
			return MenuBuilder.MakeWidget();
		}

		static TSharedRef<SWidget> GenerateBakerMenu(FNiagaraSystemToolkit* Toolkit)
		{
			FMenuBuilder MenuBuilder(true, Toolkit->GetToolkitCommands());

			MenuBuilder.AddMenuEntry(
				LOCTEXT("BakerTab", "Open Baker Tab"),
				LOCTEXT("BakerTabTooltip", "Opens the flip book tab."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateLambda([TabManager=Toolkit->TabManager]() { TabManager->TryInvokeTab(BakerTabID); }))
			);

			MenuBuilder.AddMenuEntry(
				LOCTEXT("RenderBaker", "Render Baker"),
				LOCTEXT("RenderBakerTooltip", "Renders the Baker using the current settings."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateSP(Toolkit, &FNiagaraSystemToolkit::RenderBaker))
			);

			return MenuBuilder.MakeWidget();
		}

		static TSharedRef<SWidget> FillDebugOptionsMenu(FNiagaraSystemToolkit* Toolkit)
		{
			FMenuBuilder MenuBuilder(true, Toolkit->GetToolkitCommands());

#if WITH_NIAGARA_DEBUGGER
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().OpenDebugHUD);
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().OpenDebugOutliner);
#endif
			MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().OpenAttributeSpreadsheet);
			return MenuBuilder.MakeWidget();
		}


		static void FillToolbar(FToolBarBuilder& ToolbarBuilder, FNiagaraSystemToolkit* Toolkit)
		{
			ToolbarBuilder.BeginSection("Apply");
			{
				if (Toolkit->Emitter != nullptr)
				{
					ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().Apply,
						NAME_None, TAttribute<FText>(), TAttribute<FText>(),
						FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.Apply"),
						FName(TEXT("ApplyNiagaraEmitter")));
				}
				ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().ApplyScratchPadChanges,
					NAME_None, TAttribute<FText>(), TAttribute<FText>(),
					FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.ApplyScratchPadChanges"),
					FName(TEXT("ApplyScratchPadChanges")));
			}
			ToolbarBuilder.EndSection();
			ToolbarBuilder.BeginSection("Compile");
			{
				ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().Compile,
					NAME_None,
					TAttribute<FText>(),
					TAttribute<FText>(Toolkit, &FNiagaraSystemToolkit::GetCompileStatusTooltip),
					TAttribute<FSlateIcon>(Toolkit, &FNiagaraSystemToolkit::GetCompileStatusImage),
					FName(TEXT("CompileNiagaraSystem")));
				ToolbarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateRaw(Toolkit, &FNiagaraSystemToolkit::GenerateCompileMenuContent),
					LOCTEXT("BuildCombo_Label", "Auto-Compile Options"),
					LOCTEXT("BuildComboToolTip", "Auto-Compile options menu"),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "LevelEditor.Build"),
					true);
			}
			ToolbarBuilder.EndSection();

			ToolbarBuilder.BeginSection("NiagaraThumbnail");
			{
				ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().SaveThumbnailImage, NAME_None,
					LOCTEXT("GenerateThumbnail", "Thumbnail"),
					LOCTEXT("GenerateThumbnailTooltip","Generate a thumbnail image."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "Cascade.SaveThumbnailImage"));
			}
			ToolbarBuilder.EndSection();

			ToolbarBuilder.BeginSection("NiagaraPreviewOptions");
			{
				ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().ToggleBounds, NAME_None,
					LOCTEXT("ShowBounds", "Bounds"),
					LOCTEXT("ShowBoundsTooltip", "Show the bounds for the scene."),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "Cascade.ToggleBounds"));
				ToolbarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateRaw(Toolkit, &FNiagaraSystemToolkit::GenerateBoundsMenuContent, Toolkit->GetToolkitCommands()),
					LOCTEXT("BoundsMenuCombo_Label", "Bounds Options"),
					LOCTEXT("BoundsMenuCombo_ToolTip", "Bounds options"),
					FSlateIcon(FEditorStyle::GetStyleSetName(), "Cascade.ToggleBounds"),
					true
				);
			}
			ToolbarBuilder.EndSection();
			
#if STATS
			ToolbarBuilder.BeginSection("NiagaraStatisticsOptions");
			{
				ToolbarBuilder.AddToolBarButton(FNiagaraEditorCommands::Get().ToggleStatPerformance, NAME_None,
                    LOCTEXT("NiagaraShowPerformance", "Performance"),
                    LOCTEXT("NiagaraShowPerformanceTooltip", "Show runtime performance for particle scripts."),
                    FSlateIcon(FEditorStyle::GetStyleSetName(), "MaterialEditor.ToggleMaterialStats"));
				ToolbarBuilder.AddComboButton(
                    FUIAction(),
                    FOnGetContent::CreateRaw(Toolkit, &FNiagaraSystemToolkit::GenerateStatConfigMenuContent, Toolkit->GetToolkitCommands()),
                    FText(),
                    LOCTEXT("NiagaraShowPerformanceCombo_ToolTip", "Runtime performance options"),
                    FSlateIcon(FEditorStyle::GetStyleSetName(), "MaterialEditor.ToggleMaterialStats"),
                    true);
				ToolbarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateStatic(Local::FillDebugOptionsMenu, Toolkit),
					LOCTEXT("DebugOptions", "Debug"),
					LOCTEXT("DebugOptionsTooltip", "Debug options"),
					FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.DebugOptions")
				);
			}
			ToolbarBuilder.EndSection();
#endif
			
			ToolbarBuilder.BeginSection("PlaybackOptions");
			{
				ToolbarBuilder.AddComboButton(
					FUIAction(),
					FOnGetContent::CreateStatic(Local::FillSimulationOptionsMenu, Toolkit),
					LOCTEXT("SimulationOptions", "Simulation"),
					LOCTEXT("SimulationOptionsTooltip", "Simulation options"),
					FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.SimulationOptions")
				);
			}
			ToolbarBuilder.EndSection();

			if ( GetDefault<UNiagaraEditorSettings>()->bEnableBaker )
			{
				ToolbarBuilder.BeginSection("Baker");
				{
					ToolbarBuilder.AddComboButton(
						FUIAction(),
						FOnGetContent::CreateStatic(Local::GenerateBakerMenu, Toolkit),
						LOCTEXT("Baker", "Baker"),
						LOCTEXT("BakerTooltip", "Options for Baker rendering."),
						FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "NiagaraEditor.Baker")
					);
				}
				ToolbarBuilder.EndSection();
			}
		}
	};

	TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);

	ToolbarExtender->AddToolBarExtension(
		"Asset",
		EExtensionHook::After,
		GetToolkitCommands(),
		FToolBarExtensionDelegate::CreateStatic(&Local::FillToolbar, this)
		);

	AddToolbarExtender(ToolbarExtender);

	FNiagaraEditorModule& NiagaraEditorModule = FModuleManager::LoadModuleChecked<FNiagaraEditorModule>("NiagaraEditor");
	AddToolbarExtender(NiagaraEditorModule.GetToolBarExtensibilityManager()->GetAllExtenders(GetToolkitCommands(), GetEditingObjects()));
}

TSharedRef<SWidget> FNiagaraSystemToolkit::GenerateBoundsMenuContent(TSharedRef<FUICommandList> InCommandList)
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, InCommandList);

	if(SystemToolkitMode == ESystemToolkitMode::System)
	{
		MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleBounds_SetFixedBounds_System);		
	}
	
	MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleBounds_SetFixedBounds_SelectedEmitters);

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FNiagaraSystemToolkit::GenerateStatConfigMenuContent(TSharedRef<FUICommandList> InCommandList)
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, InCommandList);

	MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ClearStatPerformance);
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleStatPerformanceGPU);
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleStatPerformanceTypeAvg);
	MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleStatPerformanceTypeMax);
	MenuBuilder.AddMenuSeparator();
	MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleStatPerformanceModePercent);
	MenuBuilder.AddMenuEntry(FNiagaraEditorCommands::Get().ToggleStatPerformanceModeAbsolute);

	return MenuBuilder.MakeWidget();
}

const FName FNiagaraSystemToolkit::GetNiagaraSystemMessageLogName(UNiagaraSystem* InSystem) const
{
	checkf(InSystem, TEXT("Tried to get MessageLog name for NiagaraSystem but InSystem was null!"));
	FName LogListingName = *FString::Printf(TEXT("%s_%s_MessageLog"), *FString::FromInt(InSystem->GetUniqueID()), *InSystem->GetName());
	return LogListingName;
}

void FNiagaraSystemToolkit::GetSequencerAddMenuContent(FMenuBuilder& MenuBuilder, TSharedRef<ISequencer> Sequencer)
{
	MenuBuilder.AddSubMenu(
		LOCTEXT("EmittersLabel", "Emitters..."),
		LOCTEXT("EmittersToolTip", "Add an existing emitter..."),
		FNewMenuDelegate::CreateLambda([&](FMenuBuilder& InMenuBuilder)
		{
			InMenuBuilder.AddWidget(CreateAddEmitterMenuContent(), FText());
		}));
}

TSharedRef<SWidget> FNiagaraSystemToolkit::CreateAddEmitterMenuContent()
{
	TArray<FRefreshItemSelectorDelegate*> RefreshItemSelectorDelegates;
	RefreshItemSelectorDelegates.Add(&RefreshItemSelector);
	FNiagaraAssetPickerListViewOptions ViewOptions;
	ViewOptions.SetCategorizeUserDefinedCategory(true);
	ViewOptions.SetCategorizeLibraryAssets(true);
	ViewOptions.SetAddLibraryOnlyCheckbox(true);

	SNiagaraTemplateTabBox::FNiagaraTemplateTabOptions TabOptions;
	TabOptions.ChangeTabState(ENiagaraScriptTemplateSpecification::Template, true);
	TabOptions.ChangeTabState(ENiagaraScriptTemplateSpecification::None, true);
	TabOptions.ChangeTabState(ENiagaraScriptTemplateSpecification::Behavior, true);

	return SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBox)
			.WidthOverride(450.f)
			.HeightOverride(500.f)
			[
				SNew(SNiagaraAssetPickerList, UNiagaraEmitter::StaticClass())
				.ClickActivateMode(EItemSelectorClickActivateMode::SingleClick)
				.ViewOptions(ViewOptions)
				.TabOptions(TabOptions)
				.RefreshItemSelectorDelegates(RefreshItemSelectorDelegates)
				.OnTemplateAssetActivated(this, &FNiagaraSystemToolkit::EmitterAssetSelected)
			]
		];
}

TSharedRef<SWidget> FNiagaraSystemToolkit::GenerateCompileMenuContent()
{
	const bool bShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bShouldCloseWindowAfterMenuSelection, nullptr);

	FUIAction Action(
		FExecuteAction::CreateStatic(&FNiagaraSystemToolkit::ToggleCompileEnabled),
		FCanExecuteAction(),
		FIsActionChecked::CreateStatic(&FNiagaraSystemToolkit::IsAutoCompileEnabled));

	FUIAction FullRebuildAction(
		FExecuteAction::CreateRaw(this, &FNiagaraSystemToolkit::CompileSystem, true));

	MenuBuilder.AddMenuEntry(LOCTEXT("FullRebuild", "Full Rebuild"),
		LOCTEXT("FullRebuildTooltip", "Triggers a full rebuild of this system, ignoring the change tracking."),
		FSlateIcon(FNiagaraEditorStyle::GetStyleSetName(), "Niagara.CompileStatus.Unknown"),
		FullRebuildAction, NAME_None, EUserInterfaceActionType::Button);
	MenuBuilder.AddMenuEntry(LOCTEXT("AutoCompile", "Automatically compile when graph changes"),
		FText(), FSlateIcon(), Action, NAME_None, EUserInterfaceActionType::ToggleButton);

	return MenuBuilder.MakeWidget();
}

FSlateIcon FNiagaraSystemToolkit::GetCompileStatusImage() const
{
	ENiagaraScriptCompileStatus Status = SystemViewModel->GetLatestCompileStatus();

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

FText FNiagaraSystemToolkit::GetCompileStatusTooltip() const
{
	ENiagaraScriptCompileStatus Status = SystemViewModel->GetLatestCompileStatus();
	return FNiagaraEditorUtilities::StatusToText(Status);
}


void FNiagaraSystemToolkit::CompileSystem(bool bFullRebuild)
{
	SystemViewModel->CompileSystem(bFullRebuild);
}

TSharedPtr<FNiagaraSystemViewModel> FNiagaraSystemToolkit::GetSystemViewModel()
{
	return SystemViewModel;
}

void FNiagaraSystemToolkit::OnToggleBounds()
{
	ToggleDrawOption(SNiagaraSystemViewport::Bounds);
}

bool FNiagaraSystemToolkit::IsToggleBoundsChecked() const
{
	return IsDrawOptionEnabled(SNiagaraSystemViewport::Bounds);
}

void FNiagaraSystemToolkit::ToggleDrawOption(int32 Element)
{
	if (Viewport.IsValid() && Viewport->GetViewportClient().IsValid())
	{
		Viewport->ToggleDrawElement((SNiagaraSystemViewport::EDrawElements)Element);
		Viewport->RefreshViewport();
	}
}

bool FNiagaraSystemToolkit::IsDrawOptionEnabled(int32 Element) const
{
	if (Viewport.IsValid() && Viewport->GetViewportClient().IsValid())
	{
		return Viewport->GetDrawElement((SNiagaraSystemViewport::EDrawElements)Element);
	}
	else
	{
		return false;
	}
}

void FNiagaraSystemToolkit::OpenDebugHUD()
{
#if WITH_NIAGARA_DEBUGGER
	TSharedPtr<SDockTab> DebugTab = FGlobalTabmanager::Get()->TryInvokeTab(SNiagaraDebugger::DebugWindowName);

	if (DebugTab.IsValid())
	{
		TSharedRef<SNiagaraDebugger> Content = StaticCastSharedRef<SNiagaraDebugger>(DebugTab->GetContent());
		Content->FocusDebugTab();
	}
#endif
}

void FNiagaraSystemToolkit::OpenDebugOutliner()
{
#if WITH_NIAGARA_DEBUGGER
	TSharedPtr<SDockTab> DebugTab = FGlobalTabmanager::Get()->TryInvokeTab(SNiagaraDebugger::DebugWindowName);

	if (DebugTab.IsValid())
	{
		TSharedRef<SNiagaraDebugger> Content = StaticCastSharedRef<SNiagaraDebugger>(DebugTab->GetContent());
		Content->FocusOutlineTab();
	}
#endif
}

void FNiagaraSystemToolkit::OpenAttributeSpreadsheet()
{
	InvokeTab(DebugSpreadsheetTabID);
}

void FNiagaraSystemToolkit::OnToggleBoundsSetFixedBounds_Emitters()
{
	FScopedTransaction Transaction(LOCTEXT("SetFixedBoundsEmitters", "Set Fixed Bounds (Emitters)"));

	SystemViewModel->UpdateEmitterFixedBounds();
}

void FNiagaraSystemToolkit::OnToggleBoundsSetFixedBounds_System()
{
	FScopedTransaction Transaction(LOCTEXT("SetFixedBoundsSystem", "Set Fixed Bounds (System)"));

	SystemViewModel->UpdateSystemFixedBounds();
}

void FNiagaraSystemToolkit::ClearStatPerformance()
{
#if STATS
	SystemViewModel->GetSystem().GetStatData().ClearStatCaptures();
	SystemViewModel->ClearEmitterStats();
#endif
}

void FNiagaraSystemToolkit::ToggleStatPerformance()
{
	bool IsEnabled = IsStatPerformanceChecked();
	if (VmStatEnabledVar)
	{
		VmStatEnabledVar->Set(!IsEnabled);
	}
	if (IsStatPerformanceGPUChecked() == IsEnabled)
	{
		ToggleStatPerformanceGPU();
	}
}

void FNiagaraSystemToolkit::ToggleStatPerformanceTypeAvg()
{
	SystemViewModel->StatEvaluationType = ENiagaraStatEvaluationType::Average;
}

void FNiagaraSystemToolkit::ToggleStatPerformanceTypeMax()
{
	SystemViewModel->StatEvaluationType = ENiagaraStatEvaluationType::Maximum;
}

bool FNiagaraSystemToolkit::IsStatPerformanceTypeAvg()
{
	return SystemViewModel->StatEvaluationType == ENiagaraStatEvaluationType::Average;
}

bool FNiagaraSystemToolkit::IsStatPerformanceTypeMax()
{
	return SystemViewModel->StatEvaluationType == ENiagaraStatEvaluationType::Maximum;
}

void FNiagaraSystemToolkit::ToggleStatPerformanceModePercent()
{
	SystemViewModel->StatDisplayMode = ENiagaraStatDisplayMode::Percent;
}

void FNiagaraSystemToolkit::ToggleStatPerformanceModeAbsolute()
{
	SystemViewModel->StatDisplayMode = ENiagaraStatDisplayMode::Absolute;
}

bool FNiagaraSystemToolkit::IsStatPerformanceModePercent()
{
	return SystemViewModel->StatDisplayMode == ENiagaraStatDisplayMode::Percent;
}

bool FNiagaraSystemToolkit::IsStatPerformanceModeAbsolute()
{
	return SystemViewModel->StatDisplayMode == ENiagaraStatDisplayMode::Absolute;
}

bool FNiagaraSystemToolkit::IsStatPerformanceChecked()
{
	return VmStatEnabledVar ? VmStatEnabledVar->GetBool() : false;
}

void FNiagaraSystemToolkit::ToggleStatPerformanceGPU()
{
	if (GpuStatEnabledVar)
	{
		GpuStatEnabledVar->Set(!IsStatPerformanceGPUChecked());
	}
}

bool FNiagaraSystemToolkit::IsStatPerformanceGPUChecked()
{
	return GpuStatEnabledVar ? GpuStatEnabledVar->GetBool() : false;
}

void FNiagaraSystemToolkit::UpdateOriginalEmitter()
{
	checkf(SystemToolkitMode == ESystemToolkitMode::Emitter, TEXT("There is no original emitter to update in system mode."));

	TSharedPtr<FNiagaraEmitterViewModel> EditableEmitterViewModel = SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterViewModel();
	UNiagaraEmitter* EditableEmitter = EditableEmitterViewModel->GetEmitter();

	if (EditableEmitter->GetChangeId() != LastSyncedEmitterChangeId)
	{
		const FScopedBusyCursor BusyCursor;
		const FText LocalizedScriptEditorApply = NSLOCTEXT("UnrealEd", "ToolTip_NiagaraEmitterEditorApply", "Apply changes to original emitter and its use in the world.");
		GWarn->BeginSlowTask(LocalizedScriptEditorApply, true);
		GWarn->StatusUpdate(1, 1, LocalizedScriptEditorApply);

		if (Emitter->IsSelected())
		{
			GEditor->GetSelectedObjects()->Deselect(Emitter);
		}

		ResetLoaders(Emitter->GetOutermost()); // Make sure that we're not going to get invalid version number linkers into the package we are going into. 
		Emitter->GetOutermost()->LinkerCustomVersion.Empty();

		TArray<UNiagaraScript*> AllScripts;
		EditableEmitter->GetScripts(AllScripts, true);
		for (UNiagaraScript* Script : AllScripts)
		{
			checkfSlow(Script->AreScriptAndSourceSynchronized(), TEXT("Editable Emitter Script change ID is out of date when applying to Original Emitter!"));
		}
		Emitter->PreEditChange(nullptr);
		// overwrite the original script in place by constructing a new one with the same name
		Emitter = (UNiagaraEmitter*)StaticDuplicateObject(EditableEmitter, Emitter->GetOuter(),
			Emitter->GetFName(), RF_AllFlags, Emitter->GetClass());

		// Restore RF_Standalone and RF_Public on the original emitter, as it had been removed from the preview emitter so that it could be GC'd.
		Emitter->SetFlags(RF_Standalone | RF_Public);

		Emitter->PostEditChange();

		TArray<UNiagaraScript*> EmitterScripts;
		Emitter->GetScripts(EmitterScripts, false);

		TArray<UNiagaraScript*> EditableEmitterScripts;
		EditableEmitter->GetScripts(EditableEmitterScripts, false);

		// Validate that the change ids on the original emitters match the editable emitters ids to ensure the DDC contents are up to data without having to recompile.
		if (ensureMsgf(EmitterScripts.Num() == EditableEmitterScripts.Num(), TEXT("Script count mismatch after copying from editable emitter to original emitter.")))
		{
			for (UNiagaraScript* EmitterScript : EmitterScripts)
			{
				UNiagaraScript** MatchingEditableEmitterScriptPtr = EditableEmitterScripts.FindByPredicate([EmitterScript](UNiagaraScript* EditableEmitterScript) { 
					return EditableEmitterScript->GetUsage() == EmitterScript->GetUsage() && EditableEmitterScript->GetUsageId() == EmitterScript->GetUsageId(); });
				if (ensureMsgf(MatchingEditableEmitterScriptPtr != nullptr, TEXT("Matching script could not be found in editable emitter after copying to original emitter.")))
				{
					ensureMsgf((*MatchingEditableEmitterScriptPtr)->GetBaseChangeID() == EmitterScript->GetBaseChangeID(), TEXT("Script change ids didn't match after copying from editable emitter to original emitter."));
				}
			}
		}

		// Record the last synced change id to detect future changes.
		LastSyncedEmitterChangeId = EditableEmitter->GetChangeId();
		bEmitterThumbnailUpdated = false;

		UpdateExistingEmitters();
		GWarn->EndSlowTask();
	}
	else if(bEmitterThumbnailUpdated)
	{
		Emitter->MarkPackageDirty();
		Emitter->ThumbnailImage = (UTexture2D*)StaticDuplicateObject(EditableEmitter->ThumbnailImage, Emitter);
		Emitter->PostEditChange();
		bEmitterThumbnailUpdated = false;
	}
}

void MergeEmittersRecursively(UNiagaraEmitter* ChangedEmitter, const TMap<UNiagaraEmitter*, TArray<UNiagaraEmitter*>>& EmitterToReferencingEmittersMap, TSet<UNiagaraEmitter*>& OutMergedEmitters)
{
	const TArray<UNiagaraEmitter*>* ReferencingEmitters = EmitterToReferencingEmittersMap.Find(ChangedEmitter);
	if (ReferencingEmitters != nullptr)
	{
		for (UNiagaraEmitter* ReferencingEmitter : (*ReferencingEmitters))
		{
			if (ReferencingEmitter->IsSynchronizedWithParent() == false)
			{
				ReferencingEmitter->MergeChangesFromParent();
				OutMergedEmitters.Add(ReferencingEmitter);
				MergeEmittersRecursively(ReferencingEmitter, EmitterToReferencingEmittersMap, OutMergedEmitters);
			}
		}
	}
}

void FNiagaraSystemToolkit::UpdateExistingEmitters()
{
	// Build a tree of references from the currently loaded emitters so that we can efficiently find all emitters that reference the modified emitter.
	TMap<UNiagaraEmitter*, TArray<UNiagaraEmitter*>> EmitterToReferencingEmittersMap;
	UNiagaraEmitter* EditableCopy = System->GetEmitterHandles()[0].GetInstance();
	for (TObjectIterator<UNiagaraEmitter> EmitterIterator; EmitterIterator; ++EmitterIterator)
	{
		UNiagaraEmitter* LoadedEmitter = *EmitterIterator;
		if (LoadedEmitter != EditableCopy && LoadedEmitter->GetParent() != nullptr)
		{
			TArray<UNiagaraEmitter*>& ReferencingEmitters = EmitterToReferencingEmittersMap.FindOrAdd(LoadedEmitter->GetParent());
			ReferencingEmitters.Add(LoadedEmitter);
		}
	}

	// Recursively merge emitters by traversing the reference chains.
	TSet<UNiagaraEmitter*> MergedEmitters;
	MergeEmittersRecursively(Emitter, EmitterToReferencingEmittersMap, MergedEmitters);

	// find referencing systems, aside from the system being edited by this toolkit and request that they recompile,
	// also refresh their view models, and reinitialize their components.
	for (TObjectIterator<UNiagaraSystem> SystemIterator; SystemIterator; ++SystemIterator)
	{
		UNiagaraSystem* LoadedSystem = *SystemIterator;
		if (LoadedSystem != System &&
			LoadedSystem->IsPendingKill() == false && 
			LoadedSystem->HasAnyFlags(RF_ClassDefaultObject) == false)
		{
			bool bUsesMergedEmitterDirectly = false;
			for (const FNiagaraEmitterHandle& EmitterHandle : LoadedSystem->GetEmitterHandles())
			{
				if (MergedEmitters.Contains(EmitterHandle.GetInstance()))
				{
					bUsesMergedEmitterDirectly = true;
					break;
				}
			}

			if (bUsesMergedEmitterDirectly)
			{
				// Request that the system recompile.
				bool bForce = false;
				LoadedSystem->RequestCompile(bForce);

				// Invalidate any view models.
				TArray<TSharedPtr<FNiagaraSystemViewModel>> ReferencingSystemViewModels;
				FNiagaraSystemViewModel::GetAllViewModelsForObject(LoadedSystem, ReferencingSystemViewModels);
				for (TSharedPtr<FNiagaraSystemViewModel> ReferencingSystemViewModel : ReferencingSystemViewModels)
				{
					ReferencingSystemViewModel->RefreshAll();
				}

				// Reinit any running components
				for (TObjectIterator<UNiagaraComponent> ComponentIterator; ComponentIterator; ++ComponentIterator)
				{
					UNiagaraComponent* Component = *ComponentIterator;
					if (Component->GetAsset() == LoadedSystem)
					{
						Component->ReinitializeSystem();
					}
				}
			}
		}
	}
}

void FNiagaraSystemToolkit::GetSaveableObjects(TArray<UObject*>& OutObjects) const
{
	if (SystemToolkitMode == ESystemToolkitMode::Emitter)
	{
		OutObjects.Add(Emitter);
	}
	else
	{
		FAssetEditorToolkit::GetSaveableObjects(OutObjects);
	}
}

void FNiagaraSystemToolkit::SaveAsset_Execute()
{
	if (SystemToolkitMode == ESystemToolkitMode::Emitter)
	{
		UE_LOG(LogNiagaraEditor, Log, TEXT("Saving and Compiling NiagaraEmitter %s"), *GetEditingObjects()[0]->GetName());
		UpdateOriginalEmitter();
	}
	SystemViewModel->NotifyPreSave();
	FAssetEditorToolkit::SaveAsset_Execute();
}

void FNiagaraSystemToolkit::SaveAssetAs_Execute()
{
	if (SystemToolkitMode == ESystemToolkitMode::Emitter)
	{
		UE_LOG(LogNiagaraEditor, Log, TEXT("Saving and Compiling NiagaraEmitter %s"), *GetEditingObjects()[0]->GetName());
		UpdateOriginalEmitter();
	}
	SystemViewModel->NotifyPreSave();
	FAssetEditorToolkit::SaveAssetAs_Execute();
}

bool FNiagaraSystemToolkit::OnRequestClose()
{
	if (GbLogNiagaraSystemChanges > 0)
	{
		FString ExportText;
		SystemViewModel->DumpToText(ExportText);
		FString FilePath;

		if (SystemToolkitMode == ESystemToolkitMode::System)
		{
			FilePath = System->GetOutermost()->FileName.ToString();
		}
		else if (SystemToolkitMode == ESystemToolkitMode::Emitter)
		{
			FilePath = Emitter->GetOutermost()->FileName.ToString();
		}

		FString PathPart, FilenamePart, ExtensionPart;
		FPaths::Split(FilePath, PathPart, FilenamePart, ExtensionPart);

		FNiagaraEditorUtilities::WriteTextFileToDisk(FPaths::ProjectLogDir(), FilenamePart + TEXT(".onClose.txt"), ExportText, true);
	}

	SystemViewModel->NotifyPreClose();

	bool bHasUnappliedScratchPadChanges = false;
	for (TSharedRef<FNiagaraScratchPadScriptViewModel> ScratchPadViewModel : SystemViewModel->GetScriptScratchPadViewModel()->GetScriptViewModels())
	{
		if (ScratchPadViewModel->HasUnappliedChanges())
		{
			bHasUnappliedScratchPadChanges = true;
			break;
		}
	}

	if (bScratchPadChangesDiscarded == false && bHasUnappliedScratchPadChanges)
	{
		// find out the user wants to do with their dirty scratch pad scripts.
		EAppReturnType::Type YesNoCancelReply = FMessageDialog::Open(EAppMsgType::YesNoCancel,
			NSLOCTEXT("NiagaraEditor", "UnsavedScratchPadScriptsPrompt", "Would you like to apply changes to scratch pad scripts? (No will discard unapplied changes)"));

		switch (YesNoCancelReply)
		{
		case EAppReturnType::Yes:
			for (TSharedRef<FNiagaraScratchPadScriptViewModel> ScratchPadViewModel : SystemViewModel->GetScriptScratchPadViewModel()->GetScriptViewModels())
			{
				ScratchPadViewModel->ApplyChanges();
			}
			break;
		case EAppReturnType::No:
			bScratchPadChangesDiscarded = true;
			break;
		case EAppReturnType::Cancel:
			return false;
			break;
		}
	}

	if (SystemToolkitMode == ESystemToolkitMode::Emitter)
	{
		TSharedPtr<FNiagaraEmitterViewModel> EmitterViewModel = SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterViewModel();
		if (bChangesDiscarded == false && (EmitterViewModel->GetEmitter()->GetChangeId() != LastSyncedEmitterChangeId || bEmitterThumbnailUpdated))
		{
			// find out the user wants to do with this dirty emitter.
			EAppReturnType::Type YesNoCancelReply = FMessageDialog::Open(EAppMsgType::YesNoCancel,
				FText::Format(
					NSLOCTEXT("UnrealEd", "Prompt_NiagaraEmitterEditorClose", "Would you like to apply changes to this Emitter to the original Emitter?\n{0}\n(No will lose all changes!)"),
					FText::FromString(Emitter->GetPathName())));

			// act on it
			switch (YesNoCancelReply)
			{
			case EAppReturnType::Yes:
				// update NiagaraScript and exit
				UpdateOriginalEmitter();
				break;
			case EAppReturnType::No:
				// Set the changes discarded to avoid showing the dialog multiple times when request close is called multiple times on shut down.
				bChangesDiscarded = true;
				break;
			case EAppReturnType::Cancel:
				// don't exit
				bScratchPadChangesDiscarded = false;
				return false;
			}
		}
		GEngine->ForceGarbageCollection(true);
		return true;
	}
	
	GEngine->ForceGarbageCollection(true);
	return FAssetEditorToolkit::OnRequestClose();
}

void FNiagaraSystemToolkit::EmitterAssetSelected(const FAssetData& AssetData)
{
	FSlateApplication::Get().DismissAllMenus();
	SystemViewModel->AddEmitterFromAssetData(AssetData);
}

void FNiagaraSystemToolkit::ToggleCompileEnabled()
{
	UNiagaraEditorSettings* Settings = GetMutableDefault<UNiagaraEditorSettings>();
	Settings->SetAutoCompile(!Settings->GetAutoCompile());
}

bool FNiagaraSystemToolkit::IsAutoCompileEnabled()
{
	return GetDefault<UNiagaraEditorSettings>()->GetAutoCompile();
}

void FNiagaraSystemToolkit::OnApply()
{
	SCOPE_CYCLE_COUNTER(STAT_NiagaraEditor_SystemToolkit_OnApply);
	UpdateOriginalEmitter();
}

bool FNiagaraSystemToolkit::OnApplyEnabled() const
{
	if (Emitter != nullptr)
	{
		TSharedPtr<FNiagaraEmitterViewModel> EmitterViewModel = SystemViewModel->GetEmitterHandleViewModels()[0]->GetEmitterViewModel();
		return EmitterViewModel->GetEmitter()->GetChangeId() != LastSyncedEmitterChangeId || bEmitterThumbnailUpdated;
	}
	return false;
}

void FNiagaraSystemToolkit::OnApplyScratchPadChanges()
{
	if (SystemViewModel.IsValid() && SystemViewModel->GetScriptScratchPadViewModel() != nullptr)
	{
		for (TSharedRef<FNiagaraScratchPadScriptViewModel> ScratchPadScriptViewModel : SystemViewModel->GetScriptScratchPadViewModel()->GetScriptViewModels())
		{
			if(ScratchPadScriptViewModel->HasUnappliedChanges())
			{
				ScratchPadScriptViewModel->ApplyChanges();
			}
		}
	}
}

bool FNiagaraSystemToolkit::OnApplyScratchPadChangesEnabled() const
{
	return SystemViewModel.IsValid() && SystemViewModel->GetScriptScratchPadViewModel() != nullptr && SystemViewModel->GetScriptScratchPadViewModel()->HasUnappliedChanges();
}

void FNiagaraSystemToolkit::OnPinnedCurvesChanged()
{
	TabManager->TryInvokeTab(CurveEditorTabID);
}

void FNiagaraSystemToolkit::RefreshParameters()
{
	TArray<UObject*> NewParameterViewSelection;

	// Always display the system parameters
	NewParameterViewSelection.Add(&SystemViewModel->GetSystem());

	TArray<TSharedRef<FNiagaraEmitterHandleViewModel>> EmitterHandlesToDisplay;
	EmitterHandlesToDisplay.Append(SystemViewModel->GetPinnedEmitterHandles());
		
	TArray<FGuid> SelectedEmitterHandleIds = SystemViewModel->GetSelectionViewModel()->GetSelectedEmitterHandleIds();
	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleViewModel : SystemViewModel->GetEmitterHandleViewModels())
	{
		if (SelectedEmitterHandleIds.Contains(EmitterHandleViewModel->GetId()))
		{
			EmitterHandlesToDisplay.AddUnique(EmitterHandleViewModel);
		}
	}

	for (TSharedRef<FNiagaraEmitterHandleViewModel> EmitterHandleToDisplay : EmitterHandlesToDisplay)
	{
		if (EmitterHandleToDisplay->IsValid() && EmitterHandleToDisplay->GetEmitterViewModel()->GetEmitter() != nullptr)
		{
			NewParameterViewSelection.Add(EmitterHandleToDisplay->GetEmitterViewModel()->GetEmitter());
		}
	}

	ObjectSelectionForParameterMapView->SetSelectedObjects(NewParameterViewSelection);
}

void FNiagaraSystemToolkit::OnSystemSelectionChanged()
{
	RefreshParameters();
}

void FNiagaraSystemToolkit::OnViewModelRequestFocusTab(FName TabName)
{
	GetTabManager()->TryInvokeTab(TabName);
}

void FNiagaraSystemToolkit::RenderBaker()
{
	BakerViewModel->RenderBaker();
}

#undef LOCTEXT_NAMESPACE
