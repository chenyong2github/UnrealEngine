// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFContainerBuilder.h"
#include "GLTFMeshBuilder.h"

FGLTFContainerBuilder::FGLTFContainerBuilder()
	: BufferBuilder(JsonRoot)
{
}

FGLTFJsonAccessorIndex FGLTFContainerBuilder::AddAccessor(const FGLTFJsonAccessor& JsonAccessor)
{
	return JsonRoot.Accessors.Add(JsonAccessor);
}

FGLTFJsonMeshIndex FGLTFContainerBuilder::AddMesh(const FGLTFJsonMesh& JsonMesh)
{
	return JsonRoot.Meshes.Add(JsonMesh);
}

FGLTFJsonNodeIndex FGLTFContainerBuilder::AddNode(const FGLTFJsonNode& JsonNode)
{
	return JsonRoot.Nodes.Add(JsonNode);
}

FGLTFJsonSceneIndex FGLTFContainerBuilder::AddScene(const FGLTFJsonScene& JsonScene)
{
	return JsonRoot.Scenes.Add(JsonScene);
}

FGLTFJsonBufferViewIndex FGLTFContainerBuilder::AddBufferView(const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget)
{
	return BufferBuilder.AddBufferView(RawData, ByteLength, Name, BufferTarget);
}

void FGLTFContainerBuilder::Serialize(FArchive& Archive)
{
	BufferBuilder.UpdateMergedBuffer();
	JsonRoot.Serialize(&Archive, true);
}

FGLTFJsonMeshIndex FGLTFContainerBuilder::AddMesh(const UStaticMesh* StaticMesh, int32 LODIndex)
{
	return FGLTFMeshBuilder(StaticMesh, LODIndex).AddMesh(*this);
}
