// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshConverters.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Tasks/GLTFMeshTasks.h"
#include "Rendering/SkeletalMeshRenderData.h"

void FGLTFStaticMeshConverter::Sanitize(const UStaticMesh*& StaticMesh, int32& LODIndex, const FColorVertexBuffer*& OverrideVertexColors, FGLTFMaterialArray& OverrideMaterials)
{
	if (LODIndex < 0)
	{
		LODIndex = FMath::Max(Builder.ExportOptions->DefaultLevelOfDetail, 0);
	}

	if (!Builder.ExportOptions->bExportVertexColors)
	{
		OverrideVertexColors = nullptr;
	}

	if (OverrideMaterials == StaticMesh->StaticMaterials)
	{
		OverrideMaterials.Empty();
	}

	if (StaticMesh->GetNumLODs() > LODIndex)
	{
		const FStaticMeshLODResources& MeshLOD = StaticMesh->GetLODForExport(LODIndex);
		if (OverrideVertexColors == &MeshLOD.VertexBuffers.ColorVertexBuffer)
		{
			OverrideVertexColors = nullptr;
		}
	}
}

FGLTFJsonMeshIndex FGLTFStaticMeshConverter::Convert(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, FGLTFMaterialArray OverrideMaterials)
{
	if (StaticMesh->GetNumLODs() <= LODIndex)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const FGLTFJsonMeshIndex MeshIndex = Builder.AddMesh();
	Builder.SetupTask<FGLTFStaticMeshTask>(Builder, MeshSectionConverter, StaticMesh, LODIndex, OverrideVertexColors, OverrideMaterials, MeshIndex);
	return MeshIndex;
}

void FGLTFSkeletalMeshConverter::Sanitize(const USkeletalMesh*& SkeletalMesh, int32& LODIndex, const FColorVertexBuffer*& OverrideVertexColors, const FSkinWeightVertexBuffer*& OverrideSkinWeights, FGLTFMaterialArray& OverrideMaterials)
{
	if (LODIndex < 0)
	{
		LODIndex = FMath::Max(Builder.ExportOptions->DefaultLevelOfDetail, 0);
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
	if (RenderData->LODRenderData.Num() > LODIndex)
	{
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
}

FGLTFJsonMeshIndex FGLTFSkeletalMeshConverter::Convert(const USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FSkinWeightVertexBuffer* OverrideSkinWeights, FGLTFMaterialArray OverrideMaterials)
{
	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();

	if (RenderData->LODRenderData.Num() <= LODIndex)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const FGLTFJsonMeshIndex MeshIndex = Builder.AddMesh();
	Builder.SetupTask<FGLTFSkeletalMeshTask>(Builder, MeshSectionConverter, SkeletalMesh, LODIndex, OverrideVertexColors, OverrideSkinWeights, OverrideMaterials, MeshIndex);
	return MeshIndex;
}
