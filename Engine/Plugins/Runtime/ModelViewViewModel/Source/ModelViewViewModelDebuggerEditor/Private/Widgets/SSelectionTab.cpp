// Copyright Epic Games, Inc. All Rights Reserved.

#include "Widgets/SSelectionTab.h"

#include "MVVMDebugView.h"
#include "MVVMDebugViewModel.h"
#include "MVVMViewModelBase.h"
#include "View/MVVMView.h"

#include "Widgets/Layout/SSplitter.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SViewSelection.h"
#include "Widgets/SViewModelSelection.h"

#define LOCTEXT_NAMESPACE "MVVMDebuggerSelectionTab"

namespace UE::MVVM
{

void SSelectionTab::Construct(const FArguments& InArgs)
{
	OnSelectionChanged = InArgs._OnSelectionChanged;

	ChildSlot
	[
		SNew(SBox)
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Vertical)
			+ SSplitter::Slot()
			[
				SAssignNew(ViewSelection, SViewSelection)
				.OnSelectionChanged(this, &SSelectionTab::HandleViewSelectionChanged)
			]
			+ SSplitter::Slot()
			[
				SAssignNew(ViewModelSelection, SViewModelSelection)
				.OnSelectionChanged(this, &SSelectionTab::HandleViewModleSelectionChanged)
			]
		]
	];
}


void SSelectionTab::SetSnapshot(TSharedPtr<FDebugSnapshot> Snapshot)
{
	if (ViewSelection)
	{
		ViewSelection->SetSnapshot(Snapshot);
	}
	if (ViewModelSelection)
	{
		ViewModelSelection->SetSnapshot(Snapshot);
	}
	CurrentSelection = ESelection::None;
}


TArray<FDebugItemId> SSelectionTab::GetSelectedItems() const
{
	TArray<FDebugItemId> Result;
	if (CurrentSelection == ESelection::View)
	{
		for (const TSharedPtr<FMVVMViewDebugEntry>& Selection : ViewSelection->GetSelectedItems())
		{
			Result.Add(FDebugItemId(FDebugItemId::EType::View, Selection->ViewInstanceDebugId));
		}
	}
	else if (CurrentSelection == ESelection::ViewModel)
	{
		for (const TSharedPtr<FMVVMViewModelDebugEntry>& Selection : ViewModelSelection->GetSelectedItems())
		{
			Result.Add(FDebugItemId(FDebugItemId::EType::ViewModel, Selection->ViewModelDebugId));
		}
	}
	return Result;
}


TArray<UObject*> SSelectionTab::GetSelectedObjects() const
{
	TArray<UObject*> Result;
	if (CurrentSelection == ESelection::View)
	{
		for (const TSharedPtr<FMVVMViewDebugEntry>& Selection : ViewSelection->GetSelectedItems())
		{
			Result.Add(Selection->LiveView.Get());
		}
	}
	else if (CurrentSelection == ESelection::ViewModel)
	{
		for (const TSharedPtr<FMVVMViewModelDebugEntry>& Selection : ViewModelSelection->GetSelectedItems())
		{
			Result.Add(Selection->LiveViewModel.Get());
		}
	}
	return Result;
}


void SSelectionTab::HandleViewSelectionChanged()
{
	CurrentSelection = ESelection::View;
	OnSelectionChanged.ExecuteIfBound();
	ViewModelSelection->SetSelection(FGuid());
}


void SSelectionTab::HandleViewModleSelectionChanged()
{
	CurrentSelection = ESelection::ViewModel;
	OnSelectionChanged.ExecuteIfBound();
	ViewSelection->SetSelection(FGuid());
}

} //namespace

#undef LOCTEXT_NAMESPACE