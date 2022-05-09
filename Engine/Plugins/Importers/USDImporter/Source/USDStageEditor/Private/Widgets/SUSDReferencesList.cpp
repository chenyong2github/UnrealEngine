// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDReferencesList.h"

#include "SUSDStageEditorStyle.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "Styling/AppStyle.h"

#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Input/STextComboBox.h"

#if USE_USD_SDK

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
		.Font( FAppStyle::GetFontStyle( UsdReferencesListConstants::NormalFont ) );
	}

	return SNew( SBox )
		.HeightOverride( FUsdStageEditorStyle::Get()->GetFloat( "UsdStageEditor.ListItemHeight" ) )
		[
			SNew( SHorizontalBox )
			+SHorizontalBox::Slot()
			.HAlign( HAlign_Left )
			.VAlign( VAlign_Center )
			.Padding( UsdReferencesListConstants::RowPadding )
			.AutoWidth()
			[
				ColumnWidget
			]
		];
}

void SUsdReferencesList::Construct( const FArguments& InArgs, const UE::FUsdStageWeak& UsdStage, const TCHAR* PrimPath )
{
	SAssignNew( HeaderRowWidget, SHeaderRow )

	+SHeaderRow::Column( FName( TEXT("AssetPath") ) )
	.DefaultLabel( NSLOCTEXT( "USDReferencesList", "References", "References" ) )
	.FillWidth( 100.f );

	SListView::Construct
	(
		SListView::FArguments()
		.ListItemsSource( &ViewModel.References )
		.OnGenerateRow( this, &SUsdReferencesList::OnGenerateRow )
		.HeaderRow( HeaderRowWidget )
	);

	SetPrimPath( UsdStage, PrimPath );
}

TSharedRef< ITableRow > SUsdReferencesList::OnGenerateRow( TSharedPtr< FUsdReference > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable )
{
	return SNew( SUsdReferenceRow, InDisplayNode, OwnerTable );
}

void SUsdReferencesList::SetPrimPath( const UE::FUsdStageWeak& UsdStage, const TCHAR* PrimPath )
{
	ViewModel.UpdateReferences( UsdStage, PrimPath );

	EVisibility NewVisibility = EVisibility::Collapsed;
	if ( UsdStage )
	{
		if ( UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath{ PrimPath } ) )
		{
			if ( UsdPrim.HasAuthoredReferences() )
			{
				NewVisibility = EVisibility::Visible;
			}
		}
	}
	SetVisibility( NewVisibility );

	RequestListRefresh();
}

#endif // #if USE_USD_SDK
