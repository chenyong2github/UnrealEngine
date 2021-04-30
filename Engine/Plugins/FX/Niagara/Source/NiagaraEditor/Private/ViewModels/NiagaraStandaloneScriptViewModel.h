// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/NiagaraParameterEditMode.h"
#include "ViewModels/NiagaraScriptViewModel.h"

class FNiagaraMessageLogViewModel;
class UNiagaraScript;

class FNiagaraStandaloneScriptViewModel : public FNiagaraScriptViewModel
{
public:
	FNiagaraStandaloneScriptViewModel(
		FText DisplayName,
		ENiagaraParameterEditMode InParameterEditMode,
		TSharedPtr<FNiagaraMessageLogViewModel> InNiagaraMessageLogViewModel,
		const FGuid& InMessageLogGuidKey
	);

	void Initialize(FVersionedNiagaraScript& InScript, const FVersionedNiagaraScript& InSourceScript);
	virtual FVersionedNiagaraScript GetStandaloneScript() override;

private:
	virtual void OnVMScriptCompiled(UNiagaraScript* InScript, const FGuid& ScriptVersion) override;

	/** Sends messages to FNiagaraMessageManager for all compile events from the last compile. */
	void SendLastCompileMessages(const FVersionedNiagaraScript& InScript);

	TSharedPtr<FNiagaraMessageLogViewModel> NiagaraMessageLogViewModel;
	FVersionedNiagaraScript SourceScript;
	const FGuid ScriptMessageLogGuidKey;
};
