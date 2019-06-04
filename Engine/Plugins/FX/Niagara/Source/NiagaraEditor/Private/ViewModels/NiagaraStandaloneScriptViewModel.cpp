// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "NiagaraStandaloneScriptViewModel.h"
#include "NiagaraMessageManager.h"

FNiagaraStandaloneScriptViewModel::FNiagaraStandaloneScriptViewModel(
	UNiagaraScript* InScript
	, FText DisplayName
	, ENiagaraParameterEditMode InParameterEditMode
	, TSharedPtr<FNiagaraMessageLogViewModel> InNiagaraMessageLogViewModel
	, UNiagaraScript* InSourceScript
	, const FGuid& InSourceScriptObjKey
	)
	: FNiagaraScriptViewModel(InScript, DisplayName, InParameterEditMode)
	, NiagaraMessageLogViewModel(InNiagaraMessageLogViewModel)
	, SourceScript(InSourceScript)
	, ScriptMessageLogGuidKey(InSourceScriptObjKey)
{
	SendLastCompileMessageJobs(InSourceScript);
}

void FNiagaraStandaloneScriptViewModel::OnVMScriptCompiled(UNiagaraScript* InScript)
{
	FNiagaraScriptViewModel::OnVMScriptCompiled(InScript);
	SendLastCompileMessageJobs(InScript);
}

void FNiagaraStandaloneScriptViewModel::SendLastCompileMessageJobs(UNiagaraScript* InScript)
{
	int32 ErrorCount = 0;
	int32 WarningCount = 0;

	TArray<TSharedPtr<const INiagaraMessageJob>> JobBatchToQueue;

	TArray<FNiagaraCompileEvent>& CurrentCompileEvents = InScript->GetVMExecutableData().LastCompileEvents;

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

		JobBatchToQueue.Add(MakeShared<FNiagaraMessageJobCompileEvent>(CompileEvent, TWeakObjectPtr<UNiagaraScript>(InScript), TOptional<const FString>(), SourceScript->GetPathName()));
	}

	JobBatchToQueue.Insert(MakeShared<FNiagaraMessageJobPostCompileSummary>(ErrorCount, WarningCount, GetLatestCompileStatus(), FText::FromString("Script")), 0);
	FNiagaraMessageManager::Get()->RefreshMessagesForAssetKeyAndMessageJobType(ScriptMessageLogGuidKey, ENiagaraMessageJobType::CompileEventMessageJob);
	FNiagaraMessageManager::Get()->QueueMessageJobBatch(JobBatchToQueue, ScriptMessageLogGuidKey);
}
