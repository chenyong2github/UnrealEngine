// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraStandaloneScriptViewModel.h"
#include "NiagaraMessageManager.h"
#include "NiagaraMessages.h"
#include "NiagaraMessageUtilities.h"

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
	SendLastCompileMessages(SourceScript);
}

UNiagaraScript* FNiagaraStandaloneScriptViewModel::GetStandaloneScript()
{
	checkf(Scripts.Num() == 1, TEXT("StandaloneScriptViewModel did not have exactly one script!"));
	return Scripts[0].Get();
}

void FNiagaraStandaloneScriptViewModel::OnVMScriptCompiled(UNiagaraScript* InScript)
{
	FNiagaraScriptViewModel::OnVMScriptCompiled(InScript);
	SendLastCompileMessages(InScript);
}

void FNiagaraStandaloneScriptViewModel::SendLastCompileMessages(const UNiagaraScript* InScript)
{
	FNiagaraMessageManager* MessageManager = FNiagaraMessageManager::Get();
	MessageManager->ClearAssetMessagesForTopic(ScriptMessageLogGuidKey, FNiagaraMessageTopics::CompilerTopicName);

	int32 ErrorCount = 0;
	int32 WarningCount = 0;

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

		MessageManager->AddMessageJob(MakeUnique<FNiagaraMessageJobCompileEvent>(CompileEvent, TWeakObjectPtr<const UNiagaraScript>(InScript), TOptional<const FString>(), SourceScript->GetPathName()), ScriptMessageLogGuidKey);
	}

	const FText PostCompileSummaryText = FNiagaraMessageUtilities::MakePostCompileSummaryText(FText::FromString("Script"), GetLatestCompileStatus(), WarningCount, ErrorCount);
	TSharedRef<const FNiagaraMessageText> PostCompileSummaryMessage =  MakeShared<FNiagaraMessageText>(PostCompileSummaryText, EMessageSeverity::Info, FNiagaraMessageTopics::CompilerTopicName);
	MessageManager->AddMessage(PostCompileSummaryMessage, ScriptMessageLogGuidKey);
}
