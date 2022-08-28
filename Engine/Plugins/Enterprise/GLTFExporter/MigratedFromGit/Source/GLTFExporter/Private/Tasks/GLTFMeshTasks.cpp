// Copyright Epic Games, Inc. All Rights Reserved.

#include "Tasks/GLTFMeshTasks.h"
#include "Converters/GLTFConverterUtility.h"
#include "Converters/GLTFMeshUtility.h"
#include "Builders/GLTFConvertBuilder.h"
#include "Rendering/SkeletalMeshRenderData.h"

namespace
{
	EGLTFJsonBufferViewErrorFlags GetBufferViewErrorFlags(FGLTFConvertBuilder& Builder, FGLTFJsonAccessorIndex AccessorIndex)
	{
		if (AccessorIndex != INDEX_NONE)
		{
			const FGLTFJsonBufferViewIndex BufferViewIndex = Builder.GetAccessor(AccessorIndex).BufferView;
			if (BufferViewIndex != INDEX_NONE)
			{
				return Builder.GetBufferView(BufferViewIndex).ErrorFlags;
			}
		}

		return EGLTFJsonBufferViewErrorFlags::None;
	}

	void AddWarningsForBufferViewErrors(FGLTFConvertBuilder& Builder, EGLTFJsonBufferViewErrorFlags ErrorFlags, const TCHAR* MeshName, const TCHAR* TypeName)
	{
		const FString TypeNamePlural = FString(TypeName) + TEXT("s");
		const FString TypeNamePluralLC = FString(TypeNamePlural).ToLower();

		if (EnumHasAllFlags(ErrorFlags, EGLTFJsonBufferViewErrorFlags::ContainsZeroLengthVectors))
		{
			Builder.AddWarningMessage(FString::Printf(
				TEXT("Mesh %s contains zero-length %s. Consider checking 'Recompute %s' in the build-settings for the mesh"),
				MeshName,
				*TypeNamePluralLC,
				*TypeNamePlural));
		}

		if (EnumHasAllFlags(ErrorFlags, EGLTFJsonBufferViewErrorFlags::ContainsNonUnitLengthVectors))
		{
			Builder.AddWarningMessage(FString::Printf(
				TEXT("Mesh %s contains non unit-length %s. Consider checking 'Recompute %s' in the build-settings for the mesh, or enable normalization in export options"),
				MeshName,
				*TypeNamePluralLC,
				*TypeNamePlural));
		}
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

	const int32 MaterialCount = StaticMesh->StaticMaterials.Num();
	JsonMesh.Primitives.AddDefaulted(MaterialCount);

	EGLTFJsonBufferViewErrorFlags NormalBufferErrorFlags(EGLTFJsonBufferViewErrorFlags::None);
	EGLTFJsonBufferViewErrorFlags TangentBufferErrorFlags(EGLTFJsonBufferViewErrorFlags::None);

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

		NormalBufferErrorFlags |= GetBufferViewErrorFlags(Builder, JsonPrimitive.Attributes.Normal);
		TangentBufferErrorFlags |= GetBufferViewErrorFlags(Builder, JsonPrimitive.Attributes.Tangent);

		const uint32 UVCount = VertexBuffer->GetNumTexCoords();
		JsonPrimitive.Attributes.TexCoords.AddUninitialized(UVCount);

		for (uint32 UVIndex = 0; UVIndex < UVCount; ++UVIndex)
		{
			JsonPrimitive.Attributes.TexCoords[UVIndex] = Builder.GetOrAddUVAccessor(ConvertedSection, VertexBuffer, UVIndex);
		}

		const UMaterialInterface* Material = Materials[MaterialIndex];
		JsonPrimitive.Material =  Builder.GetOrAddMaterial(Material, MeshData, SectionIndices);
	}

	AddWarningsForBufferViewErrors(Builder, NormalBufferErrorFlags, *JsonMesh.Name, TEXT("Normal"));
	AddWarningsForBufferViewErrors(Builder, TangentBufferErrorFlags, *JsonMesh.Name, TEXT("Tangent"));
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

	const uint16 MaterialCount = SkeletalMesh->Materials.Num();
	JsonMesh.Primitives.AddDefaulted(MaterialCount);

	EGLTFJsonBufferViewErrorFlags NormalBufferErrorFlags(EGLTFJsonBufferViewErrorFlags::None);
	EGLTFJsonBufferViewErrorFlags TangentBufferErrorFlags(EGLTFJsonBufferViewErrorFlags::None);

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

		NormalBufferErrorFlags |= GetBufferViewErrorFlags(Builder, JsonPrimitive.Attributes.Normal);
		TangentBufferErrorFlags |= GetBufferViewErrorFlags(Builder, JsonPrimitive.Attributes.Tangent);

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

	AddWarningsForBufferViewErrors(Builder, NormalBufferErrorFlags, *JsonMesh.Name, TEXT("Normal"));
	AddWarningsForBufferViewErrors(Builder, TangentBufferErrorFlags, *JsonMesh.Name, TEXT("Tangent"));
}
