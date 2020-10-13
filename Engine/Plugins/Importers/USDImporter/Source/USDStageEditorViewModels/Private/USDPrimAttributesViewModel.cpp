// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimAttributesViewModel.h"

#include "UnrealUSDWrapper.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"

#include "ScopedTransaction.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/base/tf/stringUtils.h"
	#include "pxr/usd/kind/registry.h"
	#include "pxr/usd/usd/attribute.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usdGeom/tokens.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

#define LOCTEXT_NAMESPACE "USDPrimAttributesViewModel"

FUsdPrimAttributeViewModel::FUsdPrimAttributeViewModel( FUsdPrimAttributesViewModel* InOwner )
	: Owner( InOwner )
{
}

TArray< TSharedPtr< FString > > FUsdPrimAttributeViewModel::GetDropdownOptions() const
{
#if USE_USD_SDK
	if ( Label == TEXT("Kind") )
	{
		TArray< TSharedPtr< FString > > Options;
		{
			FScopedUsdAllocs Allocs;

			std::vector< pxr::TfToken > Kinds = pxr::KindRegistry::GetAllKinds();
			Options.Reserve( Kinds.size() );

			for ( const pxr::TfToken& Kind : Kinds )
			{
				Options.Add( MakeShared< FString >( UsdToUnreal::ConvertToken( Kind ) ) );
			}

			// They are supposed to be in an unspecified order, so let's make them consistent
			Options.Sort( [](const TSharedPtr< FString >& A, const TSharedPtr< FString >& B )
				{
					return A.IsValid() && B.IsValid() && ( *A < *B );
				});

		}
		return Options;
	}
	else if ( Label == UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->purpose ) )
	{
		TArray< TSharedPtr< FString > > Options =
		{
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->default_ ) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->proxy ) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->render) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->guide ) ),
		};
		return Options;
	}
#endif // #if USE_USD_SDK

	return {};
}

void FUsdPrimAttributeViewModel::SetAttributeValue( const FString& InValue )
{
	Owner->SetPrimAttribute( Label, InValue );
	Value = InValue;
}

void FUsdPrimAttributesViewModel::SetPrimAttribute( const FString& AttributeName, const FString& InValue )
{
	FScopedTransaction Transaction( FText::Format(
		LOCTEXT( "SetPrimAttribute", "Set value '{0}' for attribute '{1}' of prim '{2}'"),
		FText::FromString( InValue ),
		FText::FromString( AttributeName ),
		FText::FromString( PrimPath )
	));

	bool bSuccess = false;

#if USE_USD_SDK
	FScopedUsdAllocs UsdAllocs;

	pxr::TfToken AttributeNameToken = UnrealToUsd::ConvertToken( *AttributeName ).Get();

	if ( UsdStage )
	{
		UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath( *PrimPath ) );
		if ( UsdPrim )
		{
			if ( AttributeName == TEXT("Kind") )
			{
				bSuccess = IUsdPrim::SetKind( UsdPrim, UnrealToUsd::ConvertToken( *InValue ).Get() );
			}
			else if ( AttributeNameToken == pxr::UsdGeomTokens->purpose )
			{
				pxr::UsdAttribute Attribute = pxr::UsdPrim( UsdPrim ).GetAttribute( AttributeNameToken );
				bSuccess = Attribute.Set( UnrealToUsd::ConvertToken( *InValue ).Get() );
			}
		}
	}

#endif // #if USE_USD_SDK

	if ( !bSuccess )
	{
		UE_LOG( LogUsd, Error, TEXT("Failed to set value '%s' for attribute '%s' of prim '%s'"), *InValue, *AttributeName, *PrimPath );
	}
}

void FUsdPrimAttributesViewModel::Refresh( const TCHAR* InPrimPath, float TimeCode )
{
	PrimPath = InPrimPath;
	PrimAttributes.Reset();

#if USE_USD_SDK
	FString PrimName;
	FString PrimKind;

	UE::FUsdPrim UsdPrim;

	if ( UsdStage )
	{
		FScopedUsdAllocs UsdAllocs;

		UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath( InPrimPath ) );

		if ( UsdPrim )
		{
			PrimPath = InPrimPath;
			PrimName = UsdPrim.GetName().ToString();
			PrimKind = UsdToUnreal::ConvertString( IUsdPrim::GetKind( UsdPrim ).GetString() );
		}
	}

	{
		FUsdPrimAttributeViewModel PrimNameProperty( this );
		PrimNameProperty.Label = TEXT("Name");
		PrimNameProperty.Value = PrimName;

		PrimAttributes.Add( MakeSharedUnreal< FUsdPrimAttributeViewModel >( MoveTemp( PrimNameProperty ) ) );
	}

	{
		FUsdPrimAttributeViewModel PrimPathProperty( this );
		PrimPathProperty.Label = TEXT("Path");
		PrimPathProperty.Value = PrimPath;

		PrimAttributes.Add( MakeSharedUnreal< FUsdPrimAttributeViewModel >( MoveTemp( PrimPathProperty ) ) );
	}

	{
		FUsdPrimAttributeViewModel PrimKindProperty( this );
		PrimKindProperty.Label = TEXT("Kind");
		PrimKindProperty.Value = PrimKind;
		PrimKindProperty.WidgetType = EPrimPropertyWidget::Dropdown;

		PrimAttributes.Add( MakeSharedUnreal< FUsdPrimAttributeViewModel >( MoveTemp( PrimKindProperty ) ) );
	}

	if ( UsdPrim )
	{
		FScopedUsdAllocs UsdAllocs;

		std::vector< pxr::UsdAttribute > PxrPrimAttributes = pxr::UsdPrim( UsdPrim ).GetAttributes();

		for ( const pxr::UsdAttribute& PrimAttribute : PxrPrimAttributes )
		{
			FUsdPrimAttributeViewModel PrimAttributeProperty( this );
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
			PrimAttributes.Add( MakeSharedUnreal< FUsdPrimAttributeViewModel >( MoveTemp( PrimAttributeProperty ) ) );
		}
	}
#endif // #if USE_USD_SDK
}

#undef LOCTEXT_NAMESPACE
