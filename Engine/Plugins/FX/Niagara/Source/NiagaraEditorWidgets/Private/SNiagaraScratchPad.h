// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "NiagaraCommon.h"
#include "Widgets/SItemSelector.h"
#include "Widgets/SCompoundWidget.h"

class UNiagaraScratchPadViewModel;
class FNiagaraScratchPadScriptViewModel;
class FNiagaraScratchPadCommandContext;

typedef SItemSelector<ENiagaraScriptUsage, TSharedRef<FNiagaraScratchPadScriptViewModel>> SNiagaraScriptViewModelSelector;

class SNiagaraScratchPad : public SCompoundWidget
{
	SLATE_BEGIN_ARGS(SNiagaraScratchPad) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UNiagaraScratchPadViewModel* InViewModel);

private:
	TSharedRef<SWidget> ConstructScriptSelector();

	TSharedRef<SWidget> ConstructScriptEditor();

	TSharedRef<SWidget> ConstructSelectionEditor();

private:
	TWeakObjectPtr<UNiagaraScratchPadViewModel> ViewModel;

	TSharedPtr<FNiagaraScratchPadCommandContext> CommandContext;
};