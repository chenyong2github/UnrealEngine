// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewModels/Stack/NiagaraStackSelection.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackSpacer.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "NiagaraSystemEditorData.h"

void UNiagaraStackSelection::Initialize(FRequiredEntryData InRequiredEntryData)
{
	Super::Initialize(InRequiredEntryData, FString());
}

bool UNiagaraStackSelection::GetCanExpand() const
{
	return false;
}

bool UNiagaraStackSelection::GetShouldShowInStack() const
{
	return false;
}

void UNiagaraStackSelection::SetSelectedEntries(const TArray<UNiagaraStackEntry*>& InSelectedEntries)
{
	SelectedEntries.Empty();
	for (UNiagaraStackEntry* SelectedEntry : InSelectedEntries)
	{
		SelectedEntries.Add(SelectedEntry);
	}
	RefreshChildren();
}

void UNiagaraStackSelection::RefreshChildrenInternal(const TArray<UNiagaraStackEntry*>& CurrentChildren, TArray<UNiagaraStackEntry*>& NewChildren, TArray<FStackIssue>& NewIssues)
{
	for (UNiagaraStackEntry* CurrentChild : CurrentChildren)
	{
		UNiagaraStackItem* CurrentChildItem = Cast<UNiagaraStackItem>(CurrentChild);
		if (CurrentChildItem != nullptr)
		{
			CurrentChildItem->OnModifiedGroupItems().RemoveAll(this);
		}
	}

	auto GetOrCreateSpacer = [&](FName SpacerExecutionCategory, FName SpacerKey)
	{
		UNiagaraStackSpacer* Spacer = FindCurrentChildOfTypeByPredicate<UNiagaraStackSpacer>(CurrentChildren,
			[SpacerKey](UNiagaraStackSpacer* CurrentSpacer) { return CurrentSpacer->GetSpacerKey() == SpacerKey; });

		if (Spacer == nullptr)
		{
			Spacer = NewObject<UNiagaraStackSpacer>(this);
			FRequiredEntryData RequiredEntryData(GetSystemViewModel(), GetEmitterViewModel(),
				SpacerExecutionCategory, NAME_None,
				GetSystemViewModel()->GetEditorData().GetStackEditorData());
			Spacer->Initialize(RequiredEntryData, SpacerKey, 1.0f);
		}

		return Spacer;
	};

	int32 EntryIndex = 0;
	for (TWeakObjectPtr<UNiagaraStackEntry> SelectedEntry : SelectedEntries)
	{
		if (SelectedEntry.IsValid() && SelectedEntry->IsFinalized() == false)
		{
			if (EntryIndex > 0)
			{
				NewChildren.Add(GetOrCreateSpacer(NAME_None, *FString::Printf(TEXT("SelectionSpacer_%i"), EntryIndex)));
			}

			UNiagaraStackItem* SelectedItem = Cast<UNiagaraStackItem>(SelectedEntry);
			if (SelectedItem != nullptr)
			{
				SelectedItem->OnModifiedGroupItems().AddUObject(this, &UNiagaraStackEntry::RefreshChildren);
			}

			SelectedEntry->SetIsExpanded(true);
			NewChildren.Add(SelectedEntry.Get());

			EntryIndex++;
		}
	}
}