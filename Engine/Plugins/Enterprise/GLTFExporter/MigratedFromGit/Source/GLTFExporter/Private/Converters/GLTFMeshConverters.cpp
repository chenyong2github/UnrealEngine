// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Builders/GLTFConvertBuilder.h"
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

	const FStaticMeshLODResources& MeshLOD = StaticMesh->GetLODForExport(LODIndex);

	FGLTFJsonMesh JsonMesh;
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

	return Builder.AddMesh(JsonMesh);
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

	const FSkeletalMeshLODRenderData& MeshLOD = RenderData->LODRenderData[LODIndex];

	FGLTFJsonMesh JsonMesh;
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

	return Builder.AddMesh(JsonMesh);
}
