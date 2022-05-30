// Copyright Epic Games, Inc. All Rights Reserved.

#include "WorldPartition/WorldPartitionFileLogger.h"

#if WITH_EDITOR
#include "HAL/FileManager.h"
#endif

#if WITH_EDITOR

FWorldPartitionFileLogger::FWorldPartitionFileLogger(const FString& InLogFilename)
	: IndentationCount(0)
{
	LogFile = IFileManager::Get().CreateFileWriter(*InLogFilename);
}

FWorldPartitionFileLogger::~FWorldPartitionFileLogger()
{
	if (LogFile)
	{
		LogFile->Close();
		delete LogFile;
	}
}

void FWorldPartitionFileLogger::WriteLine(const FString& InLine, bool bIndent /* = false*/)
{
	if (LogFile)
	{
		TStringBuilder<512> Builder;
		for (int i = 0; i < IndentationCount - 1; ++i)
		{
			Builder += TEXT(" |  ");
		}
		if (IndentationCount > 0)
		{
			Builder += TEXT(" |- ");
		}
		if (bIndent)
		{
			Builder += TEXT("[+] ");
			++IndentationCount;
		}
		Builder += InLine;
		Builder += LINE_TERMINATOR;
		FString LineEntry = Builder.ToString();
		LogFile->Serialize(TCHAR_TO_ANSI(*LineEntry), LineEntry.Len());
	}
}

void FWorldPartitionFileLogger::DecrementIndentation()
{
	if (LogFile)
	{
		check(IndentationCount);
		--IndentationCount;
	}
}

#endif