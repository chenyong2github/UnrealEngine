// Copyright Epic Games, Inc. All Rights Reserved.

#include "Converters/GLTFMeshConverters.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFNameUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Rendering/MultiSizeIndexContainer.h"
#include "Rendering/SkeletalMeshRenderData.h"

FGLTFJsonMeshIndex FGLTFStaticMeshConverter::Convert(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, FGLTFMaterialArray OverrideMaterials)
{
	if (LODIndex < 0 || StaticMesh->GetNumLODs() <= LODIndex)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const FStaticMeshLODResources& MeshLOD = StaticMesh->GetLODForExport(LODIndex);

	FGLTFJsonMesh JsonMesh;
	JsonMesh.Name = FGLTFNameUtility::GetName(StaticMesh, LODIndex);

	const FPositionVertexBuffer* PositionBuffer = &MeshLOD.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer* VertexBuffer = &MeshLOD.VertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = OverrideVertexColors != nullptr ? OverrideVertexColors : &MeshLOD.VertexBuffers.ColorVertexBuffer;

	FGLTFJsonAttributes JsonAttributes;
	JsonAttributes.Position = Builder.GetOrAddPositionAccessor(PositionBuffer);

	if (Builder.ExportOptions->bExportVertexColors)
	{
		JsonAttributes.Color0 = Builder.GetOrAddColorAccessor(ColorBuffer);
	}

	JsonAttributes.Normal = Builder.GetOrAddNormalAccessor(VertexBuffer);
	JsonAttributes.Tangent = Builder.GetOrAddTangentAccessor(VertexBuffer);

	const uint32 UVCount = VertexBuffer->GetNumTexCoords();
	JsonAttributes.TexCoords.AddUninitialized(UVCount);

	for (uint32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
	{
		JsonAttributes.TexCoords[UVIndex] = Builder.GetOrAddUVAccessor(VertexBuffer, UVIndex);
	}

	const FRawStaticIndexBuffer* IndexBuffer = &MeshLOD.IndexBuffer;

	const int32 SectionCount = MeshLOD.Sections.Num();
	JsonMesh.Primitives.AddDefaulted(SectionCount);

	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh.Primitives[SectionIndex];
		JsonPrimitive.Attributes = JsonAttributes;

		const FStaticMeshSection& Section = MeshLOD.Sections[SectionIndex];
		JsonPrimitive.Indices = Builder.GetOrAddIndexAccessor(&Section, IndexBuffer);

		const int32 MaterialIndex = Section.MaterialIndex;
		const UMaterialInterface* Material = OverrideMaterials.IsValidIndex(MaterialIndex) && OverrideMaterials[MaterialIndex] != nullptr ?
			OverrideMaterials[MaterialIndex] : StaticMesh->GetMaterial(MaterialIndex);

		if (Material != nullptr)
		{
			JsonPrimitive.Material = Builder.GetOrAddMaterial(Material);
		}
	}

	return Builder.AddMesh(JsonMesh);
}

FGLTFJsonMeshIndex FGLTFSkeletalMeshConverter::Convert(const USkeletalMesh* SkeletalMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FSkinWeightVertexBuffer* OverrideSkinWeights, FGLTFMaterialArray OverrideMaterials)
{
	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();

	if (LODIndex < 0 || RenderData->LODRenderData.Num() <= LODIndex)
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

	FGLTFJsonAttributes JsonAttributes;
	JsonAttributes.Position = Builder.GetOrAddPositionAccessor(PositionBuffer);

	if (Builder.ExportOptions->bExportVertexColors)
	{
		JsonAttributes.Color0 = Builder.GetOrAddColorAccessor(ColorBuffer);
	}

	JsonAttributes.Normal = Builder.GetOrAddNormalAccessor(VertexBuffer);
	JsonAttributes.Tangent = Builder.GetOrAddTangentAccessor(VertexBuffer);

	const uint32 UVCount = VertexBuffer->GetNumTexCoords();
	JsonAttributes.TexCoords.AddUninitialized(UVCount);

	for (uint32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
	{
		JsonAttributes.TexCoords[UVIndex] = Builder.GetOrAddUVAccessor(VertexBuffer, UVIndex);
	}

	/* TODO: enable export of vertex skin data when crash bug fixed

	const uint32 GroupCount = (SkinWeightBuffer->GetMaxBoneInfluences() + 3) / 4;
	JsonAttributes.Joints.AddUninitialized(GroupCount);
	JsonAttributes.Weights.AddUninitialized(GroupCount);

	const FGLTFBoneMap BoneMap = FGLTFBoneMap(MeshLOD.RenderSections[0].BoneMap); // TODO: can we make this assumption?

	for (uint32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
	{
		JsonAttributes.Joints[GroupIndex] = Builder.GetOrAddJointAccessor(SkinWeightBuffer, GroupIndex, BoneMap, Name + TEXT("_Joints") + FString::FromInt(GroupIndex));
		JsonAttributes.Weights[GroupIndex] = Builder.GetOrAddWeightAccessor(SkinWeightBuffer, GroupIndex, Name + TEXT("_Weights") + FString::FromInt(GroupIndex));
	}
	*/

	const FMultiSizeIndexContainer* IndexContainer = &MeshLOD.MultiSizeIndexContainer;

	const int32 SectionCount = MeshLOD.RenderSections.Num();
	JsonMesh.Primitives.AddDefaulted(SectionCount);

	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh.Primitives[SectionIndex];
		JsonPrimitive.Attributes = JsonAttributes;

		const FSkelMeshRenderSection& Section = MeshLOD.RenderSections[SectionIndex];
		JsonPrimitive.Indices = Builder.GetOrAddIndexAccessor(&Section, IndexContainer);

		const int32 MaterialIndex = Section.MaterialIndex;
		const UMaterialInterface* Material = OverrideMaterials.IsValidIndex(MaterialIndex) && OverrideMaterials[MaterialIndex] != nullptr ?
			OverrideMaterials[MaterialIndex] : SkeletalMesh->Materials[MaterialIndex].MaterialInterface;

		if (Material != nullptr)
		{
			JsonPrimitive.Material = Builder.GetOrAddMaterial(Material);
		}
	}

	return Builder.AddMesh(JsonMesh);
}
