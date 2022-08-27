// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFBufferBuilder.h"
#include "Builders/GLTFMemoryArchive.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

FGLTFBufferBuilder::FGLTFBufferBuilder(const FString& FilePath, const UGLTFExportOptions* ExportOptions)
	: FGLTFJsonBuilder(FilePath, ExportOptions)
{
}

FGLTFBufferBuilder::~FGLTFBufferBuilder()
{
	if (BufferArchive != nullptr)
	{
		BufferArchive->Close();
	}
}

bool FGLTFBufferBuilder::InitializeBuffer()
{
	JsonBuffer = FGLTFJsonBuilder::AddBuffer();

	if (bIsGlbFile)
	{
		BufferArchive = MakeUnique<FGLTFMemoryArchive>();
	}
	else
	{
		const FString ExternalBinaryPath = FPaths::ChangeExtension(FilePath, TEXT(".bin"));
		JsonBuffer->URI = FPaths::GetCleanFilename(ExternalBinaryPath);

		BufferArchive.Reset(IFileManager::Get().CreateFileWriter(*ExternalBinaryPath));
		if (BufferArchive == nullptr)
		{
			LogError(FString::Printf(TEXT("Failed to write external binary buffer to file: %s"), *ExternalBinaryPath));
			return false;
		}
	}

	return true;
}

const TArray64<uint8>* FGLTFBufferBuilder::GetBufferData() const
{
	return bIsGlbFile ? static_cast<FGLTFMemoryArchive*>(BufferArchive.Get()) : nullptr;
}

FGLTFJsonBufferView* FGLTFBufferBuilder::AddBufferView(const void* RawData, uint64 ByteLength, EGLTFJsonBufferTarget BufferTarget, uint8 DataAlignment)
{
	if (BufferArchive == nullptr && !InitializeBuffer())
	{
		// TODO: report error
		return nullptr;
	}

	uint64 ByteOffset = BufferArchive->Tell();

	// Data offset must be a multiple of the size of the glTF component type (given by ByteAlignment).
	const int64 Padding = (DataAlignment - (ByteOffset % DataAlignment)) % DataAlignment;
	if (Padding > 0)
	{
		ByteOffset += Padding;
		BufferArchive->Seek(ByteOffset);
	}

	BufferArchive->Serialize(const_cast<void*>(RawData), ByteLength);
	JsonBuffer->ByteLength = BufferArchive->Tell();

	FGLTFJsonBufferView* JsonBufferView = FGLTFJsonBuilder::AddBufferView();
	JsonBufferView->Buffer = JsonBuffer;
	JsonBufferView->ByteOffset = ByteOffset;
	JsonBufferView->ByteLength = ByteLength;
	JsonBufferView->Target = BufferTarget;

	return JsonBufferView;
}
