// Copyright Epic Games, Inc. All Rights Reserved.

#include "NiagaraScratchPadCommandContext.h"
#include "ViewModels/NiagaraScratchPadViewModel.h"

#include "Framework/Notifications/NotificationManager.h"
#include "Logging/LogMacros.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Commands/UICommandList.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "NiagaraScratchPadCommandContext"

FNiagaraScratchPadCommandContext::FNiagaraScratchPadCommandContext(UNiagaraScratchPadViewModel* InScratchPadViewModel)
	: Commands(MakeShared<FUICommandList>())
	, bCommandsAreSetup(false)
	, bProcessingCommandBindings(false)
	, ScratchPadViewModel(InScratchPadViewModel)
{
}

TSharedRef<FUICommandList> FNiagaraScratchPadCommandContext::GetCommands()
{
	if (bCommandsAreSetup == false)
	{
		SetupCommands();
		bCommandsAreSetup = true;
	}
	return Commands;
}

bool FNiagaraScratchPadCommandContext::ProcessCommandBindings(const FKeyEvent& InKeyEvent)
{
	TGuardValue<bool> ProcessingGuard(bProcessingCommandBindings, true);
	return GetCommands()->ProcessCommandBindings(InKeyEvent);
}

bool FNiagaraScratchPadCommandContext::AddEditMenuItems(FMenuBuilder& MenuBuilder)
{
	bool bSupportsCut = false;
	bool bSupportsCopy = false;
	bool bSupportsPaste = false;
	bool bSupportsDelete = true;
	if (bSupportsCut || bSupportsCopy || bSupportsPaste || bSupportsDelete)
	{
		MenuBuilder.BeginSection("EntryEdit", LOCTEXT("EntryEditActions", "Edit"));
		{
			if (bSupportsCut)
			{
				TAttribute<FText> CanCutToolTip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FNiagaraScratchPadCommandContext::GetCanCutSelectedScriptsToolTip));
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Cut, NAME_None, TAttribute<FText>(), CanCutToolTip);
			}
			if (bSupportsCopy)
			{
				TAttribute<FText> CanCopyToolTip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FNiagaraScratchPadCommandContext::GetCanCopySelectedScriptsToolTip));
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Copy, NAME_None, TAttribute<FText>(), CanCopyToolTip);
			}
			if (bSupportsPaste)
			{
				TAttribute<FText> CanPasteToolTip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FNiagaraScratchPadCommandContext::GetCanPasteSelectedScriptsToolTip));
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Paste, NAME_None, TAttribute<FText>(), CanPasteToolTip);
			}
			if (bSupportsDelete)
			{
				TAttribute<FText> CanDeleteToolTip = TAttribute<FText>::Create(TAttribute<FText>::FGetter::CreateSP(this, &FNiagaraScratchPadCommandContext::GetCanDeleteSelectedScriptsToolTip));
				MenuBuilder.AddMenuEntry(FGenericCommands::Get().Delete, NAME_None, TAttribute<FText>(), CanDeleteToolTip);
			}
		}
		MenuBuilder.EndSection();
		return true;
	}
	return false;
}

void FNiagaraScratchPadCommandContext::SetupCommands()
{
	Commands->MapAction(FGenericCommands::Get().Cut, FUIAction(
		FExecuteAction::CreateSP(this, &FNiagaraScratchPadCommandContext::CutSelectedScripts),
		FCanExecuteAction::CreateSP(this, &FNiagaraScratchPadCommandContext::CanCutSelectedScripts)));
	Commands->MapAction(FGenericCommands::Get().Copy, FUIAction(
		FExecuteAction::CreateSP(this, &FNiagaraScratchPadCommandContext::CopySelectedScripts),
		FCanExecuteAction::CreateSP(this, &FNiagaraScratchPadCommandContext::CanCopySelectedScripts)));
	Commands->MapAction(FGenericCommands::Get().Paste, FUIAction(
		FExecuteAction::CreateSP(this, &FNiagaraScratchPadCommandContext::PasteSelectedScripts),
		FCanExecuteAction::CreateSP(this, &FNiagaraScratchPadCommandContext::CanPasteSelectedScripts)));
	Commands->MapAction(FGenericCommands::Get().Delete, FUIAction(
		FExecuteAction::CreateSP(this, &FNiagaraScratchPadCommandContext::DeleteSelectedScripts),
		FCanExecuteAction::CreateSP(this, &FNiagaraScratchPadCommandContext::CanDeleteSelectedScripts)));
}

bool FNiagaraScratchPadCommandContext::CanCutSelectedScripts() const
{
	return false;
}

FText FNiagaraScratchPadCommandContext::GetCanCutSelectedScriptsToolTip() const
{
	return FText();
}

void FNiagaraScratchPadCommandContext::CutSelectedScripts() const
{
}

bool FNiagaraScratchPadCommandContext::CanCopySelectedScripts() const
{
	return false;
}

FText FNiagaraScratchPadCommandContext::GetCanCopySelectedScriptsToolTip() const
{
	return FText();
}

void FNiagaraScratchPadCommandContext::CopySelectedScripts() const
{
}

bool FNiagaraScratchPadCommandContext::CanPasteSelectedScripts() const
{
	return false;
}

FText FNiagaraScratchPadCommandContext::GetCanPasteSelectedScriptsToolTip() const
{
	return FText();
}

void FNiagaraScratchPadCommandContext::PasteSelectedScripts() const
{
}

bool FNiagaraScratchPadCommandContext::CanDeleteSelectedScripts() const
{
	return true;
}

FText FNiagaraScratchPadCommandContext::GetCanDeleteSelectedScriptsToolTip() const
{
	return LOCTEXT("DeleteScriptMessage", "Delete this script and reset references to it.");
}

void FNiagaraScratchPadCommandContext::DeleteSelectedScripts() const
{
	ScratchPadViewModel->DeleteActiveScript();
}

#undef LOCTEXT_NAMESPACE

