// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshData.h"
#include "Converters/GLTFNameUtility.h"
#include "StaticMeshAttributes.h"
#include "Developer/MeshMergeUtilities/Private/MeshMergeHelpers.h"

FGLTFMeshData::FGLTFMeshData(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex)
{
	FStaticMeshAttributes(Description).Register();

	if (StaticMeshComponent != nullptr)
	{
		FMeshMergeHelpers::RetrieveMesh(StaticMeshComponent, LODIndex, Description, true);
		Name = FGLTFNameUtility::GetName(StaticMeshComponent);
	}
	else
	{
		FMeshMergeHelpers::RetrieveMesh(StaticMesh, LODIndex, Description);
		StaticMesh->GetName(Name);
	}
}

FGLTFMeshData::FGLTFMeshData(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex)
{
	FStaticMeshAttributes(Description).Register();

	if (SkeletalMeshComponent != nullptr)
	{
		FMeshMergeHelpers::RetrieveMesh(const_cast<USkeletalMeshComponent*>(SkeletalMeshComponent), LODIndex, Description, true);
		Name = FGLTFNameUtility::GetName(SkeletalMeshComponent);
	}
	else
	{
		// TODO: add support for skeletal meshes by implementing custom utilities for retrieving mesh & sections
		SkeletalMesh->GetName(Name);
	}
}
