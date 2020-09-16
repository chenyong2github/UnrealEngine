// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#if USE_USD_SDK

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdPrim;
PXR_NAMESPACE_CLOSE_SCOPE

class UMaterialInterface;
class UMeshComponent;
namespace UsdUtils
{
	struct FUsdPrimMaterialSlot;
	struct FUsdPrimMaterialAssignmentInfo;
}

/** Implementation that can be shared between the SkelRoot translator and GeomMesh translators */
namespace MeshTranslationImpl
{
	/** Retrieves the target materials described on AssignmentInfo, considering that the previous material assignment on the mesh was ExistingAssignments */
	TMap<const UsdUtils::FUsdPrimMaterialSlot*, UMaterialInterface*> ResolveMaterialAssignmentInfo( const pxr::UsdPrim& UsdPrim, const TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo>& AssignmentInfo, const TArray<UMaterialInterface*>& ExistingAssignments, const TMap< FString, UObject* >& PrimPathsToAssets, TMap< FString, UObject* >& AssetsCache, float Time, EObjectFlags Flags );
}

#endif // #if USE_USD_SDK