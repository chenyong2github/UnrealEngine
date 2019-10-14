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
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewStack"

class SNiagaraSystemOverviewEntryListRow : public STableRow<UNiagaraStackEntry*>
{
	SLATE_BEGIN_ARGS(SNiagaraSystemOverviewEntryListRow)
	{}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackEntry* StackEntry, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		FSlateColor IconColor = FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetColorNameForExecutionCategory(StackEntry->GetExecutionCategoryName()));
		ItemBackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.HeaderBackgroundColor");
		DisabledItemBackgroundColor = ItemBackgroundColor + FLinearColor(.02f, .02f, .02f, 0.0f);

		TSharedPtr<SWidget> WrappedContent;
		if (StackEntry->IsA<UNiagaraStackItem>())
		{
			WrappedContent =
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(this, &SNiagaraSystemOverviewEntryListRow::GetItemBackgroundColor, StackEntry)
				.Padding(0)
				[
					SNew(SBorder)
					.BorderImage(this, &SNiagaraSystemOverviewEntryListRow::GetBorder)
					.Padding(FMargin(6, 4, 3, 4))
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
					.Padding(FMargin(2, 4, 7, 4))
					[
						InArgs._Content.Widget
					]
				];
		}

		STableRow<UNiagaraStackEntry*>::Construct(STableRow<UNiagaraStackEntry*>::FArguments()
			.Style(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.SystemOverview.TableViewRow")
		[
			SNew(SBorder)
			.BorderImage(this, &SNiagaraSystemOverviewEntryListRow::GetBorder)
			.Padding(FMargin(5, 2, 5, 2))
			[
				WrappedContent.ToSharedRef()
			]
		],
		InOwnerTableView);
	}

private:
	FSlateColor GetItemBackgroundColor(UNiagaraStackEntry* Entry) const
	{
		return Entry->GetIsEnabled() && Entry->GetOwnerIsEnabled() ? ItemBackgroundColor : DisabledItemBackgroundColor;
	}

private:
	FLinearColor ItemBackgroundColor;
	FLinearColor DisabledItemBackgroundColor;
};

class SNiagaraSystemOverviewEnabledCheckBox : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnCheckedChanged, bool /*bIsChecked*/);

	SLATE_BEGIN_ARGS(SNiagaraSystemOverviewEnabledCheckBox) {}
		SLATE_ATTRIBUTE(bool, IsChecked)
		SLATE_EVENT(FOnCheckedChanged, OnCheckedChanged)
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs)
	{
		IsChecked = InArgs._IsChecked;
		OnCheckedChanged = InArgs._OnCheckedChanged;

		ChildSlot
		[
			SNew(SBorder)
			.BorderImage(FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.SystemOverview.CheckBoxBorder"))
			.BorderBackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.Item.HeaderBackgroundColor"))
			.Padding(FMargin(0))
			[
				SNew(SButton)
				.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
				.OnClicked(this, &SNiagaraSystemOverviewEnabledCheckBox::OnButtonClicked)
				.ToolTipText(LOCTEXT("EnableCheckBoxToolTip", "Enable or disable this item."))
				.ContentPadding(FMargin(3, 2, 3, 2))
				[
					SNew(STextBlock)
					.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
					.Text(this, &SNiagaraSystemOverviewEnabledCheckBox::GetButtonText)
					.ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.SystemOverview.CheckBoxColor"))
				]
			]
		];
	}

private:
	FReply OnButtonClicked()
	{
		OnCheckedChanged.ExecuteIfBound(IsChecked.IsBound() && !IsChecked.Get());
		return FReply::Handled();
	}

	FText GetButtonText() const
	{
		return IsChecked.IsBound() && IsChecked.Get()
			? LOCTEXT("CheckedText", "\xf14a")
			: LOCTEXT("UncheckedText", "\xf0c8");
	}

private:
	TAttribute<bool> IsChecked;
	FOnCheckedChanged OnCheckedChanged;
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
		UNiagaraStackItem* StackItem = CastChecked<UNiagaraStackItem>(Item);
		Content = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.SystemOverview.ItemText")
				.Text_UObject(Item, &UNiagaraStackEntry::GetDisplayName)
				.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(3, 0, 0, 0)
			[
				SNew(SNiagaraSystemOverviewEnabledCheckBox)
				.Visibility(this, &SNiagaraOverviewStack::GetEnabledCheckBoxVisibility, StackItem)
				.IsEnabled_UObject(StackItem, &UNiagaraStackEntry::GetOwnerIsEnabled)
				.IsChecked_UObject(StackItem, &UNiagaraStackEntry::GetIsEnabled)
				.OnCheckedChanged_UObject(StackItem, &UNiagaraStackItem::SetIsEnabled)
			];
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
				.Image(FNiagaraEditorWidgetsStyle::Get().GetBrush(FNiagaraStackEditorWidgetsUtilities::GetIconNameForExecutionSubcategory(Item->GetExecutionSubcategoryName(), true)))
				.ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(Item->GetExecutionCategoryName())))
				.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
			]
			+ SHorizontalBox::Slot()
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.SystemOverview.GroupHeaderText")
				.Text_UObject(Item, &UNiagaraStackEntry::GetDisplayName)
				.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
			];
	}


	return SNew(SNiagaraSystemOverviewEntryListRow, Item, OwnerTable)
	[
		Content.ToSharedRef()
	];
}

EVisibility SNiagaraOverviewStack::GetEnabledCheckBoxVisibility(UNiagaraStackItem* Item) const
{
	return Item->CanChangeEnabled() ? EVisibility::Visible : EVisibility::Collapsed;
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

#undef LOCTEXT_NAMESPACE