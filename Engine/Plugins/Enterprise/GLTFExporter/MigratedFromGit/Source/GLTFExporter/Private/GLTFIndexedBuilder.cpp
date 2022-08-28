// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFIndexedBuilder.h"

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::ConvertPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return PositionVertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::ConvertColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return ColorVertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::ConvertNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return StaticMeshNormalVertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::ConvertTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return StaticMeshTangentVertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::ConvertUV0Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return StaticMeshUV0VertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::ConvertUV1Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return StaticMeshUV1VertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonBufferViewIndex FGLTFIndexedBuilder::ConvertIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName)
{
	return StaticMeshIndexBuffers.Convert(*this, DesiredName, IndexBuffer);
}

FGLTFJsonAccessorIndex FGLTFIndexedBuilder::ConvertIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName)
{
	return StaticMeshSections.Convert(*this, DesiredName, MeshSection, IndexBuffer);
}

FGLTFJsonMeshIndex FGLTFIndexedBuilder::ConvertMesh(const FStaticMeshLODResources* StaticMeshLOD, const FColorVertexBuffer* OverrideVertexColors, const FString& DesiredName)
{
	return StaticMeshes.Convert(*this, DesiredName, StaticMeshLOD, OverrideVertexColors);
}

FGLTFJsonMeshIndex FGLTFIndexedBuilder::ConvertMesh(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FString& DesiredName)
{
	const FStaticMeshLODResources* StaticMeshLOD = &StaticMesh->GetLODForExport(LODIndex);
	return ConvertMesh(StaticMeshLOD, OverrideVertexColors, DesiredName.IsEmpty() ? StaticMesh->GetName() + TEXT("_LOD") + FString::FromInt(LODIndex) : DesiredName);
}

FGLTFJsonMeshIndex FGLTFIndexedBuilder::ConvertMesh(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName)
{
	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	const int32 LODIndex = StaticMeshComponent->ForcedLodModel > 0 ? StaticMeshComponent->ForcedLodModel - 1 : /* auto-select */ 0;
	const FColorVertexBuffer* OverrideVertexColors = LODIndex < StaticMeshComponent->LODData.Num() ? StaticMeshComponent->LODData[LODIndex].OverrideVertexColors : nullptr;
	return ConvertMesh(StaticMesh, LODIndex, OverrideVertexColors, DesiredName);
}

FGLTFJsonNodeIndex FGLTFIndexedBuilder::ConvertNode(const USceneComponent* SceneComponent, bool bSelectedOnly, bool bRootNode, const FString& DesiredName)
{
	return SceneComponents.Convert(*this, DesiredName, SceneComponent, bSelectedOnly, bRootNode);
}

FGLTFJsonSceneIndex FGLTFIndexedBuilder::ConvertScene(const ULevel* Level, bool bSelectedOnly, const FString& DesiredName)
{
	return Levels.Convert(*this, DesiredName, Level, bSelectedOnly);
}

FGLTFJsonSceneIndex FGLTFIndexedBuilder::ConvertScene(const UWorld* World, bool bSelectedOnly, const FString& DesiredName)
{
	const ULevel* Level = World->PersistentLevel;
	return ConvertScene(Level, bSelectedOnly, DesiredName.IsEmpty() ? World->GetName() : DesiredName);
}
