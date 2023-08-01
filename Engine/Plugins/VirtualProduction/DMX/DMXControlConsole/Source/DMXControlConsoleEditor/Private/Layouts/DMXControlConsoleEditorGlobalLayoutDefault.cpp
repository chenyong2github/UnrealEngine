// Copyright Epic Games, Inc. All Rights Reserved.

#include "DMXControlConsoleEditorGlobalLayoutDefault.h"

#include "DMXControlConsoleFaderGroup.h"
#include "Library/DMXEntity.h"
#include "Library/DMXEntityFixturePatch.h"


#define LOCTEXT_NAMESPACE "DMXControlConsoleEditorGlobalLayoutDefault"

void UDMXControlConsoleEditorGlobalLayoutDefault::GenerateLayoutByControlConsoleData(const UDMXControlConsoleData* ControlConsoleData)
{
	Super::GenerateLayoutByControlConsoleData(ControlConsoleData);

	// Default Layout can't contain not patched Fader Groups
	CleanLayoutFromUnpatchedFaderGroups();
}

void UDMXControlConsoleEditorGlobalLayoutDefault::OnFixturePatchRemovedFromLibrary(UDMXLibrary* Library, TArray<UDMXEntity*> Entities)
{
	if (Entities.IsEmpty())
	{
		return;
	}

	const TArray<TWeakObjectPtr<UDMXControlConsoleFaderGroup>> AllFaderGroups = GetAllFaderGroups();
	for (const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup : AllFaderGroups)
	{
		if (!FaderGroup.IsValid())
		{
			continue;
		}

		const UDMXEntityFixturePatch* FixturePatch = FaderGroup->GetFixturePatch();
		if(!FixturePatch || Entities.Contains(FixturePatch))
		{
			RemoveFromLayout(FaderGroup.Get());
		}
	}

	ClearEmptyLayoutRows();
}

void UDMXControlConsoleEditorGlobalLayoutDefault::OnFaderGroupAddedToData(const UDMXControlConsoleFaderGroup* FaderGroup, UDMXControlConsoleData* ControlConsoleData)
{
	if (FaderGroup && FaderGroup->HasFixturePatch())
	{
		Modify();
		GenerateLayoutByControlConsoleData(ControlConsoleData);
	}
}

void UDMXControlConsoleEditorGlobalLayoutDefault::CleanLayoutFromUnpatchedFaderGroups()
{
	const TArray<TWeakObjectPtr<UDMXControlConsoleFaderGroup>> AllFaderGroups = GetAllFaderGroups();
	for (const TWeakObjectPtr<UDMXControlConsoleFaderGroup>& FaderGroup : AllFaderGroups)
	{
		if (FaderGroup.IsValid() && !FaderGroup->HasFixturePatch())
		{
			RemoveFromLayout(FaderGroup.Get());
		}
	}

	ClearEmptyLayoutRows();
}

#undef LOCTEXT_NAMESPACE
