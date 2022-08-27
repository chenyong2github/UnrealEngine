// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBufferBuilder.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FGLTFBufferBuilder::FGLTFBufferBuilder()
{
	BufferIndex = AddBuffer(FGLTFJsonBuffer());
}

FGLTFJsonBufferViewIndex FGLTFBufferBuilder::AddBufferView(const void* RawData, uint64 ByteLength, const FString& Name, uint8 DataAlignment, EGLTFJsonBufferTarget BufferTarget)
{
	uint64 ByteOffset = BufferData.Num();

	// Data offset must be a multiple of the size of the glTF component type (given by ByteAlignment).
	const uint8 Padding = (DataAlignment - (ByteOffset % DataAlignment)) % DataAlignment;
	if (Padding > 0)
	{
		ByteOffset += Padding;
		BufferData.AddZeroed(Padding);
	}

	BufferData.Append(static_cast<const uint8*>(RawData), ByteLength);

	FGLTFJsonBufferView JsonBufferView;
	JsonBufferView.Name = Name;
	JsonBufferView.Buffer = BufferIndex;
	JsonBufferView.ByteOffset = ByteOffset;
	JsonBufferView.ByteLength = ByteLength;
	JsonBufferView.Target = BufferTarget;

	return FGLTFJsonBuilder::AddBufferView(JsonBufferView);
}

void FGLTFBufferBuilder::UpdateJsonBufferObject(const FString& BinaryFilePath)
{
	FGLTFJsonBuffer& JsonBuffer = JsonRoot.Buffers[BufferIndex];
	JsonBuffer.URI = FPaths::GetCleanFilename(BinaryFilePath);
	JsonBuffer.ByteLength = BufferData.Num();
}

bool FGLTFBufferBuilder::Serialize(FArchive& Archive, const FString& FilePath)
{
	const FString BinaryFilePath = FPaths::ChangeExtension(FilePath, TEXT(".bin"));

	if(!FFileHelper::SaveArrayToFile(BufferData, *BinaryFilePath))
	{
		// TODO: report error
		return false;
	}

	UpdateJsonBufferObject(BinaryFilePath);
	return FGLTFJsonBuilder::Serialize(Archive, FilePath);
}
