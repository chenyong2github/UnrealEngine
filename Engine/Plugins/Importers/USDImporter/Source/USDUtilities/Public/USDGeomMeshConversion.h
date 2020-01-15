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

struct FMeshDescription;
struct FStaticMeshLODResources;
class UMaterial;
class UMaterialInstanceConstant;
class UStaticMesh;

namespace UsdToUnreal
{
	USDUTILITIES_API bool ConvertGeomMesh( const pxr::UsdGeomMesh& UsdMesh, FMeshDescription& MeshDescription, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime() );
	USDUTILITIES_API bool ConvertMaterial( const pxr::UsdShadeMaterial& UsdMesh, UMaterial& Material );

	/** Reads the first display color and opacity value and assigns it as the base color and opacity of the material */
	USDUTILITIES_API bool ConvertDisplayColor( const pxr::UsdGeomMesh& UsdMesh, UMaterialInstanceConstant& MaterialInstance, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime() );
}

namespace UnrealToUsd
{
	USDUTILITIES_API bool ConvertStaticMesh( const UStaticMesh* StaticMesh, pxr::UsdGeomMesh& UsdMesh, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::Default() );
}

#endif // #if USE_USD_SDK
