// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SLevelSnapshotsEditorFilterList.h"

#include "ConjunctionFilter.h"
#include "SLevelSnapshotsEditorFilter.h"
#include "LevelSnapshotsEditorFilters.h"

#include "Widgets/Layout/SWrapBox.h"

namespace
{
	void OnClickRemoveFilter(TSharedRef<SLevelSnapshotsEditorFilter> RemovedFilterWidget, const TSharedPtr<SWrapBox> FilterContainer, const TWeakObjectPtr<UConjunctionFilter> ManagedAndCondition)
	{
		const TWeakObjectPtr<UNegatableFilter> RemovedFilter = RemovedFilterWidget->GetSnapshotFilter();
		if (!ensure(FilterContainer.IsValid()) || !ensure(ManagedAndCondition.IsValid() || !ensure(RemovedFilter.IsValid())))
		{
			return;
		}

		FilterContainer->RemoveSlot(RemovedFilterWidget);
		ManagedAndCondition->RemoveChild(RemovedFilterWidget->GetSnapshotFilter().Get());
	}
}

SLevelSnapshotsEditorFilterList::~SLevelSnapshotsEditorFilterList()
{
	if (ensure(ManagedAndCondition.IsValid()))
	{
		ManagedAndCondition->OnChildAdded.Remove(AddDelegateHandle);
	}
}

void SLevelSnapshotsEditorFilterList::Construct(const FArguments& InArgs, UConjunctionFilter* InManagedAndCondition, const TSharedRef<FLevelSnapshotsEditorFilters>& InEditorFilterModel)
{
	ManagedAndCondition = InManagedAndCondition;

	AddDelegateHandle = InManagedAndCondition->OnChildAdded.AddRaw(this, &SLevelSnapshotsEditorFilterList::AddChild, InEditorFilterModel);

	ChildSlot
	[
		SAssignNew(FilterBox, SWrapBox)
			.UseAllottedSize(true)
	];

	for (UNegatableFilter* Filter : InManagedAndCondition->GetChildren())
	{
		AddChild(Filter, InEditorFilterModel);
	}
}
	
void SLevelSnapshotsEditorFilterList::AddChild(UNegatableFilter* AddedFilter, TSharedRef<FLevelSnapshotsEditorFilters> InEditorFilterModel) const
{
	FilterBox->AddSlot()
		.Padding(3, 3)
		[
			SNew(SLevelSnapshotsEditorFilter, AddedFilter, InEditorFilterModel)
				.OnClickRemoveFilter(SLevelSnapshotsEditorFilter::FOnClickRemoveFilter::CreateStatic(&OnClickRemoveFilter, FilterBox, ManagedAndCondition))
		];
}
