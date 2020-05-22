// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "CoreMinimal.h"

#include "USDIncludesStart.h"

#include "pxr/usd/usd/timeCode.h"

#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdAttribute;
	class UsdGeomMesh;
	class UsdShadeMaterial;
PXR_NAMESPACE_CLOSE_SCOPE

class FSHAHash;
class UMaterial;
class UTexture;

namespace UsdToUnreal
{
	USDUTILITIES_API bool ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material );
	USDUTILITIES_API bool ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material, TMap< FString, UObject* >& TexturesCache );
}

namespace UsdUtils
{
	USDUTILITIES_API FSHAHash HashShadeMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial );

	/** Returns the resolved path from a pxr::SdfAssetPath attribute. For UDIMs path, returns the path to the 1001 tile. */
	USDUTILITIES_API FString GetResolvedTexturePath( const pxr::UsdAttribute& TextureAssetPathAttr );

	/** Creates a texture from a pxr::SdfAssetPath attribute. */
	USDUTILITIES_API UTexture* CreateTexture( const pxr::UsdAttribute& TextureAssetPathAttr );
}

#endif // #if USE_USD_SDK
