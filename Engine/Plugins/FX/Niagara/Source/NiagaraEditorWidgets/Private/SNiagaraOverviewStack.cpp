// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNiagaraOverviewStack.h"
#include "NiagaraSystemEditorData.h"
#include "ViewModels/NiagaraSystemViewModel.h"
#include "ViewModels/NiagaraEmitterHandleViewModel.h"
#include "ViewModels/NiagaraSystemSelectionViewModel.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/Stack/NiagaraStackModuleItem.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackSelection.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraEditorWidgetsUtilities.h"
#include "SNiagaraStack.h"
#include "Stack/SNiagaraStackItemGroupAddButton.h"
#include "Stack/SNiagaraStackIssueIcon.h"
#include "NiagaraEditorCommon.h"

#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SToolTip.h"
#include "EditorStyleSet.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Commands/GenericCommands.h"
#include "Framework/Notifications/NotificationManager.h"
#include "ScopedTransaction.h"
#include "Logging/LogMacros.h"

#define LOCTEXT_NAMESPACE "NiagaraOverviewStack"

class SNiagaraSystemOverviewEntryListRow : public STableRow<UNiagaraStackEntry*>
{
	SLATE_BEGIN_ARGS(SNiagaraSystemOverviewEntryListRow) {}
		SLATE_ATTRIBUTE(EVisibility, IssueIconVisibility)
		SLATE_EVENT(FOnDragDetected, OnDragDetected)
		SLATE_EVENT(FOnTableRowDragLeave, OnDragLeave)
		SLATE_EVENT(FOnCanAcceptDrop, OnCanAcceptDrop)
		SLATE_EVENT(FOnAcceptDrop, OnAcceptDrop)
		SLATE_DEFAULT_SLOT(FArguments, Content);
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs, UNiagaraStackViewModel* InStackViewModel, UNiagaraStackEntry* InStackEntry, const TSharedRef<STableViewBase>& InOwnerTableView)
	{
		StackViewModel = InStackViewModel;
		StackEntry = InStackEntry;
		IssueIconVisibility = InArgs._IssueIconVisibility;
		FSlateColor IconColor = FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetColorNameForExecutionCategory(StackEntry->GetExecutionCategoryName()));

		FMargin ContentPadding;
		if (StackEntry->IsA<UNiagaraStackItem>())
		{
			BackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.SystemOverview.Item.BackgroundColor");
			ContentPadding = FMargin(0, 2, 0, 2);
		}
		else
		{
			BackgroundColor = FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.SystemOverview.Group.BackgroundColor");
			ContentPadding = FMargin(0, 5, 0, 5);
		}
		DisabledBackgroundColor = BackgroundColor + FLinearColor(.02f, .02f, .02f, 0.0f);

		STableRow<UNiagaraStackEntry*>::Construct(STableRow<UNiagaraStackEntry*>::FArguments()
			.Style(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.SystemOverview.TableViewRow")
			.OnDragDetected(InArgs._OnDragDetected)
			.OnDragLeave(InArgs._OnDragLeave)
			.OnCanAcceptDrop(InArgs._OnCanAcceptDrop)
			.OnAcceptDrop(InArgs._OnAcceptDrop)
		[
			SNew(SBox)
			.Padding(FMargin(0, 1, 0, 1))
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(this, &SNiagaraSystemOverviewEntryListRow::GetBackgroundColor, StackEntry)
				.ToolTipText_UObject(StackEntry, &UNiagaraStackEntry::GetTooltipText)
				.Padding(FMargin(0))
				[
					SNew(SBorder)
					.BorderImage(this, &SNiagaraSystemOverviewEntryListRow::GetBorder)
					.Padding(ContentPadding)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.Padding(TAttribute<FMargin>(this, &SNiagaraSystemOverviewEntryListRow::GetInnerContentPadding))
						[
							InArgs._Content.Widget
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(2, 0, 2, 0)
						[
							SNew(SNiagaraStackIssueIcon, StackViewModel, StackEntry)
							.Visibility(IssueIconVisibility)
						]
					]
				]
			]
		],
		InOwnerTableView);
	}

	// Need to use the mouse down event here since the graph eats the mouse up to show its context menu.
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			FMenuBuilder MenuBuilder(true, nullptr);
			bool bMenuItemsAdded = false;

			if (StackEntry->IsA<UNiagaraStackModuleItem>())
			{
				bMenuItemsAdded |= FNiagaraStackEditorWidgetsUtilities::AddStackModuleItemContextMenuActions(MenuBuilder, *CastChecked<UNiagaraStackModuleItem>(StackEntry), this->AsShared());
			}

			if (StackEntry->IsA<UNiagaraStackItem>())
			{
				bMenuItemsAdded |= FNiagaraStackEditorWidgetsUtilities::AddStackItemContextMenuActions(MenuBuilder, *CastChecked<UNiagaraStackItem>(StackEntry));
			}

			bMenuItemsAdded |= FNiagaraStackEditorWidgetsUtilities::AddStackEntryAssetContextMenuActions(MenuBuilder, *StackEntry);
		
			if (bMenuItemsAdded)
			{
				FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
				FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
				return FReply::Handled();
			}
		}
		return STableRow<UNiagaraStackEntry*>::OnMouseButtonDown(MyGeometry, MouseEvent);
	}

private:
	FSlateColor GetBackgroundColor(UNiagaraStackEntry* Entry) const
	{
		return Entry->GetIsEnabled() && Entry->GetOwnerIsEnabled() ? BackgroundColor : DisabledBackgroundColor;
	}

	FMargin GetInnerContentPadding() const
	{
		if (IssueIconVisibility.Get() == EVisibility::Visible)
		{
			return FMargin(5, 0, 1, 0);
		}
		else
		{
			return FMargin(5, 0, 5, 0);
		}
	}

private:
	UNiagaraStackViewModel* StackViewModel;
	UNiagaraStackEntry* StackEntry;
	FLinearColor BackgroundColor;
	FLinearColor DisabledBackgroundColor;
	TAttribute<EVisibility> IssueIconVisibility;
};

class SNiagaraSystemOverviewEnabledCheckBox : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_OneParam(FOnCheckedChanged, bool /*bIsChecked*/);

	SLATE_BEGIN_ARGS(SNiagaraSystemOverviewEnabledCheckBox) {}
		SLATE_ATTRIBUTE(bool, IsChecked)
		SLATE_EVENT(FOnCheckedChanged, OnCheckedChanged);
	SLATE_END_ARGS();

	void Construct(const FArguments& InArgs)
	{
		IsChecked = InArgs._IsChecked;
		OnCheckedChanged = InArgs._OnCheckedChanged;

		ChildSlot
		[
			SNew(SButton)
			.ButtonStyle(FEditorStyle::Get(), "HoverHintOnly")
			.ForegroundColor(FSlateColor::UseSubduedForeground())
			.OnClicked(this, &SNiagaraSystemOverviewEnabledCheckBox::OnButtonClicked)
			.ToolTipText(LOCTEXT("EnableCheckBoxToolTip", "Enable or disable this item."))
			.ContentPadding(FMargin(3, 2, 2, 2))
			[
				SNew(STextBlock)
				.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.10"))
				.Text(this, &SNiagaraSystemOverviewEnabledCheckBox::GetButtonText)
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
	Commands = MakeShared<FUICommandList>();

	SetupCommands();

	bUpdatingOverviewSelectionFromStackSelection = false;
	bUpdatingStackSelectionFromOverviewSelection = false;

	StackViewModel = &InStackViewModel;
	OverviewSelectionViewModel = &InOverviewSelectionViewModel;
	OverviewSelectionViewModel->OnEntrySelectionChanged().AddSP(this, &SNiagaraOverviewStack::SystemSelectionChanged);

	ChildSlot
	[
		SAssignNew(EntryListView, SListView<UNiagaraStackEntry*>)
		.ListItemsSource(&FlattenedEntryList)
		.OnGenerateRow(this, &SNiagaraOverviewStack::OnGenerateRowForEntry)
		.OnSelectionChanged(this, &SNiagaraOverviewStack::OnSelectionChanged)
		.SelectionMode(ESelectionMode::Multi)
		.OnItemToString_Debug_Static(&FNiagaraStackEditorWidgetsUtilities::StackEntryToStringForListDebug)
	];

	InStackViewModel.OnStructureChanged().AddSP(this, &SNiagaraOverviewStack::EntryStructureChanged);
		
	bRefreshEntryListPending = true;
	RefreshEntryList();
	SystemSelectionChanged();
}

SNiagaraOverviewStack::~SNiagaraOverviewStack()
{
	if (StackViewModel != nullptr)
	{
		StackViewModel->OnStructureChanged().RemoveAll(this);
	}

	if (OverviewSelectionViewModel != nullptr)
	{
		OverviewSelectionViewModel->OnEntrySelectionChanged().RemoveAll(this);
	}
}

bool SNiagaraOverviewStack::SupportsKeyboardFocus() const
{
	return true;
}

FReply SNiagaraOverviewStack::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	if (Commands->ProcessCommandBindings(InKeyEvent))
	{
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

void SNiagaraOverviewStack::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	RefreshEntryList();
}

void SNiagaraOverviewStack::SetupCommands()
{
	Commands->MapAction(FGenericCommands::Get().Delete, FUIAction(
		FExecuteAction::CreateSP(this, &SNiagaraOverviewStack::DeleteSelectedEntries)));
}

void SNiagaraOverviewStack::DeleteSelectedEntries()
{
	bool bIncompleteDelete = false;
	TArray<UNiagaraStackItem*> ItemsToDelete;
	TArray<UNiagaraStackEntry*> SelectedEntries;
	OverviewSelectionViewModel->GetSelectedEntries(SelectedEntries);
	for (UNiagaraStackEntry* SelectedEntry : SelectedEntries)
	{
		UNiagaraStackItem* SelectedItem = Cast<UNiagaraStackItem>(SelectedEntry);
		if (SelectedItem != nullptr)
		{
			FText DeleteMessage;
			if (SelectedItem->SupportsDelete() && SelectedItem->TestCanDeleteWithMessage(DeleteMessage))
			{
				ItemsToDelete.Add(SelectedItem);
			}
			else
			{
				bIncompleteDelete = true;
			}
		}
		else
		{
			bIncompleteDelete = true;
		}
	}

	if (ItemsToDelete.Num() > 0)
	{
		const FScopedTransaction Transaction(LOCTEXT("DeleteSelected", "Delete items from the system overview"));
		for (UNiagaraStackItem* ItemToDelete : ItemsToDelete)
		{
			ItemToDelete->Delete();
		}
	}

	if (bIncompleteDelete)
	{
		FText IncompleteDeleteMessage = LOCTEXT("DeleteIncompleteMessage", "Not all items could be deleted because they either\ndon't support being deleted or they are inherited.");
		FNotificationInfo Warning(IncompleteDeleteMessage);
		Warning.ExpireDuration = 5.0f;
		Warning.bFireAndForget = true;
		Warning.bUseLargeFont = false;
		Warning.Image = FCoreStyle::Get().GetBrush(TEXT("MessageLog.Warning"));
		FSlateNotificationManager::Get().AddNotification(Warning);
		UE_LOG(LogNiagaraEditor, Warning, TEXT("%s"), *IncompleteDeleteMessage.ToString());
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
	if (bRefreshEntryListPending)
	{
		FlattenedEntryList.Empty();
		EntryObjectKeyToParentChain.Empty();
		TArray<UClass*> AcceptableClasses;
		AcceptableClasses.Add(UNiagaraStackItemGroup::StaticClass());
		AcceptableClasses.Add(UNiagaraStackItem::StaticClass());

		UNiagaraStackEntry* RootEntry = StackViewModel->GetRootEntry();
		checkf(RootEntry != nullptr, TEXT("Root entry was null."));
		TArray<UNiagaraStackEntry*> RootChildren;
		RootEntry->GetFilteredChildren(RootChildren);
		for (UNiagaraStackEntry* RootChild : RootChildren)
		{
			checkf(RootEntry != nullptr, TEXT("Root entry child was null."));
			TArray<UNiagaraStackEntry*> ParentChain;
			AddEntriesRecursive(*RootChild, FlattenedEntryList, AcceptableClasses, ParentChain);
		}

		bRefreshEntryListPending = false;
		EntryListView->RequestListRefresh();
	}
}

void SNiagaraOverviewStack::EntryStructureChanged()
{
	bRefreshEntryListPending = true;
}

TSharedRef<ITableRow> SNiagaraOverviewStack::OnGenerateRowForEntry(UNiagaraStackEntry* Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	FVector2D IconSize = FNiagaraEditorWidgetsStyle::Get().GetVector("NiagaraEditor.Stack.IconSize");
	TSharedPtr<SWidget> Content;
	if (Item->IsA<UNiagaraStackItem>())
	{
		UNiagaraStackItem* StackItem = CastChecked<UNiagaraStackItem>(Item);
		TSharedPtr<SWidget> IndentContent;
		if (StackItem->SupportsHighlights())
		{

			TArray<FNiagaraScriptHighlight> ScriptHighlights;
			for (const FNiagaraScriptHighlight& ScriptHighlight : StackItem->GetHighlights())
			{
				if (ScriptHighlight.IsValid())
				{
					ScriptHighlights.Add(ScriptHighlight);
				}
			}

			TSharedRef<SHorizontalBox> ToolTipBox = SNew(SHorizontalBox);
			if (ScriptHighlights.Num() > 0)
			{
				for (const FNiagaraScriptHighlight& ScriptHighlight : ScriptHighlights)
				{
					ToolTipBox->AddSlot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 5, 0)
						[
							SNew(SImage)
							.Image(FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.Stack.ModuleHighlightLarge"))
							.ColorAndOpacity(ScriptHighlight.Color)
						];
					ToolTipBox->AddSlot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0, 0, 10, 0)
						[
							SNew(STextBlock)
							.Text(ScriptHighlight.DisplayName)
						];
				}
			}

			TSharedRef<SBox> IndentBox = SNew(SBox)
				.ToolTip(SNew(SToolTip) [ ToolTipBox ])
				.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				.Visibility(EVisibility::Visible)
				.WidthOverride(IconSize.X)
				.HeightOverride(IconSize.Y);
			
			if (ScriptHighlights.Num() > 0)
			{
				IndentBox->SetHAlign(HAlign_Left);
				IndentBox->SetVAlign(VAlign_Top);

				TSharedPtr<SGridPanel> HighlightsGrid;
				IndentBox->SetContent(SAssignNew(HighlightsGrid, SGridPanel));

				int32 HighlightsAdded = 0;
				for (int32 HighlightIndex = 0; HighlightIndex < ScriptHighlights.Num() && HighlightsAdded < 4; HighlightIndex++)
				{
					if (ScriptHighlights[HighlightIndex].IsValid())
					{
						FName HighlightImageBrushName;
						FLinearColor HighlightImageColor;
						if (HighlightIndex < 3)
						{
							HighlightImageBrushName = "NiagaraEditor.Stack.ModuleHighlight";
							HighlightImageColor = ScriptHighlights[HighlightIndex].Color;
						}
						else
						{
							HighlightImageBrushName = "NiagaraEditor.Stack.ModuleHighlightMore";
							HighlightImageColor = FLinearColor::White;
						}

						int32 Column = HighlightIndex % 2;
						int32 Row = HighlightIndex / 2;
						HighlightsGrid->AddSlot(Column, Row)
							.Padding(Column, Row, 1 - Column, 1 - Row)
							[
								SNew(SImage)
								.Image(FNiagaraEditorWidgetsStyle::Get().GetBrush(HighlightImageBrushName))
								.ColorAndOpacity(HighlightImageColor)
							];
						HighlightsAdded++;
					}
				}
			}
			IndentContent = IndentBox;
		}
		else if (StackItem->SupportsIcon())
		{
			IndentContent = SNew(SBox)
				.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				.WidthOverride(IconSize.X)
				.HeightOverride(IconSize.Y)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(StackItem->GetIconBrush())
				];
		}
		else
		{
			IndentContent = SNew(SBox)
				.WidthOverride(IconSize.X)
				.HeightOverride(IconSize.Y);
		}

		Content = SNew(SHorizontalBox)
			// Indent content
			+ SHorizontalBox::Slot()
			.Padding(0, 1, 2, 1)
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				IndentContent.ToSharedRef()
			]
			// Name
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(3, 3, 0, 3)
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.SystemOverview.ItemText")
				.Text_UObject(Item, &UNiagaraStackEntry::GetDisplayName)
				.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
			]
			// Enabled checkbox
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
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
	else if (Item->IsA<UNiagaraStackItemGroup>())
	{
		UNiagaraStackItemGroup* StackItemGroup = CastChecked<UNiagaraStackItemGroup>(Item);
		TSharedRef<SHorizontalBox> ContentBox = SNew(SHorizontalBox)
			// Execution category icon
			+ SHorizontalBox::Slot()
			.Padding(0, 0, 2, 0)
			.VAlign(VAlign_Center)
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(IconSize.X)
				.HeightOverride(IconSize.Y)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SImage)
					.Image(FNiagaraEditorWidgetsStyle::Get().GetBrush(FNiagaraStackEditorWidgetsUtilities::GetIconNameForExecutionSubcategory(Item->GetExecutionSubcategoryName(), true)))
					.ColorAndOpacity(FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(Item->GetExecutionCategoryName())))
					.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				]
			]
			// Name
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.Padding(3, 2, 0, 2)
			[
				SNew(STextBlock)
				.TextStyle(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.SystemOverview.GroupHeaderText")
				.Text_UObject(Item, &UNiagaraStackEntry::GetDisplayName)
				.IsEnabled_UObject(Item, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
			];

		if (StackItemGroup->GetAddUtilities() != nullptr)
		{
			ContentBox->AddSlot()
			.AutoWidth()
			.Padding(0, 0, 1, 0)
			[
				SNew(SNiagaraStackItemGroupAddButton, *StackItemGroup)
				.Width(22)
			];
		}

		Content = ContentBox;
	}
	else
	{
		Content = SNullWidget::NullWidget;
	}


	return SNew(SNiagaraSystemOverviewEntryListRow, StackViewModel, Item, OwnerTable)
		.OnDragDetected(this, &SNiagaraOverviewStack::OnRowDragDetected, TWeakObjectPtr<UNiagaraStackEntry>(Item))
		.OnDragLeave(this, &SNiagaraOverviewStack::OnRowDragLeave)
		.OnCanAcceptDrop(this, &SNiagaraOverviewStack::OnRowCanAcceptDrop)
		.OnAcceptDrop(this, &SNiagaraOverviewStack::OnRowAcceptDrop)
		.IssueIconVisibility(this, &SNiagaraOverviewStack::GetIssueIconVisibility)
	[
		Content.ToSharedRef()
	];
}

EVisibility SNiagaraOverviewStack::GetEnabledCheckBoxVisibility(UNiagaraStackItem* Item) const
{
	return Item->SupportsChangeEnabled() ? EVisibility::Visible : EVisibility::Collapsed;
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
		OverviewSelectionViewModel->UpdateSelectedEntries(SelectedEntries, DeselectedEntries, bClearCurrentSelection);

		PreviousSelection.Empty();
		for (UNiagaraStackEntry* SelectedEntry : SelectedEntries)
		{
			PreviousSelection.Add(SelectedEntry);
		}
	}
}

void SNiagaraOverviewStack::SystemSelectionChanged()
{
	if (bUpdatingOverviewSelectionFromStackSelection == false)
	{
		TGuardValue<bool> UpdateGuard(bUpdatingStackSelectionFromOverviewSelection, true);

		TArray<UNiagaraStackEntry*> SelectedListViewStackEntries;
		EntryListView->GetSelectedItems(SelectedListViewStackEntries);
		TArray<UNiagaraStackEntry*> SelectedOverviewEntries;
		OverviewSelectionViewModel->GetSelectedEntries(SelectedOverviewEntries);

		TArray<UNiagaraStackEntry*> EntriesToDeselect;
		for (UNiagaraStackEntry* SelectedListViewStackEntry : SelectedListViewStackEntries)
		{
			if (SelectedOverviewEntries.Contains(SelectedListViewStackEntry) == false)
			{
				EntriesToDeselect.Add(SelectedListViewStackEntry);
			}
		}

		TArray<UNiagaraStackEntry*> EntriesToSelect;
		RefreshEntryList();
		for (UNiagaraStackEntry* SelectedOverviewEntry : SelectedOverviewEntries)
		{
			if (FlattenedEntryList.Contains(SelectedOverviewEntry))
			{
				EntriesToSelect.Add(SelectedOverviewEntry);
			}
		}

		for (UNiagaraStackEntry* EntryToDeselect : EntriesToDeselect)
		{
			EntryListView->SetItemSelection(EntryToDeselect, false);
		}

		for (UNiagaraStackEntry* EntryToSelect : EntriesToSelect)
		{
			EntryListView->SetItemSelection(EntryToSelect, true);
		}
	}
}

FReply SNiagaraOverviewStack::OnRowDragDetected(const FGeometry& InGeometry, const FPointerEvent& InPointerEvent, TWeakObjectPtr<UNiagaraStackEntry> InStackEntryWeak)
{
	UNiagaraStackEntry* StackEntry = InStackEntryWeak.Get();
	if (StackEntry != nullptr && StackEntry->CanDrag())
	{
		TArray<UNiagaraStackEntry*> EntriesToDrag;
		StackEntry->GetSystemViewModel()->GetSelectionViewModel()->GetSelectedEntries(EntriesToDrag);
		EntriesToDrag.AddUnique(StackEntry);
		return FReply::Handled().BeginDragDrop(FNiagaraStackEditorWidgetsUtilities::ConstructDragDropOperationForStackEntries(EntriesToDrag));
	}
	return FReply::Unhandled();
}

void SNiagaraOverviewStack::OnRowDragLeave(const FDragDropEvent& InDragDropEvent)
{
	FNiagaraStackEditorWidgetsUtilities::HandleDragLeave(InDragDropEvent);
}

TOptional<EItemDropZone> SNiagaraOverviewStack::OnRowCanAcceptDrop(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry)
{
	return FNiagaraStackEditorWidgetsUtilities::RequestDropForStackEntry(InDragDropEvent, InDropZone, InTargetEntry, UNiagaraStackEntry::EDropOptions::Overview);
}

FReply SNiagaraOverviewStack::OnRowAcceptDrop(const FDragDropEvent& InDragDropEvent, EItemDropZone InDropZone, UNiagaraStackEntry* InTargetEntry)
{
	bool bHandled = FNiagaraStackEditorWidgetsUtilities::HandleDropForStackEntry(InDragDropEvent, InDropZone, InTargetEntry, UNiagaraStackEntry::EDropOptions::Overview);
	return bHandled ? FReply::Handled() : FReply::Unhandled();
}

EVisibility SNiagaraOverviewStack::GetIssueIconVisibility() const
{
	return StackViewModel->HasIssues() ? EVisibility::Visible : EVisibility::Collapsed;
}

#undef LOCTEXT_NAMESPACE