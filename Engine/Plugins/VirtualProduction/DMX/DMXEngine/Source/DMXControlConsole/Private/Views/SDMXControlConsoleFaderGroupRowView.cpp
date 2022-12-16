// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleFaderGroupRowView.h"

#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "Views/SDMXControlConsoleFaderGroupView.h"

#include "Widgets/SBoxPanel.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleFaderGroupRowView"

void SDMXControlConsoleFaderGroupRowView::Construct(const FArguments& InArgs, const TObjectPtr<UDMXControlConsoleFaderGroupRow>& InFaderGroupRow)
{
	FaderGroupRow = InFaderGroupRow;

	if (!ensureMsgf(FaderGroupRow.IsValid(), TEXT("Invalid fader group row, cannot create fader group row view correctly.")))
	{
		return;
	}

	ChildSlot
		[
			SAssignNew(FaderGroupsHorizontalBox, SHorizontalBox)
		];
}

void SDMXControlConsoleFaderGroupRowView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (!ensureMsgf(FaderGroupRow.IsValid(), TEXT("Invalid fader group row, cannot update fader group row view state correctly.")))
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderGroup*> FaderGroups = FaderGroupRow->GetFaderGroups();
	if (FaderGroups.Num() == FaderGroupViews.Num())
	{
		return;
	}

	if (FaderGroups.Num() > FaderGroupViews.Num())
	{
		OnFaderGroupAdded();
	}
	else
	{
		OnFaderGroupRemoved();
	}
}

void SDMXControlConsoleFaderGroupRowView::OnFaderGroupAdded()
{
	const TArray<UDMXControlConsoleFaderGroup*> FaderGroups = FaderGroupRow->GetFaderGroups();

	for (UDMXControlConsoleFaderGroup* FaderGroup : FaderGroups)
	{
		if (!FaderGroup)
		{
			continue;
		}

		if (ContainsFaderGroup(FaderGroup))
		{
			continue;
		}

		AddFaderGroup(FaderGroup);
	}
}

void SDMXControlConsoleFaderGroupRowView::AddFaderGroup(UDMXControlConsoleFaderGroup* FaderGroup)
{
	if (!ensureMsgf(FaderGroup, TEXT("Invalid fader group, cannot add new fader group view correctly.")))
	{
		return;
	}

	const int32 Index = FaderGroup->GetIndex();

	TSharedRef<SDMXControlConsoleFaderGroupView> FaderGroupWidget = SNew(SDMXControlConsoleFaderGroupView, FaderGroup);

	FaderGroupViews.Insert(FaderGroupWidget, Index);

	FaderGroupsHorizontalBox->InsertSlot(Index)
		.AutoWidth()
		.HAlign(HAlign_Left)
		.Padding(8.f, 0.f)
		[
			FaderGroupWidget
		];
}

void SDMXControlConsoleFaderGroupRowView::OnFaderGroupRemoved()
{
	const TArray<UDMXControlConsoleFaderGroup*> FaderGroups = FaderGroupRow->GetFaderGroups();

	TArray<TWeakPtr<SDMXControlConsoleFaderGroupView>> FaderGroupViewsToRemove;
	for (const TWeakPtr<SDMXControlConsoleFaderGroupView>& FaderGroupView : FaderGroupViews)
	{
		if (!FaderGroupView.IsValid())
		{
			continue;
		}

		const UDMXControlConsoleFaderGroup* FaderGroup = FaderGroupView.Pin()->GetFaderGroup();
		if (!FaderGroup || !FaderGroups.Contains(FaderGroup))
		{
			FaderGroupsHorizontalBox->RemoveSlot(FaderGroupView.Pin().ToSharedRef());
			FaderGroupViewsToRemove.Add(FaderGroupView);
		}
	}

	FaderGroupViews.RemoveAll([&FaderGroupViewsToRemove](const TWeakPtr<SDMXControlConsoleFaderGroupView> FaderGroupView)
		{
			return !FaderGroupView.IsValid() || FaderGroupViewsToRemove.Contains(FaderGroupView);
		});
}

bool SDMXControlConsoleFaderGroupRowView::ContainsFaderGroup(UDMXControlConsoleFaderGroup* FaderGroup)
{
	const TWeakObjectPtr<UDMXControlConsoleFaderGroup> FaderGroupWeakPtr = FaderGroup;

	auto IsFaderGroupInUseLambda = [FaderGroupWeakPtr](const TWeakPtr<SDMXControlConsoleFaderGroupView> FaderGroupView)
	{
		if (!FaderGroupView.IsValid())
		{
			return false;
		}

		const TWeakObjectPtr<UDMXControlConsoleFaderGroup> FaderGroup = FaderGroupView.Pin()->GetFaderGroup();
		if (!FaderGroup.IsValid())
		{
			return false;
		}

		return FaderGroup == FaderGroupWeakPtr;
	};

	return FaderGroupViews.ContainsByPredicate(IsFaderGroupInUseLambda);
}

#undef LOCTEXT_NAMESPACE
