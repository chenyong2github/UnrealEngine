// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/Action/SDataprepFetcherSelector.h"

#include "SchemaActions/DataprepFetcherMenuActionCollector.h"
#include "SelectionSystem/DataprepFilter.h"
#include "Widgets/SDataprepActionMenu.h"

#include "Framework/Text/TextLayout.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"

void SDataprepFetcherSelector::Construct(const FArguments& InArgs, UDataprepFilter& InFilter)
{
	Filter = &InFilter;
	
	ChildSlot
	[
		SAssignNew( FetcherTypeButton, SComboButton )
		.OnGetMenuContent( this, &SDataprepFetcherSelector::GetFetcherTypeSelector )
		.ButtonContent()
		[
			SNew( STextBlock )
			.Text( this, &SDataprepFetcherSelector::GetFetcherNameText )
			.Justification( ETextJustify::Center )
		]
	];
}

FText SDataprepFetcherSelector::GetFetcherNameText() const
{
	check( Filter );
	UDataprepFetcher* Fetcher = Filter->GetFetcher();
	if ( Fetcher )
	{
		return Fetcher->GetDisplayFetcherName();
	}

	return {};
}

TSharedRef<SWidget> SDataprepFetcherSelector::GetFetcherTypeSelector() const
{
	TSharedRef< SDataprepActionMenu > ActionMenu = SNew( SDataprepActionMenu, MakeUnique< FDataprepFetcherMenuActionCollector >( *Filter ) )
		.TransactionText( NSLOCTEXT( "SDataprepFetcherSelector", "ChangingFetcher", "Change Fetcher Type" ) );
	FetcherTypeButton->SetMenuContentWidgetToFocus( ActionMenu->GetFilterTextBox() );
	return ActionMenu;
}

void SDataprepFetcherSelector::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject( Filter );
}
