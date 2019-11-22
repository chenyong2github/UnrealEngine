// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Widgets/Action/SDataprepStringFilter.h"

#include "DataprepAsset.h"
#include "DataprepCoreUtils.h"
#include "DataprepEditorUtils.h"
#include "Parameterization/DataprepParameterizationUtils.h"
#include "SchemaActions/DataprepFetcherMenuActionCollector.h"
#include "SchemaActions/DataprepSchemaAction.h"
#include "SelectionSystem/DataprepStringFilter.h"
#include "SelectionSystem/DataprepStringsArrayFilter.h"
#include "Widgets/Action/DataprepActionWidgetsUtils.h"
#include "Widgets/Action/SDataprepFetcherSelector.h"
#include "Widgets/DataprepWidgets.h"
#include "Widgets/Parameterization/SDataprepParameterizationLinkIcon.h"

#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Internationalization/Text.h"
#include "Misc/AssertionMacros.h"
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

	if ( UDataprepAsset* DataprepAsset = FDataprepCoreUtils::GetDataprepAssetOfObject( &InFilter ) )
	{
		UClass* FilterClass = InFilter.GetClass();

		{
			FName PropertyName = FName( TEXT("StringMatchingCriteria") );
			UProperty* Property = FilterClass->FindPropertyByName( PropertyName );
			check( Property );
			TArray<FDataprepPropertyLink> PropertyChain;
			PropertyChain.Emplace( Property, PropertyName, INDEX_NONE );

			MatchingCriteriaParameterizationActionData = MakeShared<FDataprepParametrizationActionData>( *DataprepAsset, InFilter, PropertyChain );
		}

		{
			FName PropertyName = FName( TEXT("UserString") );
			UProperty* Property = FilterClass->FindPropertyByName( PropertyName );
			check( Property );
			TArray<FDataprepPropertyLink> PropertyChain;
			PropertyChain.Emplace( Property, PropertyName, INDEX_NONE );

			UserStringParameterizationActionData = MakeShared<FDataprepParametrizationActionData>( *DataprepAsset, InFilter, PropertyChain);
		}

		OnParameterizationStatusForObjectsChangedHandle = DataprepAsset->OnParameterizedObjectsChanged.AddSP( this, &SDataprepStringFilter<FilterType>::OnParameterizationStatusForObjectsChanged );
	}

	UpdateVisualDisplay();
}

template <class FilterType>
SDataprepStringFilter<FilterType>::~SDataprepStringFilter()
{
	if ( UDataprepAsset* DataprepAsset = FDataprepCoreUtils::GetDataprepAssetOfObject( Filter ) )
	{
		DataprepAsset->OnParameterizedObjectsChanged.Remove( OnParameterizationStatusForObjectsChangedHandle );
	}
}

template <class FilterType>
void SDataprepStringFilter<FilterType>::UpdateVisualDisplay()
{
	TSharedPtr<SHorizontalBox> MatchingCriteriaHorizontalBox;
	TSharedPtr<SHorizontalBox> UserStringHorizontalBox;

	ChildSlot
	[
		SNew( SBox )
		.MinDesiredWidth( 400.f )
		[
			SNew( SHorizontalBox )
			+ SHorizontalBox::Slot()
			.Padding( 5.f )
			[
				SNew( SDataprepContextMenuOverride )
				.OnContextMenuOpening( this, &SDataprepStringFilter<FilterType>::OnGetContextMenuForMatchingCriteria )
				[
					SAssignNew( MatchingCriteriaHorizontalBox, SHorizontalBox )
					+ SHorizontalBox::Slot()
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
				]
			
			]
			+ SHorizontalBox::Slot()
			.Padding( 5.f )
			[
				SNew( SDataprepContextMenuOverride )
				.OnContextMenuOpening( this, &SDataprepStringFilter<FilterType>::OnGetContextMenuForUserString )
				[
					SAssignNew( UserStringHorizontalBox, SHorizontalBox )
					+ SHorizontalBox::Slot()
					[
						SNew( SEditableTextBox )
						.Text( this, &SDataprepStringFilter::GetUserString )
						.ContextMenuExtender( this, &SDataprepStringFilter::ExtendContextMenuForUserStringBox )
						.OnTextChanged( this, &SDataprepStringFilter::OnUserStringChanged )
						.OnTextCommitted( this, &SDataprepStringFilter::OnUserStringComitted )
						.Justification( ETextJustify::Center )
					]
				]
			]
		]
	];

	if ( MatchingCriteriaParameterizationActionData && MatchingCriteriaParameterizationActionData->IsValid() )
	{
		if ( MatchingCriteriaParameterizationActionData->DataprepAsset->IsObjectPropertyBinded( Filter, MatchingCriteriaParameterizationActionData->PropertyChain ) )
		{
			MatchingCriteriaHorizontalBox->AddSlot()
				.HAlign( HAlign_Right )
				.VAlign( VAlign_Center )
				.Padding( FMargin(5.f, 0.f, 0.f, 0.f) )
				.AutoWidth()
				[
					SNew( SDataprepParameterizationLinkIcon, MatchingCriteriaParameterizationActionData->DataprepAsset, Filter, MatchingCriteriaParameterizationActionData->PropertyChain )
				];
		}
	}

	if ( UserStringParameterizationActionData && UserStringParameterizationActionData->IsValid() )
	{
		if ( UserStringParameterizationActionData->DataprepAsset->IsObjectPropertyBinded( Filter, UserStringParameterizationActionData->PropertyChain ) )
		{
			UserStringHorizontalBox->AddSlot()
				.HAlign( HAlign_Right )
				.VAlign( VAlign_Center )
				.Padding( FMargin(5.f, 0.f, 0.f, 0.f) )
				.AutoWidth()
				[
					SNew( SDataprepParameterizationLinkIcon, UserStringParameterizationActionData->DataprepAsset, Filter, UserStringParameterizationActionData->PropertyChain )
				];
		}
	}
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
TSharedPtr<SWidget> SDataprepStringFilter<FilterType>::OnGetContextMenuForMatchingCriteria()
{
	return FDataprepEditorUtils::MakeContextMenu( MatchingCriteriaParameterizationActionData );
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

		UProperty* Property = Filter->GetClass()->FindPropertyByName( TEXT("StringMatchingCriteria") );
		check( Property );

		FEditPropertyChain EditChain;
		EditChain.AddHead( Property );
		EditChain.SetActivePropertyNode( Property );
		FPropertyChangedEvent EditPropertyChangeEvent( Property, EPropertyChangeType::ValueSet );
		FPropertyChangedChainEvent EditChangeChainEvent( EditChain, EditPropertyChangeEvent );
		Filter->PostEditChangeChainProperty( EditChangeChainEvent );

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
void SDataprepStringFilter<FilterType>::ExtendContextMenuForUserStringBox(FMenuBuilder& MenuBuilder)
{
	FDataprepEditorUtils::PopulateMenuForParameterization( MenuBuilder, *UserStringParameterizationActionData->DataprepAsset,
		*Filter, UserStringParameterizationActionData->PropertyChain );
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

		UProperty* Property = Filter->GetClass()->FindPropertyByName( TEXT("UserString") );
		check( Property );

		FEditPropertyChain EditChain;
		EditChain.AddHead( Property );
		EditChain.SetActivePropertyNode( Property );
		FPropertyChangedEvent EditPropertyChangeEvent( Property, EPropertyChangeType::ValueSet );
		FPropertyChangedChainEvent EditChangeChainEvent( EditChain, EditPropertyChangeEvent );
		Filter->PostEditChangeChainProperty( EditChangeChainEvent );

		FDataprepEditorUtils::NotifySystemOfChangeInPipeline( Filter );
		OldUserString = NewUserString;
	}
}

template <class FilterType>
TSharedPtr<SWidget> SDataprepStringFilter<FilterType>::OnGetContextMenuForUserString()
{
	return FDataprepEditorUtils::MakeContextMenu( UserStringParameterizationActionData );
}

template <class FilterType>
void SDataprepStringFilter<FilterType>::OnParameterizationStatusForObjectsChanged(const TSet<UObject*>* Objects)
{
	if ( !Objects || Objects->Contains( Filter ) )
	{
		UpdateVisualDisplay();
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
