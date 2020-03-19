// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "CoreMinimal.h"

#include "USDIncludesStart.h"

#include "pxr/usd/usd/timeCode.h"

#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdGeomMesh;
	class UsdShadeMaterial;
PXR_NAMESPACE_CLOSE_SCOPE

class FSHAHash;
class UMaterial;

namespace UsdToUnreal
{
	USDUTILITIES_API bool ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material );
	USDUTILITIES_API bool ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material, TMap< FString, UObject* >& TexturesCache );

	USDUTILITIES_API FSHAHash HashShadeMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial );
}

#endif // #if USE_USD_SDK
