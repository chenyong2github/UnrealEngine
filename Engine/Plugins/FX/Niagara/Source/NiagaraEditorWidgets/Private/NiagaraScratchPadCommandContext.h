// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Templates/SharedPointer.h"

class UNiagaraScratchPadViewModel;
class FUICommandList;
class FMenuBuilder;
struct FKeyEvent;

class FNiagaraScratchPadCommandContext : public TSharedFromThis<FNiagaraScratchPadCommandContext>
{
public:
	FNiagaraScratchPadCommandContext(UNiagaraScratchPadViewModel* InScratchPadViewModel);

	TSharedRef<FUICommandList> GetCommands();

	bool ProcessCommandBindings(const FKeyEvent& InKeyEvent);

	bool AddEditMenuItems(FMenuBuilder& MenuBuilder);

private:
	void SetupCommands();

	bool CanCutSelectedScripts() const;

	FText GetCanCutSelectedScriptsToolTip() const;

	void CutSelectedScripts() const;

	bool CanCopySelectedScripts() const;

	FText GetCanCopySelectedScriptsToolTip() const;

	void CopySelectedScripts() const;

	bool CanPasteSelectedScripts() const;

	FText GetCanPasteSelectedScriptsToolTip() const;

	void PasteSelectedScripts() const;

	bool CanDeleteSelectedScripts() const;

	FText GetCanDeleteSelectedScriptsToolTip() const;

	void DeleteSelectedScripts() const;

private:
	TSharedRef<FUICommandList> Commands;

	bool bCommandsAreSetup;

	bool bProcessingCommandBindings;

	UNiagaraScratchPadViewModel* ScratchPadViewModel;
};