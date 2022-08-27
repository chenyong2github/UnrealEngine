// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshConverters.h"
#include "Converters/GLTFMeshUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMeshTasks.h"
#include "Rendering/SkeletalMeshRenderData.h"

void FGLTFStaticMeshConverter::Sanitize(const UStaticMesh*& StaticMesh, int32& LODIndex, const FColorVertexBuffer*& OverrideVertexColors, FGLTFMaterialArray& OverrideMaterials)
{
	if (LODIndex < 0)
	{
		LODIndex = FMath::Max(Builder.ExportOptions->DefaultLevelOfDetail, FGLTFMeshUtility::GetMinimumLOD(StaticMesh));
	}

	if (!Builder.ExportOptions->bExportVertexColors)
	{
		OverrideVertexColors = nullptr;
	}

	if (OverrideMaterials == StaticMesh->StaticMaterials)
	{
		OverrideMaterials.Empty();
	}

	LODIndex = FMath::Clamp(LODIndex, 0, StaticMesh->GetNumLODs() - 1);
	const FStaticMeshLODResources& MeshLOD = StaticMesh->GetLODForExport(LODIndex);

	if (OverrideVertexColors == &MeshLOD.VertexBuffers.ColorVertexBuffer)
	{
		OverrideVertexColors = nullptr;
	}
}

FGLTFJsonMeshIndex FGLTFStaticMeshConverter::Convert(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, FGLTFMaterialArray OverrideMaterials)
{
	const FGLTFJsonMeshIndex MeshIndex = Builder.AddMesh();
	Builder.SetupTask<FGLTFStaticMeshTask>(Builder, MeshSectionConverter, StaticMesh, LODIndex, OverrideVertexColors, OverrideMaterials, MeshIndex);
	return MeshIndex;
}

void FGLTFSkeletalMeshConverter::Sanitize(const USkeletalMesh*& SkeletalMesh, int32& LODIndex, const FColorVertexBuffer*& OverrideVertexColors, const FSkinWeightVertexBuffer*& OverrideSkinWeights, FGLTFMaterialArray& OverrideMaterials)
{
	if (LODIndex < 0)
	{
		LODIndex = FMath::Max(Builder.ExportOptions->DefaultLevelOfDetail, FGLTFMeshUtility::GetMinimumLOD(SkeletalMesh));
	}

	if (!Builder.ExportOptions->bExportVertexColors)
	{
		OverrideVertexColors = nullptr;
	}

	if (!Builder.ExportOptions->bExportVertexSkinWeights)
	{
		OverrideSkinWeights = nullptr;
	}

	if (OverrideMaterials == SkeletalMesh->Materials)
	{
		OverrideMaterials.Empty();
	}

	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	LODIndex = FMath::Clamp(LODIndex, 0, RenderData->LODRenderData.Num() - 1);
	const FSkeletalMeshLODRenderData& MeshLOD = RenderData->LODRenderData[LODIndex];

	if (OverrideVertexColors == &MeshLOD.StaticVertexBuffers.ColorVertexBuffer)
	{
		OverrideVertexColors = nullptr;
	}

	if (OverrideSkinWeights == MeshLOD.GetSkinWeightVertexBuffer())
	{
		OverrideSkinWeights = nullptr;
	}
}

FGLTFJsonMeshIndex FGLTFSkeletalMeshConverter::Convert(const USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FSkinWeightVertexBuffer* OverrideSkinWeights, FGLTFMaterialArray OverrideMaterials)
{
	const FGLTFJsonMeshIndex MeshIndex = Builder.AddMesh();
	Builder.SetupTask<FGLTFSkeletalMeshTask>(Builder, MeshSectionConverter, SkeletalMesh, LODIndex, OverrideVertexColors, OverrideSkinWeights, OverrideMaterials, MeshIndex);
	return MeshIndex;
}
