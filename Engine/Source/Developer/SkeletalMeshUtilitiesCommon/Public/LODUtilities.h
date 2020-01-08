// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshBuild.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Framework/Commands/UIAction.h"

namespace ClothingAssetUtils
{
	struct FClothingAssetMeshBinding;
}

//////////////////////////////////////////////////////////////////////////
// FSkeletalMeshUpdateContext


struct FSkeletalMeshUpdateContext
{
	USkeletalMesh*				SkeletalMesh;
	TArray<UActorComponent*>	AssociatedComponents;

	FExecuteAction				OnLODChanged;
};

class FSkeletalMeshImportData;

//////////////////////////////////////////////////////////////////////////
// FLODUtilities


class SKELETALMESHUTILITIESCOMMON_API FLODUtilities
{
public:

	/**
	* Process and update the vertex Influences using the predefined wedges
	* 
	* @param WedgeCount - The number of wedges in the corresponding mesh.
	* @param Influences - BoneWeights and Ids for the corresponding vertices. 
	*/
	static void ProcessImportMeshInfluences(const int32 WedgeCount, TArray<SkeletalMeshImportData::FRawBoneInfluence>& Influences);

	/** Regenerate LODs of the mesh
	*
	* @param SkeletalMesh : the mesh that will regenerate LOD
	* @param NewLODCount : Set valid value (>0) if you want to change LOD count.
	*						Otherwise, it will use the current LOD and regenerate
	* @param bRegenerateEvenIfImported : If this is true, it only regenerate even if this LOD was imported before
	*									If false, it will regenerate for only previously auto generated ones
	*
	* @return true if succeed. If mesh reduction is not available this will return false.
	*/
	static bool RegenerateLOD(USkeletalMesh* SkeletalMesh, int32 NewLODCount = 0, bool bRegenerateEvenIfImported = false, bool bGenerateBaseLOD = false);

	/** Removes a particular LOD from the SkeletalMesh. 
	*
	* @param UpdateContext - The skeletal mesh and actor components to operate on.
	* @param DesiredLOD   - The LOD index to remove the LOD from.
	*/
	static void RemoveLOD( FSkeletalMeshUpdateContext& UpdateContext, int32 DesiredLOD );

	/**
	*	Simplifies the static mesh based upon various user settings for DesiredLOD.
	*
	* @param UpdateContext - The skeletal mesh and actor components to operate on.
	* @param DesiredLOD - The LOD to simplify
	*/
	static void SimplifySkeletalMeshLOD(FSkeletalMeshUpdateContext& UpdateContext, int32 DesiredLOD, bool bRestoreClothing = false);

	/**
	*	Restore the LOD imported model to the last imported data. Call this function if you want to remove the reduce on the base LOD
	*
	* @param SkeletalMesh - The skeletal mesh to operate on.
	* @param LodIndex - The LOD index to restore the imported LOD model
	* @param bReregisterComponent - if true the component using the skeletal mesh will all be re register.
	*/
	static void RestoreSkeletalMeshLODImportedData(USkeletalMesh* SkeletalMesh, int32 LodIndex);
	
	/**
	 * Refresh LOD Change
	 * 
	 * LOD has changed, it will have to notify all SMC that uses this SM
	 * and ask them to refresh LOD
	 *
	 * @param	InSkeletalMesh	SkeletalMesh that LOD has been changed for
	 */
	static void RefreshLODChange(const USkeletalMesh* SkeletalMesh);


	/*
	 * Regenerate LOD that are dependent of LODIndex
	 */
	static void RegenerateDependentLODs(USkeletalMesh* SkeletalMesh, int32 LODIndex);

	/*
	 * Build the morph targets for the specified LOD. The function use the Morph target data stored in the FSkeletalMeshImportData ImportData structure
	 */
	static void BuildMorphTargets(USkeletalMesh* SkeletalMesh, class FSkeletalMeshImportData &ImportData, int32 LODIndex, bool ShouldImportNormals, bool ShouldImportTangents, bool bUseMikkTSpace, const FOverlappingThresholds& Thresholds);

	/**
	 *	This function apply the skinning weights from asource skeletal mesh to the destination skeletal mesh.
	 *  The Destination will receive the weights has the alternate weights.
	 *  We extract the imported skinning weight data from the SkeletalMeshSrc and we save the imported raw data into the destination mesh.
	 *  Then we call UpdateAlternateSkinWeights without the SkeletalMeshSrc
	 *
	 * @param SkeletalMeshDest - The skeletal mesh that will receive the alternate skinning weights.
	 * @param SkeletalMeshSrc - The skeletal mesh that contain the alternate skinning weights.
	 * @param LODIndexDest - the destination LOD
	 * @param LODIndexSrc - the Source LOD index
	 */
	static bool UpdateAlternateSkinWeights(USkeletalMesh* SkeletalMeshDest, const FName& ProfileNameDest, USkeletalMesh* SkeletalMeshSrc, int32 LODIndexDest, int32 LODIndexSrc, FOverlappingThresholds OverlappingThresholds, bool ShouldImportNormals, bool ShouldImportTangents, bool bUseMikkTSpace, bool bComputeWeightedNormals);

	
	/*
	 *	This function apply the skinning weights from the saved imported skinning weight data to the destination skeletal mesh.
	 *  The Destination will receive the weights has the alternate weights.
	 *
	 * @param SkeletalMeshDest - The skeletal mesh that will receive the alternate skinning weights.
	 * @param LODIndexDest - the destination LOD
	 */
	static bool UpdateAlternateSkinWeights(USkeletalMesh* SkeletalMeshDest, const FName& ProfileNameDest, int32 LODIndexDest, FOverlappingThresholds OverlappingThresholds, bool ShouldImportNormals, bool ShouldImportTangents, bool bUseMikkTSpace, bool bComputeWeightedNormals);

	static bool UpdateAlternateSkinWeights(FSkeletalMeshLODModel& LODModelDest, FSkeletalMeshImportData& ImportDataDest, const FString SkeletalMeshName, FReferenceSkeleton& RefSkeleton, const FName& ProfileNameDest, int32 LODIndexDest, FOverlappingThresholds OverlappingThresholds, bool ShouldImportNormals, bool ShouldImportTangents, bool bUseMikkTSpace, bool bComputeWeightedNormals);

	/** Re-generate all (editor-only) skin weight profile, used whenever we rebuild the skeletal mesh data which could change the chunking and bone indices */
	static void RegenerateAllImportSkinWeightProfileData(FSkeletalMeshLODModel& LODModelDest);

	static void UnbindClothingAndBackup(USkeletalMesh* SkeletalMesh, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings);
	static void UnbindClothingAndBackup(USkeletalMesh* SkeletalMesh, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings, const int32 LODIndex);
	
	static void RestoreClothingFromBackup(USkeletalMesh* SkeletalMesh, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings);
	static void RestoreClothingFromBackup(USkeletalMesh* SkeletalMesh, TArray<ClothingAssetUtils::FClothingAssetMeshBinding>& ClothingBindings, const int32 LODIndex);
	

private:
	FLODUtilities() {}

	/** Generate the editor-only data stored for a skin weight profile (relies on bone indices) */
	static void GenerateImportedSkinWeightProfileData(const FSkeletalMeshLODModel& LODModelDest, FImportedSkinWeightProfileData& ImportedProfileData);
	

	/**
	 *	Simplifies the static mesh based upon various user settings for DesiredLOD
	 *  This is private function that gets called by SimplifySkeletalMesh
	 *
	 * @param SkeletalMesh - The skeletal mesh and actor components to operate on.
	 * @param DesiredLOD - Desired LOD
	 */
	static void SimplifySkeletalMeshLOD(USkeletalMesh* SkeletalMesh, int32 DesiredLOD, bool bRestoreClothing = false);

	/**
	*  Remap the morph targets of the base LOD onto the desired LOD.
	*
	* @param SkeletalMesh - The skeletal mesh to operate on.
	* @param SourceLOD      - The source LOD morph target .
	* @param DestinationLOD   - The destination LOD morph target to apply the source LOD morph target
	*/
	static void ApplyMorphTargetsToLOD(USkeletalMesh* SkeletalMesh, int32 SourceLOD, int32 DestinationLOD);

	/**
	*  Clear generated morphtargets for the given LODs
	*
	* @param SkeletalMesh - The skeletal mesh and actor components to operate on.
	* @param DesiredLOD - Desired LOD
	*/
	static void ClearGeneratedMorphTarget(USkeletalMesh* SkeletalMesh, int32 DesiredLOD);
};
