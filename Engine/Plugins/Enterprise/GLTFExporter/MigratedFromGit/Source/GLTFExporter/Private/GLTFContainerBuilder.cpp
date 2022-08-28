// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFContainerBuilder.h"
#include "GLTFSceneBuilder.h"

FGLTFContainerBuilder::FGLTFContainerBuilder()
	: BufferBuilder(AddBuffer(FGLTFJsonBuffer()))
{
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::AddAccessor(const FGLTFJsonAccessor& JsonAccessor)
{
	return FGLTFJsonAccessorIndex(JsonRoot.Accessors.Add(JsonAccessor));
}

FGLTFJsonBufferIndex FGLTFContainerBuilder::AddBuffer(const FGLTFJsonBuffer& JsonBuffer)
{
	return FGLTFJsonBufferIndex(JsonRoot.Buffers.Add(JsonBuffer));
}

FGLTFJsonBufferViewIndex FGLTFContainerBuilder::AddBufferView(const FGLTFJsonBufferView& JsonBufferView)
{
	return FGLTFJsonBufferViewIndex(JsonRoot.BufferViews.Add(JsonBufferView));
}

FGLTFJsonMeshIndex FGLTFContainerBuilder::AddMesh(const FGLTFJsonMesh& JsonMesh)
{
	return FGLTFJsonMeshIndex(JsonRoot.Meshes.Add(JsonMesh));
}

FGLTFJsonNodeIndex FGLTFContainerBuilder::AddNode(const FGLTFJsonNode& JsonNode)
{
	return FGLTFJsonNodeIndex(JsonRoot.Nodes.Add(JsonNode));
}

FGLTFJsonSceneIndex FGLTFContainerBuilder::AddScene(const FGLTFJsonScene& JsonScene)
{
	return FGLTFJsonSceneIndex(JsonRoot.Scenes.Add(JsonScene));
}

FGLTFJsonBufferViewIndex FGLTFContainerBuilder::AddBufferView(const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget)
{
	return BufferBuilder.AddBufferView(*this, RawData, ByteLength, Name, BufferTarget);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::ConvertPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return IndexedConverts.PositionVertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::ConvertColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return IndexedConverts.ColorVertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::ConvertNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return IndexedConverts.StaticMeshNormalVertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::ConvertTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return IndexedConverts.StaticMeshTangentVertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::ConvertUV0Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return IndexedConverts.StaticMeshUV0VertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::ConvertUV1Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& DesiredName)
{
	return IndexedConverts.StaticMeshUV1VertexBuffers.Convert(*this, DesiredName, VertexBuffer);
}

FGLTFJsonBufferViewIndex FGLTFContainerBuilder::ConvertIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName)
{
	return IndexedConverts.StaticMeshIndexBuffers.Convert(*this, DesiredName, IndexBuffer);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::ConvertIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& DesiredName)
{
	return IndexedConverts.StaticMeshSections.Convert(*this, DesiredName, MeshSection, IndexBuffer);
}

FGLTFJsonMeshIndex FGLTFContainerBuilder::ConvertMesh(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors, const FString& DesiredName)
{
	return IndexedConverts.StaticMeshes.Convert(*this, DesiredName, StaticMesh, LODIndex, OverrideVertexColors);
}

FGLTFJsonMeshIndex FGLTFContainerBuilder::ConvertMesh(const UStaticMeshComponent* StaticMeshComponent, const FString& DesiredName)
{
	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	const int32 LODIndex = StaticMeshComponent->ForcedLodModel > 0 ? StaticMeshComponent->ForcedLodModel - 1 : /* auto-select */ 0;
	const FColorVertexBuffer* OverrideVertexColors = LODIndex < StaticMeshComponent->LODData.Num() ? StaticMeshComponent->LODData[LODIndex].OverrideVertexColors : nullptr;
	return ConvertMesh(StaticMesh, LODIndex, OverrideVertexColors, DesiredName);
}

FGLTFJsonSceneIndex FGLTFContainerBuilder::AddScene(const UWorld* World, bool bSelectedOnly)
{
	return FGLTFSceneBuilder(World, bSelectedOnly).AddScene(*this);
}

void FGLTFContainerBuilder::Serialize(FArchive& Archive)
{
	FGLTFJsonBuffer& JsonBuffer = JsonRoot.Buffers[BufferBuilder.BufferIndex];
	BufferBuilder.UpdateBuffer(JsonBuffer);
	JsonRoot.Serialize(&Archive, true);
}
