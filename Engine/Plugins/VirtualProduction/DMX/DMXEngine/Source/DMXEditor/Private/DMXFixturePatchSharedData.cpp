// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXFixturePatchSharedData.h"

#include "DMXEditor.h"
#include "Library/DMXEntityFixturePatch.h"


#define LOCTEXT_NAMESPACE "DMXFixturePatchSharedData"

void FDMXFixturePatchSharedData::SelectUniverse(int32 UniverseID)
{	
	check(UniverseID >= 0);

	if (UniverseID == SelectedUniverse)
	{
		return;
	}

	SelectedUniverse = UniverseID;
	OnUniverseSelectionChanged.Broadcast();
}

int32 FDMXFixturePatchSharedData::GetSelectedUniverse()
{
	return SelectedUniverse;
}

void FDMXFixturePatchSharedData::SelectFixturePatch(TWeakObjectPtr<UDMXEntityFixturePatch> Patch)
{
	if (SelectedFixturePatches.Num() == 1 &&
		SelectedFixturePatches[0] == Patch)
	{
		return;
	}

	SelectedFixturePatches.Reset();
	SelectedFixturePatches.Add(Patch);
	OnFixturePatchSelectionChanged.Broadcast();
}

void FDMXFixturePatchSharedData::AddFixturePatchToSelection(TWeakObjectPtr<UDMXEntityFixturePatch> Patch)
{
	if (!SelectedFixturePatches.Contains(Patch))
	{
		SelectedFixturePatches.Add(Patch);
		OnFixturePatchSelectionChanged.Broadcast();
	}
}

void FDMXFixturePatchSharedData::SelectFixturePatches(const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>& Patches)
{
	if (SelectedFixturePatches == Patches)
	{
		return;
	}

	SelectedFixturePatches.Reset();
	SelectedFixturePatches = Patches;
	OnFixturePatchSelectionChanged.Broadcast();
}

const TArray<TWeakObjectPtr<UDMXEntityFixturePatch>>& FDMXFixturePatchSharedData::GetSelectedFixturePatches() const
{
	return SelectedFixturePatches;
}

#undef LOCTEXT_NAMESPACE
