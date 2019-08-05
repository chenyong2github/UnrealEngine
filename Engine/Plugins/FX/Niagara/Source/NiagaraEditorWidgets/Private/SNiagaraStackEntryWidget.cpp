// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SNiagaraStackEntryWidget.h"
#include "NiagaraEditorWidgetsStyle.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SNiagaraStackEntryWidget"

void SNiagaraStackDisplayName::Construct(const FArguments& InArgs, UNiagaraStackEntry& InStackEntry, UNiagaraStackViewModel& InStackViewModel, FName InTextStyleName)
{
	StackEntry = &InStackEntry;
	StackViewModel = &InStackViewModel;
	TextStyleName = InTextStyleName;

	ColorAndOpacity = InArgs._ColorAndOpacity;

	StackViewModel->OnStructureChanged().AddSP(this, &SNiagaraStackDisplayName::StackViewModelStructureChanged);

	ChildSlot
	[
		SAssignNew(Container, SBox)
		[
			ConstructChildren()
		]
	];
}

TSharedRef<SWidget> SNiagaraStackDisplayName::ConstructChildren()
{
	TSharedRef<STextBlock> BaseNameWidget = SNew(STextBlock)
		.TextStyle(FNiagaraEditorWidgetsStyle::Get(), TextStyleName)
		.ToolTipText_UObject(StackEntry, &UNiagaraStackEntry::GetTooltipText)
		.Text_UObject(StackEntry, &UNiagaraStackEntry::GetDisplayName)
		.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
		.ColorAndOpacity(ColorAndOpacity)
		.IsEnabled_UObject(StackEntry, &UNiagaraStackEntry::GetIsEnabled);

	if (StackViewModel->GetTopLevelViewModels().Num() == 1)
	{
		TopLevelViewModelCountAtLastConstruction = 1;
		return BaseNameWidget;
	}
	else if (StackViewModel->GetTopLevelViewModels().Num() > 1)
	{
		TopLevelViewModelCountAtLastConstruction = StackViewModel->GetTopLevelViewModels().Num();
		TSharedPtr<UNiagaraStackViewModel::FTopLevelViewModel> TopLevelViewModel = StackViewModel->GetTopLevelViewModelForEntry(*StackEntry);
		if(TopLevelViewModel.IsValid())
		{
			return SNew(SWrapBox)
				.Clipping(EWidgetClipping::ClipToBoundsAlways)
				.UseAllottedWidth(true)
				+ SWrapBox::Slot()
				[
					SNew(STextBlock)
					.TextStyle(FNiagaraEditorWidgetsStyle::Get(), TextStyleName)
					.ToolTipText_UObject(StackEntry, &UNiagaraStackEntry::GetTooltipText)
					.Text(this, &SNiagaraStackDisplayName::GetTopLevelDisplayName, TWeakPtr<UNiagaraStackViewModel::FTopLevelViewModel>(TopLevelViewModel))
					.HighlightText_UObject(StackViewModel, &UNiagaraStackViewModel::GetCurrentSearchText)
					.ColorAndOpacity(ColorAndOpacity)
					.IsEnabled_UObject(StackEntry, &UNiagaraStackEntry::GetIsEnabled)
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
	if (StackViewModel->GetTopLevelViewModels().Num() != TopLevelViewModelCountAtLastConstruction)
	{
		Container->SetContent(ConstructChildren());
	}
}

FSlateColor SNiagaraStackEntryWidget::GetTextColorForSearch() const
{
	if (IsCurrentSearchMatch())
	{
		return FNiagaraEditorWidgetsStyle::Get().GetColor("NiagaraEditor.Stack.SearchHighlightColor");
	} 
	
	return FSlateColor::UseForeground();
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
