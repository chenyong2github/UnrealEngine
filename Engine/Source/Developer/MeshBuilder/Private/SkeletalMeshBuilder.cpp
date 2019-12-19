// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshBuilder.h"
#include "Modules/ModuleManager.h"
#include "MeshBoneReduction.h"
#include "Engine/EngineTypes.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/BodySetup.h"
#include "MeshDescription.h"
#include "MeshDescriptionOperations.h"
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

//TODO: move that in a public header and use it everywhere we call FSkeletalMeshImportData::CopyLODImportData
//or simply add parameter to FSkeletalMeshImportData::CopyLODImportData to do the job inside the function.
namespace SkeletalMeshBuilderHelperNS
{
	void FixFaceMaterial(USkeletalMesh* SkeletalMesh, TArray<SkeletalMeshImportData::FMaterial>& RawMeshMaterials, TArray<SkeletalMeshImportData::FMeshFace>& LODFaces)
	{
		//Fix the material for the faces
		TArray<int32> MaterialRemap;
		MaterialRemap.Reserve(RawMeshMaterials.Num());
		for (int32 MaterialIndex = 0; MaterialIndex < RawMeshMaterials.Num(); ++MaterialIndex)
		{
			MaterialRemap.Add(MaterialIndex);
			FName MaterialImportName = *(RawMeshMaterials[MaterialIndex].MaterialImportName);
			for (int32 MeshMaterialIndex = 0; MeshMaterialIndex < SkeletalMesh->Materials.Num(); ++MeshMaterialIndex)
			{
				FName MeshMaterialName = SkeletalMesh->Materials[MeshMaterialIndex].ImportedMaterialSlotName;
				if (MaterialImportName == MeshMaterialName)
				{
					MaterialRemap[MaterialIndex] = MeshMaterialIndex;
					break;
				}
			}
		}
		//Update all the faces
		for (int32 FaceIndex = 0; FaceIndex < LODFaces.Num(); ++FaceIndex)
		{
			if (MaterialRemap.IsValidIndex(LODFaces[FaceIndex].MeshMaterialIndex))
			{
				LODFaces[FaceIndex].MeshMaterialIndex = MaterialRemap[LODFaces[FaceIndex].MeshMaterialIndex];
			}
		}
	}
}

FSkeletalMeshBuilder::FSkeletalMeshBuilder()
{

}


bool FSkeletalMeshBuilder::Build(USkeletalMesh* SkeletalMesh, const int32 LODIndex, const bool bRegenDepLODs)
{
	check(SkeletalMesh->GetImportedModel());
	check(SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(LODIndex));
	check(SkeletalMesh->GetLODInfo(LODIndex) != nullptr);
	
	FSkeletalMeshLODInfo* LODInfo = SkeletalMesh->GetLODInfo(LODIndex);
	//We want to backup in case the LODModel is regenerated, this data is use to validate in the UI if the ddc must be rebuild
	const FString BackupBuildStringID = SkeletalMesh->GetImportedModel()->LODModels[LODIndex].BuildStringID;

	const FReferenceSkeleton& RefSkeleton = SkeletalMesh->RefSkeleton;

	FScopedSlowTask SlowTask(6.0f, NSLOCTEXT("SkeltalMeshBuilder", "BuildingSkeletalMeshLOD", "Building skeletal mesh LOD"));
	SlowTask.MakeDialog();

	//Prevent any PostEdit change during the build
	FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkeletalMesh, false, false);
	// Unbind any existing clothing assets before we reimport the geometry
	TArray<ClothingAssetUtils::FClothingAssetMeshBinding> ClothingBindings;
	FLODUtilities::UnbindClothingAndBackup(SkeletalMesh, ClothingBindings, LODIndex);

	FSkeletalMeshImportData SkeletalMeshImportData;
	float ProgressStepSize = SkeletalMesh->IsReductionActive(LODIndex) ? 0.5f : 1.0f;

	//This scope define where we can use the LODModel, after a reduction the LODModel must be requery since it is a new instance
	{
		FSkeletalMeshLODModel& BuildLODModel = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];
		
		BuildLODModel.RawSkeletalMeshBulkData.LoadRawMesh(SkeletalMeshImportData);

		TArray<FVector> LODPoints;
		TArray<SkeletalMeshImportData::FMeshWedge> LODWedges;
		TArray<SkeletalMeshImportData::FMeshFace> LODFaces;
		TArray<SkeletalMeshImportData::FVertInfluence> LODInfluences;
		TArray<int32> LODPointToRawMap;
		SkeletalMeshImportData.CopyLODImportData(LODPoints, LODWedges, LODFaces, LODInfluences, LODPointToRawMap);

		SkeletalMeshBuilderHelperNS::FixFaceMaterial(SkeletalMesh, SkeletalMeshImportData.Materials, LODFaces);

		//Build the skeletalmesh using mesh utilities module
		IMeshUtilities::MeshBuildOptions Options;
		Options.FillOptions(LODInfo->BuildSettings);
		IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");

		// Create skinning streams for NewModel.
		SlowTask.EnterProgressFrame(1.0f);
		MeshUtilities.BuildSkeletalMesh(
			BuildLODModel,
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
		BuildLODModel.NumTexCoords = SkeletalMeshImportData.NumTexCoords;

		//Re-apply the morph target
		SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RebuildMorphTarget", "Rebuilding morph targets..."));
		if (SkeletalMeshImportData.MorphTargetNames.Num() > 0)
		{
			FLODUtilities::BuildMorphTargets(SkeletalMesh, SkeletalMeshImportData, LODIndex, !Options.bComputeNormals, !Options.bComputeTangents, Options.bUseMikkTSpace);
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
			SlowTask.EnterProgressFrame(ProgressStepSize, NSLOCTEXT("SkeltalMeshBuilder", "RegenerateLOD", "Regenerate LOD..."));
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
			if (LODInfo->BonesToRemove.Num() > 0)
			{
				TArray<FName> BonesToRemove;
				BonesToRemove.Reserve(LODInfo->BonesToRemove.Num());
				for (const FBoneReference& BoneReference : LODInfo->BonesToRemove)
				{
					BonesToRemove.Add(BoneReference.BoneName);
				}
				MeshUtilities.RemoveBonesFromMesh(SkeletalMesh, LODIndex, &BonesToRemove);
			}
		}
	}

	FSkeletalMeshLODModel& LODModelAfterReduction = SkeletalMesh->GetImportedModel()->LODModels[LODIndex];
	
	//Re-apply the clothing using the UserSectionsData, this will ensure we remap correctly the cloth if the reduction has change the number of sections
	SlowTask.EnterProgressFrame(1.0f, NSLOCTEXT("SkeltalMeshBuilder", "RebuildClothing", "Rebuilding clothing..."));
	FLODUtilities::RestoreClothingFromBackup(SkeletalMesh, ClothingBindings, LODIndex);

	LODModelAfterReduction.SyncronizeUserSectionsDataArray();
	LODModelAfterReduction.NumTexCoords = SkeletalMeshImportData.NumTexCoords;
	LODModelAfterReduction.BuildStringID = BackupBuildStringID;

	SlowTask.EnterProgressFrame(ProgressStepSize, NSLOCTEXT("SkeltalMeshBuilder", "RegenerateDependentLODs", "Regenerate Dependent LODs..."));
	if (bRegenDepLODs)
	{
		//Regenerate dependent LODs
		FLODUtilities::RegenerateDependentLODs(SkeletalMesh, LODIndex);
	}

	SlowTask.EnterProgressFrame();

	return true;
}
