// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Internationalization/Text.h"
#include "NiagaraParameterEditMode.h"
#include "NiagaraScriptViewModel.h"

class FNiagaraMessageLogViewModel;
class UNiagaraScript;

class FNiagaraStandaloneScriptViewModel : public FNiagaraScriptViewModel
{
public:
	FNiagaraStandaloneScriptViewModel(
		UNiagaraScript* InScript
		, FText DisplayName
		, ENiagaraParameterEditMode InParameterEditMode
		, TSharedPtr<FNiagaraMessageLogViewModel> InNiagaraMessageLogViewModel
		, UNiagaraScript* InSourceScript
		, const FGuid& InMessageLogGuidKey
	);

private:
	virtual void OnVMScriptCompiled(UNiagaraScript* InScript) override;

	/** Sends message jobs to FNiagaraMessageManager for all compile events from the last compile. */
	void SendLastCompileMessageJobs(UNiagaraScript* InScript);

	TSharedPtr<FNiagaraMessageLogViewModel> NiagaraMessageLogViewModel;
	const UNiagaraScript* SourceScript;
	const FGuid ScriptMessageLogGuidKey;
};
