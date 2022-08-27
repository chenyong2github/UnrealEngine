// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFBuilder.h"
#include "GLTFMeshBuilder.h"
#include "Misc/Base64.h"

FGLTFBuilder::FGLTFBuilder()
	: BufferBuilder(JsonRoot)
{
}

FGLTFJsonBufferViewIndex FGLTFBuilder::AddBufferView(const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget)
{
	return BufferBuilder.AddBufferView(RawData, ByteLength, Name, BufferTarget);
}

void FGLTFBuilder::Serialize(FArchive& Archive)
{
	BufferBuilder.UpdateMergedBuffer();
	JsonRoot.Serialize(&Archive, true);
}

FGLTFJsonMeshIndex FGLTFBuilder::AddMesh(const UStaticMesh* StaticMesh, int32 LODIndex)
{
	return FGLTFMeshBuilder(StaticMesh, LODIndex).AddMesh(*this);
}
