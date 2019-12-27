// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "USDIncludesStart.h"

#include "pxr/pxr.h"

#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdGeomMesh;
	class UsdSkelSkeletonQuery;
	class UsdSkelSkinningQuery;
PXR_NAMESPACE_CLOSE_SCOPE

class USkeletalMesh;
class FSkeletalMeshImportData;

namespace UsdToUnreal
{
	USDUTILITIES_API bool ConvertSkeleton( const pxr::UsdSkelSkeletonQuery& UsdSkeletonQuery, FSkeletalMeshImportData& SkelMeshImportData );
	USDUTILITIES_API bool ConvertSkinnedMesh( const pxr::UsdSkelSkinningQuery& UsdSkinningQuery, FSkeletalMeshImportData& SkelMeshImportData );

	USDUTILITIES_API USkeletalMesh* GetSkeletalMeshFromImportData( FSkeletalMeshImportData& SkelMeshImportData, EObjectFlags ObjectFlags );
}

#endif // #if USE_USD_SDK
