// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshConverters.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Converters/GLTFMeshUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMeshTasks.h"
#include "Rendering/SkeletalMeshRenderData.h"

void FGLTFStaticMeshConverter::Sanitize(const UStaticMesh*& StaticMesh, const UStaticMeshComponent*& StaticMeshComponent, int32& LODIndex, FGLTFMaterialArray& OverrideMaterials)
{
	const uint32 MeshCount = (StaticMeshComponent != nullptr ? 1 : 0) + (StaticMesh != nullptr ? 1 : 0);
	check(MeshCount == 1);

	const TArray<UMaterialInterface*> Materials = StaticMeshComponent != nullptr
		? OverrideMaterials.GetOverrides(StaticMeshComponent->GetMaterials())
		: OverrideMaterials.GetOverrides(StaticMesh->StaticMaterials);

	if (StaticMeshComponent != nullptr)
	{
		// Only use the component if it's needed for baking, since we would
		// otherwise export a copy of this mesh for each mesh-component.
		if (!Builder.ExportOptions->bBakeMaterialInputs ||
			!Builder.ExportOptions->bBakeMaterialInputsUsingMeshData ||
			!FGLTFMaterialUtility::MaterialsNeedVertexData(Materials))
		{
			StaticMesh = StaticMeshComponent->GetStaticMesh();
			StaticMeshComponent = nullptr;
		}
	}

	// Clean up override materials to only contain items that differ from the original materials
	OverrideMaterials = FGLTFMaterialArray(Materials);

	if (StaticMeshComponent != nullptr)
	{
		OverrideMaterials.ClearRedundantOverrides(StaticMeshComponent->GetMaterials());
	}
	else
	{
		OverrideMaterials.ClearRedundantOverrides(StaticMesh->StaticMaterials);
	}

	const UStaticMesh* Mesh = StaticMeshComponent != nullptr ? StaticMeshComponent->GetStaticMesh() : StaticMesh;

	if (LODIndex < 0)
	{
		LODIndex = FMath::Max(Builder.ExportOptions->DefaultLevelOfDetail, FGLTFMeshUtility::GetMinimumLOD(Mesh));
	}

	LODIndex = FMath::Clamp(LODIndex, 0, Mesh->GetNumLODs() - 1);
}

FGLTFJsonMeshIndex FGLTFStaticMeshConverter::Convert(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, int32 LODIndex, FGLTFMaterialArray Materials)
{
	const FGLTFJsonMeshIndex MeshIndex = Builder.AddMesh();
	Builder.SetupTask<FGLTFStaticMeshTask>(Builder, MeshSectionConverter, StaticMesh, StaticMeshComponent, LODIndex, Materials, MeshIndex);
	return MeshIndex;
}

void FGLTFSkeletalMeshConverter::Sanitize(const USkeletalMesh*& SkeletalMesh, const USkeletalMeshComponent*& SkeletalMeshComponent, int32& LODIndex, FGLTFMaterialArray& OverrideMaterials)
{
	const uint32 MeshCount = (SkeletalMeshComponent != nullptr ? 1 : 0) + (SkeletalMesh != nullptr ? 1 : 0);
	check(MeshCount == 1);

	const TArray<UMaterialInterface*> Materials = SkeletalMeshComponent != nullptr
		? OverrideMaterials.GetOverrides(SkeletalMeshComponent->GetMaterials())
		: OverrideMaterials.GetOverrides(SkeletalMesh->Materials);

	if (SkeletalMeshComponent != nullptr)
	{
		// Only use the component if it's needed for baking, since we would
		// otherwise export a copy of this mesh for each mesh-component.
		if (!Builder.ExportOptions->bBakeMaterialInputs ||
			!Builder.ExportOptions->bBakeMaterialInputsUsingMeshData ||
			!FGLTFMaterialUtility::MaterialsNeedVertexData(Materials))
		{
			SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
			SkeletalMeshComponent = nullptr;
		}
	}

	// Clean up override materials to only contain items that differ from the original materials
	OverrideMaterials = FGLTFMaterialArray(Materials);

	if (SkeletalMeshComponent != nullptr)
	{
		OverrideMaterials.ClearRedundantOverrides(SkeletalMeshComponent->GetMaterials());
	}
	else
	{
		OverrideMaterials.ClearRedundantOverrides(SkeletalMesh->Materials);
	}

	const USkeletalMesh* Mesh = SkeletalMeshComponent != nullptr ? SkeletalMeshComponent->SkeletalMesh : SkeletalMesh;

	if (LODIndex < 0)
	{
		LODIndex = FMath::Max(Builder.ExportOptions->DefaultLevelOfDetail, FGLTFMeshUtility::GetMinimumLOD(Mesh));
	}

	const FSkeletalMeshRenderData* RenderData = Mesh->GetResourceForRendering();
	LODIndex = FMath::Clamp(LODIndex, 0, RenderData->LODRenderData.Num() - 1);
}

FGLTFJsonMeshIndex FGLTFSkeletalMeshConverter::Convert(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, int32 LODIndex, FGLTFMaterialArray Materials)
{
	const FGLTFJsonMeshIndex MeshIndex = Builder.AddMesh();
	Builder.SetupTask<FGLTFSkeletalMeshTask>(Builder, MeshSectionConverter, SkeletalMesh, SkeletalMeshComponent, LODIndex, Materials, MeshIndex);
	return MeshIndex;
}
