// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdPrim;
PXR_NAMESPACE_CLOSE_SCOPE

#endif // #if USE_USD_SDK

class UActorComponent;
class ULiveLinkComponentController;

namespace UsdUtils
{
#if USE_USD_SDK

	// Returns true in case Prim has our custom "LiveLinkAPI" schema
	USDUTILITIES_API bool PrimHasLiveLinkSchema( const pxr::UsdPrim& Prim );

#endif // USE_USD_SDK
}

namespace UnrealToUsd
{
#if USE_USD_SDK

	/**
	 * Converts UE component properties related to LiveLink into values for the attributes of our custom LiveLinkAPI
	 * schema.
	 * @param InComponent		The main component with data to convert
	 * @param InOutPrim			Prim with the LiveLinkAPI schema to receive the data
	 */
	USDUTILITIES_API void ConvertLiveLinkProperties( const UActorComponent* InComponent, pxr::UsdPrim& InOutPrim );

#endif // USE_USD_SDK
}
