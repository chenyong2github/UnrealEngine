// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFBufferBuilder.h"
#include "GLTFMeshBuilder.h"
#include "Misc/Base64.h"

FGLTFBufferBuilder::FGLTFBufferBuilder(FGLTFJsonRoot& JsonRoot)
	: JsonRoot(JsonRoot)
{
	MergedBufferIndex = JsonRoot.Buffers.AddDefaulted(1);
}

FGLTFJsonBufferViewIndex FGLTFBufferBuilder::AddBufferView(const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget)
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

void FGLTFBufferBuilder::UpdateMergedBuffer()
{
	FGLTFJsonBuffer& Buffer = JsonRoot.Buffers[MergedBufferIndex];
	if (Buffer.ByteLength != MergedBufferData.Num())
	{
		const FString DataBase64 = FBase64::Encode(MergedBufferData.GetData(), MergedBufferData.Num());

		Buffer.URI = TEXT("data:application/octet-stream;base64,") + DataBase64;
		Buffer.ByteLength = MergedBufferData.Num();
	}
}
