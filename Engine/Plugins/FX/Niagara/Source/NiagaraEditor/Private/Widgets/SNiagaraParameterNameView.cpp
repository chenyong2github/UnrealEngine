// Copyright Epic Games, Inc. All Rights Reserved.

#include "SNiagaraParameterNameView.h"
#include "ViewModels/NiagaraParameterNameViewModel.h"
#include "Widgets/SBoxPanel.h"

class SWidget;

void SNiagaraParameterNameView::Construct(const FArguments& InArgs, const TSharedPtr<INiagaraParameterNameViewModel>& InParameterNameViewModel)
{
	ParameterNameViewModel = InParameterNameViewModel;

	TSharedRef<SWidget> ScopeSlotWidget = ParameterNameViewModel->CreateScopeSlotWidget();
	TSharedRef<SWidget> NameSlotWidget = ParameterNameViewModel->CreateTextSlotWidget();

	ChildSlot
	[
		SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(3, 0)
		[
			ScopeSlotWidget
		]
		+SHorizontalBox::Slot()
		.VAlign(VAlign_Center)
		.Padding(3, 0)
		[
			NameSlotWidget
		]
	];
}
