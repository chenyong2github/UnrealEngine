// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Converters/GLTFMeshUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMaterialTasks.h"

void FGLTFMaterialConverter::Sanitize(const UMaterialInterface*& Material, const UObject*& MeshOrComponent, int32& LODIndex, FGLTFMaterialArray& OverrideMaterials)
{
	const UStaticMesh* StaticMesh = Cast<UStaticMesh>(MeshOrComponent);
	const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(MeshOrComponent);

	if (MeshOrComponent != nullptr && StaticMesh == nullptr && SkeletalMesh == nullptr)
	{
		if (const UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(MeshOrComponent))
		{
			StaticMesh = StaticMeshComponent->GetStaticMesh();
		}
		else if (const USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(MeshOrComponent))
		{
			SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
		}
	}

	if ((StaticMesh != nullptr || SkeletalMesh != nullptr) &&
		Builder.ExportOptions->bBakeMaterialInputs &&
		Builder.ExportOptions->bBakeMaterialInputsUsingMeshData &&
		FGLTFMaterialUtility::MaterialNeedsVertexData(Material))
	{
		if (LODIndex < 0)
		{
			if (StaticMesh != nullptr)
			{
				LODIndex = FMath::Max(Builder.ExportOptions->DefaultLevelOfDetail, FGLTFMeshUtility::GetMinimumLOD(StaticMesh));
			}
			else if (SkeletalMesh != nullptr)
			{
				LODIndex = FMath::Max(Builder.ExportOptions->DefaultLevelOfDetail, FGLTFMeshUtility::GetMinimumLOD(SkeletalMesh));
			}
		}
	}
	else
	{
		MeshOrComponent = nullptr;
		LODIndex = -1;
		OverrideMaterials = {};
	}
}

FGLTFJsonMaterialIndex FGLTFMaterialConverter::Convert(const UMaterialInterface* Material, const UObject* MeshOrComponent, int32 LODIndex, const FGLTFMaterialArray OverrideMaterials)
{
	if (Material == FGLTFMaterialUtility::GetDefault())
	{
		return FGLTFJsonMaterialIndex(INDEX_NONE); // use default gltf definition
	}

	const FGLTFJsonMaterialIndex MaterialIndex = Builder.AddMaterial();
	Builder.SetupTask<FGLTFMaterialTask>(Builder, Material, MeshOrComponent, LODIndex, OverrideMaterials, MaterialIndex);
	return MaterialIndex;
}
