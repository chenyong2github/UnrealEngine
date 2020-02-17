// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "ViewModels/NiagaraScriptViewModel.h"

class NIAGARAEDITOR_API FNiagaraScratchPadScriptViewModel : public FNiagaraScriptViewModel
{
public:
	DECLARE_MULTICAST_DELEGATE(FOnRenamed);

public:
	FNiagaraScratchPadScriptViewModel();

	void Initialize(UNiagaraScript* Script);

	UNiagaraScript* GetScript() const;

	bool GetIsPendingRename() const;

	void SetIsPendingRename(bool bInIsPendingRename);

	void SetScriptName(FText InScriptName);

	FOnRenamed& OnRenamed();

private:
	FText GetDisplayNameInternal() const;

private:
	bool bIsPendingRename;
	FOnRenamed OnRenamedDelegate;
};