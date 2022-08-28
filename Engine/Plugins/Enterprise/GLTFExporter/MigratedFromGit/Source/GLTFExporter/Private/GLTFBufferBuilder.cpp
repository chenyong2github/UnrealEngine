// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFBufferBuilder.h"
#include "GLTFMeshBuilder.h"
#include "Misc/Base64.h"

FGLTFBufferBuilder::FGLTFBufferBuilder(FGLTFJsonBufferIndex BufferIndex)
	: BufferIndex(BufferIndex)
{
}

FGLTFJsonBufferViewIndex FGLTFBufferBuilder::AddBufferView(FGLTFContainerBuilder& Container, const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget)
{
	FGLTFJsonBufferView BufferView;
	BufferView.Name = Name;
	BufferView.Buffer = BufferIndex;
	BufferView.ByteOffset = BufferData.Num();
	BufferView.ByteLength = ByteLength;
	BufferView.Target = BufferTarget;

	BufferData.Append(static_cast<const uint8*>(RawData), ByteLength);

	return Container.AddBufferView(BufferView);
}

void FGLTFBufferBuilder::UpdateBuffer(FGLTFJsonBuffer& JsonBuffer)
{
	const int32 ByteLength = BufferData.Num();
	if (JsonBuffer.ByteLength != ByteLength)
	{
		JsonBuffer.ByteLength = ByteLength;
		
		const FString DataBase64 = FBase64::Encode(BufferData.GetData(), ByteLength);
		JsonBuffer.URI = TEXT("data:application/octet-stream;base64,") + DataBase64;
	}
}
