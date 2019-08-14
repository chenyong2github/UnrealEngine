// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraScriptViewModel.h"

class FNiagaraSystemViewModel;
class FNiagaraScriptViewModel;
class UNiagaraSystem;

/** View model which manages the System script. */
class FNiagaraSystemScriptViewModel : public FNiagaraScriptViewModel
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnSystemCompiled)

public:
	FNiagaraSystemScriptViewModel();

	void Initialize(UNiagaraSystem& InSystem);

	~FNiagaraSystemScriptViewModel();

	FOnSystemCompiled& OnSystemCompiled();

	void CompileSystem(bool bForce);

private:
	void OnSystemVMCompiled(UNiagaraSystem* InSystem);

private:
	/** The System who's script is getting viewed and edited by this view model. */
	TWeakObjectPtr<UNiagaraSystem> System;

	FOnSystemCompiled OnSystemCompiledDelegate;
};
