// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDPrimPropertiesList.h"

#include "SUSDStageEditorStyle.h"
#include "UnrealUSDWrapper.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"

#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"

#if USE_USD_SDK

#define LOCTEXT_NAMESPACE "SUsdPrimPropertiesList"

namespace UsdPrimPropertiesListConstants
{
	const FMargin LeftRowPadding( 6.0f, 2.5f, 2.0f, 2.5f );
	const FMargin RightRowPadding( 3.0f, 2.5f, 2.0f, 2.5f );
	const FMargin ComboBoxItemPadding( 3.0f, 0.0f, 2.0f, 0.0f );

	const TCHAR* NormalFont = TEXT("PropertyWindow.NormalFont");
}

namespace UsdPrimPropertiesListImpl
{
	static TMap<FString, TArray<TSharedPtr<FString>>> TokenDropdownOptions;

	void ResetOptions( const FString& TokenName )
	{
		TokenDropdownOptions.Remove( TokenName );
	}

	TArray< TSharedPtr< FString > >* GetTokenDropdownOptions( const FUsdPrimAttributeViewModel& ViewModel )
	{
		TArray< TSharedPtr< FString> >* FoundOptions = TokenDropdownOptions.Find( ViewModel.Label );
		if ( FoundOptions )
		{
			return FoundOptions;
		}
		else
		{
			return &UsdPrimPropertiesListImpl::TokenDropdownOptions.Add( ViewModel.Label, ViewModel.GetDropdownOptions() );
		}
	}
}

class SUsdPrimPropertyRow : public SMultiColumnTableRow< TSharedPtr< FUsdPrimAttributeViewModel > >
{
	SLATE_BEGIN_ARGS( SUsdPrimPropertyRow ) {}
	SLATE_END_ARGS()

public:
	void Construct( const FArguments& InArgs, const TSharedPtr< FUsdPrimAttributeViewModel >& InUsdPrimProperty, const TSharedRef< STableViewBase >& OwnerTable );
	virtual TSharedRef< SWidget > GenerateWidgetForColumn( const FName& ColumnName ) override;

	void SetUsdPrimProperty( const TSharedPtr< FUsdPrimAttributeViewModel >& InUsdPrimProperty );

protected:
	FText GetLabel() const { return FText::FromString( UsdPrimAttribute->Label ); }
	FText GetValue() const { return FText::FromString( UsdPrimAttribute->Value ); }
	FText GetValueOrNone() const { return FText::FromString( UsdPrimAttribute->Value.IsEmpty()? TEXT("none") : UsdPrimAttribute->Value ); }

private:
	TSharedRef< SWidget > GenerateTextWidget(const TAttribute<FText>& Attribute);

	TSharedPtr< FUsdPrimAttributeViewModel > UsdPrimAttribute;
};

void SUsdPrimPropertyRow::Construct( const FArguments& InArgs, const TSharedPtr< FUsdPrimAttributeViewModel >& InUsdPrimProperty, const TSharedRef< STableViewBase >& OwnerTable )
{
	SetUsdPrimProperty( InUsdPrimProperty );

	SMultiColumnTableRow< TSharedPtr< FUsdPrimAttributeViewModel > >::Construct( SMultiColumnTableRow< TSharedPtr< FUsdPrimAttributeViewModel > >::FArguments(), OwnerTable );
}

TSharedRef< SWidget > SUsdPrimPropertyRow::GenerateWidgetForColumn( const FName& ColumnName )
{
	TSharedRef< SWidget > ColumnWidget = SNullWidget::NullWidget;

	if ( ColumnName == TEXT("PropertyName") )
	{
		ColumnWidget = SUsdPrimPropertyRow::GenerateTextWidget({this, &SUsdPrimPropertyRow::GetLabel});
	}
	else
	{
		if ( UsdPrimAttribute->WidgetType == EPrimPropertyWidget::Text )
		{
			ColumnWidget = GenerateTextWidget( {this, &SUsdPrimPropertyRow::GetValue} );
		}
		else if ( UsdPrimAttribute->WidgetType == EPrimPropertyWidget::Dropdown )
		{
			TArray< TSharedPtr< FString > >* Options = UsdPrimPropertiesListImpl::GetTokenDropdownOptions( *UsdPrimAttribute );

			// Show a dropdown if we know the available options for that token
			if ( Options )
			{
				SAssignNew( ColumnWidget, SComboBox< TSharedPtr< FString >  >)
				.OptionsSource( Options )
				.OnGenerateWidget_Lambda( [&]( TSharedPtr<FString> Option )
				{
					return SUsdPrimPropertyRow::GenerateTextWidget( FText::FromString( *Option ) );
				})
				.OnSelectionChanged_Lambda( [&]( TSharedPtr<FString> ChosenOption, ESelectInfo::Type SelectInfo )
				{
					TSharedPtr< ITypedTableView< TSharedPtr< FUsdPrimAttributeViewModel > > > PinnedParent = OwnerTablePtr.Pin();
					TSharedPtr< SUsdPrimPropertiesList> ParentList = StaticCastSharedPtr< SUsdPrimPropertiesList >( PinnedParent );

					UsdPrimAttribute->SetAttributeValue( *ChosenOption );
				})
				[
					SNew( STextBlock )
					.Text( this, &SUsdPrimPropertyRow::GetValueOrNone )
					.Font( FEditorStyle::GetFontStyle( UsdPrimPropertiesListConstants::NormalFont ) )
					.Margin( UsdPrimPropertiesListConstants::ComboBoxItemPadding )
				];
			}
			// Fallback to just displaying a simple text box
			else
			{
				ColumnWidget = SUsdPrimPropertyRow::GenerateTextWidget( {this, &SUsdPrimPropertyRow::GetValue} );
			}
		}
	}

	return SNew( SHorizontalBox )
		+SHorizontalBox::Slot()
		.HAlign( HAlign_Left )
		.VAlign( VAlign_Fill )
		.AutoWidth()
		[
			SNew(SBox)
			.HeightOverride( FUsdStageEditorStyle::Get()->GetFloat( "UsdStageEditor.ListItemHeight" ) )
			.VAlign(VAlign_Center)
			[
				ColumnWidget
			]
		];
}

void SUsdPrimPropertyRow::SetUsdPrimProperty( const TSharedPtr< FUsdPrimAttributeViewModel >& InUsdPrimProperty )
{
	UsdPrimAttribute = InUsdPrimProperty;
}

TSharedRef< SWidget > SUsdPrimPropertyRow::GenerateTextWidget(const TAttribute<FText>& Attribute)
{
	return SNew( SBox )
		.HeightOverride( FUsdStageEditorStyle::Get()->GetFloat( "UsdStageEditor.ListItemHeight" ) )
		.VAlign( VAlign_Center )
		[
			SNew( STextBlock )
			.Text( Attribute )
			.Font( FEditorStyle::GetFontStyle( UsdPrimPropertiesListConstants::NormalFont ) )
			.Margin( UsdPrimPropertiesListConstants::RightRowPadding )
		];
}

void SUsdPrimPropertiesList::Construct( const FArguments& InArgs, const TCHAR* InPrimPath )
{
	GeneratePropertiesList( InPrimPath );

	// Clear map as usd file may have additional Kinds now
	UsdPrimPropertiesListImpl::ResetOptions(TEXT("Kind"));

	SAssignNew( HeaderRowWidget, SHeaderRow )

	+SHeaderRow::Column( FName( TEXT("PropertyName") ) )
	.DefaultLabel( LOCTEXT( "PropertyName", "Property Name" ) )
	.FillWidth( 25.f )

	+SHeaderRow::Column( FName( TEXT("PropertyValue") ) )
	.DefaultLabel( LOCTEXT( "PropertyValue", "Value" ) )
	.FillWidth( 75.f );

	SListView::Construct
	(
		SListView::FArguments()
		.ListItemsSource( &ViewModel.PrimAttributes )
		.OnGenerateRow( this, &SUsdPrimPropertiesList::OnGenerateRow )
		.HeaderRow( HeaderRowWidget )
	);
}

TSharedRef< ITableRow > SUsdPrimPropertiesList::OnGenerateRow( TSharedPtr< FUsdPrimAttributeViewModel > InDisplayNode, const TSharedRef< STableViewBase >& OwnerTable )
{
	return SNew( SUsdPrimPropertyRow, InDisplayNode, OwnerTable );
}

void SUsdPrimPropertiesList::GeneratePropertiesList( const TCHAR* InPrimPath )
{
	float TimeCode = 0.f;

	IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
	AUsdStageActor* UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

	if ( UsdStageActor )
	{
		TimeCode = UsdStageActor->GetTime();
		ViewModel.UsdStage = UsdStageActor->GetUsdStage();
	}

	ViewModel.Refresh( InPrimPath, TimeCode );
}

void SUsdPrimPropertiesList::SetPrimPath( const TCHAR* InPrimPath )
{
	GeneratePropertiesList( InPrimPath );
	RequestListRefresh();
}

#undef LOCTEXT_NAMESPACE

#endif // #if USE_USD_SDK
