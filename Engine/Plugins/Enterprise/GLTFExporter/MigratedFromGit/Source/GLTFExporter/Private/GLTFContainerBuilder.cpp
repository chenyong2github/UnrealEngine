// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFContainerBuilder.h"
#include "GLTFMeshBuilder.h"
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

void FGLTFContainerBuilder::Serialize(FArchive& Archive)
{
	FGLTFJsonBuffer& JsonBuffer = JsonRoot.Buffers[BufferBuilder.BufferIndex];
	BufferBuilder.UpdateBuffer(JsonBuffer);
	JsonRoot.Serialize(&Archive, true);
}

FGLTFJsonMeshIndex FGLTFContainerBuilder::AddMesh(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors)
{
	return IndexBuilder.FindOrAdd(FGLTFStaticMeshKey(StaticMesh, LODIndex, OverrideVertexColors), *this);
}

FGLTFJsonMeshIndex FGLTFContainerBuilder::AddMesh(const UStaticMeshComponent* StaticMeshComponent)
{
	const UStaticMesh* StaticMesh = StaticMeshComponent->GetStaticMesh();
	const int32 LODIndex = StaticMeshComponent->ForcedLodModel > 0 ? StaticMeshComponent->ForcedLodModel - 1 : /* auto-select */ 0;
	const FColorVertexBuffer* OverrideVertexColors = LODIndex < StaticMeshComponent->LODData.Num() ? StaticMeshComponent->LODData[LODIndex].OverrideVertexColors : nullptr;
	return AddMesh(StaticMesh, LODIndex, OverrideVertexColors);
}

FGLTFJsonSceneIndex FGLTFContainerBuilder::AddScene(const UWorld* World, bool bSelectedOnly)
{
	return FGLTFSceneBuilder(World, bSelectedOnly).AddScene(*this);
}
