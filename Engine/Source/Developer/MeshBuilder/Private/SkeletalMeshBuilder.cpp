// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshBuilder.h"
#include "Modules/ModuleManager.h"
#include "MeshBoneReduction.h"
#include "Engine/EngineTypes.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"
#include "MeshAttributes.h"
#include "MeshDescriptionHelper.h"
#include "MeshBuild.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "GPUSkinVertexFactory.h"
#include "ThirdPartyBuildOptimizationHelper.h"
#include "Misc/ScopedSlowTask.h"
#include "LODUtilities.h"
#include "ClothingAsset.h"
#include "MeshUtilities.h"
#include "EditorFramework/AssetImportData.h"

DEFINE_LOG_CATEGORY(LogSkeletalMeshBuilder);

namespace SkeletalMeshBuilderOptimization
{
	void CacheOptimizeIndexBuffer(TArray<uint16>& Indices)
	{
		BuildOptimizationThirdParty::CacheOptimizeIndexBuffer(Indices);
	}

	void CacheOptimizeIndexBuffer(TArray<uint32>& Indices)
	{
		BuildOptimizationThirdParty::CacheOptimizeIndexBuffer(Indices);
	}
}

struct InfluenceMap
{
	FORCEINLINE bool operator()(const float& A, const float& B) const
	{
		return B > A;
	}
};

struct FSkeletalMeshVertInstanceIDAndZ
{
	FVertexInstanceID Index;
	float Z;
};

FSkeletalMeshBuilder::FSkeletalMeshBuilder()
{

}


bool FSkeletalMeshBuilder::Build(USkeletalMesh* SkeletalMesh, const int32 LODIndex, const bool bRegenDepLODs)
{
	int32 LODIndex_RemoveBones = LODIndex;

	// Set LODIndex to 0
	int32* pLODIndex = (int32*)&LODIndex;
	*pLODIndex = 0;

	check(SkeletalMesh->GetImportedModel());
	check(SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex));
	check(SkeletalMesh->GetLODInfo(LODIndex) != nullptr);
	
	FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(LODIndex);
	//We want to backup in case the LODModel is regenerated, this data is use to validate in the UI if the ddc must be rebuild
	const FString BackupBuildStringID = SkeletalMesh->GetImportedModel()->LODModels[LODIndex].BuildStringID;

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->RefSkeleton;

	FScopedSlowTask SlowTask(6.01f, NSLOCTEXT("SkeltalMeshBuilder", "BuildingSkeletalMeshLOD", "Building skeletal mesh LOD"));
	SlowTask.MakeDialog();

	//Prevent any PostEdit change during the build
	FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh, false, false);
	// Unbind any existing clothing assets before we reimport the geometry
	TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
	FLODUtilities::UnbindClothingAndBackup(SkeletalMesh, ClothingBindings, LODIndex);

	FSkeletalMeshImportData SkeletalMeshImportData;
	int32 NumTextCoord = 1; //We need to send rendering at least one tex coord buffer

	//This scope define where we can use the LODModel, after a reduction the LODModel must be requery since it is a new instance
	{
		FSkeletalMeshLODModel& BuildLODModel = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];

		//Load the imported data
		SkeletalMesh->LoadLODImportedData(LODIndex, SkeletalMeshImportData);

		TArray<FVector> LODPoints;
		TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
		TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
		TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
		TArray<int32> LODPointToRawMap;
		SkeletalMeshImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap);

		//Use the max because we need to have at least one texture coordinnate
		NumTextCoord = FMath::Max<int32>(NumTextCoord, SkeletalMeshImportData.NumTexCoords);
		
		//BaseLOD need to make sure the source data fit with the skeletalmesh materials array before using meshutilities.BuildSkeletalMesh
		FLODUtilities::AdjustImportDataFaceMaterialIndex(SkeletalMesh->Materials, SkeletalMeshImportData.Materials, LODFaces, LODIndex);

		//Build the skeletalmesh using mesh utilities module
		IMeshUtilities::MeshBuildOptions Options;
		Options.FillOptions(LODInfo->BuildSettings);
		//Force the normals or tangent in case the data is missing
		Options.bComputeNormals |= !SkeletalMeshImportData.bHasNormals;
		Options.bComputeTangents |= !SkeletalMeshImportData.bHasTangents;
		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

		// Create skinning streams for NewModel.
		SlowTask.EnterProgressFrame(1.0f);
		MeshUtilities.BuildSkeletalMesh(
			BuildLODModel, 
			SkeletalMesh->GetPathName(),
			RefSkeleton,
			LODInfluences,
			LODWedges,
			LODFaces,
			LODPoints,
			LODPointToRawMap,
			Options
		);

		//Re-Apply the user section changes, the UserSectionsData is map to original section and should match the builded LODModel
		BuildLODModel.SyncronizeUserSectionsDataArray();

		// Set texture coordinate count on the new model.
		BuildLODModel.NumTexCoords = NumTextCoord;

		//Re-apply the morph target
		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RebuildMorphTarget", "Rebuilding morph targets..."));
		if (SkeletalMeshImportData.MorphTargetNames.Num() > 0)
		{
			FLODUtilities::BuildMorphTargets(SkeletalMesh, SkeletalMeshImportData, LODIndex, !Options.bComputeNormals, !Options.bComputeTangents, Options.bUseMikkTSpace, Options.OverlappingThresholds);
		}

		//Re-apply the alternate skinning it must be after the inline reduction
		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RebuildAlternateSkinning", "Rebuilding alternate skinning..."));
		const TArray<FSkinWeightProfileInfo>& SkinProfiles = SkeletalMesh->GetSkinWeightProfiles();
		for (int32 SkinProfileIndex = 0; SkinProfileIndex < SkinProfiles.Num(); ++SkinProfileIndex)
		{
			const FSkinWeightProfileInfo& ProfileInfo = SkinProfiles[SkinProfileIndex];
			FLODUtilities::UpdateAlternateSkinWeights(SkeletalMesh, ProfileInfo.Name, LODIndex, Options.OverlappingThresholds, !Options.bComputeNormals, !Options.bComputeTangents, Options.bUseMikkTSpace, Options.bComputeWeightedNormals);
		}

		
		FSkeletalMeshUpdateContext UpdateContext;
		UpdateContext.SkeletalMesh = SkeletalMesh;
		//We are reduce ourself in this case we reduce ourself from the original data and return true.
		if (SkeletalMesh->IsReductionActive(LODIndex))
		{
			SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RegenerateLOD", "Regenerate LOD..."));
			//Update the original reduction data since we just build a new LODModel.
			if (LODInfo->ReductionSettings.BaseLOD == LODIndex && SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData.IsValidIndex(LODIndex))
			{
				//Make the copy of the data only once until the ImportedModel change (re-imported)
				SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData[LODIndex]->EmptyBulkData();
				TMap<FString, TArray<FMorphTargetDelta>> BaseLODMorphTargetData;
				BaseLODMorphTargetData.Empty(SkeletalMesh->MorphTargets.Num());
				for (UMorphTarget *MorphTarget : SkeletalMesh->MorphTargets)
				{
					if (!MorphTarget->HasDataForLOD(LODIndex))
					{
						continue;
					}
					TArray<FMorphTargetDelta>& MorphDeltasArray = BaseLODMorphTargetData.FindOrAdd(MorphTarget->GetFullName());
					const FMorphTargetLODModel& BaseMorphModel = MorphTarget->MorphLODModels[LODIndex];
					//Iterate each original morph target source index to fill the NewMorphTargetDeltas array with the TargetMatchData.
					for (const FMorphTargetDelta& MorphDelta : BaseMorphModel.Vertices)
					{
						MorphDeltasArray.Add(MorphDelta);
					}
				}
				//Copy the original SkeletalMesh LODModel
				SkeletalMesh->GetImportedModel()->OriginalReductionSourceMeshData[LODIndex]->SaveReductionData(BuildLODModel, BaseLODMorphTargetData, SkeletalMesh);

				if (LODIndex == 0)
				{
					SkeletalMesh->GetLODInfo(LODIndex)->SourceImportFilename = SkeletalMesh->AssetImportData->GetFirstFilename();
				}
			}
			FLODUtilities::SimplifySkeletalMeshLOD(UpdateContext, LODIndex, false);
		}
		else
		{
			FSkeletalMeshLODInfo* LODInfo_RemoveBones = SkeletalMesh->GetLODInfo(LODIndex_RemoveBones);
			if (LODInfo_RemoveBones->BonesToRemove.Num() > 0)
			{
				TArray<FName> BonesToRemove;
				BonesToRemove.Reserve(LODInfo_RemoveBones->BonesToRemove.Num());
				for (const FBoneReference& BoneReference : LODInfo_RemoveBones->BonesToRemove)
				{
					BonesToRemove.Add(BoneReference.BoneName);
				}
				MeshUtilities.RemoveBonesFromMesh(SkeletalMesh, LODIndex_RemoveBones, &BonesToRemove);
			}
		}
	}

	FSkeletalMeshLODModel& LODModelAfterReduction = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];
	
	//Re-apply the clothing using the UserSectionsData, this will ensure we remap correctly the cloth if the reduction has change the number of sections
	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RebuildClothing", "Rebuilding clothing..."));
	FLODUtilities::RestoreClothingFromBackup(SkeletalMesh, ClothingBindings, LODIndex);

	LODModelAfterReduction.SyncronizeUserSectionsDataArray();
	LODModelAfterReduction.NumTexCoords = NumTextCoord;
	LODModelAfterReduction.BuildStringID = BackupBuildStringID;

	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RegenerateDependentLODs", "Regenerate Dependent LODs..."));
	if (bRegenDepLODs)
	{
		//Regenerate dependent LODs
		FLODUtilities::RegenerateDependentLODs(SkeletalMesh, LODIndex);
	}
	return true;
}
