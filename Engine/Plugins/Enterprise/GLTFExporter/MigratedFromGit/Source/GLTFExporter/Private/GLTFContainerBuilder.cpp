// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFContainerBuilder.h"
#include "GLTFMeshBuilder.h"

FGLTFContainerBuilder::FGLTFContainerBuilder()
	: BufferBuilder(JsonRoot)
{
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
