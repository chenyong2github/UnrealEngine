// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDVariantSetsList.h"

#include "SUSDStageEditorStyle.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "EditorStyleSet.h"

#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/STextComboBox.h"

#if USE_USD_SDK

namespace UsdVariantSetsListConstants
{
	const FMargin LeftRowPadding( 6.0f, 0.0f, 2.0f, 0.0f );
	const FMargin RightRowPadding( 3.0f, 0.0f, 2.0f, 0.0f );

	const TCHAR* NormalFont = TEXT("PropertyWindow.NormalFont");
}

void SUsdVariantRow::Construct( const FArguments& InArgs, TSharedPtr< FUsdVariantSetViewModel > InVariantSet, const TSharedRef< STableViewBase >& OwnerTable )
{
	OnVariantSelectionChanged = InArgs._OnVariantSelectionChanged;

	VariantSet = InVariantSet;

	SMultiColumnTableRow< TSharedPtr< FUsdVariantSetViewModel > >::Construct( SMultiColumnTableRow< TSharedPtr< FUsdVariantSetViewModel > >::FArguments(), OwnerTable );
}

TSharedRef< SWidget > SUsdVariantRow::GenerateWidgetForColumn( const FName& ColumnName )
{
	TSharedRef< SWidget > ColumnWidget = SNullWidget::NullWidget;

	bool bIsLeftRow = true;

	if ( ColumnName == TEXT("VariantSetName") )
	{
		SAssignNew( ColumnWidget, STextBlock )
		.Text( FText::FromString( VariantSet->SetName ) )
		.Font( FEditorStyle::GetFontStyle( UsdVariantSetsListConstants::NormalFont ) );
	}
	else
	{
		bIsLeftRow = false;

		TSharedPtr< FString >* InitialSelectionPtr = VariantSet->Variants.FindByPredicate(
			[ VariantSelection = VariantSet->VariantSelection ]( const TSharedPtr< FString >& A )
			{
				return A->Equals( *VariantSelection, ESearchCase::IgnoreCase );
			} );

		TSharedPtr< FString > InitialSelection = InitialSelectionPtr ? *InitialSelectionPtr : TSharedPtr< FString >();

		SAssignNew( ColumnWidget, STextComboBox )
				.OptionsSource( &VariantSet->Variants )
				.InitiallySelectedItem( InitialSelection )
				.OnSelectionChanged( this, &SUsdVariantRow::OnSelectionChanged )
				.Font( FEditorStyle::GetFontStyle( UsdVariantSetsListConstants::NormalFont ) );
	}

	return SNew(SBox)
		.HeightOverride( FUsdStageEditorStyle::Get()->GetFloat( "UsdStageEditor.ListItemHeight" ) )
		[
			SNew( SHorizontalBox )
			+ SHorizontalBox::Slot()
			.HAlign( HAlign_Left )
			.VAlign( VAlign_Center )
			.Padding( bIsLeftRow ? UsdVariantSetsListConstants::LeftRowPadding : UsdVariantSetsListConstants::RightRowPadding )
			.AutoWidth()
			[
				ColumnWidget
			]
		];
}

void SUsdVariantRow::OnSelectionChanged( TSharedPtr< FString > NewValue, ESelectInfo::Type SelectInfo )
{
	OnVariantSelectionChanged.ExecuteIfBound( VariantSet.ToSharedRef(), NewValue );
}

void SVariantsList::Construct( const FArguments& InArgs, const UE::FUsdStage& UsdStage, const TCHAR* InPrimPath )
{
	PrimPath = InPrimPath;

	ViewModel.UpdateVariantSets( UsdStage, InPrimPath );

	SAssignNew( HeaderRowWidget, SHeaderRow )

	+SHeaderRow::Column( FName( TEXT("VariantSetName") ) )
	.DefaultLabel( NSLOCTEXT( "USDVariantSetsList", "VariantSetName", "Variants" ) )
	.FillWidth( 25.f )

	+SHeaderRow::Column( FName( TEXT("VariantSetSelection") ) )
	.DefaultLabel( FText::GetEmpty() )
	.FillWidth( 75.f );

	SListView::Construct
	(
		SListView::FArguments()
		.ListItemsSource( &ViewModel.VariantSets )
		.OnGenerateRow( this, &SVariantsList::OnGenerateRow )
		.HeaderRow( HeaderRowWidget )
	);
}

TSharedRef< ITableRow > SVariantsList::OnGenerateRow( TSharedPtr< FUsdVariantSetViewModel > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable )
{
	return SNew( SUsdVariantRow, InDisplayNode, OwnerTable )
			.OnVariantSelectionChanged( this, &SVariantsList::OnVariantSelectionChanged );
}

void SVariantsList::SetPrimPath( const UE::FUsdStage& UsdStage, const TCHAR* InPrimPath )
{
	PrimPath = InPrimPath;
	ViewModel.UpdateVariantSets( UsdStage, *PrimPath );
	RequestListRefresh();
}

void SVariantsList::OnVariantSelectionChanged( const TSharedRef< FUsdVariantSetViewModel >& VariantSet, const TSharedPtr< FString >& NewValue )
{
	VariantSet->SetVariantSelection( NewValue );
}

#endif // #if USE_USD_SDK
