// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleFaderGroupView.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "DMXControlConsoleManager.h"
#include "DMXControlConsoleSelection.h"
#include "Library/DMXEntityReference.h"
#include "Widgets/SDMXControlConsoleAddButton.h"
#include "Widgets/SDMXControlConsoleFader.h"
#include "Widgets/SDMXControlConsoleFaderGroup.h"

#include "ScopedTransaction.h"
#include "Delegates/IDelegateInstance.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"


#define LOCTEXT_NAMESPACE "SDMXControlConsoleFaderGroupView"

void SDMXControlConsoleFaderGroupView::Construct(const FArguments& InArgs, const TObjectPtr<UDMXControlConsoleFaderGroup>& InFaderGroup)
{
	FaderGroup = InFaderGroup;

	if (!ensureMsgf(FaderGroup.IsValid(), TEXT("Invalid fader group, cannot create fader group view correctly.")))
	{
		return;
	}

	bIsExpanded = false;

	ChildSlot
		[
			SNew(SHorizontalBox)

			//Fader Group View main slot
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SAssignNew(FaderGroupWidget, SDMXControlConsoleFaderGroup, SharedThis(this))
				.OnAddFaderGroup(this, &SDMXControlConsoleFaderGroupView::OnAddFaderGroupClicked)
				.OnAddFaderGroupRow(this, &SDMXControlConsoleFaderGroupView::OnAddFaderGroupRowClicked)
				.OnExpanded(this, &SDMXControlConsoleFaderGroupView::OnFaderGroupExpanded)
				.OnDeleted(this, &SDMXControlConsoleFaderGroupView::OnDeleteFaderGroup)
				.OnSelected(this, &SDMXControlConsoleFaderGroupView::OnSelectFaderGroup)
			]
	
			//Fader Group View Faders UI widget
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				 GenerateFadersWidget()
			]
		];

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	SelectionHandler->GetOnFaderSelectionChanged().AddSP(this, &SDMXControlConsoleFaderGroupView::OnFaderSelectionChanged);
	SelectionHandler->GetOnClearFaderGroupSelection().AddSP(this, &SDMXControlConsoleFaderGroupView::OnClearFaderSelection);
	SelectionHandler->GetOnClearFaderSelection().AddSP(this, &SDMXControlConsoleFaderGroupView::OnClearFaderSelection);
}

int32 SDMXControlConsoleFaderGroupView::GetIndex() const
{ 
	if (!FaderGroup.IsValid())
	{
		return INDEX_NONE;
	}

	return FaderGroup->GetIndex();
}

FString SDMXControlConsoleFaderGroupView::GetFaderGroupName() const
{ 
	if (!FaderGroup.IsValid())
	{
		return FString();
	}

	return FaderGroup->GetFaderGroupName(); 
}

void SDMXControlConsoleFaderGroupView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (!ensureMsgf(FaderGroup.IsValid(), TEXT("Invalid fader group, cannot update fader group view state correctly.")))
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderBase*> AllFaders = FaderGroup->GetFaders();
	if (AllFaders.Num() == Faders.Num() && !FaderGroup->HasForceRefresh())
	{
		return;
	}

	if (AllFaders.Num() > Faders.Num())
	{
		OnFaderAdded();
	}
	else
	{
		OnFaderRemoved();
	}

	FaderGroup->ForceRefresh();
}

TSharedRef<SWidget> SDMXControlConsoleFaderGroupView::GenerateFadersWidget()
{
	SAssignNew(FadersWidget, SBox)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.HeightOverride(150.f)
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &SDMXControlConsoleFaderGroupView::GetFadersWidgetVisibility))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5.f, 0.f, 0.f, 0.f)
			[
				SAssignNew(FadersListView, SListView<TWeakObjectPtr<UDMXControlConsoleFaderBase>>)
				.ListItemsSource(&Faders)
				.OnGenerateRow(this, &SDMXControlConsoleFaderGroupView::OnGenerateFader)
				.Orientation(Orient_Horizontal)
				.SelectionMode(ESelectionMode::Multi)
				.Visibility(EVisibility::SelfHitTestInvisible)
			]

			//Add Fader button
			+ SHorizontalBox::Slot()
				[
					SNew(SBox)
					.WidthOverride(25.f)
					.HeightOverride(25.f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Top)
					.Padding(5.f)
					[
						SNew(SDMXControlConsoleAddButton)
						.OnClicked(this, &SDMXControlConsoleFaderGroupView::OnAddFaderClicked)
						.Visibility(TAttribute<EVisibility>::CreateSP(this, &SDMXControlConsoleFaderGroupView::GetAddFaderButtonVisibility))
					]
				]
		];

	return FadersWidget.ToSharedRef();
}

void SDMXControlConsoleFaderGroupView::ExpandFadersWidget()
{
	if (IsExpanded())
	{
		return;
	}

	bIsExpanded = true;
}

TSharedRef<ITableRow> SDMXControlConsoleFaderGroupView::OnGenerateFader(TWeakObjectPtr<UDMXControlConsoleFaderBase> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const TSharedRef<SWidget> NewFader = SNew(SDMXControlConsoleFader, Item.Get());

	return SNew(STableRow<TWeakObjectPtr<UDMXControlConsoleFaderBase>>, OwnerTable)
		[
			NewFader
		];
}

void SDMXControlConsoleFaderGroupView::OnFaderSelectionChanged()
{
	if (!ensureMsgf(FaderGroup.IsValid(), TEXT("Invalid fader, cannot update faders selection correctly.")))
	{
		return;
	}

	if (!ensureMsgf(FadersListView.IsValid(), TEXT("Invalid ListView, cannot update faders selection correctly.")))
	{
		return;
	}

	FadersListView->ClearSelection();

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	const TArray<UDMXControlConsoleFaderBase*> SelectedFaders = SelectionHandler->GetSelectedFadersFromFaderGroup(FaderGroup.Get());
	if (SelectedFaders.IsEmpty())
	{
		return;
	}

	for (UDMXControlConsoleFaderBase* SelectedFader : SelectedFaders)
	{
		FadersListView->SetItemSelection(SelectedFader, true);
	}
}

void SDMXControlConsoleFaderGroupView::OnClearFaderSelection() const
{
	if (!FadersListView.IsValid())
	{
		return;
	}

	FadersListView->ClearSelection();
}

FReply SDMXControlConsoleFaderGroupView::OnFaderGroupExpanded()
{
	bIsExpanded = !bIsExpanded;

	return FReply::Handled();
}

FReply SDMXControlConsoleFaderGroupView::OnAddFaderGroupClicked() const
{
	if (FaderGroup.IsValid())
	{
		UDMXControlConsoleFaderGroupRow& FaderGroupRow = FaderGroup->GetOwnerFaderGroupRowChecked();

		const FScopedTransaction FaderGroupClickedTransaction(LOCTEXT("FaderGroupClickedTransaction", "Add Fader Group"));
		FaderGroupRow.Modify();

		FaderGroupRow.AddFaderGroup(GetIndex() + 1);
	}

	return FReply::Handled();
}

FReply SDMXControlConsoleFaderGroupView::OnAddFaderGroupRowClicked() const
{
	if (FaderGroup.IsValid())
	{
		UDMXControlConsoleFaderGroupRow& FaderGroupRow = FaderGroup->GetOwnerFaderGroupRowChecked();
		UDMXControlConsole& ControlConsole = FaderGroupRow.GetOwnerControlConsoleChecked();

		const FScopedTransaction FaderGroupRowClickedTransaction(LOCTEXT("FaderGroupRowClickedTransaction", "Add Fader Group"));
		ControlConsole.Modify();

		const int32 RowIndex = FaderGroupRow.GetRowIndex();
		ControlConsole.AddFaderGroupRow(RowIndex + 1);
	}

	return FReply::Handled();
}

FReply SDMXControlConsoleFaderGroupView::OnAddFaderClicked()
{
	if (FaderGroup.IsValid())
	{
		const FScopedTransaction FaderClickedTransaction(LOCTEXT("FaderClickedTransaction", "Add Fader"));
		FaderGroup->PreEditChange(nullptr);

		FaderGroup->AddRawFader();

		FaderGroup->PostEditChange();
	}

	return FReply::Handled();
}

void SDMXControlConsoleFaderGroupView::OnDeleteFaderGroup()
{
	if (!ensureMsgf(FaderGroup.IsValid(), TEXT("Invalid fader group, cannot delete fader group correctly.")))
	{
		return;
	}

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();

	const FScopedTransaction DeleteFaderGroupTransaction(LOCTEXT("DeleteFaderGroupTransaction", "Delete Fader Group"));
	if (SelectionHandler->IsMultiselectAllowed())
	{
		const TArray<TWeakObjectPtr<UObject>> SelectedFaderGroupsObjects = SelectionHandler->GetSelectedFaderGroups();
		for (const TWeakObjectPtr<UObject>& SelectedFaderGroupObject : SelectedFaderGroupsObjects)
		{
			UDMXControlConsoleFaderGroup* SelectedFaderGroup = Cast<UDMXControlConsoleFaderGroup>(SelectedFaderGroupObject);
			if (!SelectedFaderGroup)
			{
				continue;
			}

			SelectedFaderGroup->PreEditChange(nullptr);

			SelectedFaderGroup->Destroy();

			SelectedFaderGroup->PostEditChange();

			SelectionHandler->ClearFadersSelection(SelectedFaderGroup);
			SelectionHandler->RemoveFromSelection(SelectedFaderGroup);
		}
	}
	else
	{
		FaderGroup->PreEditChange(nullptr);

		SelectionHandler->RemoveFromSelection(FaderGroup.Get());
		FaderGroup->Destroy();

		FaderGroup->PostEditChange();
	}
}

void SDMXControlConsoleFaderGroupView::OnSelectFaderGroup()
{
	if (!ensureMsgf(FaderGroup.IsValid(), TEXT("Invalid fader group, cannot select fader group correctly.")))
	{
		return;
	}

	const TSharedRef<FDMXControlConsoleSelection> SelectionHandler = FDMXControlConsoleManager::Get().GetSelectionHandler();
	const bool bIsSelected = SelectionHandler->IsSelected(FaderGroup.Get());

	if (!SelectionHandler->IsMultiselectAllowed())
	{
		SelectionHandler->ClearSelection();
	}

	if (!bIsSelected)
	{
		SelectionHandler->AddToSelection(FaderGroup.Get());
	}
	else
	{
		SelectionHandler->RemoveFromSelection(FaderGroup.Get());
	}
}

void SDMXControlConsoleFaderGroupView::OnFaderAdded()
{
	const TArray<UDMXControlConsoleFaderBase*> AllFaders = FaderGroup->GetFaders();

	for (UDMXControlConsoleFaderBase* AnyFader : AllFaders)
	{
		if (Faders.Contains(AnyFader))
		{
			continue;
		}

		Faders.Add(AnyFader);
	}

	FadersListView->RequestListRefresh();
}

void SDMXControlConsoleFaderGroupView::OnFaderRemoved()
{
	if (Faders.IsEmpty())
	{
		return;
	}

	const TArray<UDMXControlConsoleFaderBase*> AllFaders = FaderGroup->GetFaders();

	auto IsFaderNoLongerInUseLambda = [AllFaders](const TWeakObjectPtr<UDMXControlConsoleFaderBase> Fader)
	{
		if (!Fader.IsValid())
		{
			return true;
		}

		if (!AllFaders.Contains(Fader))
		{
			return true;
		}

		return false;
	};

	Faders.RemoveAll(IsFaderNoLongerInUseLambda);
}

EVisibility SDMXControlConsoleFaderGroupView::GetFadersWidgetVisibility() const
{
	return bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SDMXControlConsoleFaderGroupView::GetAddFaderButtonVisibility() const
{
	if (!FaderGroup.IsValid())
	{
		return EVisibility::Collapsed;
	}

	const FDMXEntityFixturePatchRef& FixturePatchRef = FaderGroup->GetFixturePatchRef();
	return FixturePatchRef.GetFixturePatch() ? EVisibility::Collapsed : EVisibility::Visible;
}

#undef LOCTEXT_NAMESPACE
