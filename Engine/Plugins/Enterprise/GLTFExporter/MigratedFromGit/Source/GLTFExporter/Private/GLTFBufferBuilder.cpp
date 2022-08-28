// Copyright Epic Games, Inc. All Rights Reserved.

#include "GLTFBufferBuilder.h"
#include "Misc/Base64.h"

inline FGLTFBufferBuilder::FGLTFBufferBuilder(FGLTFJsonRoot& JsonRoot, const FString& Name)
	: JsonRoot(JsonRoot)
	, BufferIndex(JsonRoot.Buffers.AddDefaulted(1))
{
	JsonRoot.Buffers[BufferIndex].Name = Name;
}

inline FGLTFJsonIndex FGLTFBufferBuilder::AppendBufferView(const void* RawData, uint64 ByteLength, const FString& Name, EGLTFJsonBufferTarget BufferTarget)
{
	FGLTFJsonBufferView BufferView;
	BufferView.Name = Name;
	BufferView.Buffer = BufferIndex;
	BufferView.ByteOffset = Data.Num();
	BufferView.ByteLength = ByteLength;
	BufferView.Target = BufferTarget;

	Data.Append(static_cast<const uint8*>(RawData), ByteLength);

	FGLTFJsonIndex BufferViewIndex = JsonRoot.BufferViews.Add(BufferView);
	return BufferViewIndex;
}

void FGLTFBufferBuilder::Close()
{
	FString DataBase64 = FBase64::Encode(Data.GetData(), Data.Num());

	FGLTFJsonBuffer& Buffer = JsonRoot.Buffers[BufferIndex];
	Buffer.ByteLength = Data.Num();
	Buffer.URI = TEXT("data:application/octet-stream;base64,") + DataBase64;
}
