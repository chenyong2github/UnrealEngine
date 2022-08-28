// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFBuilder.h"
#include "GLTFMeshBuilder.h"
#include "Misc/Base64.h"

FGLTFBuilder::FGLTFBuilder()
	: MergedBufferIndex(JsonRoot.Buffers.AddDefaulted(1))
{
}

FGLTFJsonBufferViewIndex FGLTFBuilder::AddBufferView(const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget)
{
	FGLTFJsonBufferView BufferView;
	BufferView.Name = Name;
	BufferView.Buffer = MergedBufferIndex;
	BufferView.ByteOffset = MergedBufferData.Num();
	BufferView.ByteLength = ByteLength;
	BufferView.Target = BufferTarget;

	MergedBufferData.Append(static_cast<const uint8*>(RawData), ByteLength);

	return JsonRoot.BufferViews.Add(BufferView);
}

void FGLTFBuilder::UpdateMergedBuffer()
{
	FGLTFJsonBuffer& Buffer = JsonRoot.Buffers[MergedBufferIndex];
	if (Buffer.ByteLength != MergedBufferData.Num())
	{
		FString DataBase64 = FBase64::Encode(MergedBufferData.GetData(), MergedBufferData.Num());

		Buffer.URI = TEXT("data:application/octet-stream;base64,") + DataBase64;
		Buffer.ByteLength = MergedBufferData.Num();
	}
}

void FGLTFBuilder::Serialize(FArchive& Archive)
{
	UpdateMergedBuffer();
	JsonRoot.Serialize(&Archive, true);
}

FGLTFJsonMeshIndex FGLTFBuilder::AddMesh(const UStaticMesh* StaticMesh, int32 LODIndex)
{
	return FGLTFMeshBuilder(StaticMesh, LODIndex).AddMesh(*this);
}
