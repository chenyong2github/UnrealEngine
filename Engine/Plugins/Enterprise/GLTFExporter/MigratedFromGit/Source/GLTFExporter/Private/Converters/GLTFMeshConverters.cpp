// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshConverters.h"
#include "Converters/GLTFMeshUtility.h"
#include "Converters/GLTFMaterialUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMeshTasks.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"

void FGLTFStaticMeshConverter::Sanitize(const UStaticMesh*& StaticMesh, const UStaticMeshComponent*& StaticMeshComponent, FGLTFMaterialArray& Materials, int32& LODIndex)
{
	FGLTFMeshUtility::ResolveMaterials(Materials, StaticMeshComponent, StaticMesh);

	LODIndex = Builder.SanitizeLOD(StaticMesh, StaticMeshComponent, LODIndex);

	if (StaticMeshComponent != nullptr)
	{
		const bool bUsesMeshData = Builder.ExportOptions->BakeMaterialInputs == EGLTFMaterialBakeMode::UseMeshData &&
			FGLTFMaterialUtility::NeedsMeshData(Materials); // TODO: if this expensive, cache the results for each material

		const bool bIsReferencedByVariant = Builder.GetObjectVariants(StaticMeshComponent) != nullptr;

		// Only use the component if it's needed for baking or variants, since we would
		// otherwise export a copy of this mesh for each mesh-component.
		if (!bUsesMeshData && !bIsReferencedByVariant)
		{
			StaticMeshComponent = nullptr;
		}
	}
}

FGLTFJsonMeshIndex FGLTFStaticMeshConverter::Convert(const UStaticMesh* StaticMesh,  const UStaticMeshComponent* StaticMeshComponent, FGLTFMaterialArray Materials, int32 LODIndex)
{
#if !WITH_EDITOR
	if (!StaticMesh->bAllowCPUAccess)
	{
		Builder.LogSuggestion(FString::Printf(
			TEXT("Export of mesh %s can in runtime be speed-up by checking 'Allow CPU Access' in asset settings"),
			*StaticMesh->GetName()));
	}
#endif

	FGLTFJsonMesh JsonMesh;
	const int32 MaterialCount = FGLTFMeshUtility::GetMaterials(StaticMesh).Num();
	JsonMesh.Primitives.AddDefaulted(MaterialCount);

	const FGLTFJsonMeshIndex MeshIndex = Builder.AddMesh(JsonMesh);
	Builder.SetupTask<FGLTFStaticMeshTask>(Builder, MeshSectionConverter, StaticMesh, StaticMeshComponent, Materials, LODIndex, MeshIndex);
	return MeshIndex;
}

void FGLTFSkeletalMeshConverter::Sanitize(const USkeletalMesh*& SkeletalMesh, const USkeletalMeshComponent*& SkeletalMeshComponent, FGLTFMaterialArray& Materials, int32& LODIndex)
{
	FGLTFMeshUtility::ResolveMaterials(Materials, SkeletalMeshComponent, SkeletalMesh);

	LODIndex = Builder.SanitizeLOD(SkeletalMesh, SkeletalMeshComponent, LODIndex);

	if (SkeletalMeshComponent != nullptr)
	{
		const bool bUsesMeshData = Builder.ExportOptions->BakeMaterialInputs == EGLTFMaterialBakeMode::UseMeshData &&
			FGLTFMaterialUtility::NeedsMeshData(Materials); // TODO: if this expensive, cache the results for each material

		const bool bIsReferencedByVariant = Builder.GetObjectVariants(SkeletalMeshComponent) != nullptr;

		// Only use the component if it's needed for baking or variants, since we would
		// otherwise export a copy of this mesh for each mesh-component.
		if (!bUsesMeshData && !bIsReferencedByVariant)
		{
			SkeletalMeshComponent = nullptr;
		}
	}
}

FGLTFJsonMeshIndex FGLTFSkeletalMeshConverter::Convert(const USkeletalMesh* SkeletalMesh, const USkeletalMeshComponent* SkeletalMeshComponent, FGLTFMaterialArray Materials, int32 LODIndex)
{
#if !WITH_EDITOR
	if (!SkeletalMesh->GetLODInfo(LODIndex)->bAllowCPUAccess)
	{
		Builder.LogSuggestion(FString::Printf(
			TEXT("Export of mesh %s (LOD %d) can in runtime be speed-up by checking 'Allow CPU Access' in asset settings"),
			*SkeletalMesh->GetName(),
			LODIndex));
	}
#endif

	FGLTFJsonMesh JsonMesh;
	const int32 MaterialCount = FGLTFMeshUtility::GetMaterials(SkeletalMesh).Num();
	JsonMesh.Primitives.AddDefaulted(MaterialCount);

	const FGLTFJsonMeshIndex MeshIndex = Builder.AddMesh(JsonMesh);
	Builder.SetupTask<FGLTFSkeletalMeshTask>(Builder, MeshSectionConverter, SkeletalMesh, SkeletalMeshComponent, Materials, LODIndex, MeshIndex);
	return MeshIndex;
}
