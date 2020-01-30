// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDReferencesList.h"

#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "EditorStyleSet.h"

#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/STextComboBox.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/usd/sdf/reference.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/variantSets.h"

#include "USDIncludesEnd.h"

namespace UsdReferencesListConstants
{
	const FMargin RowPadding( 6.0f, 2.5f, 2.0f, 2.5f );

	const TCHAR* NormalFont = TEXT("PropertyWindow.NormalFont");
}

void SUsdReferenceRow::Construct( const FArguments& InArgs, TSharedPtr< FUsdReference > InReference, const TSharedRef< STableViewBase >& OwnerTable )
{
	Reference = InReference;

	SMultiColumnTableRow< TSharedPtr< FUsdReference > >::Construct( SMultiColumnTableRow< TSharedPtr< FUsdReference > >::FArguments(), OwnerTable );
}

TSharedRef< SWidget > SUsdReferenceRow::GenerateWidgetForColumn( const FName& ColumnName )
{	
	TSharedRef< SWidget > ColumnWidget = SNullWidget::NullWidget;

	if ( ColumnName == TEXT("AssetPath") )
	{
		SAssignNew( ColumnWidget, STextBlock )
		.Text( FText::FromString( Reference->AssetPath ) )
		.Font( FEditorStyle::GetFontStyle( UsdReferencesListConstants::NormalFont ) );
	}

	return SNew( SHorizontalBox )
		+SHorizontalBox::Slot()
		.HAlign( HAlign_Left )
		.VAlign( VAlign_Center )
		.Padding( UsdReferencesListConstants::RowPadding )
		.AutoWidth()
		[
			ColumnWidget
		];
}

void SUsdReferencesList::Construct( const FArguments& InArgs, const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath )
{
	UpdateReferences( UsdStage, PrimPath );

	SAssignNew( HeaderRowWidget, SHeaderRow )
	.Visibility( EVisibility::Collapsed )

	+SHeaderRow::Column( FName( TEXT("AssetPath") ) )
	.FillWidth( 100.f );

	SListView::Construct
	(
		SListView::FArguments()
		.ListItemsSource( &References )
		.OnGenerateRow( this, &SUsdReferencesList::OnGenerateRow )
		.HeaderRow( HeaderRowWidget )
	);
}

TSharedRef< ITableRow > SUsdReferencesList::OnGenerateRow( TSharedPtr< FUsdReference > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable )
{
	return SNew( SUsdReferenceRow, InDisplayNode, OwnerTable );
}

void SUsdReferencesList::UpdateReferences( const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath )
{
	References.Reset();

	if ( !UsdStage.Get() )
	{
		return;
	}

	FScopedUsdAllocs UsdAllocs;
	
	pxr::SdfPrimSpecHandle PrimSpec = UsdStage.Get()->GetRootLayer()->GetPrimAtPath( UnrealToUsd::ConvertPath( PrimPath ).Get() );

	if ( PrimSpec )
	{
		pxr::SdfReferencesProxy ReferencesProxy = PrimSpec->GetReferenceList();

		for ( const pxr::SdfReference& UsdReference : ReferencesProxy.GetAddedOrExplicitItems() )
		{
			FUsdReference Reference;
			Reference.AssetPath = UsdToUnreal::ConvertString( UsdReference.GetAssetPath() );

			References.Add( MakeSharedUnreal< FUsdReference >( MoveTemp( Reference ) ) );
		}
	}
}

void SUsdReferencesList::SetPrimPath( const TUsdStore< pxr::UsdStageRefPtr >& UsdStage, const TCHAR* PrimPath )
{
	UpdateReferences( UsdStage, PrimPath );
	RequestListRefresh();
}

#endif // #if USE_USD_SDK
