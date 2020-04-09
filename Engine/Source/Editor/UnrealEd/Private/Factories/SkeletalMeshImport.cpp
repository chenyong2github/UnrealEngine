// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
	SkeletalMeshImport.cpp: Skeletal mesh import code.
=============================================================================*/

#include "CoreMinimal.h"
#include "Misc/MessageDialog.h"
#include "Misc/FeedbackContext.h"
#include "Modules/ModuleManager.h"
#include "UObject/ObjectMacros.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "Materials/MaterialInterface.h"
#include "GPUSkinPublicDefs.h"
#include "ReferenceSkeleton.h"
#include "Engine/SkeletalMesh.h"
#include "EditorFramework/ThumbnailInfo.h"
#include "SkelImport.h"
#include "RawIndexBuffer.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Logging/TokenizedMessage.h"
#include "FbxImporter.h"
#include "Misc/FbxErrors.h"
#include "Engine/SkeletalMeshSocket.h"
#include "LODUtilities.h"
#include "UObject/Package.h"
#include "MeshUtilities.h"
#include "ClothingAssetBase.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "IMeshReductionManagerModule.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Engine/AssetUserData.h"
#include "UObject/MetaData.h"

DEFINE_LOG_CATEGORY_STATIC(LogSkeletalMeshImport, Log, All);

#define LOCTEXT_NAMESPACE "SkeletalMeshImport"

/** Check that root bone is the same, and that any bones that are common have the correct parent. */
bool SkeletonsAreCompatible(const FReferenceSkeleton& NewSkel, const FReferenceSkeleton& ExistSkel, bool bFailNoError)
{
	if (NewSkel.GetBoneName(0) != ExistSkel.GetBoneName(0))
	{
		if (!bFailNoError)
		{
			UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
			FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("MeshHasDifferentRoot", "Root Bone is '{0}' instead of '{1}'.\nDiscarding existing LODs."),
				FText::FromName(NewSkel.GetBoneName(0)), FText::FromName(ExistSkel.GetBoneName(0)))), FFbxErrors::SkeletalMesh_DifferentRoots);
		}
		return false;
	}

	for (int32 i = 1; i < NewSkel.GetRawBoneNum(); i++)
	{
		// See if bone is in both skeletons.
		int32 NewBoneIndex = i;
		FName NewBoneName = NewSkel.GetBoneName(NewBoneIndex);
		int32 BBoneIndex = ExistSkel.FindBoneIndex(NewBoneName);

		// If it is, check parents are the same.
		if (BBoneIndex != INDEX_NONE)
		{
			FName NewParentName = NewSkel.GetBoneName(NewSkel.GetParentIndex(NewBoneIndex));
			FName ExistParentName = ExistSkel.GetBoneName(ExistSkel.GetParentIndex(BBoneIndex));

			if (NewParentName != ExistParentName)
			{
				if (!bFailNoError)
				{
					UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
					FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("MeshHasDifferentRoot", "Root Bone is '{0}' instead of '{1}'.\nDiscarding existing LODs."),
						FText::FromName(NewBoneName), FText::FromName(NewParentName))), FFbxErrors::SkeletalMesh_DifferentRoots);
				}
				return false;
			}
		}
	}

	return true;
}

/**
* Process and fill in the mesh Materials using the raw binary import data
*
* @param Materials - [out] array of materials to update
* @param ImportData - raw binary import data to process
*/
void ProcessImportMeshMaterials(TArray<FSkeletalMaterial>& Materials, FSkeletalMeshImportData& ImportData)
{
	TArray <SkeletalMeshImportData::FMaterial>&	ImportedMaterials = ImportData.Materials;

	// If direct linkup of materials is requested, try to find them here - to get a texture name from a 
	// material name, cut off anything in front of the dot (beyond are special flags).
	Materials.Empty();
	int32 SkinOffset = INDEX_NONE;
	for (int32 MatIndex = 0; MatIndex < ImportedMaterials.Num(); ++MatIndex)
	{
		const SkeletalMeshImportData::FMaterial& ImportedMaterial = ImportedMaterials[MatIndex];

		UMaterialInterface* Material = NULL;
		FString MaterialNameNoSkin = ImportedMaterial.MaterialImportName;
		if (ImportedMaterial.Material.IsValid())
		{
			Material = ImportedMaterial.Material.Get();
		}
		else
		{
			const FString& MaterialName = ImportedMaterial.MaterialImportName;
			MaterialNameNoSkin = MaterialName;
			Material = FindObject<UMaterialInterface>(ANY_PACKAGE, *MaterialName);
			if (Material == nullptr)
			{
				SkinOffset = MaterialName.Find(TEXT("_skin"), ESearchCase::IgnoreCase, ESearchDir::FromEnd);
				if (SkinOffset != INDEX_NONE)
				{
					FString SkinXXNumber = MaterialName.Right(MaterialName.Len() - (SkinOffset + 1)).RightChop(4);
					if (SkinXXNumber.IsNumeric())
					{
						MaterialNameNoSkin = MaterialName.LeftChop(MaterialName.Len() - SkinOffset);
						Material = FindObject<UMaterialInterface>(ANY_PACKAGE, *MaterialNameNoSkin);
					}
				}
			}
		}

		const bool bEnableShadowCasting = true;
		Materials.Add(FSkeletalMaterial(Material, bEnableShadowCasting, false, Material != nullptr ? Material->GetFName() : FName(*MaterialNameNoSkin), FName(*(ImportedMaterial.MaterialImportName))));
	}

	int32 NumMaterialsToAdd = FMath::Max<int32>(ImportedMaterials.Num(), ImportData.MaxMaterialIndex + 1);

	// Pad the material pointers
	while (NumMaterialsToAdd > Materials.Num())
	{
		Materials.Add(FSkeletalMaterial(NULL, true, false, NAME_None, NAME_None));
	}
}

/**
* Process and fill in the mesh ref skeleton bone hierarchy using the raw binary import data
*
* @param RefSkeleton - [out] reference skeleton hierarchy to update
* @param SkeletalDepth - [out] depth of the reference skeleton hierarchy
* @param ImportData - raw binary import data to process
* @return true if the operation completed successfully
*/
bool ProcessImportMeshSkeleton(const USkeleton* SkeletonAsset, FReferenceSkeleton& RefSkeleton, int32& SkeletalDepth, FSkeletalMeshImportData& ImportData)
{
	TArray <SkeletalMeshImportData::FBone>&	RefBonesBinary = ImportData.RefBonesBinary;

	// Setup skeletal hierarchy + names structure.
	RefSkeleton.Empty();

	FReferenceSkeletonModifier RefSkelModifier(RefSkeleton, SkeletonAsset);

	// Digest bones to the serializable format.
	for (int32 b = 0; b < RefBonesBinary.Num(); b++)
	{
		const SkeletalMeshImportData::FBone & BinaryBone = RefBonesBinary[b];
		const FString BoneName = FSkeletalMeshImportData::FixupBoneName(BinaryBone.Name);
		const FMeshBoneInfo BoneInfo(FName(*BoneName, FNAME_Add), BinaryBone.Name, BinaryBone.ParentIndex);
		const FTransform BoneTransform(BinaryBone.BonePos.Transform);

		if (RefSkeleton.FindRawBoneIndex(BoneInfo.Name) != INDEX_NONE)
		{
			UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
			FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Error, FText::Format(LOCTEXT("SkeletonHasDuplicateBones", "Skeleton has non-unique bone names.\nBone named '{0}' encountered more than once."), FText::FromName(BoneInfo.Name))), FFbxErrors::SkeletalMesh_DuplicateBones);
			return false;
		}

		RefSkelModifier.Add(BoneInfo, BoneTransform);
	}

	// Add hierarchy index to each bone and detect max depth.
	SkeletalDepth = 0;

	TArray<int32> SkeletalDepths;
	SkeletalDepths.Empty(RefBonesBinary.Num());
	SkeletalDepths.AddZeroed(RefBonesBinary.Num());
	for (int32 b = 0; b < RefSkeleton.GetRawBoneNum(); b++)
	{
		int32 Parent = RefSkeleton.GetRawParentIndex(b);
		int32 Depth = 1.0f;

		SkeletalDepths[b] = 1.0f;
		if (Parent != INDEX_NONE)
		{
			Depth += SkeletalDepths[Parent];
		}
		if (SkeletalDepth < Depth)
		{
			SkeletalDepth = Depth;
		}
		SkeletalDepths[b] = Depth;
	}

	return true;
}

/**
* Process and update the vertex Influences using the raw binary import data
*
* @param ImportData - raw binary import data to process
*/
void ProcessImportMeshInfluences(FSkeletalMeshImportData& ImportData)
{
	FLODUtilities::ProcessImportMeshInfluences(ImportData.Wedges.Num(), ImportData.Influences);
}

bool SkeletalMeshIsUsingMaterialSlotNameWorkflow(UAssetImportData* AssetImportData)
{
	UFbxSkeletalMeshImportData* ImportData = Cast<UFbxSkeletalMeshImportData>(AssetImportData);
	if (ImportData == nullptr || ImportData->ImportMaterialOriginalNameData.Num() <= 0)
	{
		return false;
	}
	bool AllNameAreNone = true;
	for (FName ImportMaterialName : ImportData->ImportMaterialOriginalNameData)
	{
		if (ImportMaterialName != NAME_None)
		{
			AllNameAreNone = false;
			break;
		}
	}
	return !AllNameAreNone;
}

ExistingSkelMeshData* SaveExistingSkelMeshData(USkeletalMesh* ExistingSkelMesh, bool bSaveMaterials, int32 ReimportLODIndex)
{
	ExistingSkelMeshData* ExistingMeshDataPtr = nullptr;

	if (!ExistingSkelMesh)
	{
		return nullptr;
	}

	bool ReimportSpecificLOD = (ReimportLODIndex > 0) && ExistingSkelMesh->GetLODNum() > ReimportLODIndex;
	int32 SafeReimportLODIndex = ReimportLODIndex < 0 ? 0 : ReimportLODIndex;

	ExistingMeshDataPtr = new ExistingSkelMeshData();

	//Save the package UMetaData
	ExistingMeshDataPtr->ExistingUMetaDataTagValues = UMetaData::GetMapForObject(ExistingSkelMesh);

	ExistingMeshDataPtr->UseMaterialNameSlotWorkflow = SkeletalMeshIsUsingMaterialSlotNameWorkflow(ExistingSkelMesh->AssetImportData);
	ExistingMeshDataPtr->MinLOD = ExistingSkelMesh->MinLod;
	ExistingMeshDataPtr->DisableBelowMinLodStripping = ExistingSkelMesh->DisableBelowMinLodStripping;
	ExistingMeshDataPtr->bOverrideLODStreamingSettings = ExistingSkelMesh->bOverrideLODStreamingSettings;
	ExistingMeshDataPtr->bSupportLODStreaming = ExistingSkelMesh->bSupportLODStreaming;
	ExistingMeshDataPtr->MaxNumStreamedLODs = ExistingSkelMesh->MaxNumStreamedLODs;
	ExistingMeshDataPtr->MaxNumOptionalLODs = ExistingSkelMesh->MaxNumOptionalLODs;

	FSkeletalMeshModel* ImportedResource = ExistingSkelMesh->GetImportedModel();

	//Add the existing Material slot name data
	for (int32 MaterialIndex = 0; MaterialIndex < ExistingSkelMesh->Materials.Num(); ++MaterialIndex)
	{
		ExistingMeshDataPtr->ExistingImportMaterialOriginalNameData.Add(ExistingSkelMesh->Materials[MaterialIndex].ImportedMaterialSlotName);
	}

	for (int32 LodIndex = 0; LodIndex < ImportedResource->LODModels.Num(); ++LodIndex)
	{
		FSkeletalMeshLODModel OriginalLODModel;
		FSkeletalMeshLODModel* BackupLODModel = &(ImportedResource->LODModels[LodIndex]);
		if (LodIndex == SafeReimportLODIndex && (ImportedResource->OriginalReductionSourceMeshData.IsValidIndex(SafeReimportLODIndex) && !ImportedResource->OriginalReductionSourceMeshData[SafeReimportLODIndex]->IsEmpty()))
		{
			FSkeletalMeshLODInfo* LODInfo = ExistingSkelMesh->GetLODInfo(SafeReimportLODIndex);
			TMap<FString, TArray<FMorphTargetDelta>> TempLODMorphTargetData;
			//Get the before reduce LODModel, this lod model contain all the possible sections
			ImportedResource->OriginalReductionSourceMeshData[SafeReimportLODIndex]->LoadReductionData(OriginalLODModel, TempLODMorphTargetData, ExistingSkelMesh);
			//If there was section that was remove by the reduction (Disabled in the original data, zero triangle after reduction, GenerateUpTo settings...),
			//we have to use the original section data and apply the section data that was modified after the reduction
			if (OriginalLODModel.Sections.Num() > BackupLODModel->Sections.Num())
			{
				TArray<bool> OriginalMatched;
				OriginalMatched.AddZeroed(OriginalLODModel.Sections.Num());
				//Now apply the after reduce settings change, but we need to match the section since there can be reduced one
				for (int32 ReduceSectionIndex = 0; ReduceSectionIndex < BackupLODModel->Sections.Num(); ++ReduceSectionIndex)
				{
					const FSkelMeshSection& ReduceSection = BackupLODModel->Sections[ReduceSectionIndex];
					for (int32 OriginalSectionIndex = 0; OriginalSectionIndex < OriginalLODModel.Sections.Num(); ++OriginalSectionIndex)
					{
						if (OriginalMatched[OriginalSectionIndex])
						{
							continue;
						}
						FSkelMeshSection& OriginalSection = OriginalLODModel.Sections[OriginalSectionIndex];
						if ((OriginalSection.bDisabled) || (OriginalSection.GenerateUpToLodIndex != INDEX_NONE && OriginalSection.GenerateUpToLodIndex < SafeReimportLODIndex))
						{
							continue;
						}

						if (ReduceSection.MaterialIndex == OriginalSection.MaterialIndex)
						{
							OriginalMatched[OriginalSectionIndex] = true;
							OriginalSection.bDisabled = ReduceSection.bDisabled;
							OriginalSection.bCastShadow = ReduceSection.bCastShadow;
							OriginalSection.bRecomputeTangent = ReduceSection.bRecomputeTangent;
							OriginalSection.GenerateUpToLodIndex = ReduceSection.GenerateUpToLodIndex;
							break;
						}
					}
				}
				//Set the unmatched original section data using the current UserSectionsData so we keep the user changes
				for (int32 OriginalSectionIndex = 0; OriginalSectionIndex < OriginalLODModel.Sections.Num(); ++OriginalSectionIndex)
				{
					if (OriginalMatched[OriginalSectionIndex])
					{
						continue;
					}
					FSkelMeshSection& OriginalSection = OriginalLODModel.Sections[OriginalSectionIndex];
					if (FSkelMeshSourceSectionUserData* ReduceUserSectionData = BackupLODModel->UserSectionsData.Find(OriginalSection.OriginalDataSectionIndex))
					{
						OriginalSection.bDisabled = ReduceUserSectionData->bDisabled;
						OriginalSection.bCastShadow = ReduceUserSectionData->bCastShadow;
						OriginalSection.bRecomputeTangent = ReduceUserSectionData->bRecomputeTangent;
						OriginalSection.GenerateUpToLodIndex = ReduceUserSectionData->GenerateUpToLodIndex;
					}
				}
				//Use the OriginalLODModel
				BackupLODModel = &OriginalLODModel;
			}
		}
		ExistingMeshDataPtr->ExistingImportMeshLodSectionMaterialData.AddZeroed();
		check(ExistingMeshDataPtr->ExistingImportMeshLodSectionMaterialData.IsValidIndex(LodIndex));

		for (int32 SectionIndex = 0; SectionIndex < BackupLODModel->Sections.Num(); ++SectionIndex)
		{
			int32 SectionMaterialIndex = BackupLODModel->Sections[SectionIndex].MaterialIndex;
			bool SectionCastShadow = BackupLODModel->Sections[SectionIndex].bCastShadow;
			bool SectionRecomputeTangents = BackupLODModel->Sections[SectionIndex].bRecomputeTangent;
			int32 GenerateUpTo = BackupLODModel->Sections[SectionIndex].GenerateUpToLodIndex;
			bool bDisabled = BackupLODModel->Sections[SectionIndex].bDisabled;
			bool bBoneChunkedSection = BackupLODModel->Sections[SectionIndex].ChunkedParentSectionIndex != INDEX_NONE;
			//Save all the sections, even the chunked sections
			if (ExistingMeshDataPtr->ExistingImportMaterialOriginalNameData.IsValidIndex(SectionMaterialIndex))
			{
				ExistingMeshDataPtr->ExistingImportMeshLodSectionMaterialData[LodIndex].Add(ExistingMeshLodSectionData(ExistingMeshDataPtr->ExistingImportMaterialOriginalNameData[SectionMaterialIndex], SectionCastShadow, SectionRecomputeTangents, GenerateUpTo, bDisabled));
			}
		}
	}

	ExistingMeshDataPtr->ExistingSockets = ExistingSkelMesh->GetMeshOnlySocketList();
	ExistingMeshDataPtr->bSaveRestoreMaterials = bSaveMaterials;
	if (ExistingMeshDataPtr->bSaveRestoreMaterials)
	{
		ExistingMeshDataPtr->ExistingMaterials = ExistingSkelMesh->Materials;
	}
	ExistingMeshDataPtr->ExistingRetargetBasePose = ExistingSkelMesh->RetargetBasePose;

	if (ImportedResource->LODModels.Num() > 0 &&
		ExistingSkelMesh->GetLODNum() == ImportedResource->LODModels.Num())
	{
		int32 OffsetReductionLODIndex = 0;
		FSkeletalMeshLODInfo* LODInfo = ExistingSkelMesh->GetLODInfo(SafeReimportLODIndex);
		ExistingMeshDataPtr->bIsReimportLODReduced = (LODInfo && LODInfo->bHasBeenSimplified);
		if (ExistingMeshDataPtr->bIsReimportLODReduced)
		{
			//Save the imported LOD reduction settings
			ExistingMeshDataPtr->ExistingReimportLODReductionSettings = LODInfo->ReductionSettings;
		}
		ExistingMeshDataPtr->ExistingBaseLODInfo = *LODInfo;

		// Remove the zero'th LOD (ie: the LOD being reimported).
		if (!ReimportSpecificLOD)
		{
			ImportedResource->LODModels.RemoveAt(0);
			ExistingSkelMesh->RemoveLODInfo(0);
			OffsetReductionLODIndex = 1;
		}

		// Copy off the remaining LODs.
		ExistingMeshDataPtr->ExistingLODModels.Empty(ImportedResource->LODModels.Num());
		for ( int32 LODModelIndex = 0 ; LODModelIndex < ImportedResource->LODModels.Num() ; ++LODModelIndex )
		{
			FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[LODModelIndex];
			int32 ReductionLODIndex = LODModelIndex + OffsetReductionLODIndex;
			if (ImportedResource->OriginalReductionSourceMeshData.IsValidIndex(ReductionLODIndex) && !ImportedResource->OriginalReductionSourceMeshData[ReductionLODIndex]->IsEmpty())
			{
				FSkeletalMeshLODModel BaseLODModel;
				TMap<FString, TArray<FMorphTargetDelta>> BaseLODMorphTargetData;
				ImportedResource->OriginalReductionSourceMeshData[ReductionLODIndex]->LoadReductionData(BaseLODModel, BaseLODMorphTargetData, ExistingSkelMesh);
				FReductionBaseSkeletalMeshBulkData* ReductionLODData = new FReductionBaseSkeletalMeshBulkData();
				ReductionLODData->SaveReductionData(BaseLODModel, BaseLODMorphTargetData, ExistingSkelMesh);
				//Add necessary empty slot
				while (ExistingMeshDataPtr->ExistingOriginalReductionSourceMeshData.Num() < LODModelIndex)
				{
					FReductionBaseSkeletalMeshBulkData* EmptyReductionLODData = new FReductionBaseSkeletalMeshBulkData();
					ExistingMeshDataPtr->ExistingOriginalReductionSourceMeshData.Add(EmptyReductionLODData);
				}
				ExistingMeshDataPtr->ExistingOriginalReductionSourceMeshData.Add(ReductionLODData);
			}
			//Add a new LOD Model to the existing LODModels data
			ExistingMeshDataPtr->ExistingLODModels.Add(FSkeletalMeshLODModel::CreateCopy(&LODModel));
		}
		check(ExistingMeshDataPtr->ExistingLODModels.Num() == ImportedResource->LODModels.Num());

		ExistingMeshDataPtr->ExistingLODInfo = ExistingSkelMesh->GetLODInfoArray();
		ExistingMeshDataPtr->ExistingRefSkeleton = ExistingSkelMesh->RefSkeleton;

	}

	// First asset should be the one that the skeletal mesh should point too
	ExistingMeshDataPtr->ExistingPhysicsAssets.Empty();
	ExistingMeshDataPtr->ExistingPhysicsAssets.Add(ExistingSkelMesh->PhysicsAsset);
	for (TObjectIterator<UPhysicsAsset> It; It; ++It)
	{
		UPhysicsAsset* PhysicsAsset = *It;
		if (PhysicsAsset->PreviewSkeletalMesh == ExistingSkelMesh && ExistingSkelMesh->PhysicsAsset != PhysicsAsset)
		{
			ExistingMeshDataPtr->ExistingPhysicsAssets.Add(PhysicsAsset);
		}
	}

	ExistingMeshDataPtr->ExistingShadowPhysicsAsset = ExistingSkelMesh->ShadowPhysicsAsset;

	ExistingMeshDataPtr->ExistingSkeleton = ExistingSkelMesh->Skeleton;
	// since copying back original skeleton, this shoudl be safe to do
	ExistingMeshDataPtr->ExistingPostProcessAnimBlueprint = ExistingSkelMesh->PostProcessAnimBlueprint;

	ExistingMeshDataPtr->ExistingLODSettings = ExistingSkelMesh->LODSettings;

	ExistingSkelMesh->ExportMirrorTable(ExistingMeshDataPtr->ExistingMirrorTable);

	ExistingMeshDataPtr->ExistingMorphTargets.Empty(ExistingSkelMesh->MorphTargets.Num());
	ExistingMeshDataPtr->ExistingMorphTargets.Append(ExistingSkelMesh->MorphTargets);

	ExistingMeshDataPtr->ExistingAssetImportData = ExistingSkelMesh->AssetImportData;
	ExistingMeshDataPtr->ExistingThumbnailInfo = ExistingSkelMesh->ThumbnailInfo;

	ExistingMeshDataPtr->ExistingClothingAssets = ExistingSkelMesh->MeshClothingAssets;

	ExistingMeshDataPtr->ExistingSamplingInfo = ExistingSkelMesh->GetSamplingInfo();

	//Add the last fbx import data
	UFbxSkeletalMeshImportData* ImportData = Cast<UFbxSkeletalMeshImportData>(ExistingSkelMesh->AssetImportData);
	if (ImportData && ExistingMeshDataPtr->UseMaterialNameSlotWorkflow)
	{
		for (int32 ImportMaterialOriginalNameDataIndex = 0; ImportMaterialOriginalNameDataIndex < ImportData->ImportMaterialOriginalNameData.Num(); ++ImportMaterialOriginalNameDataIndex)
		{
			FName MaterialName = ImportData->ImportMaterialOriginalNameData[ImportMaterialOriginalNameDataIndex];
			ExistingMeshDataPtr->LastImportMaterialOriginalNameData.Add(MaterialName);
		}
		for (int32 LodIndex = 0; LodIndex < ImportData->ImportMeshLodData.Num(); ++LodIndex)
		{
			ExistingMeshDataPtr->LastImportMeshLodSectionMaterialData.AddZeroed();
			const FImportMeshLodSectionsData &ImportMeshLodSectionsData = ImportData->ImportMeshLodData[LodIndex];
			for (int32 SectionIndex = 0; SectionIndex < ImportMeshLodSectionsData.SectionOriginalMaterialName.Num(); ++SectionIndex)
			{
				FName MaterialName = ImportMeshLodSectionsData.SectionOriginalMaterialName[SectionIndex];
				ExistingMeshDataPtr->LastImportMeshLodSectionMaterialData[LodIndex].Add(MaterialName);
			}
		}
	}
	//Store the user asset data
	const TArray<UAssetUserData*>* UserData = ExistingSkelMesh->GetAssetUserDataArray();
	if (UserData)
	{
		for (int32 Idx = 0; Idx < UserData->Num(); Idx++)
		{
			if ((*UserData)[Idx] != nullptr)
			{
				UAssetUserData* DupObject = (UAssetUserData*)StaticDuplicateObject((*UserData)[Idx], GetTransientPackage());
				bool bAddDupToRoot = !(DupObject->IsRooted());
				if (bAddDupToRoot)
				{
					DupObject->AddToRoot();
				}
				ExistingMeshDataPtr->ExistingAssetUserData.Add(DupObject, bAddDupToRoot);
			}
		}
	}
	//Store mesh changed delegate data
	ExistingMeshDataPtr->ExistingOnMeshChanged = ExistingSkelMesh->GetOnMeshChanged();

	return ExistingMeshDataPtr;
}

void RestoreDependentLODs(ExistingSkelMeshData* MeshData, USkeletalMesh* SkeletalMesh)
{
	check(SkeletalMesh != nullptr);
	int32 TotalLOD = MeshData->ExistingLODModels.Num();
	FSkeletalMeshModel* SkeletalMeshImportedModel = SkeletalMesh->GetImportedModel();

	for (int32 Index = 0; Index < TotalLOD; ++Index)
	{
		int32 LODIndex = Index + 1;
		if (LODIndex >= SkeletalMesh->GetLODInfoArray().Num())
		{
			FSkeletalMeshLODInfo& ExistLODInfo = MeshData->ExistingLODInfo[Index];
			FSkeletalMeshLODModel& ExistLODModel = MeshData->ExistingLODModels[Index];
			// reset material maps, it won't work anyway. 
			ExistLODInfo.LODMaterialMap.Empty();

			SkeletalMeshImportedModel->LODModels.Add(FSkeletalMeshLODModel::CreateCopy(&ExistLODModel));
			// add LOD info back
			SkeletalMesh->AddLODInfo(ExistLODInfo);
			check(LODIndex < SkeletalMesh->GetLODInfoArray().Num());
		}
	}
}

namespace SkeletalMeshHelper
{
	void ApplySkinning(USkeletalMesh* SkeletalMesh, FSkeletalMeshLODModel& SrcLODModel, FSkeletalMeshLODModel& DestLODModel)
	{
		TArray<FSoftSkinVertex> SrcVertices;
		SrcLODModel.GetVertices(SrcVertices);

		FBox OldBounds(EForceInit::ForceInit);
		for (int32 SrcIndex = 0; SrcIndex < SrcVertices.Num(); ++SrcIndex)
		{
			const FSoftSkinVertex& SrcVertex = SrcVertices[SrcIndex];
			OldBounds += SrcVertex.Position;
		}

		TWedgeInfoPosOctree SrcWedgePosOctree(OldBounds.GetCenter(), OldBounds.GetExtent().GetMax());
		// Add each old vertex to the octree
		for (int32 SrcIndex = 0; SrcIndex < SrcVertices.Num(); ++SrcIndex)
		{
			FWedgeInfo WedgeInfo;
			WedgeInfo.WedgeIndex = SrcIndex;
			WedgeInfo.Position = SrcVertices[SrcIndex].Position;
			SrcWedgePosOctree.AddElement(WedgeInfo);
		}

		FOctreeQueryHelper OctreeQueryHelper(&SrcWedgePosOctree);

		TArray<FBoneIndexType> RequiredActiveBones;

		bool bUseBone = false;
		for (int32 SectionIndex = 0; SectionIndex < DestLODModel.Sections.Num(); SectionIndex++)
		{
			FSkelMeshSection& Section = DestLODModel.Sections[SectionIndex];
			Section.BoneMap.Reset();
			for (FSoftSkinVertex& DestVertex : Section.SoftVertices)
			{
				//Find the nearest wedges in the src model
				TArray<FWedgeInfo> NearestSrcWedges;
				OctreeQueryHelper.FindNearestWedgeIndexes(DestVertex.Position, NearestSrcWedges);
				if (NearestSrcWedges.Num() < 1)
				{
					//Should we check???
					continue;
				}
				//Find the matching wedges in the src model
				int32 MatchingSrcWedge = INDEX_NONE;
				for (FWedgeInfo& SrcWedgeInfo : NearestSrcWedges)
				{
					int32 SrcIndex = SrcWedgeInfo.WedgeIndex;
					const FSoftSkinVertex& SrcVertex = SrcVertices[SrcIndex];
					if (SrcVertex.Position.Equals(DestVertex.Position, THRESH_POINTS_ARE_SAME) &&
						SrcVertex.UVs[0].Equals(DestVertex.UVs[0], THRESH_UVS_ARE_SAME) &&
						(SrcVertex.TangentX == DestVertex.TangentX) &&
						(SrcVertex.TangentY == DestVertex.TangentY) &&
						(SrcVertex.TangentZ == DestVertex.TangentZ))
					{
						MatchingSrcWedge = SrcIndex;
						break;
					}
				}
				if (MatchingSrcWedge == INDEX_NONE)
				{
					//We have to find the nearest wedges, then find the most similar normal
					float MinDistance = MAX_FLT;
					float MinNormalAngle = MAX_FLT;
					for (FWedgeInfo& SrcWedgeInfo : NearestSrcWedges)
					{
						int32 SrcIndex = SrcWedgeInfo.WedgeIndex;
						const FSoftSkinVertex& SrcVertex = SrcVertices[SrcIndex];
						float VectorDelta = FVector::DistSquared(SrcVertex.Position, DestVertex.Position);
						if (VectorDelta <= (MinDistance + KINDA_SMALL_NUMBER))
						{
							if (VectorDelta < MinDistance - KINDA_SMALL_NUMBER)
							{
								MinDistance = VectorDelta;
								MinNormalAngle = MAX_FLT;
							}
							FVector DestTangentZ = DestVertex.TangentZ;
							DestTangentZ.Normalize();
							FVector SrcTangentZ = SrcVertex.TangentZ;
							SrcTangentZ.Normalize();
							float AngleDiff = FMath::Abs(FMath::Acos(FVector::DotProduct(DestTangentZ, SrcTangentZ)));
							if (AngleDiff < MinNormalAngle)
							{
								MinNormalAngle = AngleDiff;
								MatchingSrcWedge = SrcIndex;
							}
						}
					}
				}
				check(SrcVertices.IsValidIndex(MatchingSrcWedge));
				const FSoftSkinVertex& SrcVertex = SrcVertices[MatchingSrcWedge];

				//Find the src section to assign the correct remapped bone
				int32 SrcSectionIndex = INDEX_NONE;
				int32 SrcSectionWedgeIndex = INDEX_NONE;
				SrcLODModel.GetSectionFromVertexIndex(MatchingSrcWedge, SrcSectionIndex, SrcSectionWedgeIndex);
				check(SrcSectionIndex != INDEX_NONE);

				for (int32 InfluenceIndex = 0; InfluenceIndex < MAX_TOTAL_INFLUENCES; ++InfluenceIndex)
				{
					if (SrcVertex.InfluenceWeights[InfluenceIndex] > 0.0f)
					{
						Section.MaxBoneInfluences = FMath::Max(Section.MaxBoneInfluences, InfluenceIndex + 1);
						//Copy the weight
						DestVertex.InfluenceWeights[InfluenceIndex] = SrcVertex.InfluenceWeights[InfluenceIndex];
						//Copy the bone ID
						FBoneIndexType OriginalBoneIndex = SrcLODModel.Sections[SrcSectionIndex].BoneMap[SrcVertex.InfluenceBones[InfluenceIndex]];
						int32 OverrideIndex;
						if (Section.BoneMap.Find(OriginalBoneIndex, OverrideIndex))
						{
							DestVertex.InfluenceBones[InfluenceIndex] = OverrideIndex;
						}
						else
						{
							DestVertex.InfluenceBones[InfluenceIndex] = Section.BoneMap.Add(OriginalBoneIndex);
							DestLODModel.ActiveBoneIndices.AddUnique(OriginalBoneIndex);
						}
						bUseBone = true;
					}
				}
			}
		}

		if (bUseBone)
		{
			//Set the required/active bones
			DestLODModel.RequiredBones = SrcLODModel.RequiredBones;
			DestLODModel.RequiredBones.Sort();
			SkeletalMesh->RefSkeleton.EnsureParentsExistAndSort(DestLODModel.ActiveBoneIndices);
		}
	}
} //namespace SkeletalMeshHelper

void RestoreExistingSkelMeshData(ExistingSkelMeshData* MeshData, USkeletalMesh* SkeletalMesh, int32 ReimportLODIndex, bool bCanShowDialog, bool bImportSkinningOnly, bool bForceMaterialReset)
{
	if (!MeshData || !SkeletalMesh)
	{
		return;
	}

	//Restore the package metadata
	if (MeshData->ExistingUMetaDataTagValues)
	{
		UMetaData* PackageMetaData = SkeletalMesh->GetOutermost()->GetMetaData();
		checkSlow(PackageMetaData);
		PackageMetaData->SetObjectValues(SkeletalMesh, *MeshData->ExistingUMetaDataTagValues);
	}

	int32 SafeReimportLODIndex = ReimportLODIndex < 0 ? 0 : ReimportLODIndex;
	SkeletalMesh->MinLod = MeshData->MinLOD;
	SkeletalMesh->DisableBelowMinLodStripping = MeshData->DisableBelowMinLodStripping;
	SkeletalMesh->bOverrideLODStreamingSettings = MeshData->bOverrideLODStreamingSettings;
	SkeletalMesh->bSupportLODStreaming = MeshData->bSupportLODStreaming;
	SkeletalMesh->MaxNumStreamedLODs = MeshData->MaxNumStreamedLODs;
	SkeletalMesh->MaxNumOptionalLODs = MeshData->MaxNumOptionalLODs;

	FSkeletalMeshModel* SkeletalMeshImportedModel = SkeletalMesh->GetImportedModel();

	//Create a remap material Index use to find the matching section later
	TArray<int32> RemapMaterial;
	RemapMaterial.AddZeroed(SkeletalMesh->Materials.Num());
	TArray<FName> RemapMaterialName;
	RemapMaterialName.AddZeroed(SkeletalMesh->Materials.Num());

	bool bMaterialReset = false;
	if (MeshData->bSaveRestoreMaterials)
	{
		UnFbx::EFBXReimportDialogReturnOption ReturnOption;
		//Ask the user to match the materials conflict
		UnFbx::FFbxImporter::PrepareAndShowMaterialConflictDialog<FSkeletalMaterial>(MeshData->ExistingMaterials, SkeletalMesh->Materials, RemapMaterial, RemapMaterialName, bCanShowDialog, false, bForceMaterialReset, ReturnOption);

		if (ReturnOption != UnFbx::EFBXReimportDialogReturnOption::FBXRDRO_ResetToFbx)
		{
			//Build a ordered material list that try to keep intact the existing material list
			TArray<FSkeletalMaterial> MaterialOrdered;
			TArray<bool> MatchedNewMaterial;
			MatchedNewMaterial.AddZeroed(SkeletalMesh->Materials.Num());
			for (int32 ExistMaterialIndex = 0; ExistMaterialIndex < MeshData->ExistingMaterials.Num(); ++ExistMaterialIndex)
			{
				int32 MaterialIndexOrdered = MaterialOrdered.Add(MeshData->ExistingMaterials[ExistMaterialIndex]);
				FSkeletalMaterial& OrderedMaterial = MaterialOrdered[MaterialIndexOrdered];
				int32 NewMaterialIndex = INDEX_NONE;
				if (RemapMaterial.Find(ExistMaterialIndex, NewMaterialIndex))
				{
					MatchedNewMaterial[NewMaterialIndex] = true;
					RemapMaterial[NewMaterialIndex] = MaterialIndexOrdered;
					OrderedMaterial.ImportedMaterialSlotName = SkeletalMesh->Materials[NewMaterialIndex].ImportedMaterialSlotName;
				}
				else
				{
					//Unmatched material must be conserve
				}
			}

			//Add the new material entries (the one that do not match with any existing material)
			for (int32 NewMaterialIndex = 0; NewMaterialIndex < MatchedNewMaterial.Num(); ++NewMaterialIndex)
			{
				if (MatchedNewMaterial[NewMaterialIndex] == false)
				{
					int32 NewMeshIndex = MaterialOrdered.Add(SkeletalMesh->Materials[NewMaterialIndex]);
					RemapMaterial[NewMaterialIndex] = NewMeshIndex;
				}
			}

			//Set the RemapMaterialName array helper
			for (int32 MaterialIndex = 0; MaterialIndex < RemapMaterial.Num(); ++MaterialIndex)
			{
				int32 SourceMaterialMatch = RemapMaterial[MaterialIndex];
				if (MeshData->ExistingMaterials.IsValidIndex(SourceMaterialMatch))
				{
					RemapMaterialName[MaterialIndex] = MeshData->ExistingMaterials[SourceMaterialMatch].ImportedMaterialSlotName;
				}
			}

			//Copy the re ordered materials (this ensure the material array do not change when we re-import)
			SkeletalMesh->Materials = MaterialOrdered;
		}
		else
		{
			bMaterialReset = true;
		}
	}

	SkeletalMesh->LODSettings = MeshData->ExistingLODSettings;
	// ensure LOD 0 contains correct setting 
	if (SkeletalMesh->LODSettings && SkeletalMesh->GetLODInfoArray().Num() > 0)
	{
		SkeletalMesh->LODSettings->SetLODSettingsToMesh(SkeletalMesh, 0);
	}

	//Do everything we need for base LOD re-import
	if (SafeReimportLODIndex < 1)
	{
		// this is not ideal. Ideally we'll have to save only diff with indicating which joints, 
		// but for now, we allow them to keep the previous pose IF the element count is same
		if (MeshData->ExistingRetargetBasePose.Num() == SkeletalMesh->RefSkeleton.GetRawBoneNum())
		{
			SkeletalMesh->RetargetBasePose = MeshData->ExistingRetargetBasePose;
		}

		// Assign sockets from old version of this SkeletalMesh.
		// Only copy ones for bones that exist in the new mesh.
		for (int32 i = 0; i < MeshData->ExistingSockets.Num(); i++)
		{
			const int32 BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(MeshData->ExistingSockets[i]->BoneName);
			if (BoneIndex != INDEX_NONE)
			{
				SkeletalMesh->GetMeshOnlySocketList().Add(MeshData->ExistingSockets[i]);
			}
		}

		// We copy back and fix-up the LODs that still work with this skeleton.
		if (MeshData->ExistingLODModels.Num() > 0)
		{
			auto RestoreReductionSourceData = [&SkeletalMesh, &SkeletalMeshImportedModel, &MeshData](int32 ExistingIndex, int32 NewIndex)
			{
				if (!MeshData->ExistingOriginalReductionSourceMeshData.IsValidIndex(ExistingIndex) || MeshData->ExistingOriginalReductionSourceMeshData[ExistingIndex]->IsEmpty())
				{
					return;
				}
				//Restore the original reduction source mesh data
				FSkeletalMeshLODModel BaseLODModel;
				TMap<FString, TArray<FMorphTargetDelta>> BaseLODMorphTargetData;
				MeshData->ExistingOriginalReductionSourceMeshData[ExistingIndex]->LoadReductionData(BaseLODModel, BaseLODMorphTargetData, SkeletalMesh);
				FReductionBaseSkeletalMeshBulkData* ReductionLODData = new FReductionBaseSkeletalMeshBulkData();
				ReductionLODData->SaveReductionData(BaseLODModel, BaseLODMorphTargetData, SkeletalMesh);
				//Add necessary empty slot
				while (SkeletalMeshImportedModel->OriginalReductionSourceMeshData.Num() < NewIndex)
				{
					FReductionBaseSkeletalMeshBulkData* EmptyReductionLODData = new FReductionBaseSkeletalMeshBulkData();
					SkeletalMeshImportedModel->OriginalReductionSourceMeshData.Add(EmptyReductionLODData);
				}
				SkeletalMeshImportedModel->OriginalReductionSourceMeshData.Add(ReductionLODData);
			};

			if (SkeletonsAreCompatible(SkeletalMesh->RefSkeleton, MeshData->ExistingRefSkeleton, bImportSkinningOnly))
			{
				// First create mapping table from old skeleton to new skeleton.
				TArray<int32> OldToNewMap;
				OldToNewMap.AddUninitialized(MeshData->ExistingRefSkeleton.GetRawBoneNum());
				for (int32 i = 0; i < MeshData->ExistingRefSkeleton.GetRawBoneNum(); i++)
				{
					OldToNewMap[i] = SkeletalMesh->RefSkeleton.FindBoneIndex(MeshData->ExistingRefSkeleton.GetBoneName(i));
				}

				for (int32 i = 0; i < MeshData->ExistingLODModels.Num(); i++)
				{
					FSkeletalMeshLODModel& LODModel = MeshData->ExistingLODModels[i];
					FSkeletalMeshLODInfo& LODInfo = MeshData->ExistingLODInfo[i];

					// Fix ActiveBoneIndices array.
					bool bMissingBone = false;
					FName MissingBoneName = NAME_None;
					for (int32 j = 0; j < LODModel.ActiveBoneIndices.Num() && !bMissingBone; j++)
					{
						int32 OldActiveBoneIndex = LODModel.ActiveBoneIndices[j];
						if (OldToNewMap.IsValidIndex(OldActiveBoneIndex))
						{
							int32 NewBoneIndex = OldToNewMap[OldActiveBoneIndex];
							if (NewBoneIndex == INDEX_NONE)
							{
								bMissingBone = true;
								MissingBoneName = MeshData->ExistingRefSkeleton.GetBoneName(LODModel.ActiveBoneIndices[j]);
							}
							else
							{
								LODModel.ActiveBoneIndices[j] = NewBoneIndex;
							}
						}
						else
						{
							LODModel.ActiveBoneIndices.RemoveAt(j, 1, false);
							--j;
						}
					}

					// Fix RequiredBones array.
					for (int32 j = 0; j < LODModel.RequiredBones.Num() && !bMissingBone; j++)
					{
						const int32 OldBoneIndex = LODModel.RequiredBones[j];

						if (OldToNewMap.IsValidIndex(OldBoneIndex))	//Previously virtual bones could end up in this array
																	// Must validate against this
						{
							const int32 NewBoneIndex = OldToNewMap[OldBoneIndex];
							if (NewBoneIndex == INDEX_NONE)
							{
								bMissingBone = true;
								MissingBoneName = MeshData->ExistingRefSkeleton.GetBoneName(OldBoneIndex);
							}
							else
							{
								LODModel.RequiredBones[j] = NewBoneIndex;
							}
						}
						else
						{
							//Bone didn't exist in our required bones, clean up. 
							LODModel.RequiredBones.RemoveAt(j, 1, false);
							--j;
						}
					}

					// Sort ascending for parent child relationship
					LODModel.RequiredBones.Sort();
					SkeletalMesh->RefSkeleton.EnsureParentsExistAndSort(LODModel.ActiveBoneIndices);

					// Fix the sections' BoneMaps.
					for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); SectionIndex++)
					{
						FSkelMeshSection& Section = LODModel.Sections[SectionIndex];
						for (int32 BoneIndex = 0; BoneIndex < Section.BoneMap.Num(); BoneIndex++)
						{
							int32 NewBoneIndex = OldToNewMap[Section.BoneMap[BoneIndex]];
							if (NewBoneIndex == INDEX_NONE)
							{
								bMissingBone = true;
								MissingBoneName = MeshData->ExistingRefSkeleton.GetBoneName(Section.BoneMap[BoneIndex]);
								break;
							}
							else
							{
								Section.BoneMap[BoneIndex] = NewBoneIndex;
							}
						}
						if (bMissingBone)
						{
							break;
						}
					}

					if (bMissingBone)
					{
						UnFbx::FFbxImporter* FFbxImporter = UnFbx::FFbxImporter::GetInstance();
						FFbxImporter->AddTokenizedErrorMessage(FTokenizedMessage::Create(EMessageSeverity::Warning, FText::Format(LOCTEXT("NewMeshMissingBoneFromLOD", "New mesh is missing bone '{0}' required by an LOD."), FText::FromName(MissingBoneName))), FFbxErrors::SkeletalMesh_LOD_MissingBone);
						break;
					}
					else
					{
						//We need to add LODInfo
						SkeletalMeshImportedModel->LODModels.Add(FSkeletalMeshLODModel::CreateCopy(&LODModel));
						SkeletalMesh->AddLODInfo(LODInfo);
						RestoreReductionSourceData(i, SkeletalMesh->GetLODNum() - 1);
					}
				}
			}
			//We just need to restore the LOD model and LOD info the build should regenerate the LODs
			RestoreDependentLODs(MeshData, SkeletalMesh);
			
			
			//Old asset cannot use the new build system, we need to regenerate dependent LODs
			if (SkeletalMesh->IsLODImportedDataBuildAvailable(SafeReimportLODIndex) == false)
			{
				FLODUtilities::RegenerateDependentLODs(SkeletalMesh, SafeReimportLODIndex);
			}
		}

		for (int32 AssetIndex = 0; AssetIndex < MeshData->ExistingPhysicsAssets.Num(); ++AssetIndex)
		{
			UPhysicsAsset* PhysicsAsset = MeshData->ExistingPhysicsAssets[AssetIndex];
			if (AssetIndex == 0)
			{
				// First asset is the one that the skeletal mesh should point too
				SkeletalMesh->PhysicsAsset = PhysicsAsset;
			}
			// No need to mark as modified here, because the asset hasn't actually changed
			if (PhysicsAsset)
			{
				PhysicsAsset->PreviewSkeletalMesh = SkeletalMesh;
			}
		}

		SkeletalMesh->ShadowPhysicsAsset = MeshData->ExistingShadowPhysicsAsset;

		SkeletalMesh->Skeleton = MeshData->ExistingSkeleton;
		SkeletalMesh->PostProcessAnimBlueprint = MeshData->ExistingPostProcessAnimBlueprint;

		// Copy mirror table.
		SkeletalMesh->ImportMirrorTable(MeshData->ExistingMirrorTable);

		SkeletalMesh->MorphTargets.Empty(MeshData->ExistingMorphTargets.Num());
		SkeletalMesh->MorphTargets.Append(MeshData->ExistingMorphTargets);
		SkeletalMesh->InitMorphTargets();

		SkeletalMesh->AssetImportData = MeshData->ExistingAssetImportData.Get();
		SkeletalMesh->ThumbnailInfo = MeshData->ExistingThumbnailInfo.Get();

		SkeletalMesh->MeshClothingAssets = MeshData->ExistingClothingAssets;

		for (UClothingAssetBase* ClothingAsset : SkeletalMesh->MeshClothingAssets)
		{
			if (ClothingAsset)
				ClothingAsset->RefreshBoneMapping(SkeletalMesh);
		}

		SkeletalMesh->SetSamplingInfo(MeshData->ExistingSamplingInfo);
	}

	//Restore the section change only for the reimport LOD, other LOD are not affected since the material array can only grow.
	if (MeshData->UseMaterialNameSlotWorkflow)
	{
		//Restore the base LOD materialMap the LODs LODMaterialMap are restore differently
		if (SafeReimportLODIndex < 1 && SkeletalMesh->GetLODInfoArray().IsValidIndex(SafeReimportLODIndex))
		{
			FSkeletalMeshLODInfo& BaseLODInfo = SkeletalMesh->GetLODInfoArray()[SafeReimportLODIndex];
			if (bMaterialReset)
			{
				//If we reset the material array there is no point keeping the user changes
				BaseLODInfo.LODMaterialMap.Empty();
			}
			else if (SkeletalMesh->GetImportedModel() && SkeletalMesh->GetImportedModel()->LODModels.IsValidIndex(SafeReimportLODIndex))
			{
				//Restore the Base MaterialMap
				FSkeletalMeshLODModel& LODModel = SkeletalMesh->GetImportedModel()->LODModels[SafeReimportLODIndex];
				for (int32 SectionIndex = 0; SectionIndex < LODModel.Sections.Num(); ++SectionIndex)
				{
					int32 MaterialIndex = LODModel.Sections[SectionIndex].MaterialIndex;
					if (MeshData->ExistingBaseLODInfo.LODMaterialMap.IsValidIndex(SectionIndex))
					{
						int32 ExistingLODMaterialIndex = MeshData->ExistingBaseLODInfo.LODMaterialMap[SectionIndex];
						while (BaseLODInfo.LODMaterialMap.Num() <= SectionIndex)
						{
							BaseLODInfo.LODMaterialMap.Add(INDEX_NONE);
						}
						BaseLODInfo.LODMaterialMap[SectionIndex] = ExistingLODMaterialIndex;
					}
				}
			}
		}
		FSkeletalMeshLODModel &NewSkelMeshLodModel = SkeletalMeshImportedModel->LODModels[SafeReimportLODIndex];
		
		const bool bIsValidSavedSectionMaterialData = MeshData->ExistingImportMeshLodSectionMaterialData.IsValidIndex(SafeReimportLODIndex) && MeshData->LastImportMeshLodSectionMaterialData.IsValidIndex(SafeReimportLODIndex);

		const int32 MaxExistSectionNumber = bIsValidSavedSectionMaterialData ? FMath::Max(MeshData->ExistingImportMeshLodSectionMaterialData[SafeReimportLODIndex].Num(), MeshData->LastImportMeshLodSectionMaterialData[SafeReimportLODIndex].Num()) : 0;
		TBitArray<> MatchedExistSectionIndex;
		MatchedExistSectionIndex.Init(false, MaxExistSectionNumber);
		//Restore the section changes from the old import data
		for (int32 SectionIndex = 0; SectionIndex < NewSkelMeshLodModel.Sections.Num(); SectionIndex++)
		{
			//Find the import section material index by using the RemapMaterial array. Fallback on the imported index if the remap entry is not valid
			FSkelMeshSection& NewSection = NewSkelMeshLodModel.Sections[SectionIndex];
			int32 RemapMaterialIndex = RemapMaterial.IsValidIndex(NewSection.MaterialIndex) ? RemapMaterial[NewSection.MaterialIndex] : NewSection.MaterialIndex;
			if (!SkeletalMesh->Materials.IsValidIndex(RemapMaterialIndex))
			{
				//We have an invalid material section, in this case we set the material index to 0
				NewSection.MaterialIndex = 0;
				UE_LOG(LogSkeletalMeshImport, Display, TEXT("Reimport material match issue: Invalid RemapMaterialIndex [%d], will make it point to material index [0]"), RemapMaterialIndex);
				continue;
			}
			NewSection.MaterialIndex = RemapMaterialIndex;
			
			//skip the rest of the loop if we do not have valid saved data
			if (!bIsValidSavedSectionMaterialData)
			{
				continue;
			}
			//Get the RemapMaterial section Imported material slot name. We need it to match the saved existing section, so we can put back the saved existing section data
			FName CurrentSectionImportedMaterialName = SkeletalMesh->Materials[RemapMaterialIndex].ImportedMaterialSlotName;
			for (int32 ExistSectionIndex = 0; ExistSectionIndex < MaxExistSectionNumber; ++ExistSectionIndex)
			{
				//Skip already matched exist section
				if (MatchedExistSectionIndex[ExistSectionIndex])
				{
					continue;
				}
				//Verify we have valid existing section data, if not break from the loop higher index wont be valid
				if (!MeshData->LastImportMeshLodSectionMaterialData[SafeReimportLODIndex].IsValidIndex(ExistSectionIndex) || !MeshData->ExistingImportMeshLodSectionMaterialData[SafeReimportLODIndex].IsValidIndex(ExistSectionIndex))
				{
					break;
				}

				//Get the Last imported skelmesh section slot import name
				FName OriginalImportMeshSectionSlotName = MeshData->LastImportMeshLodSectionMaterialData[SafeReimportLODIndex][ExistSectionIndex];
				if (OriginalImportMeshSectionSlotName != CurrentSectionImportedMaterialName)
				{
					//Skip until we found a match between the last import
					continue;
				}

				//We have a match put back the data
				NewSection.bCastShadow = MeshData->ExistingImportMeshLodSectionMaterialData[SafeReimportLODIndex][ExistSectionIndex].bCastShadow;
				NewSection.bRecomputeTangent = MeshData->ExistingImportMeshLodSectionMaterialData[SafeReimportLODIndex][ExistSectionIndex].bRecomputeTangents;
				NewSection.GenerateUpToLodIndex = MeshData->ExistingImportMeshLodSectionMaterialData[SafeReimportLODIndex][ExistSectionIndex].GenerateUpTo;
				NewSection.bDisabled = MeshData->ExistingImportMeshLodSectionMaterialData[SafeReimportLODIndex][ExistSectionIndex].bDisabled;
				bool bBoneChunkedSection = NewSection.ChunkedParentSectionIndex >= 0;
				int32 ParentOriginalSectionIndex = NewSection.OriginalDataSectionIndex;
				if (!bBoneChunkedSection)
				{
					//Set the new Parent Index
					FSkelMeshSourceSectionUserData& UserSectionData = NewSkelMeshLodModel.UserSectionsData.FindOrAdd(ParentOriginalSectionIndex);
					UserSectionData.bDisabled = NewSection.bDisabled;
					UserSectionData.bCastShadow = NewSection.bCastShadow;
					UserSectionData.bRecomputeTangent = NewSection.bRecomputeTangent;
					UserSectionData.GenerateUpToLodIndex = NewSection.GenerateUpToLodIndex;
					//The cloth will be rebind later after the reimport is done
				}
				//Set the matched section to true to avoid using it again
				MatchedExistSectionIndex[ExistSectionIndex] = true;

				//find the corresponding current slot name in the skeletal mesh materials list to remap properly the material index, in case the user have change it before re-importing
				FName ExistMeshSectionSlotName = MeshData->ExistingImportMeshLodSectionMaterialData[SafeReimportLODIndex][ExistSectionIndex].ImportedMaterialSlotName;
				{
					for (int32 SkelMeshMaterialIndex = 0; SkelMeshMaterialIndex < SkeletalMesh->Materials.Num(); ++SkelMeshMaterialIndex)
					{
						const FSkeletalMaterial &NewSectionMaterial = SkeletalMesh->Materials[SkelMeshMaterialIndex];
						if (NewSectionMaterial.ImportedMaterialSlotName == ExistMeshSectionSlotName)
						{
							if (ExistMeshSectionSlotName != OriginalImportMeshSectionSlotName)
							{
								NewSection.MaterialIndex = SkelMeshMaterialIndex;
							}
							break;
						}
					}
				}
				//Break because we found a match and have restore the data for this SectionIndex
				break;
			}
		}
		//Make sure we reset the User section array to only what we have in the fbx
		NewSkelMeshLodModel.SyncronizeUserSectionsDataArray(true);
	}

	//Copy back the reimport LOD specific data
	if (SkeletalMesh->GetLODInfoArray().IsValidIndex(SafeReimportLODIndex))
	{
		FSkeletalMeshLODInfo& BaseLODInfo = SkeletalMesh->GetLODInfoArray()[SafeReimportLODIndex];
		//Restore the build setting first
		BaseLODInfo.BuildSettings = MeshData->ExistingBaseLODInfo.BuildSettings;
		if (MeshData->bIsReimportLODReduced)
		{
			//Restore the reimport LOD reduction settings
			BaseLODInfo.ReductionSettings = MeshData->ExistingBaseLODInfo.ReductionSettings;
		}
	}

	// Copy user data to newly created mesh
	for (auto Kvp : MeshData->ExistingAssetUserData)
	{
		UAssetUserData* UserDataObject = Kvp.Key;
		if (Kvp.Value)
		{
			//if the duplicated temporary UObject was add to root, we must remove it from the root
			UserDataObject->RemoveFromRoot();
		}
		UserDataObject->Rename(nullptr, SkeletalMesh, REN_DontCreateRedirectors | REN_DoNotDirty);
		SkeletalMesh->AddAssetUserData(UserDataObject);
	}

	if (!bImportSkinningOnly && !MeshData->bIsReimportLODReduced)
	{
		if (SkeletalMeshImportedModel->OriginalReductionSourceMeshData.IsValidIndex(SafeReimportLODIndex))
		{
			SkeletalMeshImportedModel->OriginalReductionSourceMeshData[SafeReimportLODIndex]->EmptyBulkData();
		}
	}

	//Copy mesh changed delegate data
	SkeletalMesh->GetOnMeshChanged() = MeshData->ExistingOnMeshChanged;
}
#undef LOCTEXT_NAMESPACE
