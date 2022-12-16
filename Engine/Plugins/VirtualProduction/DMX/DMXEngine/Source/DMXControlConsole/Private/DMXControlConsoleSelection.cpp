// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleSelection.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleSelection"

FDMXControlConsoleSelection::FDMXControlConsoleSelection(const TSharedRef<FDMXControlConsoleManager>& InControlConsoleManager)
{
	WeakControlConsoleManager = InControlConsoleManager;
}

void FDMXControlConsoleSelection::AddToSelection(UDMXControlConsoleFaderGroup* FaderGroup)
{
	if (!FaderGroup || SelectedFaderGroups.Contains(FaderGroup))
	{
		return;
	}

	SelectedFaderGroups.Add(FaderGroup);
	OnFaderGroupSelectionChanged.Broadcast();
}

void FDMXControlConsoleSelection::AddToSelection(UDMXControlConsoleFaderBase* Fader)
{
	if (!Fader || SelectedFaders.Contains(Fader))
	{
		return;
	}

	if (!IsMultiselectAllowed())
	{
		ClearSelection();
	}

	SelectedFaders.Add(Fader);
	OnFaderSelectionChanged.Broadcast();

	UDMXControlConsoleFaderGroup& FaderGroup = Fader->GetOwnerFaderGroupChecked();
	AddToSelection(&FaderGroup);
}

void FDMXControlConsoleSelection::RemoveFromSelection(UDMXControlConsoleFaderGroup* FaderGroup)
{
	if (!FaderGroup || !SelectedFaderGroups.Contains(FaderGroup))
	{
		return;
	}

	ClearFadersSelection(FaderGroup);
	SelectedFaderGroups.Remove(FaderGroup);
	OnFaderGroupSelectionChanged.Broadcast();

	if (!SelectedFaderGroups.IsEmpty())
	{
		return;
	}

	OnClearFaderGroupSelection.Broadcast();
}

void FDMXControlConsoleSelection::RemoveFromSelection(UDMXControlConsoleFaderBase* Fader)
{
	if (!Fader || !SelectedFaders.Contains(Fader))
	{
		return;
	}

	SelectedFaders.Remove(Fader);
	OnFaderSelectionChanged.Broadcast();

	if (!bAllowMultiselect)
	{
		OnClearFaderSelection.Broadcast();
	}
}

bool FDMXControlConsoleSelection::IsSelected(UDMXControlConsoleFaderGroup* FaderGroup) const
{
	return SelectedFaderGroups.Contains(FaderGroup);
}

bool FDMXControlConsoleSelection::IsSelected(UDMXControlConsoleFaderBase* Fader) const
{
	return SelectedFaders.Contains(Fader);
}

void FDMXControlConsoleSelection::SetAllowMultiselect(bool bAllow)
{
	bAllowMultiselect = bAllow;
}

void FDMXControlConsoleSelection::ClearFadersSelection()
{
	SelectedFaders.Empty();
	OnClearFaderSelection.Broadcast();
}

void FDMXControlConsoleSelection::ClearFadersSelection(UDMXControlConsoleFaderGroup* FaderGroup)
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

void FDMXControlConsoleSelection::ClearSelection()
{
	ClearFadersSelection();
	SelectedFaderGroups.Empty();
	OnClearFaderGroupSelection.Broadcast();
}

TArray<UDMXControlConsoleFaderBase*> FDMXControlConsoleSelection::GetSelectedFadersFromFaderGroup(UDMXControlConsoleFaderGroup* FaderGroup) const
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
