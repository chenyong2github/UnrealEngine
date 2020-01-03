// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraStandaloneScriptViewModel.h"
#include "NiagaraMessageManager.h"

FNiagaraStandaloneScriptViewModel::FNiagaraStandaloneScriptViewModel(
	FText DisplayName,
	ENiagaraParameterEditMode InParameterEditMode,
	TSharedPtr<FNiagaraMessageLogViewModel> InNiagaraMessageLogViewModel,
	const FGuid& InSourceScriptObjKey
)
	: FNiagaraScriptViewModel(DisplayName, InParameterEditMode)
	, NiagaraMessageLogViewModel(InNiagaraMessageLogViewModel)
	, ScriptMessageLogGuidKey(InSourceScriptObjKey)
{
}

void FNiagaraStandaloneScriptViewModel::Initialize(UNiagaraScript* InScript, UNiagaraScript* InSourceScript)
{
	SetScript(InScript);
	SourceScript = InSourceScript;
	SendLastCompileMessageJobs(SourceScript);
}

void FNiagaraStandaloneScriptViewModel::OnVMScriptCompiled(UNiagaraScript* InScript)
{
	FNiagaraScriptViewModel::OnVMScriptCompiled(InScript);
	SendLastCompileMessageJobs(InScript);
}

void FNiagaraStandaloneScriptViewModel::SendLastCompileMessageJobs(const UNiagaraScript* InScript)
{
	int32 ErrorCount = 0;
	int32 WarningCount = 0;

	TArray<TSharedPtr<const INiagaraMessageJob>> JobBatchToQueue;

	const TArray<FNiagaraCompileEvent>& CurrentCompileEvents = InScript->GetVMExecutableData().LastCompileEvents;

	// Iterate from back to front to avoid reordering the events when they are queued
	for (int i = CurrentCompileEvents.Num() - 1; i >= 0; --i)
	{
		const FNiagaraCompileEvent& CompileEvent = CurrentCompileEvents[i];
		if (CompileEvent.Severity == FNiagaraCompileEventSeverity::Error)
		{
			ErrorCount++;
		}
		else if (CompileEvent.Severity == FNiagaraCompileEventSeverity::Warning)
		{
			WarningCount++;
		}

		JobBatchToQueue.Add(MakeShared<FNiagaraMessageJobCompileEvent>(CompileEvent, TWeakObjectPtr<const UNiagaraScript>(InScript), TOptional<const FString>(), SourceScript->GetPathName()));
	}

	JobBatchToQueue.Insert(MakeShared<FNiagaraMessageJobPostCompileSummary>(ErrorCount, WarningCount, GetLatestCompileStatus(), FText::FromString("Script")), 0);
	FNiagaraMessageManager::Get()->RefreshMessagesForAssetKeyAndMessageJobType(ScriptMessageLogGuidKey, ENiagaraMessageJobType::CompileEventMessageJob);
	FNiagaraMessageManager::Get()->QueueMessageJobBatch(JobBatchToQueue, ScriptMessageLogGuidKey);
}
