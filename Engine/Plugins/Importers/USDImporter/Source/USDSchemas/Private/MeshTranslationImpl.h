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

class UUsdAssetCache;
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
	/** Resolves the material assignments in AssignmentInfo, returning an UMaterialInterface for each material slot */
	TMap<const UsdUtils::FUsdPrimMaterialSlot*, UMaterialInterface*> ResolveMaterialAssignmentInfo(
		const pxr::UsdPrim& UsdPrim,
		const TArray<UsdUtils::FUsdPrimMaterialAssignmentInfo>& AssignmentInfo,
		UUsdAssetCache& AssetCache,
		EObjectFlags Flags
	);

	/**
	 * Sets the material overrides on MeshComponent according to the material assignments of the UsdGeomMesh Prim.
	 * Warning: This function will temporarily switch the active LOD variant if one exists, so it's *not* thread safe!
	 */
	void SetMaterialOverrides(
		const pxr::UsdPrim& Prim,
		const TArray<UMaterialInterface*>& ExistingAssignments,
		UMeshComponent& MeshComponent,
		UUsdAssetCache& AssetCache,
		float Time,
		EObjectFlags Flags,
		bool bInterpretLODs,
		const FName& RenderContext,
		const FName& MaterialPurpose
	);

	enum class EUsdBaseMaterialProperties
	{
		None = 0,
		Translucent = 1,
		VT = 2,
		TwoSided = 4
	};
	ENUM_CLASS_FLAGS( EUsdBaseMaterialProperties )

	// Returns one of the alternatives of the UsdPreviewSurface base material depending on the material overrides
	// provided, and nullptr otherwise
	UMaterialInterface* GetBasePreviewSurfaceMaterial( EUsdBaseMaterialProperties BaseMaterialProperties );

	// Returns the VT version of the provided UsdPreviewSurface BaseMaterial. Returns the provided BaseMaterial back if
	// it is already a VT-capable base material, and returns nullptr if BaseMaterial isn't one of our base material
	// alternatives.
	// Example: Receives UsdPreviewSurfaceTwoSided -> Returns UsdPreviewSurfaceTwoSidedVT
	// Example: Receives UsdPreviewSurfaceTwoSidedVT -> Returns UsdPreviewSurfaceTwoSidedVT
	// Example: Receives SomeOtherBaseMaterial -> Returns nullptr
	UMaterialInterface* GetVTVersionOfBasePreviewSurfaceMaterial( UMaterialInterface* BaseMaterial );

	// Returns the two-sided version of the provided UsdPreviewSurface BaseMaterial. Returns the provided BaseMaterial
	// back if it is already a two-sided-capable base material, and returns nullptr if BaseMaterial isn't one of our base
	// material alternatives.
	// Example: Receives UsdPreviewSurfaceTranslucent -> Returns UsdPreviewSurfaceTwoSidedTranslucent
	// Example: Receives UsdPreviewSurfaceTwoSidedTranslucent -> Returns UsdPreviewSurfaceTwoSidedTranslucent
	// Example: Receives SomeOtherBaseMaterial -> Returns nullptr
	UMaterialInterface* GetTwoSidedVersionOfBasePreviewSurfaceMaterial( UMaterialInterface* BaseMaterial );
}

#endif // #if USE_USD_SDK