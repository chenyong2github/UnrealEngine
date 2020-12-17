// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDPrimAttributesViewModel.h"

#include "UnrealUSDWrapper.h"
#include "USDErrorUtils.h"
#include "USDLog.h"
#include "USDMemory.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdPrim.h"

#include "Framework/Notifications/NotificationManager.h"
#include "ScopedTransaction.h"
#include "Widgets/Notifications/SNotificationList.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/base/tf/stringUtils.h"
	#include "pxr/base/vt/value.h"
	#include "pxr/usd/kind/registry.h"
	#include "pxr/usd/sdf/types.h"
	#include "pxr/usd/usd/attribute.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/prim.h"
	#include "pxr/usd/usdGeom/tokens.h"
#include "USDIncludesEnd.h"

#endif // #if USE_USD_SDK

#define LOCTEXT_NAMESPACE "USDPrimAttributesViewModel"

namespace UnrealToUsd
{
#if USE_USD_SDK
	/** We use the USDElementType parameter to do the final float to pxr::GfHalf conversion, as well as double to pxr::SdfTimeCode */
	template<typename UEElementType, typename USDElementType = UEElementType>
	void TrySetSingleValue( const USDViewModels::FPrimPropertyValue& InValue, pxr::VtValue& OutValue )
	{
		if ( const UEElementType* Value = InValue.Components[ 0 ].TryGet<UEElementType>() )
		{
			OutValue = USDElementType( *Value );
		}
	}

	/** We need the USDElementType parameter to do the final float to pxr::GfHalf conversions */
	template<typename UEElementType, typename USDArrayType, typename USDElementType = UEElementType>
	void TrySetArrayLikeValue( const USDViewModels::FPrimPropertyValue& InValue, pxr::VtValue& OutValue, int32 NumElements )
	{
		if ( InValue.Components.Num() != NumElements )
		{
			return;
		}

		USDArrayType USDVal( UEElementType( 0 ) );
		USDElementType* DataPtr = USDVal.data();

		for ( int32 Index = 0; Index < NumElements; ++Index )
		{
			if ( const UEElementType* IndexValue = InValue.Components[ Index ].TryGet<UEElementType>() )
			{
				DataPtr[ Index ] = USDElementType( *IndexValue );
			}
		}
		OutValue = USDVal;
	}

	/** USD quaternions don't have the access operator defined */
	template<typename UEElementType, typename USDQuatType>
	void TrySetQuatValue( const USDViewModels::FPrimPropertyValue& InValue, pxr::VtValue& OutValue )
	{
		if ( InValue.Components.Num() == 4 )
		{
			UEElementType QuatValues[ 4 ] = { 0, 0, 0, 1 };
			for ( int32 Index = 0; Index < 4; ++Index )
			{
				if ( const UEElementType* QuatValue = InValue.Components[ Index ].TryGet<UEElementType>() )
				{
					QuatValues[ Index ] = *QuatValue;
				}
			}

			USDQuatType Quat(
				QuatValues[ 3 ],  // Real part comes first for USD
				QuatValues[ 0 ],
				QuatValues[ 1 ],
				QuatValues[ 2 ]
			);

			OutValue = Quat;
		}
	}

	bool ConvertVtValue( const USDViewModels::FPrimPropertyValue& InValue, pxr::VtValue& OutValue )
	{
		using namespace USDViewModels;

		// Clears whatever it is holding, we just have to specify a type for it to default-construct and return, for some reason
		OutValue.Remove<bool>();

		if ( InValue.Components.Num() == 0 )
		{
			return false;
		}

		switch ( InValue.SourceType )
		{
		case EUsdBasicDataTypes::Bool:
			TrySetSingleValue<bool>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Uchar:
			TrySetSingleValue<uint8>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Int:
			TrySetSingleValue<int32>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Uint:
			TrySetSingleValue<uint32>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Int64:
			TrySetSingleValue<int64>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Uint64:
			TrySetSingleValue<uint64>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Half:
			TrySetSingleValue<float, pxr::GfHalf>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Float:
			TrySetSingleValue<float>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Double:
			TrySetSingleValue<double>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Timecode:
			TrySetSingleValue<double, pxr::SdfTimeCode>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::String:
			if ( const FString* Value = InValue.Components[ 0 ].TryGet<FString>() )
			{
				OutValue = UnrealToUsd::ConvertString( **Value ).Get();
			}
			break;
		case EUsdBasicDataTypes::Token:
			if ( const FString* Value = InValue.Components[ 0 ].TryGet<FString>() )
			{
				OutValue = UnrealToUsd::ConvertToken( **Value ).Get();
			}
			break;
		case EUsdBasicDataTypes::Asset:
			if ( const FString* Value = InValue.Components[ 0 ].TryGet<FString>() )
			{
				OutValue = pxr::SdfAssetPath( UnrealToUsd::ConvertString( **Value ).Get() );
			}
			break;
		case EUsdBasicDataTypes::Matrix2d:
			TrySetArrayLikeValue<double, pxr::GfMatrix2d>( InValue, OutValue, 4 );
			break;
		case EUsdBasicDataTypes::Matrix3d:
			TrySetArrayLikeValue<double, pxr::GfMatrix3d>( InValue, OutValue, 9 );
			break;
		case EUsdBasicDataTypes::Matrix4d:
			TrySetArrayLikeValue<double, pxr::GfMatrix4d>( InValue, OutValue, 16 );
			break;
		case EUsdBasicDataTypes::Quatd:
			TrySetQuatValue<double, pxr::GfQuatd>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Quatf:
			TrySetQuatValue<float, pxr::GfQuatf>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Quath:
			TrySetQuatValue<float, pxr::GfQuath>( InValue, OutValue );
			break;
		case EUsdBasicDataTypes::Double2:
			TrySetArrayLikeValue<double, pxr::GfVec2d>( InValue, OutValue, 2 );
			break;
		case EUsdBasicDataTypes::Float2:
			TrySetArrayLikeValue<float, pxr::GfVec2f>( InValue, OutValue, 2 );
			break;
		case EUsdBasicDataTypes::Half2:
			TrySetArrayLikeValue<float, pxr::GfVec2h, pxr::GfHalf>( InValue, OutValue, 2 );
			break;
		case EUsdBasicDataTypes::Int2:
			TrySetArrayLikeValue<int32, pxr::GfVec2i>( InValue, OutValue, 2 );
			break;
		case EUsdBasicDataTypes::Double3:
			TrySetArrayLikeValue<double, pxr::GfVec3d>( InValue, OutValue, 3 );
			break;
		case EUsdBasicDataTypes::Float3:
			TrySetArrayLikeValue<float, pxr::GfVec3f>( InValue, OutValue, 3 );
			break;
		case EUsdBasicDataTypes::Half3:
			TrySetArrayLikeValue<float, pxr::GfVec3h, pxr::GfHalf>( InValue, OutValue, 3 );
			break;
		case EUsdBasicDataTypes::Int3:
			TrySetArrayLikeValue<int32, pxr::GfVec3i>( InValue, OutValue, 3 );
			break;
		case EUsdBasicDataTypes::Double4:
			TrySetArrayLikeValue<double, pxr::GfVec4d>( InValue, OutValue, 4 );
			break;
		case EUsdBasicDataTypes::Float4:
			TrySetArrayLikeValue<float, pxr::GfVec4f>( InValue, OutValue, 4 );
			break;
		case EUsdBasicDataTypes::Half4:
			TrySetArrayLikeValue<float, pxr::GfVec4h, pxr::GfHalf>( InValue, OutValue, 4 );
			break;
		case EUsdBasicDataTypes::Int4:
			TrySetArrayLikeValue<int32, pxr::GfVec4i>( InValue, OutValue, 4 );
			break;
		case EUsdBasicDataTypes::None:
		default:
			break;
		}

		return !OutValue.IsEmpty();
	}
#endif // #if USE_USD_SDK
}

namespace UsdToUnreal
{
#if USE_USD_SDK
	bool ConvertVtValue( const pxr::VtValue& InVtValue, USDViewModels::FPrimPropertyValue& OutValue, bool& bOutReadOnly )
	{
		FScopedUsdAllocs Allocs;

		using namespace USDViewModels;

		OutValue.Components.Reset();
		OutValue.SourceType = EUsdBasicDataTypes::None;
		bOutReadOnly = false;

		if ( InVtValue.IsEmpty() )
		{
			return false;
		}

		if ( !InVtValue.IsArrayValued() )
		{
			const pxr::TfType& UnderlyingType = InVtValue.GetType();
			if ( UnderlyingType == pxr::SdfValueTypeNames->Bool.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<bool>(), InVtValue.UncheckedGet<bool>() ) );
				OutValue.SourceType = EUsdBasicDataTypes::Bool;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->UChar.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<uint8>(), InVtValue.UncheckedGet<uint8_t>() ) );
				OutValue.SourceType = EUsdBasicDataTypes::Uchar;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Int.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int32>(), InVtValue.UncheckedGet<int32_t>() ) );
				OutValue.SourceType = EUsdBasicDataTypes::Int;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->UInt.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<uint32>(), InVtValue.UncheckedGet<uint32_t>() ) );
				OutValue.SourceType = EUsdBasicDataTypes::Uint;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Int64.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int64>(), InVtValue.UncheckedGet<int64_t>() ) );
				OutValue.SourceType = EUsdBasicDataTypes::Int64;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->UInt64.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<uint64>(), InVtValue.UncheckedGet<uint64_t>() ) );
				OutValue.SourceType = EUsdBasicDataTypes::Uint64;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Half.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), InVtValue.UncheckedGet<pxr::GfHalf>() ) );
				OutValue.SourceType = EUsdBasicDataTypes::Half;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Float.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), InVtValue.UncheckedGet<float>() ) );
				OutValue.SourceType = EUsdBasicDataTypes::Float;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Double.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), InVtValue.UncheckedGet<double>() ) );
				OutValue.SourceType = EUsdBasicDataTypes::Double;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->TimeCode.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), InVtValue.UncheckedGet<pxr::SdfTimeCode>().GetValue() ) );
				OutValue.SourceType = EUsdBasicDataTypes::Timecode;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->String.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<FString>(), UsdToUnreal::ConvertString( InVtValue.UncheckedGet<std::string>() ) ) );
				OutValue.SourceType = EUsdBasicDataTypes::String;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Token.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<FString>(), UsdToUnreal::ConvertToken( InVtValue.UncheckedGet<pxr::TfToken>() ) ) );
				OutValue.SourceType = EUsdBasicDataTypes::Token;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Asset.GetType() )
			{
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<FString>(), UsdToUnreal::ConvertString( InVtValue.UncheckedGet<pxr::SdfAssetPath>().GetAssetPath() ) ) );
				OutValue.SourceType = EUsdBasicDataTypes::Asset;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Matrix2d.GetType() )
			{
				pxr::GfMatrix2d Matrix = InVtValue.UncheckedGet<pxr::GfMatrix2d>();
				double* MatrixArray = Matrix.GetArray();
				for ( int32 Index = 0; Index < 4; ++Index )
				{
					OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), MatrixArray[Index] ) );
				}
				OutValue.SourceType = EUsdBasicDataTypes::Matrix2d;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Matrix3d.GetType() )
			{
				pxr::GfMatrix3d Matrix = InVtValue.UncheckedGet<pxr::GfMatrix3d>();
				double* MatrixArray = Matrix.GetArray();
				for ( int32 Index = 0; Index < 9; ++Index )
				{
					OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), MatrixArray[ Index ] ) );
				}
				OutValue.SourceType = EUsdBasicDataTypes::Matrix3d;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Matrix4d.GetType() )
			{
				pxr::GfMatrix4d Matrix = InVtValue.UncheckedGet<pxr::GfMatrix4d>();
				double* MatrixArray = Matrix.GetArray();
				for ( int32 Index = 0; Index < 16; ++Index )
				{
					OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), MatrixArray[ Index ] ) );
				}
				OutValue.SourceType = EUsdBasicDataTypes::Matrix4d;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Quatd.GetType() )
			{
				pxr::GfQuatd Quat = InVtValue.UncheckedGet<pxr::GfQuatd>();
				const pxr::GfVec3d& Img = Quat.GetImaginary();
				double Real = Quat.GetReal();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Img[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Img[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Img[2] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Real ) );
				OutValue.SourceType = EUsdBasicDataTypes::Quatd;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Quatf.GetType() )
			{
				pxr::GfQuatf Quat = InVtValue.UncheckedGet<pxr::GfQuatf>();
				const pxr::GfVec3f& Img = Quat.GetImaginary();
				float Real = Quat.GetReal();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Img[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Img[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Img[2] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Real ) );
				OutValue.SourceType = EUsdBasicDataTypes::Quatf;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Quath.GetType() )
			{
				pxr::GfQuath Quat = InVtValue.UncheckedGet<pxr::GfQuath>();
				const pxr::GfVec3h& Img = Quat.GetImaginary();
				float Real = Quat.GetReal();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Img[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Img[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Img[2] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Real ) );
				OutValue.SourceType = EUsdBasicDataTypes::Quath;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Double2.GetType() )
			{
				pxr::GfVec2d Vec = InVtValue.UncheckedGet<pxr::GfVec2d>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Vec[1] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Double2;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Float2.GetType() )
			{
				pxr::GfVec2f Vec = InVtValue.UncheckedGet<pxr::GfVec2f>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[1] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Float2;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Half2.GetType() )
			{
				pxr::GfVec2h Vec = InVtValue.UncheckedGet<pxr::GfVec2h>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[1] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Half2;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Int2.GetType() )
			{
				pxr::GfVec2i Vec = InVtValue.UncheckedGet<pxr::GfVec2i>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int32>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int32>(), Vec[1] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Int2;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Double3.GetType() )
			{
				pxr::GfVec3d Vec = InVtValue.UncheckedGet<pxr::GfVec3d>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Vec[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Vec[2] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Double3;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Float3.GetType() )
			{
				pxr::GfVec3f Vec = InVtValue.UncheckedGet<pxr::GfVec3f>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[2] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Float3;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Half3.GetType() )
			{
				pxr::GfVec3h Vec = InVtValue.UncheckedGet<pxr::GfVec3h>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[2] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Half3;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Int3.GetType() )
			{
				pxr::GfVec3i Vec = InVtValue.UncheckedGet<pxr::GfVec3i>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int32>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int32>(), Vec[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int32>(), Vec[2] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Int3;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Double4.GetType() )
			{
				pxr::GfVec4d Vec = InVtValue.UncheckedGet<pxr::GfVec4d>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Vec[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Vec[2] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<double>(), Vec[3] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Double4;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Float4.GetType() )
			{
				pxr::GfVec4f Vec = InVtValue.UncheckedGet<pxr::GfVec4f>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[2] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[3] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Float4;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Half4.GetType() )
			{
				pxr::GfVec4h Vec = InVtValue.UncheckedGet<pxr::GfVec4h>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[2] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<float>(), Vec[3] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Half4;
			}
			else if ( UnderlyingType == pxr::SdfValueTypeNames->Int4.GetType() )
			{
				pxr::GfVec4i Vec = InVtValue.UncheckedGet<pxr::GfVec4i>();
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int32>(), Vec[0] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int32>(), Vec[1] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int32>(), Vec[2] ) );
				OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<int32>(), Vec[3] ) );
				OutValue.SourceType = EUsdBasicDataTypes::Int4;
			}
		}

		// If we failed to parse, or if it's an array value, we just return a readonly string for now
		if ( OutValue.Components.Num() == 0 )
		{
			FString AttributeValue = UsdToUnreal::ConvertString( pxr::TfStringify( InVtValue ).c_str() );

			// STextBlock can get very slow calculating its desired size for very long string so chop it if needed
			const int32 MaxValueLength = 300;
			if ( AttributeValue.Len() > MaxValueLength )
			{
				AttributeValue.LeftInline( MaxValueLength );
				AttributeValue.Append( TEXT( "..." ) );
			}

			// For now we display arrays as a flat string
			OutValue.Components.Add( FPrimPropertyValueComponent( TInPlaceType<FString>(), AttributeValue ) );
			OutValue.SourceType = EUsdBasicDataTypes::String;
			bOutReadOnly = true;
		}

		return true;
	}
#endif // #if USE_USD_SDK
}

FUsdPrimAttributeViewModel::FUsdPrimAttributeViewModel( FUsdPrimAttributesViewModel* InOwner )
	: Owner( InOwner )
{
}

TArray< TSharedPtr< FString > > FUsdPrimAttributeViewModel::GetDropdownOptions() const
{
#if USE_USD_SDK
	if ( Label == TEXT("kind") )
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
				}
			);

		}
		return Options;
	}
	else if ( Label == UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->purpose ) )
	{
		return TArray< TSharedPtr< FString > >
		{
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->default_ ) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->proxy ) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->render) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->guide ) ),
		};
	}
	else if ( Label == UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->upAxis ) )
	{
		return TArray< TSharedPtr< FString > >
		{
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->y ) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->z ) ),
		};
	}
	else if ( Label == UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->visibility ) )
	{
		return TArray< TSharedPtr< FString > >
		{
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->inherited ) ),
			MakeShared< FString >( UsdToUnreal::ConvertToken( pxr::UsdGeomTokens->invisible ) ),
		};
	}
#endif // #if USE_USD_SDK

	return {};
}

void FUsdPrimAttributeViewModel::SetAttributeValue( const USDViewModels::FPrimPropertyValue& InValue )
{
	Owner->SetPrimAttribute( Label, InValue );
}

template<typename T>
void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString& AttributeName, const T& Value, USDViewModels::EUsdBasicDataTypes SourceType, const FString& SourceRole, bool bReadOnly )
{
	FUsdPrimAttributeViewModel Property( this );
	Property.Label = AttributeName;
	Property.Value.Components.Add( USDViewModels::FPrimPropertyValueComponent( TInPlaceType<T>(), Value ) );
	Property.Value.SourceType = SourceType;
	Property.Value.SourceRole = SourceRole;
	Property.bReadOnly = bReadOnly;

	PrimAttributes.Add( MakeSharedUnreal< FUsdPrimAttributeViewModel >( MoveTemp( Property ) ) );
}

void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString& AttributeName, const USDViewModels::FPrimPropertyValue& Value, bool bReadOnly )
{
	FUsdPrimAttributeViewModel Property( this );
	Property.Label = AttributeName;
	Property.Value = Value;
	Property.bReadOnly = bReadOnly;

	PrimAttributes.Add( MakeSharedUnreal< FUsdPrimAttributeViewModel >( MoveTemp( Property ) ) );
}

void FUsdPrimAttributesViewModel::SetPrimAttribute( const FString& AttributeName, const USDViewModels::FPrimPropertyValue& InValue )
{
	bool bSuccess = false;

#if USE_USD_SDK
	if ( !UsdStage )
	{
		return;
	}

	// Transact here as setting this attribute may trigger USD events that affect assets/components
	FScopedTransaction Transaction( FText::Format(
		LOCTEXT( "SetPrimAttribute", "Set value for attribute '{0}' of prim '{1}'" ),
		FText::FromString( AttributeName ),
		FText::FromString( PrimPath )
	));

	FScopedUsdAllocs UsdAllocs;

	pxr::TfToken AttributeNameToken = UnrealToUsd::ConvertToken( *AttributeName ).Get();
	const bool bIsStageAttribute = PrimPath == TEXT( "/" ) || PrimPath.IsEmpty();

	if ( bIsStageAttribute )
	{
		pxr::VtValue VtValue;
		if ( UnrealToUsd::ConvertVtValue( InValue, VtValue ) )
		{
			// To set stage metadata the edit target must be the root or session layer
			pxr::UsdStageRefPtr Stage{ UsdStage };
			pxr::UsdEditContext( Stage, Stage->GetRootLayer() );
			bSuccess = Stage->SetMetadata( AttributeNameToken, VtValue );
		}
	}
	else if ( UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath( *PrimPath ) ) )
	{
		pxr::VtValue NewValue;
		if ( AttributeName == TEXT( "kind" ) && InValue.Components.Num() == 1 && InValue.Components[ 0 ].IsType<FString>() )
		{
			bSuccess = IUsdPrim::SetKind(
				UsdPrim,
				UnrealToUsd::ConvertToken( *( InValue.Components[ 0 ].Get<FString>() ) ).Get()
			);
		}
		else
		{
			if ( pxr::UsdAttribute Attribute = pxr::UsdPrim( UsdPrim ).GetAttribute( AttributeNameToken ) )
			{
				if ( UnrealToUsd::ConvertVtValue( InValue, NewValue ) )
				{
					bSuccess = Attribute.Set( NewValue );
				}
			}
		}
	}

	if ( !bSuccess )
	{
		const FText ErrorMessage = FText::Format( LOCTEXT( "FailToSetAttributeMessage", "Failed to set attribute '{0}' on {1} '{2}'" ),
			FText::FromString( UsdToUnreal::ConvertToken( AttributeNameToken ) ),
			FText::FromString( bIsStageAttribute ? TEXT( "stage" ) : TEXT( "prim" ) ),
			FText::FromString( bIsStageAttribute ? UsdStage.GetRootLayer().GetRealPath() : PrimPath )
		);

		FNotificationInfo ErrorToast( ErrorMessage );
		ErrorToast.ExpireDuration = 5.0f;
		ErrorToast.bFireAndForget = true;
		ErrorToast.Image = FCoreStyle::Get().GetBrush( TEXT( "MessageLog.Warning" ) );
		FSlateNotificationManager::Get().AddNotification( ErrorToast );

		FUsdLogManager::LogMessage( EMessageSeverity::Warning, ErrorMessage );
	}
#endif // #if USE_USD_SDK
}

void FUsdPrimAttributesViewModel::Refresh( const TCHAR* InPrimPath, float TimeCode )
{
	FScopedUnrealAllocs UnrealAllocs;

	PrimPath = InPrimPath;
	PrimAttributes.Reset();

#if USE_USD_SDK
	if ( UsdStage )
	{
		// Show info about the stage
		if ( PrimPath.Equals( TEXT( "/" ) ) || PrimPath.IsEmpty() )
		{
			const bool bReadOnly = true;
			const FString Role = TEXT( "" );
			CreatePrimAttribute( TEXT( "path" ), UsdStage.GetRootLayer().GetRealPath(), USDViewModels::EUsdBasicDataTypes::String, Role, bReadOnly );

			FScopedUsdAllocs UsdAllocs;

			std::vector<pxr::TfToken> TokenVector = pxr::SdfSchema::GetInstance().GetMetadataFields( pxr::SdfSpecType::SdfSpecTypePseudoRoot );
			for ( const pxr::TfToken& Token : TokenVector )
			{
				pxr::VtValue VtValue;
				if ( pxr::UsdStageRefPtr( UsdStage )->GetMetadata( Token, &VtValue ) )
				{
					FString AttributeName = UsdToUnreal::ConvertToken( Token );

					USDViewModels::FPrimPropertyValue AttributeValue;
					bool bAttrReadOnly = false;
					const bool bHasAttribute = UsdToUnreal::ConvertVtValue( VtValue, AttributeValue, bAttrReadOnly );
					if ( !bHasAttribute )
					{
						continue;
					}

					CreatePrimAttribute( AttributeName, AttributeValue, bAttrReadOnly );
				}
			}

		}
		// Show info about a prim
		else if ( UE::FUsdPrim UsdPrim = UsdStage.GetPrimAtPath( UE::FSdfPath( InPrimPath ) ) )
		{
			// For now we can't rename/reparent prims through this
			const bool bReadOnly = true;
			const FString Role = TEXT( "" );
			CreatePrimAttribute( TEXT( "name" ), UsdPrim.GetName().ToString(), USDViewModels::EUsdBasicDataTypes::String, Role, bReadOnly );
			CreatePrimAttribute( TEXT( "path" ), FString( InPrimPath ), USDViewModels::EUsdBasicDataTypes::String, Role, bReadOnly );
			CreatePrimAttribute( TEXT( "kind" ), UsdToUnreal::ConvertString( IUsdPrim::GetKind( UsdPrim ).GetString() ), USDViewModels::EUsdBasicDataTypes::Token );

			FScopedUsdAllocs UsdAllocs;

			for ( const pxr::UsdAttribute& PrimAttribute : pxr::UsdPrim( UsdPrim ).GetAttributes() )
			{
				FString AttributeName = UsdToUnreal::ConvertString( PrimAttribute.GetName().GetString() );

				pxr::VtValue VtValue;
				PrimAttribute.Get( &VtValue, TimeCode );

				USDViewModels::FPrimPropertyValue AttributeValue;
				bool bAttrReadOnly = false;
				const bool bHasValue = UsdToUnreal::ConvertVtValue( VtValue, AttributeValue, bAttrReadOnly );

				if ( bHasValue )
				{
					CreatePrimAttribute( AttributeName, AttributeValue, bAttrReadOnly );
				}

				if ( PrimAttribute.HasAuthoredConnections() )
				{
					const FString ConnectionAttributeName = AttributeName + TEXT(":connect");

					pxr::SdfPathVector ConnectedSources;
					PrimAttribute.GetConnections( &ConnectedSources );

					for ( pxr::SdfPath& ConnectedSource : ConnectedSources )
					{
						USDViewModels::FPrimPropertyValue ConnectionPropertyValue;
						ConnectionPropertyValue.SourceType = USDViewModels::EUsdBasicDataTypes::String;

						USDViewModels::FPrimPropertyValueComponent ConnectionValueComponent;
						ConnectionPropertyValue.Components.Emplace( TInPlaceType< FString >(), UsdToUnreal::ConvertPath( ConnectedSource ) );

						const bool bConnectionValueReadOnly = true;
						CreatePrimAttribute( ConnectionAttributeName, ConnectionPropertyValue, bConnectionValueReadOnly );
					}
				}
			}
		}
	}
#endif // #if USE_USD_SDK
}

// One for each variant of FPrimPropertyValue. These should all be implicitly instantiated from the above code anyway, but just in case that changes somehow
template void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString&, const bool&,	USDViewModels::EUsdBasicDataTypes, const FString&, bool );
template void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString&, const uint8&,	USDViewModels::EUsdBasicDataTypes, const FString&, bool );
template void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString&, const int32&,	USDViewModels::EUsdBasicDataTypes, const FString&, bool );
template void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString&, const uint32&,	USDViewModels::EUsdBasicDataTypes, const FString&, bool );
template void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString&, const int64&,	USDViewModels::EUsdBasicDataTypes, const FString&, bool );
template void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString&, const uint64&,	USDViewModels::EUsdBasicDataTypes, const FString&, bool );
template void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString&, const float&,	USDViewModels::EUsdBasicDataTypes, const FString&, bool );
template void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString&, const double&,	USDViewModels::EUsdBasicDataTypes, const FString&, bool );
template void FUsdPrimAttributesViewModel::CreatePrimAttribute( const FString&, const FString&, USDViewModels::EUsdBasicDataTypes, const FString&, bool );

#undef LOCTEXT_NAMESPACE


