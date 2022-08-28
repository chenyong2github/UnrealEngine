// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFZipUtility.h"

THIRD_PARTY_INCLUDES_START
#include "ThirdParty/zlib/zlib-1.2.5/Inc/zlib.h"
#include "ThirdParty/zlib/zlib-1.2.5/Src/contrib/minizip/unzip.h"
THIRD_PARTY_INCLUDES_END

bool FGLTFZipUtility::ExtractToDirectory(const FString& ArchiveFile, const FString& TargetDirectory)
{
	const unzFile ZipHandle = unzOpen64(TCHAR_TO_ANSI(*ArchiveFile));
	if (ZipHandle == nullptr)
	{
		return false;
	}

	if (unzGoToFirstFile(ZipHandle) != UNZ_OK)
	{
		unzClose(ZipHandle);
		return false;
	}

	do
	{
		if (!ExtractCurrentFile(ZipHandle, TargetDirectory))
		{
			unzClose(ZipHandle);
			return false;
		}
	} while (unzGoToNextFile(ZipHandle) == UNZ_OK);

	unzClose(ZipHandle);
	return true;
}

bool FGLTFZipUtility::ExtractCurrentFile(void* ZipHandle, const FString& TargetDirectory)
{
	unz_file_info64 FileInfo;
	if (unzGetCurrentFileInfo64(ZipHandle, &FileInfo, nullptr, 0, nullptr, 0, nullptr, 0) != UNZ_OK)
	{
		return false;
	}

	TArray<char> Filename;
	Filename.Init('\0', FileInfo.size_filename + 1);
	if (unzGetCurrentFileInfo64(ZipHandle, nullptr, Filename.GetData(), Filename.Num() - 1, nullptr, 0, nullptr, 0) != UNZ_OK)
	{
		return false;
	}

	const FString DestinationFilePath = TargetDirectory / Filename.GetData();
	if (DestinationFilePath.EndsWith(TEXT("/")) || DestinationFilePath.EndsWith(TEXT("\\")))
	{
		IFileManager::Get().MakeDirectory(*DestinationFilePath, true);
	}
	else
	{
		FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*DestinationFilePath);
		if (FileWriter == nullptr)
		{
			return false;
		}

		if (unzOpenCurrentFile(ZipHandle) != UNZ_OK)
		{
			return false;
		}

		uint8 ReadBuffer[8192];
		while (const int ReadSize = unzReadCurrentFile(ZipHandle, ReadBuffer, sizeof(ReadBuffer)))
		{
			if (ReadSize < 0)
			{
				unzCloseCurrentFile(ZipHandle);
				return false;
			}

			FileWriter->Serialize(ReadBuffer, ReadSize);
		}

		if (unzCloseCurrentFile(ZipHandle) != UNZ_OK)
		{
			return false;
		}

		FileWriter->Close();
	}

	return true;
}
