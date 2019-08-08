// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewStack.h"
#include "NiagaraSystemEditorData.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackSelection.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorWidgetsUtilities.h"
#include "SNiagaraStack.h"

#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Images/SImage.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"

class SNiagaraSystemOverviewEntryListRow : public STableRow<UNiagaraStackEntry*>
{
	SLATE_BEGIN_ARGS(SNiagaraSystemOverviewEntryListRow)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackEntry* StackEntry, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		FSlateColor IconColor = FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetColorNameForExecutionCategory(StackEntry->GetExecutionCategoryName()));

		TSharedPtr<SWidget> WrappedContent;
		if (StackEntry->IsA<UNiagaraStackItem>())
		{
			WrappedContent =
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.HeaderBackgroundColor"))
				.Padding(0)
				[
					SNew(SBorder)
					.BorderImage(this, &SNiagaraSystemOverviewEntryListRow::GetBorder)
					.Padding(FMargin(8, 4, 8, 4))
					[
						InArgs._Content.Widget
					]
				];
		}
		else
		{
			WrappedContent =
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("NoBrush"))
				.Padding(0)
				[
					SNew(SBorder)
					.BorderImage(this, &SNiagaraSystemOverviewEntryListRow::GetBorder)
					.Padding(FMargin(2, 6, 8, 2))
					[
						InArgs._Content.Widget
					]
				];
		}

		STableRow<UNiagaraStackEntry*>::Construct(STableRow<UNiagaraStackEntry*>::FArguments()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.ContentBackgroundColor"))
			.Padding(0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Fill)
				[
					SNew(SBorder)
					.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
					.BorderBackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(StackEntry->GetExecutionCategoryName())))
					.Padding(0)
					[
						SNew(SBox)
						.WidthOverride(5)
					]
				]
				+ SHorizontalBox::Slot()
				[
					SNew(SBorder)
					.BorderImage(this, &SNiagaraSystemOverviewEntryListRow::GetBorder)
					.Padding(FMargin(7, 0, 7, 4))
					[
						WrappedContent.ToSharedRef()
					]
				]
			]
		],
		InOwnerTableView);
	}
};

void SNiagaraOverviewStack::Construct(const FArguments& InArgs, UNiagaraStackViewModel& InStackViewModel, UNiagaraSystemSelectionViewModel& InOverviewSelectionViewModel)
{
	bRefreshEntryListPending = false;
	bUpdatingOverviewSelectionFromStackSelection = false;
	bUpdatingStackSelectionFromOverviewSelection = false;

	StackViewModel = &InStackViewModel;
	OverviewSelectionViewModel = &InOverviewSelectionViewModel;
	OverviewSelectionViewModel->OnSelectionChanged().AddSP(this, &SNiagaraOverviewStack::SystemSelectionChanged);

	ChildSlot
	[
		SAssignNew(EntryListView, SListView<UNiagaraStackEntry*>)
		.ListItemsSource(&FlattenedEntryList)
		.OnGenerateRow(this, &SNiagaraOverviewStack::OnGenerateRowForEntry)
		.OnSelectionChanged(this, &SNiagaraOverviewStack::OnSelectionChanged)
		.SelectionMode(ESelectionMode::Multi)
	];

	InStackViewModel.OnStructureChanged().AddSP(this, &SNiagaraOverviewStack::EntryStructureChanged);
		
	bRefreshEntryListPending = true;
	RefreshEntryList();
}

SNiagaraOverviewStack::~SNiagaraOverviewStack()
{
	if (StackViewModel != nullptr)
	{
		StackViewModel->OnStructureChanged().RemoveAll(this);
	}
}

void SNiagaraOverviewStack::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	if (bRefreshEntryListPending)
	{
		RefreshEntryList();
		bRefreshEntryListPending = false;
	}
}

void SNiagaraOverviewStack::AddEntriesRecursive(UNiagaraStackEntry& EntryToAdd, TArray<UNiagaraStackEntry*>& EntryList, const TArray<UClass*>& AcceptableClasses, TArray<UNiagaraStackEntry*> ParentChain)
{
	if (AcceptableClasses.ContainsByPredicate([&] (UClass* Class) { return EntryToAdd.IsA(Class); }))
	{
		EntryList.Add(&EntryToAdd);
		EntryObjectKeyToParentChain.Add(FObjectKey(&EntryToAdd), ParentChain);
		TArray<UNiagaraStackEntry*> Children;
		EntryToAdd.GetFilteredChildren(Children);
		ParentChain.Add(&EntryToAdd);
		for (UNiagaraStackEntry* Child : Children)
		{
			checkf(Child != nullptr, TEXT("Stack entry had null child."));
			AddEntriesRecursive(*Child, EntryList, AcceptableClasses, ParentChain);
		}
	}
}

void SNiagaraOverviewStack::RefreshEntryList()
{
	FlattenedEntryList.Empty();
	EntryObjectKeyToParentChain.Empty();
	TArray<UClass*> AcceptableClasses;
	AcceptableClasses.Add(UNiagaraStackItemGroup::StaticClass());
	AcceptableClasses.Add(UNiagaraStackItem::StaticClass());
	for (UNiagaraStackEntry* RootEntry : StackViewModel->GetRootEntries())
	{
		checkf(RootEntry != nullptr, TEXT("Root entry was null."));
		TArray<UNiagaraStackEntry*> RootChildren;
		RootEntry->GetFilteredChildren(RootChildren);
		for (UNiagaraStackEntry* RootChild : RootChildren)
		{
			checkf(RootEntry != nullptr, TEXT("Root entry child was null."));
			TArray<UNiagaraStackEntry*> ParentChain;
			AddEntriesRecursive(*RootChild, FlattenedEntryList, AcceptableClasses, ParentChain);
		}
	}
	EntryListView->RequestListRefresh();
}

void SNiagaraOverviewStack::EntryStructureChanged()
{
	bRefreshEntryListPending = true;
}

TSharedRef<ITableRow> SNiagaraOverviewStack::OnGenerateRowForEntry(UNiagaraStackEntry* Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	TSharedPtr<SWidget> Content;
	if (Item->IsA<UNiagaraStackItem>())
	{
		Content = SNew(STextBlock)
			.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.SystemOverview.ItemText")
			.Text_UObject(Item, &UNiagaraStackEntry::GetDisplayName);
	}
	else
	{
		Content = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.Padding(2, 0, 6, 0)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SImage)
				//.Visibility(this, &SNiagaraStackTableRow::GetExecutionCategoryIconVisibility)
				.Image(FNiagaraEditorWidgetsStyle::Get().GetBrush(FNiagaraStackEditorWidgetsUtilities::GetIconNameForExecutionSubcategory(Item->GetExecutionSubcategoryName(), true)))
				.ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(Item->GetExecutionCategoryName())))
			]
			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.SystemOverview.GroupHeaderText")
				.Text_UObject(Item, &UNiagaraStackEntry::GetDisplayName)
			];
	}


	return SNew(SNiagaraSystemOverviewEntryListRow, Item, OwnerTable)
	[
		Content.ToSharedRef()
	];
}

void SNiagaraOverviewStack::OnSelectionChanged(UNiagaraStackEntry* InNewSelection, ESelectInfo::Type SelectInfo)
{
	if (bUpdatingStackSelectionFromOverviewSelection == false)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingOverviewSelectionFromStackSelection, true);
		TArray<UNiagaraStackEntry*> SelectedEntries;
		EntryListView->GetSelectedItems(SelectedEntries);

		TArray<UNiagaraStackEntry*> DeselectedEntries;

		for (TWeakObjectPtr<UNiagaraStackEntry> PreviousSelectedEntry : PreviousSelection)
		{
			if (PreviousSelectedEntry.IsValid() && SelectedEntries.Contains(PreviousSelectedEntry.Get()) == false)
			{
				DeselectedEntries.Add(PreviousSelectedEntry.Get());
			}
		}

		bool bClearCurrentSelection = FSlateApplication::Get().GetModifierKeys().IsControlDown() == false;
		OverviewSelectionViewModel->UpdateSelectionFromEntries(SelectedEntries, DeselectedEntries, bClearCurrentSelection);

		PreviousSelection.Empty();
		for (UNiagaraStackEntry* SelectedEntry : SelectedEntries)
		{
			PreviousSelection.Add(SelectedEntry);
		}
	}
}

void SNiagaraOverviewStack::SystemSelectionChanged(UNiagaraSystemSelectionViewModel::ESelectionChangeSource SelectionChangeSource)
{
	if (bUpdatingOverviewSelectionFromStackSelection == false)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingStackSelectionFromOverviewSelection, true);

		TArray<UNiagaraStackEntry*> SelectedStackEntries;
		EntryListView->GetSelectedItems(SelectedStackEntries);
		TArray<UNiagaraStackEntry*> SelectedOverviewEntries = OverviewSelectionViewModel->GetSelectedEntries();

		TArray<UNiagaraStackEntry*> EntriesToDeselect;
		for (UNiagaraStackEntry* SelectedStackEntry : SelectedStackEntries)
		{
			if (SelectedOverviewEntries.Contains(SelectedStackEntry) == false)
			{
				EntriesToDeselect.Add(SelectedStackEntry);
			}
		}

		for (UNiagaraStackEntry* EntryToDeselect : EntriesToDeselect)
		{
			EntryListView->SetItemSelection(EntryToDeselect, false);
		}
	}
}