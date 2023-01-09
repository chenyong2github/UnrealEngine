// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorSelection.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleEditorManager.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"

#include "Algo/Sort.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorSelection"

FDMXControlConsoleEditorSelection::FDMXControlConsoleEditorSelection(const TSharedRef<FDMXControlConsoleEditorManager>& InControlConsoleManager)
{
	WeakControlConsoleManager = InControlConsoleManager;
}

void FDMXControlConsoleEditorSelection::AddToSelection(UDMXControlConsoleFaderGroup* FaderGroup)
{
	if (!FaderGroup || SelectedFaderGroups.Contains(FaderGroup))
	{
		return;
	}

	SelectedFaderGroups.Add(FaderGroup);
	OnSelectionChanged.Broadcast();
	OnFaderGroupSelectionChanged.Broadcast(FaderGroup);
}

void FDMXControlConsoleEditorSelection::AddToSelection(UDMXControlConsoleFaderBase* Fader)
{
	if (!Fader || SelectedFaders.Contains(Fader))
	{
		return;
	}

	SelectedFaders.Add(Fader);
	OnSelectionChanged.Broadcast();
	OnFaderSelectionChanged.Broadcast(Fader);

	UDMXControlConsoleFaderGroup& FaderGroup = Fader->GetOwnerFaderGroupChecked();
	AddToSelection(&FaderGroup);
}

void FDMXControlConsoleEditorSelection::RemoveFromSelection(UDMXControlConsoleFaderGroup* FaderGroup)
{
	if (!FaderGroup || !SelectedFaderGroups.Contains(FaderGroup))
	{
		return;
	}

	ClearFadersSelection(FaderGroup);
	SelectedFaderGroups.Remove(FaderGroup);
	OnSelectionChanged.Broadcast();
	OnFaderGroupSelectionChanged.Broadcast(FaderGroup);

	if (!SelectedFaderGroups.IsEmpty())
	{
		return;
	}

	OnClearFaderGroupSelection.Broadcast();
}

void FDMXControlConsoleEditorSelection::RemoveFromSelection(UDMXControlConsoleFaderBase* Fader)
{
	if (!Fader || !SelectedFaders.Contains(Fader))
	{
		return;
	}

	SelectedFaders.Remove(Fader);
	OnSelectionChanged.Broadcast();
	OnFaderSelectionChanged.Broadcast(Fader);
}

void FDMXControlConsoleEditorSelection::Multiselect(UDMXControlConsoleFaderGroup* FaderGroup)
{
	if (!FaderGroup)
	{
		return;
	}

	UDMXControlConsole* ControlConsole = FDMXControlConsoleEditorManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return;
	}

	TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = ControlConsole->GetAllFaderGroups();
	auto SortSelectedFaderGroupsLambda = [AllFaderGroups](TWeakObjectPtr<UObject> FaderGroupObjectA, TWeakObjectPtr<UObject> FaderGroupObjectB)
	{
		UDMXControlConsoleFaderGroup* FaderGroupA = Cast<UDMXControlConsoleFaderGroup>(FaderGroupObjectA);
		UDMXControlConsoleFaderGroup* FaderGroupB = Cast<UDMXControlConsoleFaderGroup>(FaderGroupObjectB);

		if (!FaderGroupA || !FaderGroupB)
		{
			return false;
		}

		const int32 IndexA = AllFaderGroups.IndexOfByKey(FaderGroupA);
		const int32 IndexB = AllFaderGroups.IndexOfByKey(FaderGroupB);

		bool bIsBefore = IndexA < IndexB;
		return bIsBefore;
	};

	if (IsSelected(FaderGroup))
	{
		TArray<TWeakObjectPtr<UObject>> CurrentSelectedFaderGroups = GetSelectedFaderGroups();

		Algo::Sort(CurrentSelectedFaderGroups, SortSelectedFaderGroupsLambda);

		const int32 Index = CurrentSelectedFaderGroups.IndexOfByKey(FaderGroup);

		// Remove from selection all fader groups after the given one
		for (int32 FaderGroupIndex = Index + 1; FaderGroupIndex < CurrentSelectedFaderGroups.Num(); ++FaderGroupIndex)
		{
			UDMXControlConsoleFaderGroup* FaderGroupToUnselect = Cast<UDMXControlConsoleFaderGroup>(CurrentSelectedFaderGroups[FaderGroupIndex]);
			if (!FaderGroupToUnselect)
			{
				continue;
			}

			RemoveFromSelection(FaderGroupToUnselect);
		}
	}
	else
	{
		AddToSelection(FaderGroup);

		TArray<TWeakObjectPtr<UObject>> CurrentSelectedFaderGroups = GetSelectedFaderGroups();
		if (CurrentSelectedFaderGroups.Num() <= 1)
		{
			return;
		}

		Algo::Sort(CurrentSelectedFaderGroups, SortSelectedFaderGroupsLambda);

		ClearSelection();

		// Check fader groups mutual position
		int32 FirstIndex;
		int32 LastIndex;
		if (FaderGroup == CurrentSelectedFaderGroups[0])
		{
			FirstIndex = AllFaderGroups.IndexOfByKey(FaderGroup);
			LastIndex = AllFaderGroups.IndexOfByKey(CurrentSelectedFaderGroups.Last());
		}
		else 
		{
			FirstIndex = AllFaderGroups.IndexOfByKey(CurrentSelectedFaderGroups[0]);
			LastIndex = AllFaderGroups.IndexOfByKey(FaderGroup);
		}

		// Select all fader groups between first and last selected
		for (int32 FaderGroupIndex = FirstIndex; FaderGroupIndex <= LastIndex; ++FaderGroupIndex)
		{
			UDMXControlConsoleFaderGroup* FaderGroupToSelect = AllFaderGroups[FaderGroupIndex];
			if (!FaderGroupToSelect)
			{
				continue;
			}
			
			// Select only if it's not already selected
			if (!IsSelected(FaderGroupToSelect))
			{
				AddToSelection(FaderGroupToSelect);
			}

			// Select all its faders anyway
			TArray<UDMXControlConsoleFaderBase*> Faders = FaderGroupToSelect->GetFaders();
			for (UDMXControlConsoleFaderBase* Fader : Faders)
			{
				if (!Fader || IsSelected(Fader))
				{
					continue;
				}

				AddToSelection(Fader);
			}
		}
	}
}

void FDMXControlConsoleEditorSelection::Multiselect(UDMXControlConsoleFaderBase* Fader)
{
	if (!Fader)
	{
		return;
	}

	UDMXControlConsoleFaderGroup& FaderGroup = Fader->GetOwnerFaderGroupChecked();

	auto SortSelectedFadersLambda = [&FaderGroup](TWeakObjectPtr<UObject> FaderObjectA, TWeakObjectPtr<UObject> FaderObjectB)
		{
			UDMXControlConsoleFaderBase* FaderA = Cast<UDMXControlConsoleFaderBase>(FaderObjectA);
			UDMXControlConsoleFaderBase* FaderB = Cast<UDMXControlConsoleFaderBase>(FaderObjectB);

			if (!FaderA || !FaderB)
			{
				return false;
			}

			const int32 IndexA = FaderGroup.GetFaders().IndexOfByKey(FaderA);
			const int32 IndexB = FaderGroup.GetFaders().IndexOfByKey(FaderB);

			bool bIsBefore = IndexA < IndexB;
			return bIsBefore;
		};

	if (IsSelected(Fader))
	{
		TArray<UDMXControlConsoleFaderBase*> CurrentSelectedFaders = GetSelectedFadersFromFaderGroup(&FaderGroup);

		Algo::Sort(CurrentSelectedFaders, SortSelectedFadersLambda);

		const int32 Index = CurrentSelectedFaders.IndexOfByKey(Fader);

		// Remove from selection all faders after the given one
		for (int32 FaderIndex = Index + 1; FaderIndex < CurrentSelectedFaders.Num(); ++FaderIndex)
		{
			UDMXControlConsoleFaderBase* FaderToUnselect = CurrentSelectedFaders[FaderIndex];
			if (!FaderToUnselect)
			{
				continue;
			}

			RemoveFromSelection(FaderToUnselect);
		}
	}
	else
	{
		AddToSelection(Fader);

		TArray<UDMXControlConsoleFaderBase*> Faders = FaderGroup.GetFaders();
		TArray<UDMXControlConsoleFaderBase*> CurrentSelectedFaders = GetSelectedFadersFromFaderGroup(&FaderGroup);

		if (CurrentSelectedFaders.Num() <= 1)
		{
			return;
		}

		Algo::Sort(CurrentSelectedFaders, SortSelectedFadersLambda);

		ClearFadersSelection(&FaderGroup);

		int32 FirstIndex;
		int32 LastIndex;

		// Check faders mutual position
		if (Fader == CurrentSelectedFaders[0])
		{
			FirstIndex = Faders.IndexOfByKey(Fader);
			LastIndex = Faders.IndexOfByKey(CurrentSelectedFaders.Last());
		}
		else
		{
			FirstIndex = Faders.IndexOfByKey(CurrentSelectedFaders[0]);
			LastIndex = Faders.IndexOfByKey(Fader);
		}

		// Select all faders between first and last selected
		for (int32 FaderIndex = FirstIndex; FaderIndex <= LastIndex; ++FaderIndex)
		{
			UDMXControlConsoleFaderBase* FaderToSelect = Faders[FaderIndex];
			if (!FaderToSelect || IsSelected(FaderToSelect))
			{
				continue;
			}

			AddToSelection(FaderToSelect);
		}
	}
}

void FDMXControlConsoleEditorSelection::ReplaceInSelection(UDMXControlConsoleFaderGroup* FaderGroup)
{
	if (!FaderGroup || !IsSelected(FaderGroup))
	{
		return;
	}

	RemoveFromSelection(FaderGroup);

	const UDMXControlConsole* ControlConsole = FDMXControlConsoleEditorManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = ControlConsole->GetAllFaderGroups();
	if (AllFaderGroups.Num() <= 1)
	{
		return;
	}

	const int32 Index = AllFaderGroups.IndexOfByKey(FaderGroup);

	int32 NewIndex = Index - 1;
	if (!AllFaderGroups.IsValidIndex(NewIndex))
	{
		NewIndex = Index + 1;
	}

	UDMXControlConsoleFaderGroup* NewSelectedFaderGroup = AllFaderGroups.IsValidIndex(NewIndex) ? AllFaderGroups[NewIndex] : nullptr;
	AddToSelection(NewSelectedFaderGroup);
	return;
}

void FDMXControlConsoleEditorSelection::ReplaceInSelection(UDMXControlConsoleFaderBase* Fader)
{
	if (!Fader || !IsSelected(Fader))
	{
		return;
	}

	RemoveFromSelection(Fader);

	const UDMXControlConsoleFaderGroup& FaderGroup = Fader->GetOwnerFaderGroupChecked();
	const TArray<UDMXControlConsoleFaderBase*> Faders = FaderGroup.GetFaders();
	if (Faders.Num() <= 1)
	{
		return;
	}

	const int32 IndexToReplace = Faders.IndexOfByKey(Fader);
	int32 NewIndex = IndexToReplace - 1;
	if (!Faders.IsValidIndex(NewIndex))
	{
		NewIndex = IndexToReplace + 1;
	}

	UDMXControlConsoleFaderBase* NewSelectedFader = Faders.IsValidIndex(NewIndex) ? Faders[NewIndex] : nullptr;
	AddToSelection(NewSelectedFader);
}

bool FDMXControlConsoleEditorSelection::IsSelected(UDMXControlConsoleFaderGroup* FaderGroup) const
{
	return SelectedFaderGroups.Contains(FaderGroup);
}

bool FDMXControlConsoleEditorSelection::IsSelected(UDMXControlConsoleFaderBase* Fader) const
{
	return SelectedFaders.Contains(Fader);
}

void FDMXControlConsoleEditorSelection::ClearFadersSelection()
{
	SelectedFaders.Empty();
	OnClearFaderSelection.Broadcast();
}

void FDMXControlConsoleEditorSelection::ClearFadersSelection(UDMXControlConsoleFaderGroup* FaderGroup)
{
	if (!FaderGroup || !SelectedFaderGroups.Contains(FaderGroup))
	{
		return;
	}

	TArray<UDMXControlConsoleFaderBase*> Faders = FaderGroup->GetFaders();

	auto IsFaderGroupOwnerLambda = [Faders](const TWeakObjectPtr<UObject> SelectedObject)
		{
			const UDMXControlConsoleFaderBase* SelectedFader = Cast<UDMXControlConsoleFaderBase>(SelectedObject);
			if (!SelectedFader)
			{
				return true;
			}

			if (Faders.Contains(SelectedFader))
			{
				return true;
			}

			return false;
		};

	SelectedFaders.RemoveAll(IsFaderGroupOwnerLambda);

	OnClearFaderSelection.Broadcast();
}

void FDMXControlConsoleEditorSelection::ClearSelection()
{
	ClearFadersSelection();
	SelectedFaderGroups.Empty();
	OnClearFaderGroupSelection.Broadcast();
}

UDMXControlConsoleFaderGroup* FDMXControlConsoleEditorSelection::GetFirstSelectedFaderGroup() const
{
	TArray<TWeakObjectPtr<UObject>> CurrentSelectedFaderGroups = GetSelectedFaderGroups();
	if (CurrentSelectedFaderGroups.IsEmpty())
	{
		return nullptr;
	}

	auto SortSelectedFaderGroupsLambda = [](TWeakObjectPtr<UObject> FaderGroupObjectA, TWeakObjectPtr<UObject> FaderGroupObjectB)
		{
			const UDMXControlConsoleFaderGroup* FaderGroupA = Cast<UDMXControlConsoleFaderGroup>(FaderGroupObjectA);
			const UDMXControlConsoleFaderGroup* FaderGroupB = Cast<UDMXControlConsoleFaderGroup>(FaderGroupObjectB);

			if (!FaderGroupA || !FaderGroupB)
			{
				return false;
			}

			const int32 RowIndexA = FaderGroupA->GetOwnerFaderGroupRowChecked().GetRowIndex();
			const int32 RowIndexB = FaderGroupB->GetOwnerFaderGroupRowChecked().GetRowIndex();

			if (RowIndexA != RowIndexB)
			{
				return RowIndexA < RowIndexB;
			}

			const int32 IndexA = FaderGroupA->GetIndex();
			const int32 IndexB = FaderGroupB->GetIndex();

			return IndexA < IndexB;
		};

	Algo::Sort(CurrentSelectedFaderGroups, SortSelectedFaderGroupsLambda);
	return Cast<UDMXControlConsoleFaderGroup>(CurrentSelectedFaderGroups[0]);
}

UDMXControlConsoleFaderBase* FDMXControlConsoleEditorSelection::GetFirstSelectedFader() const
{
	const UDMXControlConsole* ControlConsole = FDMXControlConsoleEditorManager::Get().GetDMXControlConsole();
	if (!ControlConsole)
	{
		return nullptr;
	}

	const TArray<UDMXControlConsoleFaderGroup*> AllFaderGroups = ControlConsole->GetAllFaderGroups();
	if (AllFaderGroups.IsEmpty())
	{
		return nullptr;
	}

	TArray<TWeakObjectPtr<UObject>> CurrentSelectedFaders = GetSelectedFaders();
	if (CurrentSelectedFaders.IsEmpty())
	{
		return nullptr;
	}

	auto SortSelectedFadersLambda = [AllFaderGroups](TWeakObjectPtr<UObject> FaderObjectA, TWeakObjectPtr<UObject> FaderObjectB)
		{
			const UDMXControlConsoleFaderBase* FaderA = Cast<UDMXControlConsoleFaderBase>(FaderObjectA);
			const UDMXControlConsoleFaderBase* FaderB = Cast<UDMXControlConsoleFaderBase>(FaderObjectB);

			if (!FaderA || !FaderB)
			{
				return false;
			}

			const UDMXControlConsoleFaderGroup& FaderGroupA = FaderA->GetOwnerFaderGroupChecked();
			const UDMXControlConsoleFaderGroup& FaderGroupB = FaderB->GetOwnerFaderGroupChecked();

			const int32 FaderGroupIndexA = AllFaderGroups.IndexOfByKey(&FaderGroupA);
			const int32 FaderGroupIndexB = AllFaderGroups.IndexOfByKey(&FaderGroupB);

			if (FaderGroupIndexA != FaderGroupIndexB)
			{
				return FaderGroupIndexA < FaderGroupIndexB;
			}

			const int32 IndexA = FaderGroupA.GetFaders().IndexOfByKey(FaderA);
			const int32 IndexB = FaderGroupB.GetFaders().IndexOfByKey(FaderB);

			return IndexA < IndexB;
		};

	Algo::Sort(CurrentSelectedFaders, SortSelectedFadersLambda);
	return Cast<UDMXControlConsoleFaderBase>(CurrentSelectedFaders[0]);
}

TArray<UDMXControlConsoleFaderBase*> FDMXControlConsoleEditorSelection::GetSelectedFadersFromFaderGroup(UDMXControlConsoleFaderGroup* FaderGroup) const
{
	TArray<UDMXControlConsoleFaderBase*> CurrentSelectedFaders;

	if (!FaderGroup)
	{
		return CurrentSelectedFaders;
	}

	TArray<UDMXControlConsoleFaderBase*> Faders = FaderGroup->GetFaders();
	for (UDMXControlConsoleFaderBase* Fader : Faders)
	{
		if (!Fader)
		{
			continue;
		}

		if (!SelectedFaders.Contains(Fader))
		{
			continue;
		}

		CurrentSelectedFaders.Add(Fader);
	}

	return CurrentSelectedFaders;
}

#undef LOCTEXT_NAMESPACE
