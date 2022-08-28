// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFConvertBuilder.h"
#include "GLTFBuilderUtility.h"

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return PositionVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return ColorVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return NormalVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return TangentVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddUVAccessor(const FStaticMeshVertexBuffer* VertexBuffer, int32 UVIndex, const FString& DesiredName)
{
	if (VertexBuffer == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return UVVertexBufferConverter.GetOrAdd(*this, DesiredName, VertexBuffer, UVIndex);
}

FGLTFJsonBufferViewIndex FGLTFConvertBuilder::GetOrAddIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName)
{
	if (IndexBuffer == nullptr)
	{
		return FGLTFJsonBufferViewIndex(INDEX_NONE);
	}

	return IndexBufferConverter.GetOrAdd(*this, DesiredName, IndexBuffer);
}

FGLTFJsonAccessorIndex FGLTFConvertBuilder::GetOrAddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName)
{
	if (MeshSection == nullptr)
	{
		return FGLTFJsonAccessorIndex(INDEX_NONE);
	}

	return StaticMeshSectionConverter.GetOrAdd(*this, DesiredName, MeshSection, IndexBuffer);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FGLTFMaterialArray& OverrideMaterials, const FString& DesiredName)
{
	if (StaticMesh == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	return StaticMeshConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? FGLTFBuilderUtility::GetMeshName(StaticMesh, LODIndex) : DesiredName, StaticMesh, LODIndex, OverrideVertexColors, OverrideMaterials);
}

FGLTFJsonMeshIndex FGLTFConvertBuilder::GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName)
{
	if (StaticMeshComponent == nullptr)
	{
		return FGLTFJsonMeshIndex(INDEX_NONE);
	}

	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	const int32 LODIndex = StaticMeshComponent->ForcedLodModel > 0 ? StaticMeshComponent->ForcedLodModel - 1 : /* auto-select */ 0;
	const FColorVertexBuffer* OverrideVertexColors = StaticMeshComponent->LODData.IsValidIndex(LODIndex) ? StaticMeshComponent->LODData[LODIndex].OverrideVertexColors : nullptr;
	const FGLTFMaterialArray OverrideMaterials = FGLTFMaterialArray(StaticMeshComponent->OverrideMaterials);

	return GetOrAddMesh(StaticMesh, LODIndex, OverrideVertexColors, OverrideMaterials, DesiredName);
}

FGLTFJsonMaterialIndex FGLTFConvertBuilder::GetOrAddMaterial(const UMaterialInterface* Material, const FString& DesiredName)
{
	if (Material == nullptr)
	{
		return FGLTFJsonMaterialIndex(INDEX_NONE);
	}

	return MaterialConverter.GetOrAdd(*this, DesiredName.IsEmpty() ? Material->GetName() : DesiredName, Material);
}

FGLTFJsonNodeIndex FGLTFConvertBuilder::GetOrAddNode(const USceneComponent* SceneComponent, bool bSelectedOnly, bool bRootNode, const FString& DesiredName)
{
	if (SceneComponent == nullptr)
	{
		return FGLTFJsonNodeIndex(INDEX_NONE);
	}

	return SceneComponentConverter.GetOrAdd(*this, DesiredName, SceneComponent, bSelectedOnly, bRootNode);
}

FGLTFJsonSceneIndex FGLTFConvertBuilder::GetOrAddScene(const ULevel* Level, bool bSelectedOnly, const FString& DesiredName)
{
	if (Level == nullptr)
	{
		return FGLTFJsonSceneIndex(INDEX_NONE);
	}

	return LevelConverter.GetOrAdd(*this, DesiredName, Level, bSelectedOnly);
}

FGLTFJsonSceneIndex FGLTFConvertBuilder::GetOrAddScene(const UWorld* World, bool bSelectedOnly, const FString& DesiredName)
{
	if (World == nullptr)
	{
		return FGLTFJsonSceneIndex(INDEX_NONE);
	}

	return GetOrAddScene(World->PersistentLevel, bSelectedOnly, DesiredName.IsEmpty() ? World->GetName() : DesiredName);
}
