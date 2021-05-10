// Copyright Epic Games, Inc. All Rights Reserved.

#include "RewindDebuggerModule.h"
#include "Containers/Ticker.h"
#include "Editor.h"
#include "Engine/Engine.h"
#include "Engine/Selection.h"
#include "Features/IModularFeatures.h"
#include "Framework/Docking/LayoutExtender.h"
#include "IAnimationBlueprintEditorModule.h"
#include "LevelEditorMenuContext.h"
#include "Modules/ModuleManager.h"
#include "RewindDebugger.h"
#include "RewindDebuggerStyle.h"
#include "RewindDebuggerCommands.h"
#include "SRewindDebugger.h"
#include "SSCSEditorMenuContext.h"
#include "SSCSEditor.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "RewindDebuggerModule"

static const FName RewindDebuggerTabName("RewindDebugger");

TSharedRef<SDockTab> FRewindDebuggerModule::SpawnRewindDebuggerTab(const FSpawnTabArgs& SpawnTabArgs)
{
	const TSharedRef<SDockTab> MajorTab = SNew(SDockTab)
		.TabRole(ETabRole::NomadTab);

	TSharedPtr<SWidget> TabContent;

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
								.DebugComponents(&DebuggerInstance->GetDebugComponents())
								.TraceTime(DebuggerInstance->GetTraceTimeProperty())
								.OnScrubPositionChanged(SRewindDebugger::FOnScrubPositionChanged::CreateRaw(DebuggerInstance,&FRewindDebugger::ScrubToTime))
								.ScrubTime_Lambda([]() { return FRewindDebugger::Instance()->GetScrubTime(); });

	DebuggerInstance->OnTrackCursor(FRewindDebugger::FOnTrackCursor::CreateSP(RewindDebuggerWidget.Get(), &SRewindDebugger::TrackCursor));
	DebuggerInstance->OnComponentListChanged(FRewindDebugger::FOnComponentListChanged::CreateSP(RewindDebuggerWidget.Get(), &SRewindDebugger::RefreshDebugComponents));

	MajorTab->SetContent(RewindDebuggerWidget.ToSharedRef());

	return MajorTab;
}

void FRewindDebuggerModule::StartupModule()
{
	FRewindDebugger::Initialize();
	FRewindDebuggerStyle::Initialize();
	FRewindDebuggerCommands::Register();

	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
		RewindDebuggerTabName, FOnSpawnTab::CreateRaw(this, &FRewindDebuggerModule::SpawnRewindDebuggerTab))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetDeveloperToolsDebugCategory())
		.SetDisplayName(LOCTEXT("TabTitle", "Rewind Debugger"))
		.SetIcon(FSlateIcon("RewindDebuggerStyle", "RewindDebugger.RewindIcon"))
		.SetTooltipText(LOCTEXT("TooltipText", "Opens Rewind Debugger."));


	TickerHandle = FTicker::GetCoreTicker().AddTicker(TEXT("RewindDebugger"), 0.0f, [this](float DeltaTime)
	{
		QUICK_SCOPE_CYCLE_COUNTER(STAT_FRewindDebuggerModule_Tick);

		FRewindDebugger::Instance()->Tick(DeltaTime);

		return true;
	});

}

void FRewindDebuggerModule::ShutdownModule()
{
	FTicker::GetCoreTicker().RemoveTicker(TickerHandle);

	FRewindDebuggerCommands::Unregister();
	FRewindDebuggerStyle::Shutdown();
	FRewindDebugger::Shutdown();
}

IMPLEMENT_MODULE(FRewindDebuggerModule, RewindDebugger);

#undef LOCTEXT_NAMESPACE
