// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Stack/SNiagaraStackTableRow.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "EditorStyleSet.h"
#include "ViewModels/Stack/NiagaraStackViewModel.h"
#include "ViewModels/Stack/NiagaraStackItem.h"
#include "ViewModels/Stack/NiagaraStackItemGroup.h"
#include "ViewModels/Stack/NiagaraStackEntry.h"
#include "NiagaraEditorWidgetsUtilities.h"
#include "NiagaraStackCommandContext.h"
#include "Stack/SNiagaraStackIssueIcon.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "NiagaraStackTableRow"

const float IndentSize = 12;

void SNiagaraStackTableRow::Construct(const FArguments& InArgs, UNiagaraStackViewModel* InStackViewModel, UNiagaraStackEntry* InStackEntry, TSharedRef<FNiagaraStackCommandContext> InStackCommandContext, const TSharedRef<STreeView<UNiagaraStackEntry*>>& InOwnerTree)
{
	ContentPadding = InArgs._ContentPadding;
	bIsCategoryIconHighlighted = InArgs._IsCategoryIconHighlighted;
	bShowExecutionCategoryIcon = InArgs._ShowExecutionCategoryIcon;
	NameColumnWidth = InArgs._NameColumnWidth;
	ValueColumnWidth = InArgs._ValueColumnWidth;
	NameColumnWidthChanged = InArgs._OnNameColumnWidthChanged;
	ValueColumnWidthChanged = InArgs._OnValueColumnWidthChanged;
	IssueIconVisibility = InArgs._IssueIconVisibility;
	RowPadding = InArgs._RowPadding;
	StackViewModel = InStackViewModel;
	StackEntry = InStackEntry;
	StackCommandContext = InStackCommandContext;
	OwnerTree = InOwnerTree;

	ExpandedImage = FCoreStyle::Get().GetBrush("TreeArrow_Expanded");
	CollapsedImage = FCoreStyle::Get().GetBrush("TreeArrow_Collapsed");

	ItemBackgroundColor = InArgs._ItemBackgroundColor;
	DisabledItemBackgroundColor = ItemBackgroundColor + FLinearColor(.02f, .02f, .02f, 0.0f);
	ForegroundColor = InArgs._ItemForegroundColor;

	ExecutionCategoryToolTipText = InStackEntry->GetExecutionSubcategoryName() != NAME_None
		? FText::Format(LOCTEXT("ExecutionCategoryToolTipFormat", "{0} - {1}"), FText::FromName(InStackEntry->GetExecutionCategoryName()), FText::FromName(InStackEntry->GetExecutionSubcategoryName()))
		: FText::FromName(InStackEntry->GetExecutionCategoryName());

	ConstructInternal(
		STableRow<UNiagaraStackEntry*>::FArguments()
			.Style(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.TableViewRow")
			.OnDragDetected(InArgs._OnDragDetected)
			.OnDragLeave(InArgs._OnDragLeave)
			.OnCanAcceptDrop(InArgs._OnCanAcceptDrop)
			.OnAcceptDrop(InArgs._OnAcceptDrop)
		, OwnerTree.ToSharedRef());
}

void SNiagaraStackTableRow::SetOverrideNameWidth(TOptional<float> InMinWidth, TOptional<float> InMaxWidth)
{
	NameMinWidth = InMinWidth;
	NameMaxWidth = InMaxWidth;
}

void SNiagaraStackTableRow::SetOverrideNameAlignment(EHorizontalAlignment InHAlign, EVerticalAlignment InVAlign)
{
	NameHorizontalAlignment = InHAlign;
	NameVerticalAlignment = InVAlign;
}

void SNiagaraStackTableRow::SetOverrideValueWidth(TOptional<float> InMinWidth, TOptional<float> InMaxWidth)
{
	ValueMinWidth = InMinWidth;
	ValueMaxWidth = InMaxWidth;
}

void SNiagaraStackTableRow::SetOverrideValueAlignment(EHorizontalAlignment InHAlign, EVerticalAlignment InVAlign)
{
	ValueHorizontalAlignment = InHAlign;
	ValueVerticalAlignment = InVAlign;
}

FMargin SNiagaraStackTableRow::GetContentPadding() const
{
	return ContentPadding;
}

void SNiagaraStackTableRow::SetContentPadding(FMargin InContentPadding)
{
	ContentPadding = InContentPadding;
}

void SNiagaraStackTableRow::SetNameAndValueContent(TSharedRef<SWidget> InNameWidget, TSharedPtr<SWidget> InValueWidget)
{
	FSlateColor IconColor = FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetColorNameForExecutionCategory(StackEntry->GetExecutionCategoryName()));
	if (bIsCategoryIconHighlighted)
	{
		IconColor = FNiagaraEditorWidgetsStyle::Get().GetColor(FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(StackEntry->GetExecutionCategoryName()));
	}
	TSharedRef<SHorizontalBox> NameContent = SNew(SHorizontalBox)
	.Clipping(EWidgetClipping::OnDemand)
	// Indent
	+ SHorizontalBox::Slot()
	.AutoWidth()
	[
		SNew(SBox)
		.WidthOverride(this, &SNiagaraStackTableRow::GetIndentSize)
	]
	// Expand button
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0, 0, 1, 0)
	[
		SNew(SBox)
		.WidthOverride(14)
		[
			SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.Visibility(this, &SNiagaraStackTableRow::GetExpanderVisibility)
			.OnClicked(this, &SNiagaraStackTableRow::ExpandButtonClicked)
			.ForegroundColor(FSlateColor::UseForeground())
			.ContentPadding(2)
			.HAlign(HAlign_Center)
			[
				SNew(SImage)
				.Image(this, &SNiagaraStackTableRow::GetExpandButtonImage)
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		]
	]
	// Execution sub-category icon
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(FMargin(1, 1, 4, 1))
	.VAlign(EVerticalAlignment::VAlign_Center)
	.HAlign(EHorizontalAlignment::HAlign_Center)
	[
		SNew(SBox)
		.WidthOverride(FNiagaraEditorWidgetsStyle::Get().GetFloat("NiagaraEditor.Stack.IconHighlightedSize"))
		.HAlign(EHorizontalAlignment::HAlign_Center)
		.VAlign(EVerticalAlignment::VAlign_Center)
		.ToolTipText(ExecutionCategoryToolTipText)
		.Visibility(this, &SNiagaraStackTableRow::GetExecutionCategoryIconVisibility)
		.IsEnabled_UObject(StackEntry, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
		[
			SNew(SImage)
			.Visibility(this, &SNiagaraStackTableRow::GetExecutionCategoryIconVisibility)
			.Image(FNiagaraEditorWidgetsStyle::Get().GetBrush(FNiagaraStackEditorWidgetsUtilities::GetIconNameForExecutionSubcategory(StackEntry->GetExecutionSubcategoryName(), bIsCategoryIconHighlighted)))
			.ColorAndOpacity(IconColor)
		]
	]
	// Name content
	+ SHorizontalBox::Slot()
	[
		InNameWidget
	];

	TSharedPtr<SWidget> ChildContent;

	if (InValueWidget.IsValid())
	{
		ChildContent = SNew(SSplitter)
		.Style(FNiagaraEditorWidgetsStyle::Get(), "NiagaraEditor.Stack.Splitter")
		.PhysicalSplitterHandleSize(1.0f)
		.HitDetectionSplitterHandleSize(5.0f)

		+ SSplitter::Slot()
		.Value(NameColumnWidth)
		.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SNiagaraStackTableRow::OnNameColumnWidthChanged))
		[
			SNew(SBox)
			.Padding(FMargin(ContentPadding.Left, ContentPadding.Top, 5, ContentPadding.Bottom))
			.HAlign(NameHorizontalAlignment)
			.VAlign(NameVerticalAlignment)
			.MinDesiredWidth(NameMinWidth.IsSet() ? NameMinWidth.GetValue() : FOptionalSize())
			.MaxDesiredWidth(NameMaxWidth.IsSet() ? NameMaxWidth.GetValue() : FOptionalSize())
			[
				NameContent
			]
		]

		// Value
		+ SSplitter::Slot()
		.Value(ValueColumnWidth)
		.OnSlotResized(SSplitter::FOnSlotResized::CreateSP(this, &SNiagaraStackTableRow::OnValueColumnWidthChanged))
		[
			SNew(SBox)
			.Padding(FMargin(4, ContentPadding.Top, ContentPadding.Right, ContentPadding.Bottom))
			.HAlign(ValueHorizontalAlignment)
			.VAlign(ValueVerticalAlignment)
			.MinDesiredWidth(ValueMinWidth.IsSet() ? ValueMinWidth.GetValue() : FOptionalSize())
			.MaxDesiredWidth(ValueMaxWidth.IsSet() ? ValueMaxWidth.GetValue() : FOptionalSize())
			[
				InValueWidget.ToSharedRef()
			]
		];
	}
	else
	{
		ChildContent = SNew(SBox)
		.Padding(ContentPadding)
		.HAlign(NameHorizontalAlignment)
		.VAlign(NameVerticalAlignment)
		.MinDesiredWidth(NameMinWidth.IsSet() ? NameMinWidth.GetValue() : FOptionalSize())
		.MaxDesiredWidth(NameMaxWidth.IsSet() ? NameMaxWidth.GetValue() : FOptionalSize())
		[
			NameContent
		];
	}

	FName AccentColorName = FNiagaraStackEditorWidgetsUtilities::GetIconColorNameForExecutionCategory(StackEntry->GetExecutionCategoryName());
	FLinearColor AccentColor = AccentColorName != NAME_None ? FNiagaraEditorWidgetsStyle::Get().GetColor(AccentColorName) : FLinearColor::Transparent;
	ChildSlot
	[
		SNew(SHorizontalBox)
		.Visibility(this, &SNiagaraStackTableRow::GetRowVisibility)
		// Accent color.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(1, 0, 6, 0)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(AccentColor)
			.Padding(0)
			[
				SNew(SBox)
				.WidthOverride(4)
			]
		]
		// Content
		+ SHorizontalBox::Slot()
		[
			// Row content
			SNew(SBox)
			.Padding(RowPadding)
			[
				SNew(SBorder)
				.BorderImage(FEditorStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor(this, &SNiagaraStackTableRow::GetItemBackgroundColor)
				.ForegroundColor(ForegroundColor)
				.Padding(0)
				[
					SNew(SBorder)
					.BorderImage(this, &SNiagaraStackTableRow::GetSearchResultBorderBrush)
					.BorderBackgroundColor(FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.SearchHighlightColor"))
					.Padding(0)
					[
						SNew(SBorder)
						.BorderImage(this, &SNiagaraStackTableRow::GetSelectionBorderBrush)
						.Padding(0)
						[
							ChildContent.ToSharedRef()
						]
					]
				]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(3, 0, 0, 0)
		[
			SNew(SNiagaraStackIssueIcon, StackViewModel, StackEntry)
			.Visibility(IssueIconVisibility)
		]
	];
}

void SNiagaraStackTableRow::AddFillRowContextMenuHandler(FOnFillRowContextMenu FillRowContextMenuHandler)
{
	OnFillRowContextMenuHanders.Add(FillRowContextMenuHandler);
}

FReply SNiagaraStackTableRow::OnMouseButtonDoubleClick(const FGeometry& InMyGeometry, const FPointerEvent& InMouseEvent)
{
	return FReply::Unhandled();
}

FReply SNiagaraStackTableRow::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		TSharedPtr<ITypedTableView<UNiagaraStackEntry*>> OwnerTable = OwnerTablePtr.Pin();
		if (OwnerTable.IsValid())
		{
			if (OwnerTable->GetSelectedItems().Contains(StackEntry) == false)
			{
				OwnerTable->Private_ClearSelection();
				OwnerTable->Private_SetItemSelection(StackEntry, true, true);
				OwnerTable->Private_SignalSelectionChanged(ESelectInfo::OnMouseClick);
			}
		}

		FMenuBuilder MenuBuilder(true, StackCommandContext->GetCommands());
		for (FOnFillRowContextMenu& OnFillRowContextMenuHandler : OnFillRowContextMenuHanders)
		{
			OnFillRowContextMenuHandler.ExecuteIfBound(MenuBuilder);
		}

		FNiagaraStackEditorWidgetsUtilities::AddStackEntryAssetContextMenuActions(MenuBuilder, *StackEntry);
		StackCommandContext->AddEditMenuItems(MenuBuilder);

		TArray<UNiagaraStackEntry*> EntriesToProcess;
		TArray<UNiagaraStackEntry*> NavigationEntries;
		StackViewModel->GetPathForEntry(StackEntry, EntriesToProcess);
		for (UNiagaraStackEntry* Parent : EntriesToProcess)
		{
			UNiagaraStackItemGroup* GroupParent = Cast<UNiagaraStackItemGroup>(Parent);
			UNiagaraStackItem* ItemParent = Cast<UNiagaraStackItem>(Parent);
			if (GroupParent != nullptr || ItemParent != nullptr)
			{
				MenuBuilder.BeginSection("StackRowNavigation", LOCTEXT("NavigationMenuSection", "Navigation"));
				{
					if (GroupParent != nullptr)
					{
						MenuBuilder.AddMenuEntry(
							LOCTEXT("TopOfSection", "Top of Section"),
							FText::Format(LOCTEXT("NavigateToFormatted", "Navigate to {0}"), Parent->GetDisplayName()),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateSP(this, &SNiagaraStackTableRow::NavigateTo, Parent)));
					}
					if (ItemParent != nullptr)
					{
						MenuBuilder.AddMenuEntry(
							LOCTEXT("TopOfModule", "Top of Module"),
							FText::Format(LOCTEXT("NavigateToFormatted", "Navigate to {0}"), Parent->GetDisplayName()),
							FSlateIcon(),
							FUIAction(FExecuteAction::CreateSP(this, &SNiagaraStackTableRow::NavigateTo, Parent)));
					}
				}
				MenuBuilder.EndSection();
			}
		}

		MenuBuilder.BeginSection("StackActions", LOCTEXT("StackActions", "Stack Actions"));
		MenuBuilder.AddMenuEntry(
			LOCTEXT("ExpandAllItems", "Expand All"),
			LOCTEXT("ExpandAllItemsToolTip", "Expand all items under this header."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SNiagaraStackTableRow::ExpandChildren)));
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CollapseAllItems", "Collapse All"),
			LOCTEXT("CollapseAllItemsToolTip", "Collapse all items under this header."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateSP(this, &SNiagaraStackTableRow::CollapseChildren)));
		MenuBuilder.EndSection();

		FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
		FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
		return FReply::Handled();
	}
	return STableRow<UNiagaraStackEntry*>::OnMouseButtonUp(MyGeometry, MouseEvent);
}

const FSlateBrush* SNiagaraStackTableRow::GetBorder() const
{
	// Return no brush here so that the background doesn't change.  The border color changing will be handled by an internal border.
	return FEditorStyle::GetBrush("NoBrush");
}

void SNiagaraStackTableRow::CollapseChildren()
{
	TArray<UNiagaraStackEntry*> Children;
	StackEntry->GetUnfilteredChildren(Children);
	for (UNiagaraStackEntry* Child : Children)
	{
		if (Child->GetCanExpand())
		{
			Child->SetIsExpanded(false);
		}
	}
	// Calling SetIsExpanded doesn't broadcast structure change automatically due to the expense of synchronizing
	// expanded state with the tree to prevent items being expanded on tick, so we call this manually here.
	StackEntry->OnStructureChanged().Broadcast();
}

void SNiagaraStackTableRow::ExpandChildren()
{
	TArray<UNiagaraStackEntry*> Children;
	StackEntry->GetUnfilteredChildren(Children);
	for (UNiagaraStackEntry* Child : Children)
	{
		if (Child->GetCanExpand())
		{
			Child->SetIsExpanded(true);
		}
	}
	// Calling SetIsExpanded doesn't broadcast structure change automatically due to the expense of synchronizing
	// expanded state with the tree to prevent items being expanded on tick, so we call this manually here.
	StackEntry->OnStructureChanged().Broadcast();
}

EVisibility SNiagaraStackTableRow::GetRowVisibility() const
{
	return StackEntry->GetShouldShowInStack()
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

EVisibility SNiagaraStackTableRow::GetExecutionCategoryIconVisibility() const
{
	return bShowExecutionCategoryIcon && StackEntry->GetExecutionSubcategoryName() != NAME_None
		? EVisibility::Visible
		: EVisibility::Collapsed;
}

FOptionalSize SNiagaraStackTableRow::GetIndentSize() const
{
	return StackEntry->GetIndentLevel() * IndentSize;
}

EVisibility SNiagaraStackTableRow::GetExpanderVisibility() const
{
	if (StackEntry->GetCanExpand())
	{
		// TODO Cache this and refresh the cache when the entries structure changes.
		TArray<UNiagaraStackEntry*> Children;
		StackEntry->GetFilteredChildren(Children);
		return Children.Num() > 0
			? EVisibility::Visible
			: EVisibility::Hidden;
	}
	else
	{
		return EVisibility::Collapsed;
	}
}

FReply SNiagaraStackTableRow::ExpandButtonClicked()
{
	const bool bWillBeExpanded = !StackEntry->GetIsExpanded();
	// Recurse the expansion if "shift" is being pressed
	const FModifierKeysState ModKeyState = FSlateApplication::Get().GetModifierKeys();
	if (ModKeyState.IsShiftDown())
	{
		StackEntry->SetIsExpanded_Recursive(bWillBeExpanded);
	}
	else
	{
		StackEntry->SetIsExpanded(bWillBeExpanded);
	}
	// Calling SetIsExpanded doesn't broadcast structure change automatically due to the expense of synchronizing
	// expanded state with the tree to prevent items being expanded on tick, so we call this manually here.
	StackEntry->OnStructureChanged().Broadcast();
	return FReply::Handled();
}

const FSlateBrush* SNiagaraStackTableRow::GetExpandButtonImage() const
{
	return StackEntry->GetIsExpanded() ? ExpandedImage : CollapsedImage;
}

void SNiagaraStackTableRow::OnNameColumnWidthChanged(float Width)
{
	NameColumnWidthChanged.ExecuteIfBound(Width);
}

void SNiagaraStackTableRow::OnValueColumnWidthChanged(float Width)
{
	ValueColumnWidthChanged.ExecuteIfBound(Width);
}

FSlateColor SNiagaraStackTableRow::GetItemBackgroundColor() const
{
	return StackEntry->GetIsEnabledAndOwnerIsEnabled() 
		? ItemBackgroundColor 
		: DisabledItemBackgroundColor;
}

const FSlateBrush* SNiagaraStackTableRow::GetSelectionBorderBrush() const
{
	return STableRow<UNiagaraStackEntry*>::GetBorder();
}

const FSlateBrush* SNiagaraStackTableRow::GetSearchResultBorderBrush() const
{
	return StackViewModel->GetCurrentFocusedEntry() == StackEntry
		? FNiagaraEditorWidgetsStyle::Get().GetBrush("NiagaraEditor.Stack.SearchResult")
		: FEditorStyle::GetBrush("NoBrush");
}

void SNiagaraStackTableRow::NavigateTo(UNiagaraStackEntry* Item)
{
	OwnerTree->RequestNavigateToItem(Item, 0);
}

#undef LOCTEXT_NAMESPACE