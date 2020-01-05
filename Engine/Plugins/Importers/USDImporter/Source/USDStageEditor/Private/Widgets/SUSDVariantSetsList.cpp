// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDVariantSetsList.h"

#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "EditorStyleSet.h"

#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/STextComboBox.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/variantSets.h"

#include "USDIncludesEnd.h"

namespace UsdVariantSetsListConstants
{
	const FMargin LeftRowPadding( 6.0f, 2.5f, 2.0f, 2.5f );
	const FMargin RightRowPadding( 3.0f, 2.5f, 2.0f, 2.5f );

	const TCHAR* NormalFont = TEXT("PropertyWindow.NormalFont");
}

void SUsdVariantRow::Construct( const FArguments& InArgs, TSharedPtr< FUsdVariantSet > InVariantSet, const TSharedRef< STableViewBase >& OwnerTable )
{
	OnVariantSelectionChanged = InArgs._OnVariantSelectionChanged;

	VariantSet = InVariantSet;

	SMultiColumnTableRow< TSharedPtr< FUsdVariantSet > >::Construct( SMultiColumnTableRow< TSharedPtr< FUsdVariantSet > >::FArguments(), OwnerTable );
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

	return SNew( SHorizontalBox )
		+SHorizontalBox::Slot()
		.HAlign( HAlign_Left )
		.VAlign( VAlign_Center )
		.Padding( bIsLeftRow ? UsdVariantSetsListConstants::LeftRowPadding : UsdVariantSetsListConstants::RightRowPadding )
		.AutoWidth()
		[
			ColumnWidget
		];
}

void SUsdVariantRow::OnSelectionChanged( TSharedPtr< FString > NewValue, ESelectInfo::Type SelectInfo )
{
	VariantSet->VariantSelection = NewValue;
	OnVariantSelectionChanged.ExecuteIfBound( VariantSet.ToSharedRef() );
}

void SVariantsList::Construct( const FArguments& InArgs, const TCHAR* InPrimPath )
{
	PrimPath = InPrimPath;

	UpdateVariantSets( InPrimPath );

	SAssignNew( HeaderRowWidget, SHeaderRow )
	.Visibility( EVisibility::Collapsed )

	+SHeaderRow::Column( FName( TEXT("VariantSetName") ) )
	.FillWidth( 20.f )

	+SHeaderRow::Column( FName( TEXT("VariantSetSelection") ) )
	.FillWidth( 80.f );

	SListView::Construct
	(
		SListView::FArguments()
		.ListItemsSource( &VariantSets )
		.OnGenerateRow( this, &SVariantsList::OnGenerateRow )
		.HeaderRow( HeaderRowWidget )
	);
}

TSharedRef< ITableRow > SVariantsList::OnGenerateRow( TSharedPtr< FUsdVariantSet > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable )
{
	return SNew( SUsdVariantRow, InDisplayNode, OwnerTable )
			.OnVariantSelectionChanged( this, &SVariantsList::OnVariantSelectionChanged );
}

void SVariantsList::UpdateVariantSets( const TCHAR* InPrimPath )
{
	VariantSets.Reset();

	IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
	AUsdStageActor* UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

	if ( !UsdStageActor )
	{
		return;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdStageRefPtr UsdStage = UsdStageActor->GetUsdStage();

	if ( !UsdStage )
	{
		return;
	}

	pxr::UsdPrim UsdPrim = UsdStage->GetPrimAtPath( UnrealToUsd::ConvertPath( InPrimPath ).Get() );

	if ( !UsdPrim )
	{
		return;
	}

	pxr::UsdVariantSets UsdVariantSets = UsdPrim.GetVariantSets();

	std::vector< std::string > UsdVariantSetsNames;
	UsdVariantSets.GetNames( &UsdVariantSetsNames );

	for ( const std::string& UsdVariantSetName : UsdVariantSetsNames )
	{
		FUsdVariantSet VariantSet;
		VariantSet.SetName = UsdToUnreal::ConvertString( UsdVariantSetName.c_str() );

		pxr::UsdVariantSet UsdVariantSet = UsdPrim.GetVariantSet( UsdVariantSetName.c_str() );
		VariantSet.VariantSelection = MakeSharedUnreal< FString >( UsdToUnreal::ConvertString( UsdVariantSet.GetVariantSelection().c_str() ) );

		std::vector< std::string > VariantNames = UsdVariantSet.GetVariantNames();

		for ( const std::string& VariantName : VariantNames )
		{
			VariantSet.Variants.Add( MakeSharedUnreal< FString >( UsdToUnreal::ConvertString( VariantName ) ) );
		}

		VariantSets.Add( MakeSharedUnreal< FUsdVariantSet >( MoveTemp( VariantSet ) ) );
	}
}

void SVariantsList::SetPrimPath( const TCHAR* InPrimPath )
{
	PrimPath = InPrimPath;
	UpdateVariantSets( *PrimPath );
	RequestListRefresh();
}

void SVariantsList::OnVariantSelectionChanged( const TSharedRef< FUsdVariantSet >& VariantSet )
{
	IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
	AUsdStageActor* UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

	if ( !UsdStageActor )
	{
		return;
	}

	FScopedUsdAllocs UsdAllocs;

	pxr::UsdStageRefPtr UsdStage = UsdStageActor->GetUsdStage();

	if ( !UsdStage )
	{
		return;
	}

	pxr::UsdPrim UsdPrim = UsdStage->GetPrimAtPath( UnrealToUsd::ConvertPath( *PrimPath ).Get() );

	if ( !UsdPrim )
	{
		return;
	}

	std::string UsdVariantSelection;

	if ( VariantSet->VariantSelection )
	{
		UsdVariantSelection = UnrealToUsd::ConvertString( *(*VariantSet->VariantSelection) ).Get();
	}

	pxr::UsdVariantSets UsdVariantSets = UsdPrim.GetVariantSets();
	UsdVariantSets.SetSelection( UnrealToUsd::ConvertString( *VariantSet->SetName ).Get(), UsdVariantSelection );
}

#endif // #if USE_USD_SDK
