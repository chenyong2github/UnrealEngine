// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetUtils/StaticMeshMaterialUtil.h"

#include "Engine/StaticMesh.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"

using namespace UE::AssetUtils;


bool UE::AssetUtils::GetStaticMeshLODAssetMaterials(
	UStaticMesh* StaticMeshAsset,
	int32 LODIndex,
	FStaticMeshLODMaterialSetInfo& MaterialInfoOut)
{
	if (!StaticMeshAsset)
	{
		return false;
	}

#if WITH_EDITOR

	if (StaticMeshAsset->IsSourceModelValid(LODIndex) == false)
	{
		return false;
	}

	TArray<FStaticMaterial> StaticMaterials = StaticMeshAsset->GetStaticMaterials();
	MaterialInfoOut.MaterialSlots.Reset();
	for (FStaticMaterial Mat : StaticMaterials)
	{
		MaterialInfoOut.MaterialSlots.Add( FStaticMeshMaterialSlot{ Mat.MaterialInterface, Mat.MaterialSlotName } );
	}

	const FMeshSectionInfoMap& SectionInfoMap = StaticMeshAsset->GetSectionInfoMap();
	MaterialInfoOut.LODIndex = LODIndex;
	MaterialInfoOut.NumSections = SectionInfoMap.GetSectionNumber(LODIndex);

	MaterialInfoOut.SectionSlotIndexes.SetNum(MaterialInfoOut.NumSections);
	MaterialInfoOut.SectionMaterials.SetNum(MaterialInfoOut.NumSections);
	for (int32 k = 0; k < MaterialInfoOut.NumSections; ++k)
	{
		FMeshSectionInfo SectionInfo = SectionInfoMap.Get(LODIndex, k);
		if (SectionInfo.MaterialIndex >= 0 && SectionInfo.MaterialIndex < MaterialInfoOut.MaterialSlots.Num())
		{
			MaterialInfoOut.SectionSlotIndexes[k] = SectionInfo.MaterialIndex;
			MaterialInfoOut.SectionMaterials[k] = MaterialInfoOut.MaterialSlots[SectionInfo.MaterialIndex].Material;
		}
		else
		{
			ensure(false);		// this is *not* supposed to be able to happen! SectionMap is broken...
			MaterialInfoOut.SectionSlotIndexes[k] = -1;
			MaterialInfoOut.SectionMaterials[k] = nullptr;
		}
	}

	return true;

#else
	// TODO: how would we handle this for runtime static mesh?
	return false;
#endif

}



bool UE::AssetUtils::GetStaticMeshLODMaterialListBySection(
	UStaticMesh* StaticMeshAsset,
	int32 LODIndex,
	TArray<UMaterialInterface*>& MaterialListOut,
	TArray<int32>& MaterialIndexOut)
{
#if WITH_EDITOR
	// need valid MeshDescription in Editor path
	if (StaticMeshAsset->IsMeshDescriptionValid(LODIndex) == false)
	{
		return false;
	}
#endif

	FStaticMeshLODMaterialSetInfo MaterialSetInfo;
	if (GetStaticMeshLODAssetMaterials(StaticMeshAsset, LODIndex, MaterialSetInfo) == false)
	{
		return false;
	}

#if WITH_EDITOR

	const FMeshDescription* SourceMesh = StaticMeshAsset->GetMeshDescription(LODIndex);

	// # Sections == # PolygonGroups
	int32 NumPolygonGroups = SourceMesh->PolygonGroups().Num();

	MaterialListOut.Reset();
	MaterialIndexOut.Reset();
	for (int32 k = 0; k < NumPolygonGroups; ++k)
	{
		int32 UseSlotIndex = -1;
		if (k < MaterialSetInfo.SectionSlotIndexes.Num())
		{
			UseSlotIndex = MaterialSetInfo.SectionSlotIndexes[k];
		}
		else if (MaterialSetInfo.SectionSlotIndexes.Num() > 0)
		{
			UseSlotIndex = 0;
		}

		if (UseSlotIndex >= 0)
		{
			MaterialIndexOut.Add(UseSlotIndex);
			MaterialListOut.Add(MaterialSetInfo.MaterialSlots[UseSlotIndex].Material);
		}
		else
		{
			MaterialIndexOut.Add(-1);
			MaterialListOut.Add(nullptr);
		}
	}

	return true;

#else
	// TODO: how would we handle this for runtime static mesh?
	return false;
#endif


}



FName UE::AssetUtils::GenerateNewMaterialSlotName(
	const TArray<FStaticMaterial>& ExistingMaterials,
	UMaterialInterface* SlotMaterial,
	int32 NewSlotIndex)
{
	FString MaterialName = (SlotMaterial) ? SlotMaterial->GetName() : TEXT("Material");
	FName BaseName(MaterialName);

	bool bFound = false;
	for (const FStaticMaterial& Mat : ExistingMaterials)
	{
		if (Mat.MaterialSlotName == BaseName || Mat.ImportedMaterialSlotName == BaseName)
		{
			bFound = true;
			break;
		}
	}
	if (bFound == false && SlotMaterial != nullptr)
	{
		return BaseName;
	}

	bFound = true;
	while (bFound)
	{
		bFound = false;

		BaseName = FName(FString::Printf(TEXT("%s_%d"), *MaterialName, NewSlotIndex++));
		for (const FStaticMaterial& Mat : ExistingMaterials)
		{
			if (Mat.MaterialSlotName == BaseName || Mat.ImportedMaterialSlotName == BaseName)
			{
				bFound = true;
				break;
			}
		}
	}

	return BaseName;
}