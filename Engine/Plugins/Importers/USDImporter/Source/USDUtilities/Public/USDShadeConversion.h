// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "CoreMinimal.h"
#include "Engine/TextureDefines.h"
#include "Materials/MaterialInterface.h"
#include "UObject/WeakObjectPtr.h"

#include "USDIncludesStart.h"
	#include "pxr/usd/usd/timeCode.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdAttribute;
	class UsdGeomMesh;
	class UsdPrim;
	class UsdShadeMaterial;
PXR_NAMESPACE_CLOSE_SCOPE

class FSHAHash;
class UUsdAssetCache;
class UMaterial;
class UMaterialOptions;
class UTexture;
struct FFlattenMaterial;
struct FPropertyEntry;
enum class EFlattenMaterialProperties : uint8;
enum EMaterialProperty;
namespace UE
{
	class FUsdPrim;
}

namespace UsdToUnreal
{
	/**
	 * Extracts material data from UsdShadeMaterial and places the results in Material. Note that since this is used for UMaterialInstanceDynamics at runtime as well,
	 * it will not set base property overrides (e.g. BlendMode) or the parent material, and will just assume that the caller handles that.
	 * @param UsdShadeMaterial - Shade material with the data to convert
	 * @param Material - Output parameter that will be filled with the converted data. Only the versions that receive a dynamic material instance will work at runtime
	 * @param TexturesCache - Cache to prevent importing a texture more than once
	 * @param PrimvarToUVIndex - Output parameter that will be filled the name of a primvar the material wants to use as UV set name, and the corresponding UV index it will sample texture coordinates from
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterialInstance& Material );
	USDUTILITIES_API bool ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterialInstance& Material, UUsdAssetCache* TexturesCache, TMap< FString, int32 >& PrimvarToUVIndex );
	USDUTILITIES_API bool ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material );
	USDUTILITIES_API bool ConvertMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterial& Material, UUsdAssetCache* TexturesCache, TMap< FString, int32 >& PrimvarToUVIndex );

	/**
	 * Attemps to assign the values of the surface shader inputs to the MaterialInstance parameters by matching the inputs display names to the parameters names.
	 * @param UsdShadeMaterial - Shade material with the data to convert
	 * @param MaterialInstance - Material instance on which we will set the parameter values
	 * @param TexturesCache - Cache to prevent importing a texture more than once
	 * @param RenderContext - The USD render context to use when fetching the surface shader
	 * @return Whether the conversion was successful or not.
	 * 
	 */
	USDUTILITIES_API bool ConvertShadeInputsToParameters( const pxr::UsdShadeMaterial& UsdShadeMaterial, UMaterialInstance& MaterialInstance, UUsdAssetCache* TexturesCache, const TCHAR* RenderContext = nullptr );
}

#if WITH_EDITOR
namespace UnrealToUsd
{
	/**
	 * Bakes InMaterial into textures and constants, and configures OutUsdShadeMaterial to use the baked data.
	 * @param InMaterial - Source material to bake
	 * @param InMaterialProperties - Material properties to bake from InMaterial
	 * @param InDefaultTextureSize - Size of the baked texture to use for any material property that does not have a custom size set
	 * @param InTexturesDir - Directory where the baked textures will be placed
	 * @param OutUsdShadeMaterialPrim - UsdPrim with the UsdShadeMaterial schema that will be configured to use the baked textures and constants
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertMaterialToBakedSurface( const UMaterialInterface& InMaterial, const TArray<FPropertyEntry>& InMaterialProperties, const FIntPoint& InDefaultTextureSize, const FDirectoryPath& InTexturesDir, pxr::UsdPrim& OutUsdShadeMaterialPrim );

	/**
	 * Converts a flattened material's data into textures placed at InTexturesDir, and configures OutUsdShadeMaterial to use the baked textures.
	 * Note that to avoid a potentially useless copy, InMaterial's samples will be modified in place to have 255 alpha before being exported to textures.
	 *
	 * @param MaterialName - Name of the material, used as prefix on the exported texture filenames
	 * @param InMaterial - Source material to bake
	 * @param InMaterialProperties - Object used *exclusively* to provide floating point constant values if necessary
	 * @param InTexturesDir - Directory where the baked textures will be placed
	 * @param OutUsdShadeMaterialPrim - UsdPrim with the UsdShadeMaterial schema that will be configured to use the baked textures and constants
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertFlattenMaterial( const FString& InMaterialName, FFlattenMaterial& InMaterial, const TArray<FPropertyEntry>& InMaterialProperties, const FDirectoryPath& InTexturesDir, UE::FUsdPrim& OutUsdShadeMaterialPrim );
}
#endif // WITH_EDITOR

namespace UsdUtils
{
	/**
	 * Returns whether the material needs to be rendered with the Translucent rendering mode.
	 * This function exists because we need this information *before* we pick the right parent for a material instance and properly convert it.
	 */
	USDUTILITIES_API bool IsMaterialTranslucent( const pxr::UsdShadeMaterial& UsdShadeMaterial );

	USDUTILITIES_API FSHAHash HashShadeMaterial( const pxr::UsdShadeMaterial& UsdShadeMaterial );

	/** Returns the resolved path from a pxr::SdfAssetPath attribute. For UDIMs path, returns the path to the 1001 tile. */
	USDUTILITIES_API FString GetResolvedTexturePath( const pxr::UsdAttribute& TextureAssetPathAttr );

	/** Creates a texture from a pxr::SdfAssetPath attribute. PrimPath is optional, and should point to the source shadematerial prim path. It will be placed in its UUsdAssetImportData */
	USDUTILITIES_API UTexture* CreateTexture( const pxr::UsdAttribute& TextureAssetPathAttr, const FString& PrimPath = FString(), TextureGroup LODGroup = TEXTUREGROUP_World, UObject* Outer = GetTransientPackage() );

#if WITH_EDITOR
	/** Convert between the two different types used to represent material channels to bake */
	USDUTILITIES_API EFlattenMaterialProperties MaterialPropertyToFlattenProperty( EMaterialProperty MaterialProperty );

	/** Convert between the two different types used to represent material channels to bake */
	USDUTILITIES_API EMaterialProperty FlattenPropertyToMaterialProperty( EFlattenMaterialProperties FlattenProperty );
#endif // WITH_EDITOR

	/** Converts channels that have the same value for every pixel into a channel that only has a single pixel with that value */
	USDUTILITIES_API void CollapseConstantChannelsToSinglePixel( FFlattenMaterial& InMaterial );

	/** Temporary function until UnrealWrappers can create attributes, just adds a custom bool attribute 'worldSpaceNormals' as true */
	USDUTILITIES_API bool MarkMaterialPrimWithWorldSpaceNormals( const UE::FUsdPrim& MaterialPrim );
}

#endif // #if USE_USD_SDK
