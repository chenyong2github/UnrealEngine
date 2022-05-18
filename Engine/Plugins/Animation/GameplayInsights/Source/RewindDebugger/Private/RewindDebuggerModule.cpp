// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerModule.h"


#include "Engine/Selection.h"
#include "Features/IModularFeatures.h"
#include "Framework/Docking/LayoutExtender.h"
#include "IAnimationBlueprintEditorModule.h"
#include "Modules/ModuleManager.h"
#include "RewindDebugger.h"
#include "RewindDebuggerStyle.h"
#include "RewindDebuggerCommands.h"
#include "SRewindDebugger.h"
#include "SRewindDebuggerDetails.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"
#include "AnimInstanceHelpers.h"
#include "PropertyTraceMenu.h"
#include "ToolMenus.h"

#define LOCTEXT_NAMESPACE "RewindDebuggerModule"

static const FName RewindDebuggerTabName("RewindDebugger");
static const FName RewindDebuggerDetailsTabName("RewindDebuggerDetails");


TSharedRef<SDockTab> FRewindDebuggerModule::SpawnRewindDebuggerDetailsTab(const FSpawnTabArgs& SpawnTabArgs)
{
	if (FRewindDebugger::Instance() == nullptr)
	{
		FRewindDebugger::Initialize();
	}
	
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	RewindDebuggerDetailsWidget = SNew(SRewindDebuggerDetails, MajorTab, SpawnTabArgs.GetOwnerWindow());

	MajorTab->SetContent(RewindDebuggerDetailsWidget.ToSharedRef());

	return MajorTab;
}

TSharedRef<SDockTab> FRewindDebuggerModule::SpawnRewindDebuggerTab(const FSpawnTabArgs& SpawnTabArgs)
{
	if (FRewindDebugger::Instance() == nullptr)
	{
		FRewindDebugger::Initialize();
	}
	
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		.OnTabClosed_Lambda([this](TSharedRef<SDockTab>)
		{
			// clear reference to widget so it will be destroyed
			RewindDebuggerWidget = nullptr;
		});

	TSharedPtr<FUICommandList> CommandList = MakeShared<FUICommandList>();
	const FRewindDebuggerCommands& Commands = FRewindDebuggerCommands::Get();

	FRewindDebugger* DebuggerInstance = FRewindDebugger::Instance();

	CommandList->MapAction(Commands.Play,
							FExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::Play), 
							FCanExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::CanPlay),
							FIsActionChecked(),
							FIsActionButtonVisible());

	CommandList->MapAction(Commands.Pause,
							FExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::Pause), 
							FCanExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::CanPause),
							FIsActionChecked(),
							FIsActionButtonVisible());

	CommandList->MapAction(Commands.ReversePlay,
							FExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::PlayReverse), 
							FCanExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::CanPlayReverse),
							FIsActionChecked(),
							FIsActionButtonVisible());

	CommandList->MapAction(Commands.PreviousFrame,
							FExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::StepBackward), 
							FCanExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::CanScrub),
							FIsActionChecked(),
							FIsActionButtonVisible());


	CommandList->MapAction(Commands.FirstFrame,
							FExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::ScrubToStart), 
							FCanExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::CanScrub),
							FIsActionChecked(),
							FIsActionButtonVisible());

	CommandList->MapAction(Commands.LastFrame,
							FExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::ScrubToEnd), 
							FCanExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::CanScrub),
							FIsActionChecked(),
							FIsActionButtonVisible());

	CommandList->MapAction(Commands.NextFrame,
							FExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::StepForward), 
							FCanExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::CanScrub),
							FIsActionChecked(),
							FIsActionButtonVisible());

	CommandList->MapAction(Commands.StartRecording, 
							FExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::StartRecording), 
							FCanExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::CanStartRecording),
							FIsActionChecked(),
							FIsActionButtonVisible::CreateLambda([]() { return !FRewindDebugger::Instance()->IsRecording();}));

	CommandList->MapAction(Commands.StopRecording,
							 FExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::StopRecording),
							 FCanExecuteAction::CreateRaw(DebuggerInstance, &FRewindDebugger::CanStopRecording),
							 FIsActionChecked(),
							 FIsActionButtonVisible::CreateRaw(DebuggerInstance, &FRewindDebugger::CanStopRecording));


	RewindDebuggerWidget = SNew(SRewindDebugger, CommandList.ToSharedRef(), MajorTab, SpawnTabArgs.GetOwnerWindow())
								.DebugTargetActor(DebuggerInstance->GetDebugTargetActorProperty())
								.RecordingDuration(DebuggerInstance->GetRecordingDurationProperty())
								.DebugComponents(&DebuggerInstance->GetDebugTracks())
								.TraceTime(DebuggerInstance->GetTraceTimeProperty())
								.OnScrubPositionChanged_Raw(DebuggerInstance,&FRewindDebugger::ScrubToTime)
								.OnViewRangeChanged_Raw(DebuggerInstance,&FRewindDebugger::SetCurrentViewRange)
								.OnComponentDoubleClicked_Raw(DebuggerInstance, &FRewindDebugger::ComponentDoubleClicked)
								.OnComponentSelectionChanged_Raw(DebuggerInstance, &FRewindDebugger::ComponentSelectionChanged)
								.BuildComponentContextMenu_Raw(DebuggerInstance, &FRewindDebugger::BuildComponentContextMenu)
								.ScrubTime_Lambda([]() { return FRewindDebugger::Instance()->GetScrubTime(); });

	DebuggerInstance->OnTrackCursor(FRewindDebugger::FOnTrackCursor::CreateSP(RewindDebuggerWidget.Get(), &SRewindDebugger::TrackCursor));
	DebuggerInstance->OnComponentListChanged(FRewindDebugger::FOnComponentListChanged::CreateSP(RewindDebuggerWidget.Get(), &SRewindDebugger::RefreshDebugComponents));

	MajorTab->SetContent(RewindDebuggerWidget.ToSharedRef());

	return MajorTab;
}

static FAnimInstanceDoubleClickHandler AnimInstanceDoubleClickHandler;

void FRewindDebuggerModule::StartupModule()
{
	UToolMenus::Get()->RegisterMenu("RewindDebugger.MainMenu");
	UToolMenus::Get()->RegisterMenu("RewindDebugger.ComponentContextMenu");

	FRewindDebuggerStyle::Initialize();
	FRewindDebuggerCommands::Register();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		RewindDebuggerTabName, FOnSpawnTab::CreateRaw(this, &FRewindDebuggerModule::SpawnRewindDebuggerTab))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory())
		.SetDisplayName(LOCTEXT("TabTitle", "Rewind Debugger"))
		.SetIcon(FSlateIcon("RewindDebuggerStyle", "RewindDebugger.RewindIcon"))
		.SetTooltipText(LOCTEXT("TooltipText", "Opens Rewind Debugger."));
	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		RewindDebuggerDetailsTabName,
    	FOnSpawnTab::CreateRaw(this, &FRewindDebuggerModule::SpawnRewindDebuggerDetailsTab))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory())
		.SetDisplayName(LOCTEXT("DetailsTabTitle", "Rewind Debugger Details"))
		.SetIcon(FSlateIcon("RewindDebuggerStyle", "RewindDebugger.RewindDetailsIcon"))
		.SetTooltipText(LOCTEXT("TooltipText", "Opens Rewind Debugger Details Window."));

	RewindDebuggerCameraExtension.Initialize();
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, &RewindDebuggerCameraExtension);
	IModularFeatures::Get().RegisterModularFeature(IRewindDebuggerDoubleClickHandler::ModularFeatureName, &AnimInstanceDoubleClickHandler);

	FPropertyTraceMenu::Register();
	FAnimInstanceMenu::Register();
}

void FRewindDebuggerModule::ShutdownModule()
{
	IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerExtension::ModularFeatureName, &RewindDebuggerCameraExtension);
	IModularFeatures::Get().UnregisterModularFeature(IRewindDebuggerDoubleClickHandler::ModularFeatureName, &AnimInstanceDoubleClickHandler);

	FRewindDebuggerCommands::Unregister();
	FRewindDebuggerStyle::Shutdown();
	FRewindDebugger::Shutdown();
}

IMPLEMENT_MODULE(FRewindDebuggerModule, RewindDebugger);

#undef LOCTEXT_NAMESPACE
