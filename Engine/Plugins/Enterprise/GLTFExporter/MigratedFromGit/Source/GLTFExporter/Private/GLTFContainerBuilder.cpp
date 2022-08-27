// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFContainerBuilder.h"
#include "GLTFSceneBuilder.h"

FGLTFContainerBuilder::FGLTFContainerBuilder()
	: BufferBuilder(CreateBuffer(FGLTFJsonBuffer()))
{
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::CreateAccessor(const FGLTFJsonAccessor& JsonAccessor)
{
	return FGLTFJsonAccessorIndex(JsonRoot.Accessors.Add(JsonAccessor));
}

FGLTFJsonBufferIndex FGLTFContainerBuilder::CreateBuffer(const FGLTFJsonBuffer& JsonBuffer)
{
	return FGLTFJsonBufferIndex(JsonRoot.Buffers.Add(JsonBuffer));
}

FGLTFJsonBufferViewIndex FGLTFContainerBuilder::CreateBufferView(const FGLTFJsonBufferView& JsonBufferView)
{
	return FGLTFJsonBufferViewIndex(JsonRoot.BufferViews.Add(JsonBufferView));
}

FGLTFJsonMeshIndex FGLTFContainerBuilder::CreateMesh(const FGLTFJsonMesh& JsonMesh)
{
	return FGLTFJsonMeshIndex(JsonRoot.Meshes.Add(JsonMesh));
}

FGLTFJsonNodeIndex FGLTFContainerBuilder::CreateNode(const FGLTFJsonNode& JsonNode)
{
	return FGLTFJsonNodeIndex(JsonRoot.Nodes.Add(JsonNode));
}

FGLTFJsonSceneIndex FGLTFContainerBuilder::CreateScene(const FGLTFJsonScene& JsonScene)
{
	return FGLTFJsonSceneIndex(JsonRoot.Scenes.Add(JsonScene));
}

FGLTFJsonBufferViewIndex FGLTFContainerBuilder::CreateBufferView(const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget)
{
	return BufferBuilder.CreateBufferView(*this, RawData, ByteLength, Name, BufferTarget);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::AddPositionAccessor(const FPositionVertexBuffer* VertexBuffer, const FString& Name)
{
	return IndexBuilder.FindOrConvertPositionAccessor(FGLTFPositionVertexBufferKey(VertexBuffer, Name), *this);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::AddNormalAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name)
{
	return IndexBuilder.FindOrConvertNormalAccessor(FGLTFStaticMeshVertexBufferKey(VertexBuffer, Name), *this);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::AddTangentAccessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name)
{
	return IndexBuilder.FindOrConvertTangentAccessor(FGLTFStaticMeshVertexBufferKey(VertexBuffer, Name), *this);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::AddUV0Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name)
{
	return IndexBuilder.FindOrConvertUV0Accessor(FGLTFStaticMeshVertexBufferKey(VertexBuffer, Name), *this);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::AddUV1Accessor(const FStaticMeshVertexBuffer* VertexBuffer, const FString& Name)
{
	return IndexBuilder.FindOrConvertUV1Accessor(FGLTFStaticMeshVertexBufferKey(VertexBuffer, Name), *this);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::AddColorAccessor(const FColorVertexBuffer* VertexBuffer, const FString& Name)
{
	return IndexBuilder.FindOrConvertColorAccessor(FGLTFColorVertexBufferKey(VertexBuffer, Name), *this);
}

FGLTFJsonBufferViewIndex FGLTFContainerBuilder::AddIndexBufferView(const FRawStaticIndexBuffer* IndexBuffer, const FString& Name)
{
	return IndexBuilder.FindOrConvertIndexBufferView(FGLTFRawStaticIndexBufferKey(IndexBuffer, Name), *this);
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::AddIndexAccessor(const FStaticMeshSection* MeshSection, const FRawStaticIndexBuffer* IndexBuffer, const FString& Name)
{
	return IndexBuilder.FindOrConvertIndexAccessor(FGLTFStaticMeshSectionKey(MeshSection, IndexBuffer, Name), *this);
}

void FGLTFContainerBuilder::Serialize(FArchive& Archive)
{
	FGLTFJsonBuffer& JsonBuffer = JsonRoot.Buffers[BufferBuilder.BufferIndex];
	BufferBuilder.UpdateBuffer(JsonBuffer);
	JsonRoot.Serialize(&Archive, true);
}

FGLTFJsonMeshIndex FGLTFContainerBuilder::AddMesh(const UStaticMesh* StaticMesh, int32 LODIndex, const FColorVertexBuffer* OverrideVertexColors)
{
	return IndexBuilder.FindOrConvertMesh(FGLTFStaticMeshKey(StaticMesh, LODIndex, OverrideVertexColors), *this);
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
