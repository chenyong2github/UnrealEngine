// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

class UNiagaraStackEntry;

namespace FNiagaraStackClipboardUtilities
{
	bool NIAGARAEDITOR_API TestCanCutSelectionWithMessage(const TArray<UNiagaraStackEntry*>& SelectedEntries, FText& OutCanCutMessage);

	void NIAGARAEDITOR_API CutSelection(const TArray<UNiagaraStackEntry*>& SelectedEntries);

	bool NIAGARAEDITOR_API TestCanCopySelectionWithMessage(const TArray<UNiagaraStackEntry*>& SelectedEntries, FText& OutCanCopyMessage);

	void NIAGARAEDITOR_API CopySelection(const TArray<UNiagaraStackEntry*>& SelectedEntries);

	bool NIAGARAEDITOR_API TestCanPasteSelectionWithMessage(const TArray<UNiagaraStackEntry*>& SelectedEntries, FText& OutCanPasteMessage);

	void NIAGARAEDITOR_API PasteSelection(const TArray<UNiagaraStackEntry*>& SelectedEntries);

	bool NIAGARAEDITOR_API TestCanDeleteSelectionWithMessage(const TArray<UNiagaraStackEntry*>& SelectedEntries, FText& OutMessage);

	void NIAGARAEDITOR_API DeleteSelection(const TArray<UNiagaraStackEntry*>& SelectedEntries);
}