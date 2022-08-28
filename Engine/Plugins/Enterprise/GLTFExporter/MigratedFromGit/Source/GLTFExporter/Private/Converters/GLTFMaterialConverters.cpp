// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMaterialConverters.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Converters/GLTFMeshUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMaterialTasks.h"

void FGLTFMaterialConverter::Sanitize(const UMaterialInterface*& Material, const UObject*& MeshOrComponent, FGLTFMaterialArray& OverrideMaterials)
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

	if (StaticMesh == nullptr && SkeletalMesh == nullptr ||
		!Builder.ExportOptions->bBakeMaterialInputs ||
		!Builder.ExportOptions->bBakeMaterialInputsUsingMeshData ||
		!FGLTFMaterialUtility::MaterialNeedsVertexData(Material))
	{
		MeshOrComponent = nullptr;
		OverrideMaterials = {};
	}
}

FGLTFJsonMaterialIndex FGLTFMaterialConverter::Convert(const UMaterialInterface* Material, const UObject* MeshOrComponent, const FGLTFMaterialArray OverrideMaterials)
{
	if (Material == FGLTFMaterialUtility::GetDefault())
	{
		return FGLTFJsonMaterialIndex(INDEX_NONE); // use default gltf definition
	}

	const int32 LODIndex = FGLTFMeshUtility::GetLOD(MeshOrComponent, Builder.ExportOptions->DefaultLevelOfDetail);

	const FGLTFJsonMaterialIndex MaterialIndex = Builder.AddMaterial();
	Builder.SetupTask<FGLTFMaterialTask>(Builder, Material, MeshOrComponent, LODIndex, OverrideMaterials, MaterialIndex);
	return MaterialIndex;
}
