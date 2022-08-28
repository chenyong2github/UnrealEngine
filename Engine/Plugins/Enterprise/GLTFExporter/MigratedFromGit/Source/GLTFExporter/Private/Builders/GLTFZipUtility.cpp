// Copyright Epic Games, Inc. All Rights Reserved.

#include "Builders/GLTFZipUtility.h"

THIRD_PARTY_INCLUDES_START
#include "ThirdParty/zlib/zlib-1.2.5/Inc/zlib.h"
#include "ThirdParty/zlib/zlib-1.2.5/Src/contrib/minizip/unzip.h"
THIRD_PARTY_INCLUDES_END

bool FGLTFZipUtility::ExtractToDirectory(const FString& SourceFilePath, const FString& DestinationDirectoryPath, FGLTFMessageBuilder& Builder)
{
	const unzFile ZipFile = unzOpen64(TCHAR_TO_ANSI(*SourceFilePath));
	if (ZipFile == nullptr)
	{
		Builder.AddErrorMessage(TEXT("Can't open zip archive"));
		return false;
	}

	if (const int ErrorCode = unzGoToFirstFile(ZipFile))
	{
		Builder.AddErrorMessage(FString::Printf(TEXT("Can't locate first file in zip archive (error %d)"), ErrorCode));
		unzClose(ZipFile);
		return false;
	}

	do
	{
		if (!ExtractCurrentFile(ZipFile, DestinationDirectoryPath, Builder))
		{
			unzClose(ZipFile);
			return false;
		}
	} while (unzGoToNextFile(ZipFile) == UNZ_OK);

	unzClose(ZipFile);
	return true;
}

bool FGLTFZipUtility::ExtractCurrentFile(void* ZipFile, const FString& DestinationDirectoryPath, FGLTFMessageBuilder& Builder)
{
	unz_file_info64 FileInfo;
	if (const int ErrorCode = unzGetCurrentFileInfo64(ZipFile, &FileInfo, nullptr, 0, nullptr, 0, nullptr, 0))
	{
		Builder.AddErrorMessage(FString::Printf(TEXT("Can't get file info in zip archive (error %d)"), ErrorCode));
		return false;
	}

	TArray<char> Filename;
	Filename.Init('\0', FileInfo.size_filename + 1);
	if (const int ErrorCode = unzGetCurrentFileInfo64(ZipFile, nullptr, Filename.GetData(), Filename.Num() - 1, nullptr, 0, nullptr, 0))
	{
		Builder.AddErrorMessage(FString::Printf(TEXT("Can't get file name in zip archive (error %d)"), ErrorCode));
		return false;
	}

	const FString DestinationFilePath = DestinationDirectoryPath / Filename.GetData();
	if (DestinationFilePath.EndsWith(TEXT("/")) || DestinationFilePath.EndsWith(TEXT("\\")))
	{
		IFileManager::Get().MakeDirectory(*DestinationFilePath, true);
	}
	else
	{
		FArchive* FileWriter = IFileManager::Get().CreateFileWriter(*DestinationFilePath);
		if (FileWriter == nullptr)
		{
			Builder.AddErrorMessage(FString::Printf(TEXT("Can't write to file %s from zip archive"), *DestinationFilePath));
			return false;
		}

		if (const int ErrorCode = unzOpenCurrentFile(ZipFile))
		{
			Builder.AddErrorMessage(FString::Printf(TEXT("Can't open file %s in zip archive (error %d)"), ANSI_TO_TCHAR(Filename.GetData()), ErrorCode));
			return false;
		}

		uint8 ReadBuffer[8192];
		while (const int ReadSize = unzReadCurrentFile(ZipFile, ReadBuffer, sizeof(ReadBuffer)))
		{
			if (ReadSize < 0)
			{
				Builder.AddErrorMessage(FString::Printf(TEXT("Can't read file %s in zip archive (error %d)"), ANSI_TO_TCHAR(Filename.GetData()), ReadSize));
				unzCloseCurrentFile(ZipFile);
				return false;
			}

			FileWriter->Serialize(ReadBuffer, ReadSize);
		}

		if (const int ErrorCode = unzCloseCurrentFile(ZipFile))
		{
			Builder.AddErrorMessage(FString::Printf(TEXT("Can't close file %s in zip archive (error %d)"), ANSI_TO_TCHAR(Filename.GetData()), ErrorCode));
			return false;
		}

		FileWriter->Close();
	}

	return true;
}
