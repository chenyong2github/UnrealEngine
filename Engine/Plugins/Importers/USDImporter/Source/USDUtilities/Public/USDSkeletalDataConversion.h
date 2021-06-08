// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Animation/MorphTarget.h"
#include "Animation/Skeleton.h"
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

#include "USDConversionUtils.h"

#if USE_USD_SDK
#include "USDIncludesStart.h"
	#include "pxr/pxr.h"
	#include "pxr/usd/usd/timeCode.h"
#include "USDIncludesEnd.h"

PXR_NAMESPACE_OPEN_SCOPE
	class UsdGeomMesh;
	class UsdSkelAnimQuery;
	class UsdSkelBlendShape;
	class UsdSkelRoot;
	class UsdSkelSkeleton;
	class UsdSkelSkeletonQuery;
	class UsdSkelSkinningQuery;
	template<typename T> class VtArray;
PXR_NAMESPACE_CLOSE_SCOPE

class FSkeletalMeshImportData;
class UAnimSequence;
class USkeletalMesh;
struct FUsdStageInfo;
namespace SkeletalMeshImportData
{
	struct FBone;
	struct FMaterial;
}
namespace UsdUtils
{
	struct FUsdPrimMaterialSlot;
}
namespace UE
{
	class FUsdStage;
}

#endif // #if USE_USD_SDK

namespace UsdUtils
{
	struct USDUTILITIES_API FUsdBlendShapeInbetween
	{
		// Name of the FUsdBlendShape/UMorphTarget that holds the morph data for this inbetween
		FString Name;

		float InbetweenWeight;

		friend FArchive& operator<<( FArchive& Ar, FUsdBlendShapeInbetween& BlendShape )
		{
			Ar << BlendShape.Name;
			Ar << BlendShape.InbetweenWeight;
			return Ar;
		}
	};

	struct USDUTILITIES_API FUsdBlendShape
	{
		FString Name;

		// Because Meshes need to target BlendShapes with USD relationships, and because relationships can't
		// target things inside USD variants, we get that we can never have different data for different LODs within the
		// same blend shape, like UMorphTarget does. At most, we can be *used* by different USD LOD meshes, which this member tracks.
		TSet<int32> LODIndicesThatUseThis;
		TArray<FMorphTargetDelta> Vertices;
		TArray<FUsdBlendShapeInbetween> Inbetweens;
		bool bHasAuthoredTangents = false;

		bool IsValid() const { return Vertices.Num() > 0; }

		friend FArchive& operator<<( FArchive& Ar, FUsdBlendShape& BlendShape )
		{
			Ar << BlendShape.Name;
			Ar << BlendShape.Inbetweens;
			return Ar;
		}
	};

	/**
	 * Maps from a full blend shape path (e.g. '/Scene/Mesh/BlendShapeName') to the parsed FUsdBlendShape struct.
	 * We need this to be case sensitive because USD paths are, and even the sample HumanFemale Skel scene has
	 * paths that differ only by case (e.g. 'JawUD' and 'JAWUD' blend shapes)
	 */
	using FBlendShapeMap = TMap<FString, FUsdBlendShape, FDefaultSetAllocator, FCaseSensitiveStringMapFuncs< FUsdBlendShape > >;

	/**
	 * We decompose inbetween blend shapes on import into separate morph targets, alongside the primary blend shape, which also becomes a morph target.
	 * This function returns the morph target weights for the primary morph target, as well as all the morph targets for inbetween shapes of 'InBlendShape', given an
	 * initial input weight value for the USD blend shape. Calculations are done following the equations at
	 * https://graphics.pixar.com/usd/docs/api/_usd_skel__schemas.html#UsdSkel_BlendShape_Inbetweens
	 *
	 * Note: This assumes that the morph targets in InBlendShape are sorted by weight.
	 */
	USDUTILITIES_API void ResolveWeightsForBlendShape( const FUsdBlendShape& InBlendShape, float InWeight, float& OutPrimaryWeight, TArray<float>& OutInbetweenWeights );

#if USE_USD_SDK
	/** Allows creation of a skinning query from the underlying skinned mesh and skeleton. Adapted from the USD SDK implementation */
	USDUTILITIES_API pxr::UsdSkelSkinningQuery CreateSkinningQuery( const pxr::UsdGeomMesh& SkinnedMesh, const pxr::UsdSkelSkeletonQuery& SkeletonQuery );

	/**
	 * Sets prim AnimationSource as the animation source for prim Prim.
	 * Applies the SkelBindingAPI to Prim. See pxr::SkelBindingAPI::GetAnimationSourceRel.
	 */
	USDUTILITIES_API void BindAnimationSource( pxr::UsdPrim& Prim, const pxr::UsdPrim& AnimationSource );
#endif // USE_USD_SDK
}

#if USE_USD_SDK && WITH_EDITOR
namespace UsdToUnreal
{
	/**
	 * Extracts skeleton data from UsdSkeletonQuery and places the results in SkelMeshImportData.
	 * @param UsdSkeletonQuery - SkeletonQuery with the data to convert
	 * @param SkelMeshImportData - Output parameter that will be filled with the converted data
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertSkeleton( const pxr::UsdSkelSkeletonQuery& UsdSkeletonQuery, FSkeletalMeshImportData& SkelMeshImportData );

	/**
	 * Converts an USD blend shape into zero, one or more FUsdBlendShapes, and places them in OutBlendShapes
	 * @param UsdBlendShape - Source USD object with the blend shape data
	 * @param StageInfo - Details about the stage, required to do the coordinate conversion from the USD blend shape to morph target data
	 * @param LODIndex - LODIndex of the SkeletalMesh that will use the imported FUsdBlendShape
	 * @param AdditionalTransform - Additional affine transform to apply to the blend shape deltas and tangents
	 * @param PointIndexOffset - Index into the corresponding SkelMeshImportData.Points where the points corresponding to the UsdBlendShape's prim start
	 * @param UsedMorphTargetNames - Names that the newly created FUsdBlendShapes cannot have (this function will also add the names of the newly created FUsdBlendShapes to it)
	 * @param OutBlendShapes - Where the newly created blend shapes will be added to
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertBlendShape( const pxr::UsdSkelBlendShape& UsdBlendShape, const FUsdStageInfo& StageInfo, const FTransform& AdditionalTransform, uint32 PointIndexOffset, TSet<FString>& UsedMorphTargetNames, UsdUtils::FBlendShapeMap& OutBlendShapes );
	USDUTILITIES_API bool ConvertBlendShape( const pxr::UsdSkelBlendShape& UsdBlendShape, const FUsdStageInfo& StageInfo, int32 LODIndex, const FTransform& AdditionalTransform, uint32 PointIndexOffset, TSet<FString>& UsedMorphTargetNames, UsdUtils::FBlendShapeMap& OutBlendShapes );

	/**
	 * Extracts skeletal mesh data fro UsdSkinningQuery, and places the results in SkelMeshImportData.
	 * @param UsdSkinningQuery - SkinningQuery with the data to convert
	 * @param AdditionalTransform - Additional transform to apply to the vertices of the mesh
	 * @param SkelMeshImportData - Output parameter that will be filled with the converted data
	 * @param MaterialAssignments - Output parameter that will be filled with the material assignment data extracted from UsdSkinningQuery
	 * @param MaterialToPrimvarsUVSetNames - Maps from a material prim path, to pairs indicating which primvar names are used as 'st' coordinates for this mesh, and which UVIndex materials will sample from (e.g. ["st0", 0], ["myUvSet2", 2], etc). This is used to pick which primvars will become UV sets.
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertSkinnedMesh( const pxr::UsdSkelSkinningQuery& UsdSkinningQuery, const FTransform& AdditionalTransform, FSkeletalMeshImportData& SkelMeshImportData, TArray< UsdUtils::FUsdPrimMaterialSlot >& MaterialAssignments, const TMap< FString, TMap< FString, int32 > >& MaterialToPrimvarsUVSetNames );

	/**
	 * Will extract animation data from the animation source of InUsdSkeletonQuery's skeleton, and populate OutSkeletalAnimationAsset with the data.
	 * Warning: UAnimSequence must be previously set with a USkeleton generated from the skeletal data of the same UsdSkelSkeletonQuery.
	 * @param InUsdSkeletonQuery - SkinningQuery with the data to convert
	 * @param InSkinningTargets - Skinned meshes that use the skeleton of InUsdSkeletonQuery. Required to fetch the blend shape ordering of each mesh. Optional (can be nullptr to ignore)
	 * @param InBlendShapes - Converted blend shape data that will be used to interpret blend shape weights as morph target weight float curves. Optional (can be nullptr to ignore)
	 * @param InInterpretLODs - Whether we try parsing animation data from all LODs of skinning meshes that are inside LOD variant sets
	 * @param OutSkeletalAnimationAsset - Output parameter that will be filled with the converted data
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertSkelAnim( const pxr::UsdSkelSkeletonQuery& InUsdSkeletonQuery, const pxr::VtArray<pxr::UsdSkelSkinningQuery>* InSkinningTargets, const UsdUtils::FBlendShapeMap* InBlendShapes, bool bInInterpretLODs, UAnimSequence* OutSkeletalAnimationAsset );

	/**
	 * Builds a USkeletalMesh and USkeleton from the imported data in SkelMeshImportData
	 * @param LODIndexToSkeletalMeshImportData - Container with the imported skeletal mesh data per LOD level
	 * @param InSkeletonBones - Bones to use for the reference skeleton (skeleton data on each LODIndexToSkeletalMeshImportData will be ignored).
	 * @param BlendShapesByPath - Blend shapes to convert to morph targets
	 * @param ObjectFlags - Flags to use when creating the USkeletalMesh and corresponding USkeleton
	 * @return Newly created USkeletalMesh, or nullptr in case of failure
	 */
	USDUTILITIES_API USkeletalMesh* GetSkeletalMeshFromImportData( TArray<FSkeletalMeshImportData>& LODIndexToSkeletalMeshImportData, const TArray<SkeletalMeshImportData::FBone>& InSkeletonBones, UsdUtils::FBlendShapeMap& InBlendShapesByPath, EObjectFlags ObjectFlags );
}

namespace UnrealToUsd
{
	/**
	 * Converts the bone data from Skeleton into UsdSkeleton.
	 * WARNING: Sometimes Skeleton->ReferenceSkeleton() has slightly different transforms than USkeletalMesh->GetRefSkeleton(), so make
	 * sure you're using the correct one for what you wish to do!
	 *
	 * @param Skeleton - Source UE data to convert
	 * @param UsdSkeleton - Previously created prim with the UsdSkelSkeleton schema that will be filled with converted data
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertSkeleton( const USkeleton* Skeleton, pxr::UsdSkelSkeleton& UsdSkeleton );
	USDUTILITIES_API bool ConvertSkeleton( const FReferenceSkeleton& ReferenceSkeleton, pxr::UsdSkelSkeleton& UsdSkeleton );

	/**
	 * Converts SkeletalMesh, its skeleton and morph target data into the corresponding USD objects and populates SkelRoot with them, at time TimeCode
	 * @param SkeletalMesh - Mesh with the source data. If it contains multiple LODs it will lead to the creation of LOD variant sets and variants within SkelRoot
	 * @param SkelRoot - Root prim of the output source data. Child UsdSkelSkeleton, UsdGeomMesh, and UsdSkelBlendShape will be created as children of it, containing the converted data
	 * @param TimeCode - TimeCode with which the converted data will be placed in the USD stage
	 * @param StageForMaterialAssignments - Stage to use when authoring material assignments (we use this when we want to export the mesh to a payload layer, but the material assignments to an asset layer)
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertSkeletalMesh( const USkeletalMesh* SkeletalMesh, pxr::UsdPrim& SkelRootPrim, const pxr::UsdTimeCode TimeCode = pxr::UsdTimeCode::Default(), UE::FUsdStage* StageForMaterialAssignments = nullptr );

	/**
	 * Converts an AnimSequence to a UsdSkelAnimation. Includes bone transforms and blend shape weights.
	 * Keys will be baked at the stage TimeCodesPerSecond resolution.
	 * @param AnimSequence - The AnimSequence to convert
	 * @param SkelAnimPrim - Expected to be of type UsdkSkelAnimation
	 * @return Whether the conversion was successful or not.
	 */
	USDUTILITIES_API bool ConvertAnimSequence( UAnimSequence* AnimSequence, pxr::UsdPrim& SkelAnimPrim );
}

#endif // #if USE_USD_SDK && WITH_EDITOR
