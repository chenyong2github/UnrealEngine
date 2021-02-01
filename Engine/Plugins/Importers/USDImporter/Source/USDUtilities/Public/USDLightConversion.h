// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "CoreMinimal.h"

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
class UsdLuxLight;
class UsdLuxDiskLight;
class UsdLuxDistantLight;
class UsdLuxDomeLight;
class UsdLuxRectLight;
class UsdLuxShapingAPI;
class UsdLuxSphereLight;

class UsdPrim;
class UsdStage;
template< typename T > class TfRefPtr;

using UsdStageRefPtr = TfRefPtr< UsdStage >;
PXR_NAMESPACE_CLOSE_SCOPE

class UUsdAssetCache;
class UDirectionalLightComponent;
class ULightComponentBase;
class UPointLightComponent;
class URectLightComponent;
class USkyLightComponent;
class USpotLightComponent;
struct FUsdStageInfo;

/**
 * Converts UsdLuxLight attributes to the corresponding ULightComponent.
 * Each function handles its specific attributes only. Meaning that to convert a UsdLuxRectLight, one has to call ConvertXformable, ConvertLight and ConvertRectLight.
 *
 * Corresponding UsdLuxLight schema to Unreal component:
 *
 *	UsdLuxLight			->	ULightComponent
 *	UsdLuxDistantLight	->	UDirectionalLightComponent
 *	UsdLuxRectLight		->	URectLightComponent
 *	UsdLuxDiskLight		->	URectLightComponent
 *	UsdLuxSphereLight	->	UPointLightComponent
 */
namespace UsdToUnreal
{
	USDUTILITIES_API bool ConvertLight( const pxr::UsdLuxLight& Light, ULightComponentBase& LightComponentBase, double TimeCode );
	USDUTILITIES_API bool ConvertDistantLight( const pxr::UsdLuxDistantLight& DistantLight, UDirectionalLightComponent& LightComponent, double TimeCode );
	USDUTILITIES_API bool ConvertRectLight( const FUsdStageInfo& StageInfo, const pxr::UsdLuxRectLight& RectLight, URectLightComponent& LightComponent, double TimeCode );
	USDUTILITIES_API bool ConvertDiskLight( const FUsdStageInfo& StageInfo, const pxr::UsdLuxDiskLight& DiskLight, URectLightComponent& LightComponent, double TimeCode );
	USDUTILITIES_API bool ConvertSphereLight( const FUsdStageInfo& StageInfo, const pxr::UsdLuxSphereLight& SphereLight, UPointLightComponent& LightComponent, double TimeCode );
	USDUTILITIES_API bool ConvertDomeLight( const FUsdStageInfo& StageInfo, const pxr::UsdLuxDomeLight& DomeLight, USkyLightComponent& LightComponent, UUsdAssetCache* TexturesCache, double TimeCode );
	USDUTILITIES_API bool ConvertLuxShapingAPI( const FUsdStageInfo& StageInfo, const pxr::UsdLuxShapingAPI& ShapingAPI, USpotLightComponent& LightComponent, double TimeCode );
}

namespace UnrealToUsd
{
	USDUTILITIES_API bool ConvertLightComponent( const ULightComponentBase& LightComponent, pxr::UsdPrim& Prim, double TimeCode );
	USDUTILITIES_API bool ConvertDirectionalLightComponent( const UDirectionalLightComponent& LightComponent, pxr::UsdPrim& Prim, double TimeCode );
	USDUTILITIES_API bool ConvertRectLightComponent( const URectLightComponent& LightComponent, pxr::UsdPrim& Prim, double TimeCode );
	USDUTILITIES_API bool ConvertPointLightComponent( const UPointLightComponent& LightComponent, pxr::UsdPrim& Prim, double TimeCode );
	USDUTILITIES_API bool ConvertSkyLightComponent( const USkyLightComponent& LightComponent, pxr::UsdPrim& Prim, double TimeCode );
	USDUTILITIES_API bool ConvertSpotLightComponent( const USpotLightComponent& LightComponent, pxr::UsdPrim& Prim, double TimeCode );
}

#endif // #if USE_USD_SDK
