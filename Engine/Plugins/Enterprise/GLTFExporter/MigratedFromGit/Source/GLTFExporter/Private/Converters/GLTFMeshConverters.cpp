// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshConverters.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMeshTasks.h"

void FGLTFStaticMeshConverter::Sanitize(const UStaticMesh*& StaticMesh, const UStaticMeshComponent*& StaticMeshComponent, FGLTFMaterialArray& OverrideMaterials)
{
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
}

FGLTFJsonMeshIndex FGLTFStaticMeshConverter::Convert(const UStaticMesh* StaticMesh, const UStaticMeshComponent* StaticMeshComponent, FGLTFMaterialArray OverrideMaterials)
{
	const FGLTFJsonMeshIndex MeshIndex = Builder.AddMesh();
	Builder.SetupTask<FGLTFStaticMeshTask>(Builder, MeshSectionConverter, MeshDataConverter, StaticMesh, StaticMeshComponent, OverrideMaterials, MeshIndex);
	return MeshIndex;
}

void FGLTFSkeletalMeshConverter::Sanitize(const USkeletalMesh*& SkeletalMesh, const USkeletalMeshComponent*& SkeletalMeshComponent, FGLTFMaterialArray& OverrideMaterials)
{
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
}

FGLTFJsonMeshIndex FGLTFSkeletalMeshConverter::Convert(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, FGLTFMaterialArray OverrideMaterials)
{
	const FGLTFJsonMeshIndex MeshIndex = Builder.AddMesh();
	Builder.SetupTask<FGLTFSkeletalMeshTask>(Builder, MeshSectionConverter, MeshDataConverter, SkeletalMesh, SkeletalMeshComponent, OverrideMaterials, MeshIndex);
	return MeshIndex;
}
