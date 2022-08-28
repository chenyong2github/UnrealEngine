// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/GLTFMeshTasks.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Rendering/SkeletalMeshRenderData.h"

void FGLTFStaticMeshTask::Complete()
{
	const FStaticMeshLODResources& MeshLOD = StaticMesh->GetLODForExport(LODIndex);

	FGLTFJsonMesh& JsonMesh = Builder.GetMesh(MeshIndex);
	JsonMesh.Name = FGLTFNameUtility::GetName(StaticMesh, LODIndex);

	const FPositionVertexBuffer* PositionBuffer = &MeshLOD.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer* VertexBuffer = &MeshLOD.VertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = OverrideVertexColors != nullptr ? OverrideVertexColors : &MeshLOD.VertexBuffers.ColorVertexBuffer;

	const int32 MaterialCount = StaticMesh->StaticMaterials.Num();
	JsonMesh.Primitives.AddDefaulted(MaterialCount);

	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		const FGLTFMeshSection* ConvertedSection = MeshSectionConverter.GetOrAdd(&MeshLOD, MaterialIndex);

		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh.Primitives[MaterialIndex];
		JsonPrimitive.Indices = Builder.GetOrAddIndexAccessor(ConvertedSection);

		JsonPrimitive.Attributes.Position = Builder.GetOrAddPositionAccessor(ConvertedSection, PositionBuffer);
		if (JsonPrimitive.Attributes.Position == INDEX_NONE)
		{
			// TODO: report warning
		}

		if (Builder.ExportOptions->bExportVertexColors)
		{
			JsonPrimitive.Attributes.Color0 = Builder.GetOrAddColorAccessor(ConvertedSection, ColorBuffer);
		}

		JsonPrimitive.Attributes.Normal = Builder.GetOrAddNormalAccessor(ConvertedSection, VertexBuffer);
		JsonPrimitive.Attributes.Tangent = Builder.GetOrAddTangentAccessor(ConvertedSection, VertexBuffer);

		const uint32 UVCount = VertexBuffer->GetNumTexCoords();
		JsonPrimitive.Attributes.TexCoords.AddUninitialized(UVCount);

		for (uint32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
		{
			JsonPrimitive.Attributes.TexCoords[UVIndex] = Builder.GetOrAddUVAccessor(ConvertedSection, VertexBuffer, UVIndex);
		}

		const UMaterialInterface* Material = OverrideMaterials.GetOverride(StaticMesh->StaticMaterials, MaterialIndex);
		if (Material == nullptr)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		JsonPrimitive.Material = Builder.GetOrAddMaterial(Material);
	}
}

void FGLTFSkeletalMeshTask::Complete()
{
	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	const FSkeletalMeshLODRenderData& MeshLOD = RenderData->LODRenderData[LODIndex];

	FGLTFJsonMesh& JsonMesh = Builder.GetMesh(MeshIndex);
	JsonMesh.Name = FGLTFNameUtility::GetName(SkeletalMesh, LODIndex);

	const FPositionVertexBuffer* PositionBuffer = &MeshLOD.StaticVertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer* VertexBuffer = &MeshLOD.StaticVertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = OverrideVertexColors != nullptr ? OverrideVertexColors : &MeshLOD.StaticVertexBuffers.ColorVertexBuffer;
	const FSkinWeightVertexBuffer* SkinWeightBuffer = OverrideSkinWeights != nullptr ? OverrideSkinWeights : MeshLOD.GetSkinWeightVertexBuffer();

	const uint16 MaterialCount = SkeletalMesh->Materials.Num();
	JsonMesh.Primitives.AddDefaulted(MaterialCount);

	for (uint16 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		const FGLTFMeshSection* ConvertedSection = MeshSectionConverter.GetOrAdd(&MeshLOD, MaterialIndex);

		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh.Primitives[MaterialIndex];
		JsonPrimitive.Indices = Builder.GetOrAddIndexAccessor(ConvertedSection);

		JsonPrimitive.Attributes.Position = Builder.GetOrAddPositionAccessor(ConvertedSection, PositionBuffer);
		if (JsonPrimitive.Attributes.Position == INDEX_NONE)
		{
			// TODO: report warning
		}

		if (Builder.ExportOptions->bExportVertexColors)
		{
			JsonPrimitive.Attributes.Color0 = Builder.GetOrAddColorAccessor(ConvertedSection, ColorBuffer);
		}

		JsonPrimitive.Attributes.Normal = Builder.GetOrAddNormalAccessor(ConvertedSection, VertexBuffer);
		JsonPrimitive.Attributes.Tangent = Builder.GetOrAddTangentAccessor(ConvertedSection, VertexBuffer);

		const uint32 UVCount = VertexBuffer->GetNumTexCoords();
		JsonPrimitive.Attributes.TexCoords.AddUninitialized(UVCount);

		for (uint32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
		{
			JsonPrimitive.Attributes.TexCoords[UVIndex] = Builder.GetOrAddUVAccessor(ConvertedSection, VertexBuffer, UVIndex);
		}

		if (Builder.ExportOptions->bExportVertexSkinWeights)
		{
			const uint32 GroupCount = (SkinWeightBuffer->GetMaxBoneInfluences() + 3) / 4;
			JsonPrimitive.Attributes.Joints.AddUninitialized(GroupCount);
			JsonPrimitive.Attributes.Weights.AddUninitialized(GroupCount);

			for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
			{
				JsonPrimitive.Attributes.Joints[GroupIndex] = Builder.GetOrAddJointAccessor(ConvertedSection, SkinWeightBuffer, GroupIndex * 4);
				JsonPrimitive.Attributes.Weights[GroupIndex] = Builder.GetOrAddWeightAccessor(ConvertedSection, SkinWeightBuffer, GroupIndex * 4);
			}
		}

		const UMaterialInterface* Material = OverrideMaterials.GetOverride(SkeletalMesh->Materials, MaterialIndex);
		if (Material == nullptr)
		{
			Material = UMaterial::GetDefaultMaterial(MD_Surface);
		}

		JsonPrimitive.Material = Builder.GetOrAddMaterial(Material);
	}
}
