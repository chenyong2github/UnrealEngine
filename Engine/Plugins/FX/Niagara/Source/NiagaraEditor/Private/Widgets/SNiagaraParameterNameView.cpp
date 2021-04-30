// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraParameterNameView.h"
#include "ViewModels/NiagaraParameterNameViewModel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"

class SWidget;

void SNiagaraParameterNameView::Construct(const FArguments& InArgs, const TSharedPtr<INiagaraParameterNameViewModel>& InParameterNameViewModel)
{
	ParameterNameViewModel = InParameterNameViewModel;

	ScopeSlotWidget = ParameterNameViewModel->CreateScopeSlotWidget();
	NameSlotWidget = ParameterNameViewModel->CreateTextSlotWidget();
	TSharedRef<SWidget> NameSlotWidgetMade = NameSlotWidget.ToSharedRef();
	TSharedRef<SWidget> ScopeSlotWidgetMade = ScopeSlotWidget.ToSharedRef();

	ChildSlot
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(3, 0)
		[
			ScopeSlotWidgetMade
		]
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(3, 0)
		[
			NameSlotWidgetMade
		]
	];
}

void SNiagaraParameterNameView::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) 
{
	if (bPendingRename)
	{
		NameSlotWidget.Get()->EnterEditingMode();
		bPendingRename = false;
	}
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
}
