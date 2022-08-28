// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/GLTFMeshTasks.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFMeshUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace
{
	template <typename VertexBufferType>
	void ValidateVertexBuffer(FGLTFConvertBuilder& Builder, const VertexBufferType* VertexBuffer, const TCHAR* MeshName)
	{
		if (VertexBuffer == nullptr)
		{
			return;
		}

		bool bHasZeroLengthNormals;
		bool bHasZeroLengthTangents;

		if (VertexBuffer->GetUseHighPrecisionTangentBasis())
		{
			AnalyzeTangents<VertexBufferType, FPackedRGBA16N>(VertexBuffer, bHasZeroLengthNormals, bHasZeroLengthTangents);
		}
		else
		{
			AnalyzeTangents<VertexBufferType, FPackedNormal>(VertexBuffer, bHasZeroLengthNormals, bHasZeroLengthTangents);
		}

		if (bHasZeroLengthNormals)
		{
			Builder.AddWarningMessage(FString::Printf(
				TEXT("Mesh %s has some nearly zero-length normals which can create some issues. Consider checking 'Recompute Normals' in the asset settings"),
				MeshName));
		}

		if (bHasZeroLengthTangents)
		{
			Builder.AddWarningMessage(FString::Printf(
				TEXT("Mesh %s chas some nearly zero-length tangents which can create some issues. Consider checking 'Recompute Tangents' in the asset settings"),
				MeshName));
		}
	}

	template <typename VertexBufferType, typename TangentVectorType>
	void AnalyzeTangents(const VertexBufferType* VertexBuffer, bool& bOutHasZeroLengthNormals, bool& bOutHasZeroLengthTangents)
	{
		bool bHasZeroLengthNormals = false;
		bool bHasZeroLengthTangents = false;

		typedef TStaticMeshVertexTangentDatum<TangentVectorType> VertexTangentType;

		const void* TangentData = const_cast<FStaticMeshVertexBuffer*>(VertexBuffer)->GetTangentData();
		const VertexTangentType* VertexTangents = TangentData != nullptr ? static_cast<const VertexTangentType*>(TangentData) : nullptr;

		if (VertexTangents != nullptr)
		{
			const uint32 VertexCount = VertexBuffer->GetNumVertices();
			for (uint32 VertexIndex = 0; VertexIndex < VertexCount; ++VertexIndex)
			{
				const VertexTangentType& VertexTangent = VertexTangents[VertexIndex];
				bHasZeroLengthNormals |= VertexTangent.TangentZ.ToFVector().IsNearlyZero();
				bHasZeroLengthTangents |= VertexTangent.TangentX.ToFVector().IsNearlyZero();
			}
		}

		bOutHasZeroLengthNormals = bHasZeroLengthNormals;
		bOutHasZeroLengthTangents = bHasZeroLengthTangents;
	}
}

void FGLTFStaticMeshTask::Complete()
{
	FGLTFJsonMesh& JsonMesh = Builder.GetMesh(MeshIndex);
	JsonMesh.Name = StaticMeshComponent != nullptr ? FGLTFNameUtility::GetName(StaticMeshComponent) : StaticMesh->GetName();

	const FStaticMeshLODResources& MeshLOD = StaticMesh->GetLODForExport(LODIndex);
	const FPositionVertexBuffer* PositionBuffer = &MeshLOD.VertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer* VertexBuffer = &MeshLOD.VertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = &MeshLOD.VertexBuffers.ColorVertexBuffer;

	if (StaticMeshComponent != nullptr && StaticMeshComponent->LODData.IsValidIndex(LODIndex))
	{
		const FStaticMeshComponentLODInfo& LODInfo = StaticMeshComponent->LODData[LODIndex];
		ColorBuffer = LODInfo.OverrideVertexColors != nullptr ? LODInfo.OverrideVertexColors : ColorBuffer;
	}

	const FGLTFMeshData* MeshData = Builder.ExportOptions->BakeMaterialInputs == EGLTFMaterialBakeMode::UseMeshData ?
		Builder.StaticMeshDataConverter.GetOrAdd(StaticMesh, StaticMeshComponent, LODIndex) : nullptr;

	if (MeshData != nullptr)
	{
		if (MeshData->Description.IsEmpty())
		{
			// TODO: report warning in case the mesh actually has data, which means we failed to extract a mesh description.
			MeshData = nullptr;
		}
		else if (MeshData->TexCoord < 0)
		{
			// TODO: report warning (about missing non-overlapping texture coordinate for baking with mesh data).
			MeshData = nullptr;
		}
	}

	ValidateVertexBuffer(Builder, VertexBuffer, *StaticMesh->GetName());

	const int32 MaterialCount = StaticMesh->StaticMaterials.Num();
	JsonMesh.Primitives.AddDefaulted(MaterialCount);

	for (int32 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		const TArray<int32> SectionIndices = FGLTFMeshUtility::GetSectionIndices(MeshLOD, MaterialIndex);
		const FGLTFMeshSection* ConvertedSection = MeshSectionConverter.GetOrAdd(&MeshLOD, SectionIndices);

		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh.Primitives[MaterialIndex];
		JsonPrimitive.Indices = Builder.GetOrAddIndexAccessor(ConvertedSection);

		JsonPrimitive.Attributes.Position = Builder.GetOrAddPositionAccessor(ConvertedSection, PositionBuffer);
		if (JsonPrimitive.Attributes.Position == INDEX_NONE)
		{
			// TODO: report warning?
		}

		if (Builder.ExportOptions->bExportVertexColors)
		{
			// TODO: report warning (about vertex colors may be represented differently in glTF)

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

		const UMaterialInterface* Material = Materials[MaterialIndex];
		JsonPrimitive.Material =  Builder.GetOrAddMaterial(Material, MeshData, SectionIndices);
	}
}

void FGLTFSkeletalMeshTask::Complete()
{
	FGLTFJsonMesh& JsonMesh = Builder.GetMesh(MeshIndex);
	JsonMesh.Name = SkeletalMeshComponent != nullptr ? FGLTFNameUtility::GetName(SkeletalMeshComponent) : SkeletalMesh->GetName();

	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	const FSkeletalMeshLODRenderData& MeshLOD = RenderData->LODRenderData[LODIndex];

	const FPositionVertexBuffer* PositionBuffer = &MeshLOD.StaticVertexBuffers.PositionVertexBuffer;
	const FStaticMeshVertexBuffer* VertexBuffer = &MeshLOD.StaticVertexBuffers.StaticMeshVertexBuffer;
	const FColorVertexBuffer* ColorBuffer = &MeshLOD.StaticVertexBuffers.ColorVertexBuffer;
	const FSkinWeightVertexBuffer* SkinWeightBuffer = MeshLOD.GetSkinWeightVertexBuffer();

	if (SkeletalMeshComponent != nullptr && SkeletalMeshComponent->LODInfo.IsValidIndex(LODIndex))
	{
		const FSkelMeshComponentLODInfo& LODInfo = SkeletalMeshComponent->LODInfo[LODIndex];
		ColorBuffer = LODInfo.OverrideVertexColors != nullptr ? LODInfo.OverrideVertexColors : ColorBuffer;
		SkinWeightBuffer = LODInfo.OverrideSkinWeights != nullptr ? LODInfo.OverrideSkinWeights : SkinWeightBuffer;
	}

	const FGLTFMeshData* MeshData = Builder.ExportOptions->BakeMaterialInputs == EGLTFMaterialBakeMode::UseMeshData ?
		Builder.SkeletalMeshDataConverter.GetOrAdd(SkeletalMesh, SkeletalMeshComponent, LODIndex) : nullptr;

	if (MeshData != nullptr)
	{
		if (MeshData->Description.IsEmpty())
		{
			// TODO: report warning in case the mesh actually has data, which means we failed to extract a mesh description.
			MeshData = nullptr;
		}
		else if (MeshData->TexCoord < 0)
		{
			// TODO: report warning (about missing non-overlapping texture coordinate for baking with mesh data).
			MeshData = nullptr;
		}
	}

	ValidateVertexBuffer(Builder, VertexBuffer, *SkeletalMesh->GetName());

	const uint16 MaterialCount = SkeletalMesh->Materials.Num();
	JsonMesh.Primitives.AddDefaulted(MaterialCount);

	for (uint16 MaterialIndex = 0; MaterialIndex < MaterialCount; ++MaterialIndex)
	{
		const TArray<int32> SectionIndices = FGLTFMeshUtility::GetSectionIndices(MeshLOD, MaterialIndex);
		const FGLTFMeshSection* ConvertedSection = MeshSectionConverter.GetOrAdd(&MeshLOD, SectionIndices);

		FGLTFJsonPrimitive& JsonPrimitive = JsonMesh.Primitives[MaterialIndex];
		JsonPrimitive.Indices = Builder.GetOrAddIndexAccessor(ConvertedSection);

		JsonPrimitive.Attributes.Position = Builder.GetOrAddPositionAccessor(ConvertedSection, PositionBuffer);
		if (JsonPrimitive.Attributes.Position == INDEX_NONE)
		{
			// TODO: report warning?
		}

		if (Builder.ExportOptions->bExportVertexColors)
		{
			// TODO: report warning (about vertex colors may be represented differently in glTF)

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

		const UMaterialInterface* Material = Materials[MaterialIndex];
		JsonPrimitive.Material =  Builder.GetOrAddMaterial(Material, MeshData, SectionIndices);
	}
}
