// Copyright Epic Games, Inc. All Rights Reserved.

#include "SUSDPrimPropertiesList.h"

#include "USDLog.h"
#include "USDMemory.h"
#include "USDStageActor.h"
#include "USDStageModule.h"
#include "USDTypesConversion.h"
#include "UnrealUSDWrapper.h"

#include "EditorStyleSet.h"
#include "Engine/World.h"
#include "Modules/ModuleManager.h"
#include "ScopedTransaction.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"

#include "pxr/base/tf/stringUtils.h"
#include "pxr/usd/kind/registry.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usdGeom/tokens.h"

#include "USDIncludesEnd.h"


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

	void ResetOptions(const FString& TokenName)
	{
		TokenDropdownOptions.Remove(TokenName);
	}

	TArray<TSharedPtr<FString>>* GetTokenDropdownOptions(const FString& TokenName)
	{
		TArray<TSharedPtr<FString>>* FoundOptions = TokenDropdownOptions.Find(TokenName);
		if (FoundOptions)
		{
			return FoundOptions;
		}

		if (TokenName == TEXT("Kind"))
		{
			TArray<TSharedPtr<FString>> Options;
			{
				FScopedUsdAllocs Allocs;

				std::vector<pxr::TfToken> Kinds = pxr::KindRegistry::GetAllKinds();
				Options.Reserve(Kinds.size());

				for (const pxr::TfToken& Kind : Kinds)
				{
					Options.Add(MakeShared<FString>(UsdToUnreal::ConvertToken(Kind)));
				}

				// They are supposed to be in an unspecified order, so let's make them consistent
				Options.Sort([](const TSharedPtr<FString>& A, const TSharedPtr<FString>& B)
				{
					return A.IsValid() && B.IsValid() && (*A < *B);
				});
			}
			return &UsdPrimPropertiesListImpl::TokenDropdownOptions.Add(TokenName, Options);
		}
		else if (TokenName == UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->purpose))
		{
			TArray<TSharedPtr<FString>> Options =
			{
				MakeShared<FString>(UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->default_)),
				MakeShared<FString>(UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->proxy)),
				MakeShared<FString>(UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->render)),
				MakeShared<FString>(UsdToUnreal::ConvertToken(pxr::UsdGeomTokens->guide)),
			};
			return &UsdPrimPropertiesListImpl::TokenDropdownOptions.Add(TokenName, Options);
		}

		return FoundOptions;
	}

	void SetPrimAttribute(const FString& PrimPath, const FString& AttributeName, const FString& Value)
	{
		FScopedTransaction Transaction(FText::Format(
			LOCTEXT("SetPrimAttribute", "Set value '{0}' for attribute '{1}' of prim '{2}'"),
			FText::FromString(Value),
			FText::FromString(AttributeName),
			FText::FromString(PrimPath)
		));

		IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >("UsdStage");
		AUsdStageActor* UsdStageActor = &UsdStageModule.GetUsdStageActor(GWorld);

		FScopedUsdAllocs UsdAllocs;

		bool bSuccess = false;

		pxr::TfToken AttributeNameToken = UnrealToUsd::ConvertToken(*AttributeName).Get();

		if (pxr::UsdStageRefPtr UsdStage = UsdStageActor->GetUsdStage())
		{
			pxr::UsdPrim UsdPrim = UsdStage->GetPrimAtPath(UnrealToUsd::ConvertPath(*PrimPath).Get());
			if (UsdPrim)
			{
				if (AttributeName == TEXT("Kind"))
				{
					bSuccess = IUsdPrim::SetKind(UsdPrim, UnrealToUsd::ConvertToken(*Value).Get());
				}
				else if (AttributeNameToken == pxr::UsdGeomTokens->purpose)
				{
					pxr::UsdAttribute Attribute = UsdPrim.GetAttribute(AttributeNameToken);
					bSuccess = Attribute.Set(UnrealToUsd::ConvertToken(*Value).Get());
				}
			}
		}

		if (!bSuccess)
		{
			UE_LOG(LogUsd, Error, TEXT("Failed to set value '%s' for attribute '%s' of prim '%s'"), *Value, *AttributeName, *PrimPath);
		}
	}
}

enum class EPrimPropertyWidget : uint8
{
	Text,
	Dropdown,
};

struct FUsdPrimProperty : public TSharedFromThis< FUsdPrimProperty >
{
	FString Label;
	FString Value;
	EPrimPropertyWidget WidgetType = EPrimPropertyWidget::Text;
};

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
	FText GetValueOrNone() const { return FText::FromString( UsdPrimProperty->Value.IsEmpty()? TEXT("none") : UsdPrimProperty->Value ); }

private:
	TSharedRef< SWidget > GenerateTextWidget(const TAttribute<FText>& Attribute);

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
		ColumnWidget = SUsdPrimPropertyRow::GenerateTextWidget({this, &SUsdPrimPropertyRow::GetLabel});
	}
	else
	{
		if (UsdPrimProperty->WidgetType == EPrimPropertyWidget::Text)
		{
			ColumnWidget = GenerateTextWidget({this, &SUsdPrimPropertyRow::GetValue});
		}
		else if (UsdPrimProperty->WidgetType == EPrimPropertyWidget::Dropdown)
		{
			TArray<TSharedPtr<FString>>* Options = UsdPrimPropertiesListImpl::GetTokenDropdownOptions(UsdPrimProperty->Label);

			// Show a dropdown if we know the available options for that token
			if (Options)
			{
				SAssignNew(ColumnWidget, SComboBox<TSharedPtr<FString>>)
				.OptionsSource(Options)
				.OnGenerateWidget_Lambda([&](TSharedPtr<FString> Option)
				{
					return SUsdPrimPropertyRow::GenerateTextWidget(FText::FromString(*Option));
				})
				.OnSelectionChanged_Lambda([&](TSharedPtr<FString> ChosenOption, ESelectInfo::Type SelectInfo)
				{
					TSharedPtr<ITypedTableView<TSharedPtr<FUsdPrimProperty>>> PinnedParent = OwnerTablePtr.Pin();
					TSharedPtr<SUsdPrimPropertiesList> ParentList = StaticCastSharedPtr<SUsdPrimPropertiesList>(PinnedParent);

					UsdPrimPropertiesListImpl::SetPrimAttribute(ParentList->GetPrimPath(), UsdPrimProperty->Label, *ChosenOption);
				})
				[
					SNew(STextBlock)
					.Text(this, &SUsdPrimPropertyRow::GetValueOrNone)
					.Font(FEditorStyle::GetFontStyle(UsdPrimPropertiesListConstants::NormalFont))
					.Margin(UsdPrimPropertiesListConstants::ComboBoxItemPadding)
				];
			}
			// Fallback to just displaying a simple text box
			else
			{
				ColumnWidget = SUsdPrimPropertyRow::GenerateTextWidget({this, &SUsdPrimPropertyRow::GetValue});
			}
		}
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

TSharedRef< SWidget > SUsdPrimPropertyRow::GenerateTextWidget(const TAttribute<FText>& Attribute)
{
	return SNew( STextBlock )
	.Text( Attribute )
	.Font( FEditorStyle::GetFontStyle( UsdPrimPropertiesListConstants::NormalFont ) )
	.Margin( UsdPrimPropertiesListConstants::RightRowPadding );
}

void SUsdPrimPropertiesList::Construct( const FArguments& InArgs, const TCHAR* InPrimPath )
{
	PrimPath = InPrimPath;

	GeneratePropertiesList( InPrimPath );

	// Clear map as usd file may have additional Kinds now
	UsdPrimPropertiesListImpl::ResetOptions(TEXT("Kind"));

	SAssignNew( HeaderRowWidget, SHeaderRow )

	+SHeaderRow::Column( FName( TEXT("PropertyName") ) )
	.DefaultLabel( LOCTEXT( "PropertyName", "Property Name" ) )
	.FillWidth( 25.f )

	+SHeaderRow::Column( FName( TEXT("PropertyValue") ) )
	.DefaultLabel( LOCTEXT( "PropertyValue", "Value" ) )
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

	TUsdStore< pxr::UsdPrim > UsdPrim;
	pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime();

	{
		IUsdStageModule& UsdStageModule = FModuleManager::Get().LoadModuleChecked< IUsdStageModule >( "UsdStage" );
		AUsdStageActor* UsdStageActor = &UsdStageModule.GetUsdStageActor( GWorld );

		if ( UsdStageActor )
		{
			TimeCode = pxr::UsdTimeCode( UsdStageActor->GetTime() );

			FScopedUsdAllocs UsdAllocs;

			pxr::UsdStageRefPtr UsdStage = UsdStageActor->GetUsdStage();

			if ( UsdStage )
			{
				UsdPrim = UsdStage->GetPrimAtPath( UnrealToUsd::ConvertPath( InPrimPath ).Get() );

				if ( UsdPrim.Get() )
				{
					PrimPath = InPrimPath;
					PrimName = UsdToUnreal::ConvertString( UsdPrim.Get().GetName() );
					PrimKind = UsdToUnreal::ConvertString( IUsdPrim::GetKind( UsdPrim.Get() ).GetString() );
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
		PrimKindProperty.WidgetType = EPrimPropertyWidget::Dropdown;

		PrimProperties.Add( MakeSharedUnreal< FUsdPrimProperty >( MoveTemp( PrimKindProperty ) ) );
	}

	if ( UsdPrim.Get() )
	{
		FScopedUsdAllocs UsdAllocs;

		std::vector< pxr::UsdAttribute > PrimAttributes = UsdPrim.Get().GetAttributes();

		for ( const pxr::UsdAttribute& PrimAttribute : PrimAttributes )
		{
			FUsdPrimProperty PrimAttributeProperty;
			PrimAttributeProperty.Label = UsdToUnreal::ConvertString( PrimAttribute.GetName().GetString() );

			// Just the Purpose attribute for now
			PrimAttributeProperty.WidgetType = PrimAttribute.GetName() == pxr::UsdGeomTokens->purpose ? EPrimPropertyWidget::Dropdown : EPrimPropertyWidget::Text;

			pxr::VtValue VtValue;
			PrimAttribute.Get( &VtValue, TimeCode );
			FString AttributeValue = UsdToUnreal::ConvertString( pxr::TfStringify( VtValue ).c_str() );

			// STextBlock can get very slow calculating its desired size for very long string so chop it if needed
			const int32 MaxValueLength = 300;
			if ( AttributeValue.Len() > MaxValueLength )
			{
				AttributeValue.LeftInline( MaxValueLength );
				AttributeValue.Append( TEXT("...") );
			}

			PrimAttributeProperty.Value = MoveTemp( AttributeValue );
			PrimProperties.Add( MakeSharedUnreal< FUsdPrimProperty >( MoveTemp( PrimAttributeProperty ) ) );
		}
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
