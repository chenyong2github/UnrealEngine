// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDIntegrationsPanel.h"

#if USE_USD_SDK

#include "SUSDStageEditorStyle.h"
#include "USDIntegrationUtils.h"
#include "USDTypesConversion.h"
#include "USDValueConversion.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdPrim.h"
#include "UsdWrappers/UsdStage.h"
#include "UsdWrappers/VtValue.h"

#include "Styling/AppStyle.h"
#include "ISinglePropertyView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Roles/LiveLinkAnimationRole.h"
#include "Roles/LiveLinkTransformRole.h"
#include "SLiveLinkSubjectRepresentationPicker.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Views/SHeaderRow.h"

#define LOCTEXT_NAMESPACE "SUSDIntegrationsPanel"

namespace UE::SUsdIntergrationsPanel::Private
{
	const FMargin LeftRowPadding( 6.0f, 0.0f, 2.0f, 0.0f );
	const FMargin RightRowPadding( 3.0f, 0.0f, 2.0f, 0.0f );
	const float DesiredNumericEntryBoxWidth = 80.0f;

	const TCHAR* NormalFont = TEXT( "PropertyWindow.NormalFont" );
}

void SUsdIntegrationsPanelRow::Construct( const FArguments& InArgs, TSharedPtr<UE::FUsdAttribute> InAttr, const TSharedRef< STableViewBase >& OwnerTable )
{
	Attribute = InAttr;

	SMultiColumnTableRow< TSharedPtr<UE::FUsdAttribute> >::Construct( SMultiColumnTableRow< TSharedPtr<UE::FUsdAttribute> >::FArguments(), OwnerTable );
}

TSharedRef< SWidget > SUsdIntegrationsPanelRow::GenerateWidgetForColumn( const FName& ColumnName )
{
	TSharedRef< SWidget > ColumnWidget = SNullWidget::NullWidget;

	bool bIsLeftColumn = true;
	FOptionalSize RowHeight = FUsdStageEditorStyle::Get()->GetFloat( "UsdStageEditor.ListItemHeight" );

	FScopedUsdAllocs Allocs;

	FName AttributeName = Attribute->GetName();

	if ( ColumnName == TEXT("PropertyName") )
	{
		using DisplayTextForPropertiesEntry = TPairInitializer<const FName&, const FText&>;
		const static TMap<FName, FText> DisplayTextForProperties
		({
			DisplayTextForPropertiesEntry( *UsdToUnreal::ConvertToken( UnrealIdentifiers::UnrealLiveLinkAnimBlueprintPath ), LOCTEXT( "AnimBlueprintPathText", "AnimBlueprint asset" ) ),
			DisplayTextForPropertiesEntry( *UsdToUnreal::ConvertToken( UnrealIdentifiers::UnrealLiveLinkSubjectName ), LOCTEXT( "SubjectNameText", "LiveLink subject name" ) ),
			DisplayTextForPropertiesEntry( *UsdToUnreal::ConvertToken( UnrealIdentifiers::UnrealLiveLinkEnabled ), LOCTEXT( "LiveLinkEnabledText", "Enable LiveLink" ) ),
		});

		if ( const FText* TextToDisplay = DisplayTextForProperties.Find( AttributeName ) )
		{
			SAssignNew( ColumnWidget, STextBlock )
			.Text( *TextToDisplay )
			.Font( FAppStyle::GetFontStyle( UE::SUsdIntergrationsPanel::Private::NormalFont ) );
		}
		else
		{
			ensure(false);
			ColumnWidget = SNullWidget::NullWidget;
		}
	}
	else
	{
		bIsLeftColumn = false;

		FName TypeName = Attribute->GetTypeName();
		TSharedPtr<UE::FUsdAttribute> AttributeCopy = Attribute;

		if ( AttributeName == *UsdToUnreal::ConvertToken( UnrealIdentifiers::UnrealLiveLinkAnimBlueprintPath ) )
		{
			if ( UUsdIntegrationsPanelPropertyDummy* Dummy = GetMutableDefault<UUsdIntegrationsPanelPropertyDummy>() )
			{
				// Let the object picker row be as tall as it wants
				RowHeight = FOptionalSize();

				FSinglePropertyParams Params;
				Params.Font = FAppStyle::GetFontStyle( UE::SUsdIntergrationsPanel::Private::NormalFont );
				Params.NamePlacement = EPropertyNamePlacement::Hidden;

				UE::FVtValue Value;
				if ( AttributeCopy->Get( Value ) && !Value.IsEmpty() )
				{
					Dummy->AnimBPProperty = FSoftObjectPath( UsdUtils::Stringify( Value ) ).TryLoad();
				}
				else
				{
					Dummy->AnimBPProperty = nullptr;
				}

				FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>( TEXT( "PropertyEditor" ) );
				TSharedPtr<class ISinglePropertyView> PropertyView = PropertyEditor.CreateSingleProperty(
					Dummy,
					GET_MEMBER_NAME_CHECKED( UUsdIntegrationsPanelPropertyDummy, AnimBPProperty ),
					Params
				);

				FSimpleDelegate PropertyChanged = FSimpleDelegate::CreateLambda( [AttributeCopy]()
				{
					UUsdIntegrationsPanelPropertyDummy* Dummy = GetMutableDefault<UUsdIntegrationsPanelPropertyDummy>();

					FString Path = ( Dummy && Dummy->AnimBPProperty ) ? Dummy->AnimBPProperty->GetPathName() : FString{};

					TArray< UsdUtils::FConvertedVtValueComponent > Components;
					Components.Add( UsdUtils::FConvertedVtValueComponent( TInPlaceType<FString>(), Path ) );

					UsdUtils::FConvertedVtValue Converted;
					Converted.SourceType = UsdUtils::EUsdBasicDataTypes::String;
					Converted.bIsArrayValued = false;
					Converted.bIsEmpty = false;
					Converted.Entries.Add( Components );

					UE::FVtValue Value;
					if ( UnrealToUsd::ConvertValue( Converted, Value ) && !Value.IsEmpty() )
					{
						AttributeCopy->Set( Value );
					}
				} );
				PropertyView->SetOnPropertyValueChanged( PropertyChanged );

				ColumnWidget = PropertyView.ToSharedRef();
			}
			else
			{
				ColumnWidget = SNullWidget::NullWidget;
			}
		}
		else if ( AttributeName == *UsdToUnreal::ConvertToken( UnrealIdentifiers::UnrealLiveLinkSubjectName ) )
		{
			// SLiveLinkSubjectRepresentationPicker is also a little bit larger than one of our rows, so let it expand a bit
			RowHeight = FOptionalSize();

			SAssignNew( ColumnWidget, SLiveLinkSubjectRepresentationPicker )
			.ShowRole( false )
			.ShowSource( false )
			.Font( FAppStyle::GetFontStyle( UE::SUsdIntergrationsPanel::Private::NormalFont ) )
			.Value_Lambda( [AttributeCopy]()
			{
				SLiveLinkSubjectRepresentationPicker::FLiveLinkSourceSubjectRole Result;

				Result.Role = AttributeCopy->GetPrim().IsA(TEXT("SkelRoot"))
					? ULiveLinkAnimationRole::StaticClass()
					: ULiveLinkTransformRole::StaticClass();

				UE::FVtValue Value;
				AttributeCopy->Get( Value );
				Result.Subject = FLiveLinkSubjectName{ FName{*UsdUtils::Stringify( Value )} };

				return Result;
			})
			.OnValueChanged_Lambda( [AttributeCopy]( SLiveLinkSubjectRepresentationPicker::FLiveLinkSourceSubjectRole NewValue )
			{
				TArray< UsdUtils::FConvertedVtValueComponent > Components;
				Components.Add( UsdUtils::FConvertedVtValueComponent( TInPlaceType<FString>(), NewValue.Subject.ToString() ) );

				UsdUtils::FConvertedVtValue Converted;
				Converted.SourceType = UsdUtils::EUsdBasicDataTypes::String;
				Converted.bIsArrayValued = false;
				Converted.bIsEmpty = false;
				Converted.Entries.Add( Components );

				UE::FVtValue Value;
				if ( UnrealToUsd::ConvertValue( Converted, Value ) && !Value.IsEmpty() )
				{
					AttributeCopy->Set( Value );
				}
			});
		}
		else if ( AttributeName == *UsdToUnreal::ConvertToken( UnrealIdentifiers::UnrealLiveLinkEnabled ) )
		{
			SAssignNew( ColumnWidget, SBox )
			.HeightOverride( FUsdStageEditorStyle::Get()->GetFloat( "UsdStageEditor.ListItemHeight" ) )
			.VAlign( VAlign_Center )
			[
				SNew( SCheckBox )
				.IsChecked_Lambda( [AttributeCopy]()
				{
					UE::FVtValue Value;
					AttributeCopy->Get( Value );

					UsdUtils::FConvertedVtValue Converted;

					if ( UsdToUnreal::ConvertValue( Value, Converted ) && Converted.SourceType == UsdUtils::EUsdBasicDataTypes::Bool && !Converted.bIsEmpty )
					{
						if ( bool* bBoolValue = Converted.Entries[ 0 ][ 0 ].TryGet<bool>() )
						{
							return *bBoolValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
						}
					}

					return ECheckBoxState::Undetermined;
				})
				.OnCheckStateChanged_Lambda( [AttributeCopy]( ECheckBoxState NewState )
				{
					TArray< UsdUtils::FConvertedVtValueComponent > Components;
					Components.Add( UsdUtils::FConvertedVtValueComponent( TInPlaceType<bool>(), NewState == ECheckBoxState::Checked ) );

					UsdUtils::FConvertedVtValue Converted;
					Converted.SourceType = UsdUtils::EUsdBasicDataTypes::Bool;
					Converted.bIsArrayValued = false;
					Converted.bIsEmpty = false;
					Converted.Entries.Add( Components );

					UE::FVtValue Value;
					if ( UnrealToUsd::ConvertValue( Converted, Value ) && !Value.IsEmpty() )
					{
						AttributeCopy->Set( Value );
					}
				})
			];
		}
		else
		{
			ensure(false);
		}
	}

	return SNew(SBox)
		.HeightOverride( RowHeight )
		[
			SNew( SHorizontalBox )
			+ SHorizontalBox::Slot()
			.HAlign( HAlign_Left )
			.VAlign( VAlign_Center )
			.Padding( bIsLeftColumn ? UE::SUsdIntergrationsPanel::Private::LeftRowPadding : UE::SUsdIntergrationsPanel::Private::RightRowPadding )
			.AutoWidth()
			[
				ColumnWidget
			]
		];
}

void SUsdIntegrationsPanel::Construct( const FArguments& InArgs, const UE::FUsdStageWeak& InUsdStage, const TCHAR* InPrimPath )
{
	SAssignNew( HeaderRowWidget, SHeaderRow )

	+SHeaderRow::Column( FName( TEXT("PropertyName") ) )
	.DefaultLabel( NSLOCTEXT( "SUsdIntegrationsPanel", "Integrations", "Integrations" ) )
	.FillWidth( 30.0f )

	+SHeaderRow::Column( FName( TEXT("Value") ) )
	.DefaultLabel( FText::GetEmpty() )
	.FillWidth( 70.0f );

	SListView::Construct
	(
		SListView::FArguments()
		.ListItemsSource( &Attributes )
		.OnGenerateRow( this, &SUsdIntegrationsPanel::OnGenerateRow )
		.HeaderRow( HeaderRowWidget )
	);

	SetPrimPath( InUsdStage, InPrimPath );
}

void SUsdIntegrationsPanel::SetPrimPath( const UE::FUsdStageWeak& InUsdStage, const TCHAR* InPrimPath )
{
	PrimPath = InPrimPath;
	UsdStage = InUsdStage;

	Attributes.Reset();

	EVisibility NewVisibility = EVisibility::Collapsed;

	if ( InUsdStage )
	{
		if ( UE::FUsdPrim Prim = InUsdStage.GetPrimAtPath( UE::FSdfPath{ InPrimPath } ) )
		{
			if ( UsdUtils::PrimHasLiveLinkSchema( Prim ) )
			{
				NewVisibility = EVisibility::Visible;

				// Only show the dedicated AnimBlueprint picker for SkelRoots
				if ( Prim.IsA( TEXT( "SkelRoot" ) ) )
				{
					if ( UE::FUsdAttribute Attr = Prim.GetAttribute( *UsdToUnreal::ConvertToken( UnrealIdentifiers::UnrealLiveLinkAnimBlueprintPath ) ) )
					{
						Attributes.Add( MakeShared<UE::FUsdAttribute>( Attr ) );
					}
				}

				if ( UE::FUsdAttribute Attr = Prim.GetAttribute( *UsdToUnreal::ConvertToken( UnrealIdentifiers::UnrealLiveLinkSubjectName ) ) )
				{
					Attributes.Add( MakeShared<UE::FUsdAttribute>( Attr ) );
				}

				if ( UE::FUsdAttribute Attr = Prim.GetAttribute( *UsdToUnreal::ConvertToken( UnrealIdentifiers::UnrealLiveLinkEnabled ) ) )
				{
					Attributes.Add( MakeShared<UE::FUsdAttribute>( Attr ) );
				}
			}
		}
	}

	SetVisibility( NewVisibility );
	RequestListRefresh();
}

TSharedRef< ITableRow > SUsdIntegrationsPanel::OnGenerateRow( TSharedPtr<UE::FUsdAttribute> InAttr, const TSharedRef< STableViewBase >& OwnerTable )
{
	return SNew( SUsdIntegrationsPanelRow, InAttr, OwnerTable );
}

#undef LOCTEXT_NAMESPACE
#endif // USE_USD_SDK