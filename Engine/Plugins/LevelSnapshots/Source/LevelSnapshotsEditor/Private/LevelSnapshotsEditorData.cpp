// Copyright Epic Games, Inc. All Rights Reserved.

#include "LevelSnapshotsEditorData.h"

#include "LevelSnapshotFilters.h"

ULevelSnapshotFilter* ULevelSnapshotEditorFilterGroup::AddOrFindFilter(TSubclassOf<ULevelSnapshotFilter> InClass, const FName& InName)
{
	if (ULevelSnapshotFilter** ExistingFilter = Filters.Find(InName))
	{
		return *ExistingFilter;
	}

	ULevelSnapshotFilter* const NewFilter = NewObject<ULevelSnapshotFilter>(this, InClass, InName);

	Filters.Add(InName, NewFilter);

	return NewFilter;
}

ULevelSnapshotEditorFilterGroup* ULevelSnapshotsEditorData::AddOrFindGroup(const FName& InName)
{
	if (ULevelSnapshotEditorFilterGroup** ExistingGroup = FilterGroups.Find(InName))
	{
		return *ExistingGroup;
	}

	ULevelSnapshotEditorFilterGroup* const NewGroup = NewObject<ULevelSnapshotEditorFilterGroup>(this, ULevelSnapshotEditorFilterGroup::StaticClass(), InName);

	FilterGroups.Add(InName, NewGroup);

	return NewGroup;
}