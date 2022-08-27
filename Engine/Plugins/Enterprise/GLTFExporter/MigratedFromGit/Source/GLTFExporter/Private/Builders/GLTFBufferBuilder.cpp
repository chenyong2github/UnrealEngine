// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBufferBuilder.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

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
