// Copyright Epic Games, Inc. All Rights Reserved.

#include "MVR/DMXMVRUnzip.h"

#include "DMXRuntimeLog.h"

#include "GenericPlatform/GenericPlatform.h"
#include "Misc/Base64.h"
#include "Misc/Compression.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"


TSharedPtr<FDMXMVRUnzip> FDMXMVRUnzip::CreateFromData(const uint8* DataPtr, const int64 DataNum)
{
	const TSharedRef<FDMXMVRUnzip> NewMVRUnzip = MakeShared<FDMXMVRUnzip>();
	if (NewMVRUnzip->InitializeFromDataInternal(DataPtr, DataNum))
	{
		return NewMVRUnzip;
	}

	return nullptr;
}

TSharedPtr<FDMXMVRUnzip> FDMXMVRUnzip::CreateFromFile(const FString& FilePathAndName)
{
	const TSharedRef<FDMXMVRUnzip> NewMVRUnzip = MakeShared<FDMXMVRUnzip>();
	if (NewMVRUnzip->InitializeFromFileInternal(FilePathAndName))
	{
		return NewMVRUnzip;
	}

	return nullptr;
}

bool FDMXMVRUnzip::GetFileContent(const FString& FilenameInZip, TArray64<uint8>& OutData)
{
	uint32* Offset = OffsetsMap.Find(FilenameInZip);
	if (!Offset)
	{
		return false;
	}

	constexpr uint64 LocalEntryMinSize = 30;

	if (*Offset + LocalEntryMinSize > Data.Num())
	{
		return false;
	}

	uint16 Compression = 0;
	uint32 CompressedSize;
	uint32 UncompressedSize = 0;
	uint16 FilenameLen = 0;
	uint16 ExtraFieldLen = 0;

	constexpr uint8 CompressionOffset = 8;
	Data.Seek(*Offset + CompressionOffset);
	Data << Compression;

	constexpr uint8 CompressionSizeOffset = 18;
	Data.Seek(*Offset + CompressionSizeOffset);
	Data << CompressedSize;
	Data << UncompressedSize;
	Data << FilenameLen;
	Data << ExtraFieldLen;

	if (*Offset + LocalEntryMinSize + FilenameLen + ExtraFieldLen + CompressedSize > Data.Num())
	{
		return false;
	}

	if (Compression == 8)
	{
		OutData.AddUninitialized(UncompressedSize);
		if (!FCompression::UncompressMemory(NAME_Zlib, OutData.GetData(), UncompressedSize, Data.GetData() + *Offset + LocalEntryMinSize + FilenameLen + ExtraFieldLen, CompressedSize, COMPRESS_NoFlags, -15))
		{
			return false;
		}
	}
	else if (Compression == 0 && CompressedSize == UncompressedSize)
	{
		OutData.Append(Data.GetData() + *Offset + LocalEntryMinSize + FilenameLen + ExtraFieldLen, UncompressedSize);
	}
	else
	{
		return false;
	}

	return true;
}

bool FDMXMVRUnzip::Contains(const FString& FilenameInZip) const
{
	return OffsetsMap.Contains(FilenameInZip);
}

FString FDMXMVRUnzip::GetFirstFilenameByExtension(const FString& Extension) const
{
	for (const TPair<FString, uint32>& Pair : OffsetsMap)
	{
		if (Pair.Key.EndsWith(Extension, ESearchCase::IgnoreCase))
		{
			return Pair.Key;
		}
	}

	return "";
}

FDMXMVRUnzip::FDMXScopedUnzipToTempFile::FDMXScopedUnzipToTempFile(const TSharedRef<FDMXMVRUnzip>& MVRUnzip, const FString& FilenameInZip)
{
	if (!ensureMsgf(MVRUnzip->Contains(FilenameInZip), TEXT("Tried to unzip %s from MVR, but zip does not contain the file.")))
	{
		return;
	}
	TArray64<uint8> FileData;
	MVRUnzip->GetFileContent(FilenameInZip, FileData);

	const FString Directory = FPaths::EngineSavedDir() / TEXT("DMXMVR");
	TempFilePathAndName = FPaths::ConvertRelativePathToFull(Directory / FilenameInZip);

	FFileHelper::SaveArrayToFile(FileData, *TempFilePathAndName);
}

FDMXMVRUnzip::FDMXScopedUnzipToTempFile::~FDMXScopedUnzipToTempFile()
{
	// Delete the extracted file
	IFileManager& FileManager = IFileManager::Get();
	constexpr bool bRequireExists = true;
	constexpr bool bEvenIfReadOnly = false;
	constexpr bool bQuiet = true;
	FileManager.Delete(*TempFilePathAndName, bRequireExists, bEvenIfReadOnly, bQuiet);
}

bool FDMXMVRUnzip::InitializeFromDataInternal(const uint8* DataPtr, const int64 DataNum)
{
	Data.Append(DataPtr, DataNum);

	// Step 0: retrieve the trailer magic
	TArray<uint8> Magic;
	bool bIndexFound = false;
	int64 Index = 0;
	for (Index = Data.Num() - 1; Index >= 0; Index--)
	{
		Magic.Insert(Data[Index], 0);
		if (Magic.Num() == 4)
		{
			if (Magic[0] == 0x50 && Magic[1] == 0x4b && Magic[2] == 0x05 && Magic[3] == 0x06)
			{
				bIndexFound = true;
				break;
			}
			Magic.Pop();
		}
	}

	if (!bIndexFound)
	{
		return false;
	}

	uint16 DiskEntries = 0;
	uint16 TotalEntries = 0;
	uint32 CentralDirectorySize = 0;
	uint32 CentralDirectoryOffset = 0;
	uint16 CommentLen = 0;

	constexpr uint64 TrailerMinSize = 22;
	constexpr uint64 CentralDirectoryMinSize = 46;

	if (Index + TrailerMinSize > Data.Num())
	{
		return false;
	}

	// Skip signature and disk data
	Data.Seek(Index + 8);
	Data << DiskEntries;
	Data << TotalEntries;
	Data << CentralDirectorySize;
	Data << CentralDirectoryOffset;
	Data << CommentLen;

	uint16 DirectoryEntries = FMath::Min(DiskEntries, TotalEntries);

	for (uint16 DirectoryIndex = 0; DirectoryIndex < DirectoryEntries; DirectoryIndex++)
	{
		if (CentralDirectoryOffset + CentralDirectoryMinSize > Data.Num())
		{
			return false;
		}

		uint16 FilenameLen = 0;
		uint16 ExtraFieldLen = 0;
		uint16 EntryCommentLen = 0;
		uint32 EntryOffset = 0;

		// seek to FilenameLen
		Data.Seek(CentralDirectoryOffset + 28);
		Data << FilenameLen;
		Data << ExtraFieldLen;
		Data << EntryCommentLen;
		// seek to EntryOffset
		Data.Seek(CentralDirectoryOffset + 42);
		Data << EntryOffset;

		if (CentralDirectoryOffset + CentralDirectoryMinSize + FilenameLen + ExtraFieldLen + EntryCommentLen > Data.Num())
		{
			return false;
		}

		TArray64<uint8> FilenameBytes;
		FilenameBytes.Append(Data.GetData() + CentralDirectoryOffset + CentralDirectoryMinSize, FilenameLen);
		FilenameBytes.Add(0);

		FString Filename = FString(UTF8_TO_TCHAR(FilenameBytes.GetData()));

		OffsetsMap.Add(Filename, EntryOffset);

		CentralDirectoryOffset += CentralDirectoryMinSize + FilenameLen + ExtraFieldLen + EntryCommentLen;
	}

	return true;
}

bool FDMXMVRUnzip::InitializeFromFileInternal(const FString& FilePathAndName)
{
	if (!FPaths::FileExists(FilePathAndName))
	{
		UE_LOG(LogDMXRuntime, Error, TEXT("Failed to load '%s'. File does not exists."), *FilePathAndName);
		return false;
	}

	TArray<uint8> NewData;
	if (!FFileHelper::LoadFileToArray(NewData, *FilePathAndName))
	{
		UE_LOG(LogDMXRuntime, Error, TEXT("Faild to load '%s'. Access denied."), *FilePathAndName);
		return false;
	}

	return InitializeFromDataInternal(NewData.GetData(), NewData.Num());
}


