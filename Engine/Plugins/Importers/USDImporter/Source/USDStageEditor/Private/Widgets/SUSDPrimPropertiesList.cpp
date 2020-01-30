// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDPrimPropertiesList.h"

#include "UnrealUSDWrapper.h"
#include "USDMemory.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/usd/usd/prim.h"

#include "USDIncludesEnd.h"


#define LOCTEXT_NAMESPACE "SUsdPrimPropertiesList"

namespace UsdPrimPropertiesListConstants
{
	const FMargin LeftRowPadding( 6.0f, 2.5f, 2.0f, 2.5f );
	const FMargin RightRowPadding( 3.0f, 2.5f, 2.0f, 2.5f );

	const TCHAR* NormalFont = TEXT("PropertyWindow.NormalFont");
}

class SUsdPrimPropertyRow : public SMultiColumnTableRow< TSharedPtr< FUsdPrimProperty > >
{
	SLATE_BEGIN_ARGS( SUsdPrimPropertyRow ) {}
	SLATE_END_ARGS()

public:
	void Construct( const FArguments& InArgs, const TSharedPtr< FUsdPrimProperty >& InUsdPrimProperty, const TSharedRef< STableViewBase >& OwnerTable );
	virtual TSharedRef< SWidget > GenerateWidgetForColumn( const FName& ColumnName ) override;

	void SetUsdPrimProperty( const TSharedPtr< FUsdPrimProperty >& InUsdPrimProperty );

protected:
	FText GetLabel() const { return FText::FromString( UsdPrimProperty->Label ); }
	FText GetValue() const { return FText::FromString( UsdPrimProperty->Value ); }
	
private:
	TSharedPtr< FUsdPrimProperty > UsdPrimProperty;
};

void SUsdPrimPropertyRow::Construct( const FArguments& InArgs, const TSharedPtr< FUsdPrimProperty >& InUsdPrimProperty, const TSharedRef< STableViewBase >& OwnerTable )
{
	SetUsdPrimProperty( InUsdPrimProperty );

	SMultiColumnTableRow< TSharedPtr< FUsdPrimProperty > >::Construct( SMultiColumnTableRow< TSharedPtr< FUsdPrimProperty > >::FArguments(), OwnerTable );
}

TSharedRef< SWidget > SUsdPrimPropertyRow::GenerateWidgetForColumn( const FName& ColumnName )
{	
	TSharedRef< SWidget > ColumnWidget = SNullWidget::NullWidget;

	if ( ColumnName == TEXT("PropertyName") )
	{
		SAssignNew( ColumnWidget, STextBlock )
		.Text( this, &SUsdPrimPropertyRow::GetLabel )
		.Font( FEditorStyle::GetFontStyle( UsdPrimPropertiesListConstants::NormalFont ) )
		.Margin( UsdPrimPropertiesListConstants::LeftRowPadding );
	}
	else
	{
		SAssignNew( ColumnWidget, STextBlock )
		.Text( this, &SUsdPrimPropertyRow::GetValue )
		.Font( FEditorStyle::GetFontStyle( UsdPrimPropertiesListConstants::NormalFont ) )
		.Margin( UsdPrimPropertiesListConstants::RightRowPadding );
	}

	return SNew( SHorizontalBox )
		+SHorizontalBox::Slot()
		.HAlign( HAlign_Left )
		.VAlign( VAlign_Center )
		.AutoWidth()
		[
			ColumnWidget
		];
}

void SUsdPrimPropertyRow::SetUsdPrimProperty( const TSharedPtr< FUsdPrimProperty >& InUsdPrimProperty )
{
	UsdPrimProperty = InUsdPrimProperty;
}

void SUsdPrimPropertiesList::Construct( const FArguments& InArgs, const TCHAR* InPrimPath )
{
	PrimPath = InPrimPath;

	GeneratePropertiesList( InPrimPath );

	SAssignNew( HeaderRowWidget, SHeaderRow )
	.Visibility( EVisibility::Collapsed )

	+SHeaderRow::Column( FName( TEXT("PropertyName") ) )
	.FillWidth( 20.f )

	+SHeaderRow::Column( FName( TEXT("PropertyValue") ) )
	.FillWidth( 80.f );

	SListView::Construct
	(
		SListView::FArguments()
		.ListItemsSource( &PrimProperties )
		.OnGenerateRow( this, &SUsdPrimPropertiesList::OnGenerateRow )
		.HeaderRow( HeaderRowWidget )
	);
}

TSharedRef< ITableRow > SUsdPrimPropertiesList::OnGenerateRow( TSharedPtr< FUsdPrimProperty > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable )
{
	return SNew( SUsdPrimPropertyRow, InDisplayNode, OwnerTable );
}

void SUsdPrimPropertiesList::GeneratePropertiesList( const TCHAR* InPrimPath )
{
	PrimProperties.Reset();

	FString PrimName;
	FString PrimKind;

	{
		IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
		AUsdStageActor* UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

		if ( UsdStageActor )
		{
			FScopedUsdAllocs UsdAllocs;

			pxr::UsdStageRefPtr UsdStage = UsdStageActor->GetUsdStage();

			if ( UsdStage )
			{
				pxr::UsdPrim UsdPrim = UsdStage->GetPrimAtPath( UnrealToUsd::ConvertPath( InPrimPath ).Get() );

				if ( UsdPrim )
				{
					PrimPath = InPrimPath;
					PrimName = UsdToUnreal::ConvertString( UsdPrim.GetName() );
					PrimKind = UsdToUnreal::ConvertString( IUsdPrim::GetKind( UsdPrim ).GetString() );
				}
			}
		}
	}

	{
		FUsdPrimProperty PrimNameProperty;
		PrimNameProperty.Label = TEXT("Name");
		PrimNameProperty.Value = PrimName;
	
		PrimProperties.Add( MakeSharedUnreal< FUsdPrimProperty >( MoveTemp( PrimNameProperty ) ) );
	}

	{
		FUsdPrimProperty PrimPathProperty;
		PrimPathProperty.Label = TEXT("Path");
		PrimPathProperty.Value = PrimPath;

		PrimProperties.Add( MakeSharedUnreal< FUsdPrimProperty >( MoveTemp( PrimPathProperty ) ) );
	}

	{
		FUsdPrimProperty PrimKindProperty;
		PrimKindProperty.Label = TEXT("Kind");
		PrimKindProperty.Value = PrimKind;

		PrimProperties.Add( MakeSharedUnreal< FUsdPrimProperty >( MoveTemp( PrimKindProperty ) ) );
	}
}

void SUsdPrimPropertiesList::SetPrimPath( const TCHAR* InPrimPath )
{
	PrimPath = InPrimPath;
	GeneratePropertiesList( *PrimPath );
	RequestListRefresh();
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
