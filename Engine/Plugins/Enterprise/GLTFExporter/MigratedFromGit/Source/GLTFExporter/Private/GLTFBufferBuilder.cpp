// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFBufferBuilder.h"
#include "Misc/Base64.h"

FGLTFBufferBuilder::FGLTFBufferBuilder()
{
	BufferIndex = AddBuffer(FGLTFJsonBuffer());
}

FGLTFJsonBufferViewIndex FGLTFBufferBuilder::AddBufferView(const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget)
{
	FGLTFJsonBufferView JsonBufferView;
	JsonBufferView.Name = Name;
	JsonBufferView.Buffer = BufferIndex;
	JsonBufferView.ByteOffset = BufferData.Num();
	JsonBufferView.ByteLength = ByteLength;
	JsonBufferView.Target = BufferTarget;

	BufferData.Append(static_cast<const uint8*>(RawData), ByteLength);

	return FGLTFJsonBuilder::AddBufferView(JsonBufferView);
}

void FGLTFBufferBuilder::UpdateBuffer()
{
	FGLTFJsonBuffer& JsonBuffer = JsonRoot.Buffers[BufferIndex];
	const int32 ByteLength = BufferData.Num();

	if (JsonBuffer.ByteLength != ByteLength)
	{
		JsonBuffer.ByteLength = ByteLength;

		const FString DataBase64 = FBase64::Encode(BufferData.GetData(), ByteLength);
		JsonBuffer.URI = TEXT("data:application/octet-stream;base64,") + DataBase64;
	}
}

void FGLTFBufferBuilder::Serialize(FArchive& Archive)
{
	UpdateBuffer();
	FGLTFJsonBuilder::Serialize(Archive);
}
