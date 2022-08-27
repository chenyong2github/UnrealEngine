// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFContainerBuilder.h"

void FGLTFContainerBuilder::Write(FArchive& Archive, FFeedbackContext* Context)
{
	CompleteAllTasks(Context);

	if (bIsGlbFile)
	{
		WriteGlb(Archive);
	}
	else
	{
		WriteJson(Archive);
	}

	const TSet<EGLTFJsonExtension> CustomExtensions = GetCustomExtensionsUsed();
	if (CustomExtensions.Num() > 0)
	{
		const FString ExtensionsString = FString::JoinBy(CustomExtensions, TEXT(", "),
			[](EGLTFJsonExtension Extension)
		{
			return FGLTFJsonUtility::GetValue(Extension);
		});

		LogWarning(FString::Printf(TEXT("Export uses some extensions that may only be supported in Unreal's glTF viewer: %s"), *ExtensionsString));
	}
}

FGLTFContainerBuilder::FGLTFContainerBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions, bool bSelectedActorsOnly)
	: FGLTFConvertBuilder(FilePath, ExportOptions, bSelectedActorsOnly)
{
}

void FGLTFContainerBuilder::WriteGlb(FArchive& Archive)
{
	FBufferArchive JsonData;
	WriteJson(JsonData);

	const FBufferArchive* BufferData = GetBufferData();
	WriteGlb(Archive, &JsonData, BufferData);
}

void FGLTFContainerBuilder::WriteGlb(FArchive& Archive, const FBufferArchive* JsonData, const FBufferArchive* BinaryData)
{
	const uint32 JsonChunkType = 0x4E4F534A; // "JSON" in ASCII
	const uint32 BinaryChunkType = 0x004E4942; // "BIN" in ASCII
	uint32 FileSize =
		3 * sizeof(uint32) +
		2 * sizeof(uint32) + GetPaddedChunkSize(JsonData->Num());

	if (BinaryData != nullptr)
	{
		FileSize += 2 * sizeof(uint32) + GetPaddedChunkSize(BinaryData->Num());
	}

	WriteHeader(Archive, FileSize);
	WriteChunk(Archive, JsonChunkType, *JsonData, 0x20);

	if (BinaryData != nullptr)
	{
		WriteChunk(Archive, BinaryChunkType, *BinaryData, 0x0);
	}
}

void FGLTFContainerBuilder::WriteHeader(FArchive& Archive, uint32 FileSize)
{
	const uint32 FileSignature = 0x46546C67; // "glTF" in ASCII
	const uint32 FileVersion = 2;

	WriteInt(Archive, FileSignature);
	WriteInt(Archive, FileVersion);
	WriteInt(Archive, FileSize);
}

void FGLTFContainerBuilder::WriteChunk(FArchive& Archive, uint32 ChunkType, const TArray<uint8>& ChunkData, uint8 ChunkTrailingByte)
{
	const uint32 ChunkLength = GetPaddedChunkSize(ChunkData.Num());
	const uint32 ChunkTrailing = GetTrailingChunkSize(ChunkData.Num());

	WriteInt(Archive, ChunkLength);
	WriteInt(Archive, ChunkType);
	WriteData(Archive, ChunkData);
	WriteFill(Archive, ChunkTrailing, ChunkTrailingByte);
}

void FGLTFContainerBuilder::WriteInt(FArchive& Archive, uint32 Value)
{
	Archive.SerializeInt(Value, MAX_uint32);
}

void FGLTFContainerBuilder::WriteData(FArchive& Archive, const TArray<uint8>& Data)
{
	Archive.Serialize(const_cast<uint8*>(Data.GetData()), Data.Num());
}

void FGLTFContainerBuilder::WriteFill(FArchive& Archive, int32 Size, uint8 Value)
{
	while (--Size >= 0)
	{
		Archive.Serialize(&Value, sizeof(Value));
	}
}

int32 FGLTFContainerBuilder::GetPaddedChunkSize(int32 Size)
{
	return (Size + 3) & ~3;
}

int32 FGLTFContainerBuilder::GetTrailingChunkSize(int32 Size)
{
	return (4 - (Size & 3)) & 3;
}
