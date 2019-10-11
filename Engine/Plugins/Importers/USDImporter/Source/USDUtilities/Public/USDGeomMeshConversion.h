// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

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
class UStaticMesh;

namespace UsdToUnreal
{
	USDUTILITIES_API bool ConvertGeomMesh( const pxr::UsdGeomMesh& UsdMesh, FMeshDescription& MeshDescription, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::EarliestTime() );
	USDUTILITIES_API bool ConvertMaterial( const pxr::UsdShadeMaterial& UsdMesh, UMaterial& Material );
}

namespace UnrealToUsd
{
	USDUTILITIES_API bool ConvertStaticMesh( const UStaticMesh* StaticMesh, pxr::UsdGeomMesh& UsdMesh, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::Default() );
}

#endif // #if USE_USD_SDK
