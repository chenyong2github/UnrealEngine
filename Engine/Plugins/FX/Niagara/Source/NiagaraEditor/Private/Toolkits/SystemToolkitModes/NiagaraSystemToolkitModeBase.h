// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraBakerViewModel.h"
#include "WorkflowOrientedApp/ApplicationMode.h"
#include "Widgets/Docking/SDockTab.h"

class FNiagaraSystemToolkit;

class FNiagaraSystemToolkitModeBase : public FApplicationMode
{
public:
	FNiagaraSystemToolkitModeBase(FName InModeName, TWeakPtr<FNiagaraSystemToolkit> InSystemToolkit) : FApplicationMode(InModeName), SystemToolkit(InSystemToolkit)
	{ }
	
	virtual void RegisterTabFactories(TSharedPtr<FTabManager> InTabManager) override;

private:
	TSharedRef<SDockTab> SpawnTab_Viewport(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_CurveEd(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Sequencer(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SystemScript(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SystemParameters(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SystemParameterDefinitions(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SelectedEmitterStack(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SelectedEmitterGraph(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_DebugSpreadsheet(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_PreviewSettings(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_GeneratedCode(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_MessageLog(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_SystemOverview(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ScratchPad(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_ScriptStats(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Baker(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_Versioning(const FSpawnTabArgs& Args);

protected:
	TWeakPtr<FNiagaraSystemToolkit> SystemToolkit;

public:
	static const FName ViewportTabID;
	static const FName CurveEditorTabID;
	static const FName SequencerTabID;
	static const FName SystemScriptTabID;
	static const FName SystemDetailsTabID;
	static const FName SystemParametersTabID;
	static const FName SystemParameterDefinitionsTabID;
	static const FName SelectedEmitterStackTabID;
	static const FName SelectedEmitterGraphTabID;
	static const FName DebugSpreadsheetTabID;
	static const FName PreviewSettingsTabId;
	static const FName GeneratedCodeTabID;
	static const FName MessageLogTabID;
	static const FName SystemOverviewTabID;
	static const FName ScratchPadTabID;
	static const FName ScriptStatsTabID;
	static const FName BakerTabID;
	static const FName VersioningTabID;
};

