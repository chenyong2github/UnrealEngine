// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SkeletalMeshAdapter.h"
#include "MeshMergeHelpers.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "UObject/Package.h"
#include "Rendering/SkeletalMeshModel.h"

FSkeletalMeshComponentAdapter::FSkeletalMeshComponentAdapter(USkeletalMeshComponent* InSkeletalMeshComponent)
	: SkeletalMeshComponent(InSkeletalMeshComponent), SkeletalMesh(InSkeletalMeshComponent->SkeletalMesh)
{
	checkf(SkeletalMesh != nullptr, TEXT("Invalid skeletal mesh in adapter"));
	NumLODs = SkeletalMesh->GetLODNum();
}

int32 FSkeletalMeshComponentAdapter::GetNumberOfLODs() const
{
	return NumLODs;
}

void FSkeletalMeshComponentAdapter::RetrieveRawMeshData(int32 LODIndex, FMeshDescription& InOutRawMesh, bool bPropogateMeshData) const
{
	FMeshMergeHelpers::RetrieveMesh(SkeletalMeshComponent, LODIndex, InOutRawMesh, bPropogateMeshData);
}

void FSkeletalMeshComponentAdapter::RetrieveMeshSections(int32 LODIndex, TArray<FSectionInfo>& InOutSectionInfo) const
{
	FMeshMergeHelpers::ExtractSections(SkeletalMeshComponent, LODIndex, InOutSectionInfo);
}

int32 FSkeletalMeshComponentAdapter::GetMaterialIndex(int32 LODIndex, int32 SectionIndex) const
{
	const FSkeletalMeshLODInfo* LODInfoPtr = SkeletalMesh->GetLODInfo(LODIndex);
	if (LODInfoPtr && LODInfoPtr->LODMaterialMap.IsValidIndex(SectionIndex) && LODInfoPtr->LODMaterialMap[SectionIndex] != INDEX_NONE)
	{
		return LODInfoPtr->LODMaterialMap[SectionIndex];
	}
	return SkeletalMesh->GetImportedModel()->LODModels[LODIndex].Sections[SectionIndex].MaterialIndex;
}

UPackage* FSkeletalMeshComponentAdapter::GetOuter() const
{
	return nullptr;
}

FString FSkeletalMeshComponentAdapter::GetBaseName() const
{
	return SkeletalMesh->GetOutermost()->GetName();
}

void FSkeletalMeshComponentAdapter::SetMaterial(int32 MaterialIndex, UMaterialInterface* Material)
{
	//Use the material name has the slot name and imported slot name
	//TODO: find a way to pass the original Material names MaterialSlotName and ImportedMaterialSlotName
	SkeletalMesh->Materials[MaterialIndex] = FSkeletalMaterial(Material, true, false, Material->GetFName(), Material->GetFName());
}

void FSkeletalMeshComponentAdapter::RemapMaterialIndex(int32 LODIndex, int32 SectionIndex, int32 NewMaterialIndex)
{
	FSkeletalMeshLODInfo* LODInfoPtr = SkeletalMesh->GetLODInfo(LODIndex);
	check(LODInfoPtr);
	if (SkeletalMesh->GetImportedModel()->LODModels[LODIndex].Sections[SectionIndex].MaterialIndex == NewMaterialIndex)
	{
		if (LODInfoPtr->LODMaterialMap.IsValidIndex(SectionIndex))
		{
			LODInfoPtr->LODMaterialMap[SectionIndex] = INDEX_NONE;
		}
	}
	
	while (!LODInfoPtr->LODMaterialMap.IsValidIndex(SectionIndex))
	{
		LODInfoPtr->LODMaterialMap.Add(INDEX_NONE);
	}
	LODInfoPtr->LODMaterialMap[SectionIndex] = NewMaterialIndex;
}

int32 FSkeletalMeshComponentAdapter::AddMaterial(UMaterialInterface* Material)
{
	return SkeletalMesh->Materials.Add(Material);
}

void FSkeletalMeshComponentAdapter::UpdateUVChannelData()
{
	SkeletalMesh->UpdateUVChannelData(false);
}

bool FSkeletalMeshComponentAdapter::IsAsset() const
{
	return true;
}

int32 FSkeletalMeshComponentAdapter::LightmapUVIndex() const
{
	return INDEX_NONE;
}

FBoxSphereBounds FSkeletalMeshComponentAdapter::GetBounds() const
{
	return SkeletalMesh->GetBounds();
}
