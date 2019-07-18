// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/Action/SDataprepStringFilter.h"

#include "DataprepEditorUtils.h"
#include "SchemaActions/DataprepFetcherMenuActionCollector.h"
#include "SchemaActions/DataprepSchemaAction.h"
#include "SelectionSystem/DataprepStringFilter.h"
#include "Widgets/Action/DataprepActionWidgetsUtils.h"
#include "Widgets/Action/SDataprepFetcherSelector.h"

#include "Internationalization/Text.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DataprepStringFilter"

void SDataprepStringFilter::Construct(const FArguments& InArgs, UDataprepStringFilter& InFilter)
{
	Filter = &InFilter;
	OldUserString = Filter->GetUserString();

	DataprepActionWidgetsUtils::GenerateListEntriesFromEnum< EDataprepStringMatchType >( StringMatchingOptions );

	ChildSlot
	[
		SNew( SBox )
		.MinDesiredWidth( 400.f )
		[
			SNew( SHorizontalBox )
			+ SHorizontalBox::Slot()
			.Padding( 5.f )
			[
				SNew( SDataprepFetcherSelector, InFilter )
			]
			+ SHorizontalBox::Slot()
			.Padding( 5.f )
			[
				SAssignNew( StringMatchingCriteriaWidget, SComboBox< TSharedPtr< FListEntry > > )
				.OptionsSource( &StringMatchingOptions )
				.OnGenerateWidget( this, &SDataprepStringFilter::OnGenerateWidgetForMatchingCriteria )
				.OnSelectionChanged( this, &SDataprepStringFilter::OnSelectedCriteriaChanged )
				.OnComboBoxOpening( this, &SDataprepStringFilter::OnCriteriaComboBoxOpenning )
				[
					SNew( STextBlock )
					.Text( this, &SDataprepStringFilter::GetSelectedCriteriaText )
					.ToolTipText( this, &SDataprepStringFilter::GetSelectedCriteriaTooltipText )
					.Justification( ETextJustify::Center )
				]
			
			]
			+ SHorizontalBox::Slot()
			.Padding( 5.f )
			[
				SNew( SEditableTextBox )
				.Text( this, &SDataprepStringFilter::GetUserString )
				.OnTextChanged( this, &SDataprepStringFilter::OnUserStringChanged )
				.OnTextCommitted( this, &SDataprepStringFilter::OnUserStringComitted )
				.Justification( ETextJustify::Center )
			]
		]
	];
}

TSharedRef<SWidget> SDataprepStringFilter::OnGenerateWidgetForMatchingCriteria(TSharedPtr<FListEntry> ListEntry) const
{
	return SNew(STextBlock)
		.Text( ListEntry->Get<0>() )
		.ToolTipText( ListEntry->Get<1>() );
}

FText SDataprepStringFilter::GetSelectedCriteriaText() const
{
	check( Filter );
	UEnum* Enum = StaticEnum< EDataprepStringMatchType >();
	return Enum->GetDisplayNameTextByValue( static_cast<uint8>( Filter->GetStringMatchingCriteria() ) );
}

FText SDataprepStringFilter::GetSelectedCriteriaTooltipText() const
{
	check( Filter );
	UEnum* Enum = StaticEnum< EDataprepStringMatchType >();
	return  Enum->GetToolTipTextByIndex( Enum->GetIndexByValue( static_cast<uint8>( Filter->GetStringMatchingCriteria() ) ) );
}

void SDataprepStringFilter::OnCriteriaComboBoxOpenning()
{
	check( Filter );
	UEnum* Enum = StaticEnum< EDataprepStringMatchType >();
	int32 EnumValueMapping = Enum->GetIndexByValue( static_cast<uint8>( Filter->GetStringMatchingCriteria() ) );

	TSharedPtr<FListEntry> ItemToSelect;
	for ( const TSharedPtr<FListEntry>& Entry : StringMatchingOptions )
	{
		if ( Entry->Get<2>() == EnumValueMapping )
		{
			ItemToSelect = Entry;
			break;
		}
	}

	check( StringMatchingCriteriaWidget );
	StringMatchingCriteriaWidget->SetSelectedItem( ItemToSelect );
}

void SDataprepStringFilter::OnSelectedCriteriaChanged(TSharedPtr<FListEntry> ListEntry, ESelectInfo::Type SelectionType)
{
	check( Filter );
	UEnum* Enum = StaticEnum< EDataprepStringMatchType >();
	EDataprepStringMatchType StringMatchType = static_cast< EDataprepStringMatchType >( Enum->GetValueByIndex( ListEntry->Get<2>() ) );

	if ( StringMatchType != Filter->GetStringMatchingCriteria() )
	{	
		FScopedTransaction Transaction( LOCTEXT("SelectionCriteriaChangedTransaction","Changed the String Selection Criteria") );
		Filter->SetStringMatchingCriteria( StringMatchType );
		FDataprepEditorUtils::NotifySystemOfChangeInPipeline( Filter );
	}
}

FText SDataprepStringFilter::GetUserString() const
{
	check( Filter );
	return FText::FromString( Filter->GetUserString() );
}

void SDataprepStringFilter::OnUserStringChanged(const FText& NewText)
{
	check( Filter );
	Filter->SetUserString( NewText.ToString() );
}

void SDataprepStringFilter::OnUserStringComitted(const FText& NewText, ETextCommit::Type CommitType)
{
	check( Filter );
	FString NewUserString = NewText.ToString();
	if ( OldUserString != NewUserString )
	{
		Filter->SetUserString( OldUserString );
		FScopedTransaction Transaction( LOCTEXT("SelectionStringChangedTransaction","Changed the Selection String") );
		Filter->SetUserString( NewUserString );
		FDataprepEditorUtils::NotifySystemOfChangeInPipeline( Filter );
		OldUserString = NewUserString;
	}
}

void SDataprepStringFilter::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject( Filter );
}

#undef LOCTEXT_NAMESPACE
