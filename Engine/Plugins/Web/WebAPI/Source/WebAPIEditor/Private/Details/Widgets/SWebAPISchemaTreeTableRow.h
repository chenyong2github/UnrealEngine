// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SWidget.h"
#include "Widgets/Views/STableRow.h"
#include "Widgets/Views/STableViewBase.h"

class IWebAPISchemaTreeTableRow
{
};

template <typename ItemType>
class SWebAPISchemaTreeTableRow
	: public STableRow<TSharedRef<ItemType>>
	, public IWebAPISchemaTreeTableRow
{
public:
	DECLARE_DELEGATE_OneParam(FOnColumnWidthChanged, float);
	
public:
	SLATE_BEGIN_ARGS(SWebAPISchemaTreeTableRow)
	{ }
		SLATE_DEFAULT_SLOT(typename SWebAPISchemaTreeTableRow<ItemType>::FArguments, Content)
	SLATE_END_ARGS()

public:
	void Construct(const FArguments& InArgs, const TSharedRef<ItemType>& InViewModel, const TSharedRef<STableViewBase>& InOwnerTableView);

protected:
	TSharedPtr<ItemType> ViewModel;
};

template <typename ItemType>
void SWebAPISchemaTreeTableRow<ItemType>::Construct(const FArguments& InArgs, const TSharedRef<ItemType>& InViewModel, const TSharedRef<STableViewBase>& InOwnerTableView)
{
	ViewModel = InViewModel;
	
	STableRow<TSharedRef<ItemType>>::Construct(
		SMultiColumnTableRow<TSharedRef<ItemType>>::FArguments()
		.Padding(0)		
		.Content()
		[
			// Wrap in border to use common Tooltip getter
			SNew(SBorder)
			.BorderBackgroundColor(FLinearColor(0,0,0,0))
			.ToolTipText(InViewModel->GetTooltip())
			[
				InArgs._Content.Widget
			]
		],
		InOwnerTableView);
}
