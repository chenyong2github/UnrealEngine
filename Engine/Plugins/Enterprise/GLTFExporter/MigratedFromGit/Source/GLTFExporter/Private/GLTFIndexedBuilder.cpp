// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFIndexedBuilder.h"

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::GetOrAddPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return PositionVertexBuffers.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::GetOrAddColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return ColorVertexBuffers.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::GetOrAddNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return StaticMeshNormalVertexBuffers.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::GetOrAddTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return StaticMeshTangentVertexBuffers.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::GetOrAddUV0Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return StaticMeshUV0VertexBuffers.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::GetOrAddUV1Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return StaticMeshUV1VertexBuffers.GetOrAdd(*this, DesiredName, VertexBuffer);
}

FGLTFJsonBufferViewIndex FGLTFIndexedBuilder::GetOrAddIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName)
{
	return StaticMeshIndexBuffers.GetOrAdd(*this, DesiredName, IndexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::GetOrAddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName)
{
	return StaticMeshSections.GetOrAdd(*this, DesiredName, MeshSection, IndexBuffer);
}

FGLTFJsonMeshIndex FGLTFIndexedBuilder::GetOrAddMesh(const FStaticMeshLODResources* StaticMeshLOD, const FColorVertexBuffer* OverrideVertexColors, const FString& DesiredName)
{
	return StaticMeshes.GetOrAdd(*this, DesiredName, StaticMeshLOD, OverrideVertexColors);
}

FGLTFJsonMeshIndex FGLTFIndexedBuilder::GetOrAddMesh(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FString& DesiredName)
{
	const FStaticMeshLODResources* StaticMeshLOD = &StaticMesh->GetLODForExport(LODIndex);
	FString Name = DesiredName.IsEmpty() ? StaticMesh->GetName() : DesiredName;

	if (LODIndex != 0)
	{
		Name += TEXT("_LOD") + FString::FromInt(LODIndex);
	}

	return GetOrAddMesh(StaticMeshLOD, OverrideVertexColors, Name);
}

FGLTFJsonMeshIndex FGLTFIndexedBuilder::GetOrAddMesh(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName)
{
	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	const int32 LODIndex = StaticMeshComponent->ForcedLodModel > 0 ? StaticMeshComponent->ForcedLodModel - 1 : /* auto-select */ 0;
	const FColorVertexBuffer* OverrideVertexColors = LODIndex < StaticMeshComponent->LODData.Num() ? StaticMeshComponent->LODData[LODIndex].OverrideVertexColors : nullptr;

	return GetOrAddMesh(StaticMesh, LODIndex, OverrideVertexColors, DesiredName);
}

FGLTFJsonNodeIndex FGLTFIndexedBuilder::GetOrAddNode(const USceneComponent* SceneComponent, bool bSelectedOnly, bool bRootNode, const FString& DesiredName)
{
	return SceneComponents.GetOrAdd(*this, DesiredName, SceneComponent, bSelectedOnly, bRootNode);
}

FGLTFJsonSceneIndex FGLTFIndexedBuilder::GetOrAddScene(const ULevel* Level, bool bSelectedOnly, const FString& DesiredName)
{
	return Levels.GetOrAdd(*this, DesiredName, Level, bSelectedOnly);
}

FGLTFJsonSceneIndex FGLTFIndexedBuilder::GetOrAddScene(const UWorld* World, bool bSelectedOnly, const FString& DesiredName)
{
	return GetOrAddScene(World->PersistentLevel, bSelectedOnly, DesiredName.IsEmpty() ? World->GetName() : DesiredName);
}
