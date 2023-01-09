// Copyright Epic Games, Inc. All Rights Reserved.

#include "SDMXControlConsoleEditorFaderGroupView.h"

#include "DMXControlConsole.h"
#include "DMXControlConsoleEditorManager.h"
#include "DMXControlConsoleEditorSelection.h"
#include "DMXControlConsoleFaderBase.h"
#include "DMXControlConsoleFaderGroup.h"
#include "DMXControlConsoleFaderGroupRow.h"
#include "Library/DMXEntityReference.h"
#include "Widgets/SDMXControlConsoleEditorAddButton.h"
#include "Widgets/SDMXControlConsoleEditorFader.h"
#include "Widgets/SDMXControlConsoleEditorFaderGroup.h"

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


#define LOCTEXT_NAMESPACE "SDMXControlConsoleEditorFaderGroupView"

void SDMXControlConsoleEditorFaderGroupView::Construct(const FArguments& InArgs, const TObjectPtr<UDMXControlConsoleFaderGroup>& InFaderGroup)
{
	FaderGroup = InFaderGroup;

	if (!ensureMsgf(FaderGroup.IsValid(), TEXT("Invalid fader group, cannot create fader group view correctly.")))
	{
		return;
	}

	ChildSlot
		[
			SNew(SHorizontalBox)

			//Fader Group View main slot
			+SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				SAssignNew(FaderGroupWidget, SDMXControlConsoleEditorFaderGroup, SharedThis(this))
				.OnAddFaderGroup(this, &SDMXControlConsoleEditorFaderGroupView::OnAddFaderGroupClicked)
				.OnAddFaderGroupRow(this, &SDMXControlConsoleEditorFaderGroupView::OnAddFaderGroupRowClicked)
				.OnExpanded(this, &SDMXControlConsoleEditorFaderGroupView::OnFaderGroupExpanded)
			]
	
			//Fader Group View Faders UI widget
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			[
				 GenerateFadersWidget()
			]
		];

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
	SelectionHandler->GetOnFaderSelectionChanged().AddSP(this, &SDMXControlConsoleEditorFaderGroupView::OnFaderSelectionChanged);
	SelectionHandler->GetOnClearFaderGroupSelection().AddSP(this, &SDMXControlConsoleEditorFaderGroupView::OnClearFaderSelection);
	SelectionHandler->GetOnClearFaderSelection().AddSP(this, &SDMXControlConsoleEditorFaderGroupView::OnClearFaderSelection);
}

int32 SDMXControlConsoleEditorFaderGroupView::GetIndex() const
{ 
	if (!FaderGroup.IsValid())
	{
		return INDEX_NONE;
	}

	return FaderGroup->GetIndex();
}

FString SDMXControlConsoleEditorFaderGroupView::GetFaderGroupName() const
{ 
	if (!FaderGroup.IsValid())
	{
		return FString();
	}

	return FaderGroup->GetFaderGroupName(); 
}

void SDMXControlConsoleEditorFaderGroupView::ExpandFadersWidget()
{
	bIsExpanded = true;
}

void SDMXControlConsoleEditorFaderGroupView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
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

TSharedRef<SWidget> SDMXControlConsoleEditorFaderGroupView::GenerateFadersWidget()
{
	SAssignNew(FadersWidget, SBox)
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.HeightOverride(150.f)
		.Visibility(TAttribute<EVisibility>::CreateSP(this, &SDMXControlConsoleEditorFaderGroupView::GetFadersWidgetVisibility))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5.f, 0.f, 0.f, 0.f)
			[
				SAssignNew(FadersListView, SListView<TWeakObjectPtr<UDMXControlConsoleFaderBase>>)
				.ListItemsSource(&Faders)
				.OnGenerateRow(this, &SDMXControlConsoleEditorFaderGroupView::OnGenerateFader)
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
					SNew(SDMXControlConsoleEditorAddButton)
					.OnClicked(this, &SDMXControlConsoleEditorFaderGroupView::OnAddFaderClicked)
					.Visibility(TAttribute<EVisibility>::CreateSP(this, &SDMXControlConsoleEditorFaderGroupView::GetAddFaderButtonVisibility))
				]
			]
		];

	return FadersWidget.ToSharedRef();
}

TSharedRef<ITableRow> SDMXControlConsoleEditorFaderGroupView::OnGenerateFader(TWeakObjectPtr<UDMXControlConsoleFaderBase> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	const TSharedRef<SWidget> NewFader = SNew(SDMXControlConsoleEditorFader, Item.Get());

	return SNew(STableRow<TWeakObjectPtr<UDMXControlConsoleFaderBase>>, OwnerTable)
		[
			NewFader
		];
}

void SDMXControlConsoleEditorFaderGroupView::OnFaderSelectionChanged(UDMXControlConsoleFaderBase* Fader)
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

	const TSharedRef<FDMXControlConsoleEditorSelection> SelectionHandler = FDMXControlConsoleEditorManager::Get().GetSelectionHandler();
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

void SDMXControlConsoleEditorFaderGroupView::OnClearFaderSelection() const
{
	if (!FadersListView.IsValid())
	{
		return;
	}

	FadersListView->ClearSelection();
}

FReply SDMXControlConsoleEditorFaderGroupView::OnFaderGroupExpanded()
{
	bIsExpanded = !bIsExpanded;

	return FReply::Handled();
}

FReply SDMXControlConsoleEditorFaderGroupView::OnAddFaderGroupClicked() const
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

FReply SDMXControlConsoleEditorFaderGroupView::OnAddFaderGroupRowClicked() const
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

FReply SDMXControlConsoleEditorFaderGroupView::OnAddFaderClicked()
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

void SDMXControlConsoleEditorFaderGroupView::OnFaderAdded()
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

void SDMXControlConsoleEditorFaderGroupView::OnFaderRemoved()
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

EVisibility SDMXControlConsoleEditorFaderGroupView::GetFadersWidgetVisibility() const
{
	return bIsExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility SDMXControlConsoleEditorFaderGroupView::GetAddFaderButtonVisibility() const
{
	if (!FaderGroup.IsValid())
	{
		return EVisibility::Collapsed;
	}

	return FaderGroup->GetFixturePatch() ? EVisibility::Collapsed : EVisibility::Visible;
}

#undef LOCTEXT_NAMESPACE
