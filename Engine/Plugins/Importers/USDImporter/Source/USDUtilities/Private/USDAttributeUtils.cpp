// Copyright Epic Games, Inc. All Rights Reserved.

#include "USDAttributeUtils.h"

#include "UnrealUSDWrapper.h"
#include "USDErrorUtils.h"
#include "USDLayerUtils.h"
#include "USDLog.h"
#include "USDTypesConversion.h"

#include "UsdWrappers/SdfLayer.h"
#include "UsdWrappers/SdfPath.h"
#include "UsdWrappers/UsdAttribute.h"
#include "UsdWrappers/UsdStage.h"

#if USE_USD_SDK

#include "USDIncludesStart.h"
	#include "pxr/usd/pcp/cache.h"
	#include "pxr/usd/pcp/primIndex.h"
	#include "pxr/usd/pcp/propertyIndex.h"
	#include "pxr/usd/usd/usdFileFormat.h"

	#include "pxr/usd/sdf/layer.h"
	#include "pxr/usd/sdf/layerUtils.h"
	#include "pxr/usd/usd/attribute.h"
	#include "pxr/usd/usd/editContext.h"
	#include "pxr/usd/usd/stage.h"
#include "USDIncludesEnd.h"

namespace UsdUtils
{
	const pxr::TfToken MutedToken = UnrealToUsd::ConvertToken( TEXT( "UE:Muted" ) ).Get();
}

bool UsdUtils::MuteAttribute( UE::FUsdAttribute& Attribute, const UE::FUsdStage& Stage )
{
	FScopedUsdAllocs Allocs;

	const pxr::UsdAttribute& UsdAttribute = static_cast< const pxr::UsdAttribute& >( Attribute );
	const pxr::UsdStageRefPtr UsdStage{ Stage };
	if ( !UsdAttribute || !UsdStage )
	{
		return false;
	}

	pxr::SdfLayerRefPtr UEPersistentState = UsdUtils::GetUEPersistentStateSublayer( Stage );
	if ( !UEPersistentState )
	{
		return false;
	}

	pxr::SdfLayerRefPtr UESessionState = UsdUtils::GetUESessionStateSublayer( Stage );
	if ( !UESessionState )
	{
		return false;
	}

	pxr::SdfChangeBlock ChangeBlock;

	// Mark it as muted on the persistent state
	{
		pxr::UsdEditContext Context( Stage, UEPersistentState );

		UsdAttribute.SetCustomDataByKey( MutedToken, pxr::VtValue{ true } );
	}

	// Actually author the opinions that cause it to be muted on the session state
	{
		pxr::UsdEditContext Context( Stage, UESessionState );

		pxr::VtValue Value;
		UsdAttribute.Get( &Value, pxr::UsdTimeCode::Default() );

		// Clear the attribute so that it also gets rid of any time samples it may have
		UsdAttribute.Clear();

		if ( Value.IsEmpty() )
		{
			// It doesn't have any default value, so just mute the attribute completely
			UsdAttribute.Block();
		}
		else
		{
			// It has a default, non-animated value from a weaker opinion: Use that instead
			UsdAttribute.Set( Value );
		}
	}

	return true;
}

bool UsdUtils::UnmuteAttribute( UE::FUsdAttribute& Attribute, const UE::FUsdStage& Stage )
{
	FScopedUsdAllocs Allocs;

	pxr::UsdAttribute& UsdAttribute = static_cast< pxr::UsdAttribute& >( Attribute );
	const pxr::UsdStageRefPtr UsdStage{ Stage };
	if ( !UsdAttribute || !UsdStage )
	{
		return false;
	}

	if ( !IsAttributeMuted( Attribute, Stage ) )
	{
		return true;
	}

	pxr::SdfLayerRefPtr UEPersistentState = UsdUtils::GetUEPersistentStateSublayer( Stage );
	if ( !UEPersistentState )
	{
		return false;
	}

	pxr::SdfLayerRefPtr UESessionState = UsdUtils::GetUESessionStateSublayer( Stage );
	if ( !UESessionState )
	{
		return false;
	}

	pxr::SdfChangeBlock ChangeBlock;

	// Remove the mute tag on the persistent state layer
	{
		pxr::UsdEditContext Context( Stage, UEPersistentState );
		UsdAttribute.ClearCustomDataByKey( MutedToken );
	}

	// Clear our opinion of it on our session state layer
	{
		pxr::UsdEditContext Context( Stage, UESessionState );
		UsdAttribute.Clear();
	}

	return true;
}

bool UsdUtils::IsAttributeMuted( const UE::FUsdAttribute& Attribute, const UE::FUsdStage& Stage )
{
	FScopedUsdAllocs Allocs;

	const pxr::UsdAttribute& UsdAttribute = static_cast< const pxr::UsdAttribute& >( Attribute );
	if ( !UsdAttribute )
	{
		return false;
	}

	pxr::VtValue Data = UsdAttribute.GetCustomDataByKey( MutedToken );
	if ( Data.IsHolding<bool>() )
	{
		return Data.Get<bool>();
	}

	return false;
}

#endif // #if USE_USD_SDK
