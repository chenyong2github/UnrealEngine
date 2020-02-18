// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraStackEntryWidget.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "NiagaraStackEditorData.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

#define LOCTEXT_NAMESPACE "SNiagaraStackEntryWidget"

void SNiagaraStackDisplayName::Construct(const FArguments& InArgs, UNiagaraStackEntry& InStackEntry, UNiagaraStackViewModel& InStackViewModel, FName InTextStyleName)
{
	StackEntryItem = &InStackEntry;
	StackViewModel = &InStackViewModel;
	TextStyleName = InTextStyleName;

	TypeNameStyle = InArgs._TypeNameStyle;
	OnRenameCommitted = InArgs._OnRenameCommitted;

	StackViewModel->OnStructureChanged().AddSP(this, &SNiagaraStackDisplayName::StackViewModelStructureChanged);

	ChildSlot
	[
		SAssignNew(Container, SBox)
		[
			ConstructChildren()
		]
	];
}

SNiagaraStackDisplayName::~SNiagaraStackDisplayName()
{
	StackViewModel->OnStructureChanged().RemoveAll(this);
}

TSharedRef<SWidget> SNiagaraStackDisplayName::ConstructChildren()
{
	const FInlineEditableTextBlockStyle& EditableStyle = FNiagaraEditorWidgetsStyle::Get().GetWidgetStyle<FInlineEditableTextBlockStyle>(TextStyleName);

	TSharedPtr<SWidget> BaseNameWidget;
	if (StackEntryItem->SupportsRename())
	{
		BaseNameWidget = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Bottom)
			[
				SAssignNew(EditableTextBlock, SInlineEditableTextBlock)
					.Style(&EditableStyle)
					.ToolTipText_UObject(StackEntryItem, &UNiagaraStackEntry::GetTooltipText)
					.Text_UObject(StackEntryItem, &UNiagaraStackEntry::GetDisplayName)
					.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
					.ColorAndOpacity(this, &SNiagaraStackEntryWidget::GetTextColorForSearch, FSlateColor::UseForeground())
					.OnTextCommitted(OnRenameCommitted)
					.IsEnabled_UObject(StackEntryItem, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(5,0,0,0)
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Bottom)
			[
				SNew(STextBlock)
				.TextStyle(TypeNameStyle)
				.ToolTipText_UObject(StackEntryItem, &UNiagaraStackEntry::GetTooltipText)
				.Text(this, &SNiagaraStackDisplayName::GetOriginalName)
				.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
				.ColorAndOpacity(this, &SNiagaraStackDisplayName::GetTextColorForSearch, FSlateColor::UseSubduedForeground())
				.IsEnabled_UObject(StackEntryItem, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				.Visibility(this, &SNiagaraStackDisplayName::ShouldShowOriginalName)
			];
	}
	else
	{
		BaseNameWidget = SNew(STextBlock)
			.TextStyle(&EditableStyle.TextStyle)
			.ToolTipText_UObject(StackEntryItem, &UNiagaraStackEntry::GetTooltipText)
			.Text_UObject(StackEntryItem, &UNiagaraStackEntry::GetDisplayName)
			.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
			.ColorAndOpacity(this, &SNiagaraStackDisplayName::GetTextColorForSearch, FSlateColor::UseForeground())
			.IsEnabled_UObject(StackEntryItem, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled);
	}

	int32 NumTopLevelEmitters = 0;
	for (const TSharedRef<UNiagaraStackViewModel::FTopLevelViewModel>& TopLevelViewModel : StackViewModel->GetTopLevelViewModels())
	{
		if (TopLevelViewModel->EmitterHandleViewModel.IsValid())
		{
			NumTopLevelEmitters++;
		}
	}

	if (NumTopLevelEmitters <= 1)
	{
		TopLevelViewModelCountAtLastConstruction = 1;
		return BaseNameWidget.ToSharedRef();
	}
	else
	{
		TopLevelViewModelCountAtLastConstruction = StackViewModel->GetTopLevelViewModels().Num();
		TSharedPtr<UNiagaraStackViewModel::FTopLevelViewModel> TopLevelViewModel = StackViewModel->GetTopLevelViewModelForEntry(*StackEntryItem);
		if(TopLevelViewModel.IsValid())
		{
			return SNew(SWrapBox)
				.Clipping(EWidgetClipping::ClipToBoundsAlways)
				.UseAllottedWidth(true)
				+ SWrapBox::Slot()
				[
					SNew(STextBlock)
					.TextStyle(FNiagaraEditorWidgetsStyle::Get(), TextStyleName)
					.ToolTipText_UObject(StackEntryItem, &UNiagaraStackEntry::GetTooltipText)
					.Text(this, &SNiagaraStackDisplayName::GetTopLevelDisplayName, TWeakPtr<UNiagaraStackViewModel::FTopLevelViewModel>(TopLevelViewModel))
					.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
					.ColorAndOpacity(this, &SNiagaraStackDisplayName::GetTextColorForSearch, FSlateColor::UseForeground())
					.IsEnabled_UObject(StackEntryItem, &UNiagaraStackEntry::GetIsEnabledAndOwnerIsEnabled)
				]
				+ SWrapBox::Slot()
				[
					BaseNameWidget.ToSharedRef()
				];
		}
	}
	TopLevelViewModelCountAtLastConstruction = -1;
	return SNullWidget::NullWidget;
}

FText SNiagaraStackDisplayName::GetTopLevelDisplayName(TWeakPtr<UNiagaraStackViewModel::FTopLevelViewModel> TopLevelViewModelWeak) const
{
	TSharedPtr<UNiagaraStackViewModel::FTopLevelViewModel> TopLevelViewModel = TopLevelViewModelWeak.Pin();
	if (TopLevelViewModel.IsValid())
	{
		if (TopLevelViewModel->GetDisplayName().IdenticalTo(TopLevelDisplayNameCache) == false)
		{
			TopLevelDisplayNameCache = TopLevelViewModel->GetDisplayName();
			TopLevelDisplayNameFormattedCache = FText::Format(LOCTEXT("TopLevelDisplayNameFormat", "{0} - "), TopLevelDisplayNameCache);
		}
	}
	else
	{
		TopLevelDisplayNameFormattedCache = FText();
	}
	return TopLevelDisplayNameFormattedCache;
}

void SNiagaraStackDisplayName::StackViewModelStructureChanged()
{
	if (StackEntryItem->IsFinalized() == false && StackViewModel->GetTopLevelViewModels().Num() != TopLevelViewModelCountAtLastConstruction)
	{
		Container->SetContent(ConstructChildren());
	}
}

FText SNiagaraStackDisplayName::GetOriginalName() const
{
	if (StackEntryItem->IsFinalized() == false)
	{
		return FText::Format(FTextFormat::FromString(TEXT("({0})")), StackEntryItem->GetOriginalName());
	}
	return FText::GetEmpty();
}

EVisibility SNiagaraStackDisplayName::ShouldShowOriginalName() const
{
	const FText* CurrentDisplayName = StackEntryItem->GetStackEditorData().GetStackEntryDisplayName(StackEntryItem->GetStackEditorDataKey());
	return CurrentDisplayName != nullptr ? EVisibility::Visible : EVisibility::Collapsed;
}

void SNiagaraStackDisplayName::StartRename()
{
	if (EditableTextBlock.IsValid())
	{
		EditableTextBlock->EnterEditingMode();
	}
}

FSlateColor SNiagaraStackEntryWidget::GetTextColorForSearch(FSlateColor DefaultColor) const
{
	if (IsCurrentSearchMatch())
	{
		return FSlateColor(FLinearColor(FColor::Orange));
	}
	return DefaultColor;
}

bool SNiagaraStackEntryWidget::IsCurrentSearchMatch() const
{
	UNiagaraStackEntry* FocusedEntry = StackViewModel->GetCurrentFocusedEntry();
	return StackEntryItem != nullptr && FocusedEntry == StackEntryItem;
}

FReply SNiagaraStackEntryWidget::ExpandEntry()
{
	StackEntryItem->SetIsExpanded(true);
	StackEntryItem->OnStructureChanged().Broadcast();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
