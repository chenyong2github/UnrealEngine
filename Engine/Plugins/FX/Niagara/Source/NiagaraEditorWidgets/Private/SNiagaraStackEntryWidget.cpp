// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraStackEntryWidget.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SNiagaraStackEntryWidget"

void SNiagaraStackDisplayName::Construct(const FArguments& InArgs, UNiagaraStackEntry& InStackEntry, UNiagaraStackViewModel& InStackViewModel, FName InTextStyleName)
{
	StackEntryItem = &InStackEntry;
	StackViewModel = &InStackViewModel;
	TextStyleName = InTextStyleName;

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
	TSharedRef<STextBlock> BaseNameWidget = SNew(STextBlock)
		.TextStyle(FNiagaraEditorWidgetsStyle::Get(), TextStyleName)
		.ToolTipText_UObject(StackEntryItem, &UNiagaraStackEntry::GetTooltipText)
		.Text_UObject(StackEntryItem, &UNiagaraStackEntry::GetDisplayName)
		.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
		.ColorAndOpacity(this, &SNiagaraStackDisplayName::GetTextColorForSearch, FSlateColor::UseForeground())
		.IsEnabled(this, &SNiagaraStackDisplayName::GetIsEnabled);

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
		return BaseNameWidget;
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
					.IsEnabled(this, &SNiagaraStackDisplayName::GetIsEnabled)
				]
				+ SWrapBox::Slot()
				[
					BaseNameWidget
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

bool SNiagaraStackDisplayName::GetIsEnabled() const
{
	return StackEntryItem->GetOwnerIsEnabled() && StackEntryItem->GetIsEnabled();
}

FSlateColor SNiagaraStackEntryWidget::GetTextColorForSearch(FSlateColor DefaultColor) const
{
	if (IsCurrentSearchMatch())
	{
		return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.SearchHighlightColor");
	} 
	
	return DefaultColor;
}

bool SNiagaraStackEntryWidget::IsCurrentSearchMatch() const
{
	auto FocusedEntry = StackViewModel->GetCurrentFocusedEntry();
	return StackEntryItem != nullptr && FocusedEntry == StackEntryItem;
}

FReply SNiagaraStackEntryWidget::ExpandEntry()
{
	StackEntryItem->SetIsExpanded(true);
	StackEntryItem->OnStructureChanged().Broadcast();
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
