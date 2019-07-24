// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/Action/SDataprepFloatFilter.h"

#include "DataprepEditorUtils.h"
#include "SchemaActions/DataprepFetcherMenuActionCollector.h"
#include "SchemaActions/DataprepSchemaAction.h"
#include "SelectionSystem/DataprepFloatFilter.h"
#include "Widgets/Action/DataprepActionWidgetsUtils.h"
#include "Widgets/Action/SDataprepFetcherSelector.h"

#include "Internationalization/Text.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DataprepFloatFilter"

void SDataprepFloatFilter::Construct(const FArguments& InArgs, UDataprepFloatFilter& InFilter)
{
	Filter = &InFilter;
	OldEqualValue = Filter->GetEqualValue();
	OldTolerance = Filter->GetTolerance();

	DataprepActionWidgetsUtils::GenerateListEntriesFromEnum< EDataprepFloatMatchType >( FloatMatchingOptions );

	ChildSlot
	[
		SNew( SBox )
		.MinDesiredWidth( 400.f )
		[
			SNew( SVerticalBox )
			+ SVerticalBox::Slot()
			.AutoHeight()
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
					SAssignNew( FloatMatchingCriteriaWidget, SComboBox< TSharedPtr< FListEntry > > )
					.OptionsSource( &FloatMatchingOptions )
					.OnGenerateWidget( this, &SDataprepFloatFilter::OnGenerateWidgetForMatchingCriteria )
					.OnSelectionChanged( this, &SDataprepFloatFilter::OnSelectedCriteriaChanged )
					.OnComboBoxOpening( this, &SDataprepFloatFilter::OnCriteriaComboBoxOpenning )
					[
						SNew( STextBlock )
						.Text( this, &SDataprepFloatFilter::GetSelectedCriteriaText )
						.ToolTipText( this, &SDataprepFloatFilter::GetSelectedCriteriaTooltipText )
						.Justification( ETextJustify::Center )
					]
			
				]
				+ SHorizontalBox::Slot()
				.Padding( 5.f )
				[
					SNew( SSpinBox<float> )
					.Value( this, &SDataprepFloatFilter::GetEqualValue )
					.OnValueChanged( this, &SDataprepFloatFilter::OnEqualValueChanged )
					.OnValueCommitted( this, &SDataprepFloatFilter::OnEqualValueComitted )
					.Justification( ETextJustify::Center )
					.MinValue( TOptional< float >() )
					.MaxValue( TOptional< float >() )
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				.Visibility( this, &SDataprepFloatFilter::GetToleranceRowVisibility )
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign( VAlign_Center )
				.Padding( 5.f )
				[
					SNew( STextBlock )
					.Text( LOCTEXT("ToleranceText", "Tolerance") )
					.ToolTipText(this, &SDataprepFloatFilter::GetSelectedCriteriaTooltipText)
					.Justification( ETextJustify::Center )
				]
				+ SHorizontalBox::Slot()
				.Padding( 5.f )
				[
					SNew( SSpinBox<float> )
					.Value( this, &SDataprepFloatFilter::GetTolerance )
					.OnValueChanged( this, &SDataprepFloatFilter::OnToleranceChanged )
					.OnValueCommitted( this, &SDataprepFloatFilter::OnToleranceComitted )
					.Justification( ETextJustify::Center )
					.MinValue( TOptional< float >() )
					.MaxValue( TOptional< float >() )
				]
			]
		]
	];
}

TSharedRef<SWidget> SDataprepFloatFilter::OnGenerateWidgetForMatchingCriteria(TSharedPtr<FListEntry> ListEntry) const
{
	return SNew( STextBlock )
		.Text( ListEntry->Get<0>() )
		.ToolTipText( ListEntry->Get<1>() );
}

FText SDataprepFloatFilter::GetSelectedCriteriaText() const
{
	check( Filter );
	UEnum* Enum = StaticEnum< EDataprepFloatMatchType >();
	return Enum->GetDisplayNameTextByValue( static_cast<uint8>( Filter->GetFloatMatchingCriteria() ) );
}

FText SDataprepFloatFilter::GetSelectedCriteriaTooltipText() const
{
	check( Filter );
	UEnum* Enum = StaticEnum< EDataprepFloatMatchType >();
	return Enum->GetToolTipTextByIndex( Enum->GetIndexByValue( static_cast<uint8>( Filter->GetFloatMatchingCriteria() ) ) );
}

void SDataprepFloatFilter::OnSelectedCriteriaChanged(TSharedPtr<FListEntry> ListEntry, ESelectInfo::Type SelectionType)
{
	check( Filter );
	UEnum* Enum = StaticEnum< EDataprepFloatMatchType >();
	EDataprepFloatMatchType FloatMatchType = static_cast< EDataprepFloatMatchType >( Enum->GetValueByIndex( ListEntry->Get<2>() ) );

	if ( FloatMatchType != Filter->GetFloatMatchingCriteria() )
	{
		FScopedTransaction Transaction( LOCTEXT("SelectionCriteriaChangedTransaction", "Changed the Float Selection Criteria") );
		Filter->SetFloatMatchingCriteria( FloatMatchType );
		FDataprepEditorUtils::NotifySystemOfChangeInPipeline( Filter );
	}
}

void SDataprepFloatFilter::OnCriteriaComboBoxOpenning()
{
	check( Filter );
	UEnum* Enum = StaticEnum< EDataprepFloatMatchType >();
	int32 EnumValueMapping = Enum->GetIndexByValue( static_cast<uint8>( Filter->GetFloatMatchingCriteria() ) );

	TSharedPtr<FListEntry> ItemToSelect;
	for ( const TSharedPtr<FListEntry>& Entry : FloatMatchingOptions )
	{
		if ( Entry->Get<2>() == EnumValueMapping )
		{
			ItemToSelect = Entry;
			break;
		}
	}

	check( FloatMatchingCriteriaWidget );
	FloatMatchingCriteriaWidget->SetSelectedItem( ItemToSelect );
}

void SDataprepFloatFilter::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject( Filter );
}

float SDataprepFloatFilter::GetEqualValue() const
{
	check( Filter );
	return Filter->GetEqualValue();
}

void SDataprepFloatFilter::OnEqualValueChanged(float NewEqualValue)
{
	check( Filter );
	Filter->SetEqualValue( NewEqualValue );
}

void SDataprepFloatFilter::OnEqualValueComitted(float NewEqualValue, ETextCommit::Type CommitType)
{
	check( Filter );

	if ( OldEqualValue != NewEqualValue )
	{
		// Trick for the transaction 
		Filter->SetEqualValue( OldEqualValue );
		FScopedTransaction Transaction( LOCTEXT("EqualValueChangedTransaction","Change the Equal Value") );
		Filter->SetEqualValue( NewEqualValue );
		FDataprepEditorUtils::NotifySystemOfChangeInPipeline( Filter );
		OldEqualValue = NewEqualValue;
	}
}

EVisibility SDataprepFloatFilter::GetToleranceRowVisibility() const
{
	check( Filter );
	return Filter->GetFloatMatchingCriteria() == EDataprepFloatMatchType::IsNearlyEqual ? EVisibility::Visible : EVisibility::Collapsed;
}

float SDataprepFloatFilter::GetTolerance() const
{
	check( Filter );
	return Filter->GetTolerance();
}

void SDataprepFloatFilter::OnToleranceChanged(float NewTolerance)
{
	check( Filter );
	Filter->SetTolerance( NewTolerance );
}

void SDataprepFloatFilter::OnToleranceComitted(float NewTolerance, ETextCommit::Type CommitType)
{
	check( Filter );

	if ( OldTolerance != NewTolerance )
	{
		// Trick for the transaction 
		Filter->SetTolerance( OldTolerance );
		FScopedTransaction Transaction( LOCTEXT("ToleranceChangedTransaction", "Change the Tolerance") );
		Filter->SetTolerance( NewTolerance );
		FDataprepEditorUtils::NotifySystemOfChangeInPipeline( Filter );
		OldTolerance = NewTolerance;
	}
}

#undef LOCTEXT_NAMESPACE
