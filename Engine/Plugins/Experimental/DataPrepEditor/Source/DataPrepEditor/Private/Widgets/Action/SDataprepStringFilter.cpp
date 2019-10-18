// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/Action/SDataprepStringFilter.h"

#include "DataprepEditorUtils.h"
#include "SchemaActions/DataprepFetcherMenuActionCollector.h"
#include "SchemaActions/DataprepSchemaAction.h"
#include "SelectionSystem/DataprepStringFilter.h"
#include "SelectionSystem/DataprepStringsArrayFilter.h"
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

template <class FilterType>
void SDataprepStringFilter<FilterType>::Construct(const FArguments& InArgs, FilterType& InFilter)
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

template <class FilterType>
TSharedRef<SWidget> SDataprepStringFilter<FilterType>::OnGenerateWidgetForMatchingCriteria(TSharedPtr<FListEntry> ListEntry) const
{
	return SNew(STextBlock)
		.Text( ListEntry->Get<0>() )
		.ToolTipText( ListEntry->Get<1>() );
}

template <class FilterType>
FText SDataprepStringFilter<FilterType>::GetSelectedCriteriaText() const
{
	check( Filter );
	UEnum* Enum = StaticEnum< EDataprepStringMatchType >();
	return Enum->GetDisplayNameTextByValue( static_cast<uint8>( Filter->GetStringMatchingCriteria() ) );
}

template <class FilterType>
FText SDataprepStringFilter<FilterType>::GetSelectedCriteriaTooltipText() const
{
	check( Filter );
	UEnum* Enum = StaticEnum< EDataprepStringMatchType >();
	return  Enum->GetToolTipTextByIndex( Enum->GetIndexByValue( static_cast<uint8>( Filter->GetStringMatchingCriteria() ) ) );
}

template <class FilterType>
void SDataprepStringFilter<FilterType>::OnCriteriaComboBoxOpenning()
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

template <class FilterType>
void SDataprepStringFilter<FilterType>::OnSelectedCriteriaChanged(TSharedPtr<FListEntry> ListEntry, ESelectInfo::Type SelectionType)
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

template <class FilterType>
FText SDataprepStringFilter<FilterType>::GetUserString() const
{
	check( Filter );
	return FText::FromString( Filter->GetUserString() );
}

template <class FilterType>
void SDataprepStringFilter<FilterType>::OnUserStringChanged(const FText& NewText)
{
	check( Filter );
	Filter->SetUserString( NewText.ToString() );
}

template <class FilterType>
void SDataprepStringFilter<FilterType>::OnUserStringComitted(const FText& NewText, ETextCommit::Type CommitType)
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

template <class FilterType>
void SDataprepStringFilter<FilterType>::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject( Filter );
}

// Explicit template instantiation
template class SDataprepStringFilter<UDataprepStringFilter>;
template class SDataprepStringFilter<UDataprepStringsArrayFilter>;

#undef LOCTEXT_NAMESPACE
