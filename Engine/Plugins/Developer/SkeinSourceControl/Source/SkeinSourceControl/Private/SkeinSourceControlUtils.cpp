// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeinSourceControlUtils.h"
#include "ISourceControlModule.h"

namespace SkeinSourceControlConstants
{
	/** The maximum number of files we submit in a single Skein command */
	const int32 MaxFilesPerBatch = 50;
}

namespace SkeinSourceControlUtils
{

// Launch the Skein command line process and extract its results & errors
static bool RunCommandInternalRaw(const FString& InCommand, const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, FString& OutResults, FString& OutErrors)
{
	int32 ReturnCode = 0;

	FString FullCommand;
	FString LoggableCommand; // short version of the command for logging purpose

	// Append the Skein command itself
	LoggableCommand += InCommand;

	// Append to the command all parameters, and then finally the files
	for(const auto& Parameter : InParameters)
	{
		LoggableCommand += TEXT(" ");
		LoggableCommand += Parameter;
	}
	for(const auto& File : InFiles)
	{
		LoggableCommand += TEXT(" \"");
		LoggableCommand += File;
		LoggableCommand += TEXT("\"");
	}

	FullCommand += LoggableCommand;

#if UE_BUILD_DEBUG
	UE_LOG(LogSourceControl, Log, TEXT("RunCommandInternalRaw: 'skein %s'"), *LoggableCommand);
#endif

	FPlatformProcess::ExecProcess(*InSkeinBinaryPath, *FullCommand, &ReturnCode, &OutResults, &OutErrors, *InSkeinProjectRoot);

#if UE_BUILD_DEBUG

	if (OutResults.IsEmpty())
	{
		UE_LOG(LogSourceControl, Log, TEXT("RunCommandInternalRaw: 'OutResults=n/a'"));
	}
	else
	{
		UE_LOG(LogSourceControl, Log, TEXT("RunCommandInternalRaw(%s): OutResults=\n%s"), *InCommand, *OutResults);
	}

	if(ReturnCode != 0)
	{
		if (OutErrors.IsEmpty())
		{
			UE_LOG(LogSourceControl, Warning, TEXT("RunCommandInternalRaw: 'OutErrors=n/a'"));
		}
		else
		{
			UE_LOG(LogSourceControl, Warning, TEXT("RunCommandInternalRaw(%s): OutErrors=\n%s"), *InCommand, *OutErrors);
		}
	}
#endif

	return ReturnCode == 0;
}

// Basic parsing or results & errors from the Skein command line process
static bool RunCommandInternal(const FString& InCommand, const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages)
{
	bool bResult;
	FString Results;
	FString Errors;

	bResult = RunCommandInternalRaw(InCommand, InSkeinBinaryPath, InSkeinProjectRoot, InParameters, InFiles, Results, Errors);
	Results.ParseIntoArray(OutResults, TEXT("\n"), true);
	Errors.ParseIntoArray(OutErrorMessages, TEXT("\n"), true);

	return bResult;
}

}

namespace SkeinSourceControlUtils
{

FString FindSkeinBinaryPath()
{
	FString SkeinBinaryPath;

#if PLATFORM_WINDOWS
	SkeinBinaryPath = FPaths::EngineDir() / TEXT("Binaries") / TEXT("Win64") / TEXT("skein.exe");
#elif PLATFORM_LINUX
	SkeinBinaryPath = FPaths::EngineDir() / TEXT("Binaries") / TEXT("Linux") / TEXT("skein");
#elif PLATFORM_MAC
	SkeinBinaryPath = FPaths::EngineDir() / TEXT("Binaries") / TEXT("Mac") / TEXT("skein");
#endif

	bool bBinaryExists = FPlatformFileManager::Get().GetPlatformFile().FileExists(*SkeinBinaryPath);
	if (bBinaryExists)
		return SkeinBinaryPath;

	return FString();
}

FString FindSkeinProjectRoot(const FString& InPath)
{
	FString PathToSkeinProjectRoot = FPaths::ConvertRelativePathToFull(InPath);
	FPaths::NormalizeDirectoryName(PathToSkeinProjectRoot);

	while (!PathToSkeinProjectRoot.IsEmpty())
	{
		// Look for the "skein.yml" file present at the root of every Skein project
		FString PathToSkeinFile = PathToSkeinProjectRoot / TEXT("skein.yml");
		
		bool bFound = IFileManager::Get().FileExists(*PathToSkeinFile);
		if (bFound)
		{
			// Found it!
			break;
		}
		else
		{
			int32 LastSlashIndex;
			if (PathToSkeinProjectRoot.FindLastChar('/', LastSlashIndex))
			{
				PathToSkeinProjectRoot.LeftInline(LastSlashIndex);
			}
			else
			{
				PathToSkeinProjectRoot.Empty();
			}
		}
	}

	return PathToSkeinProjectRoot;
}

bool IsSkeinAvailable()
{
	FString SkeinBinaryPath = FindSkeinBinaryPath();
	if (SkeinBinaryPath.IsEmpty())
		return false;

	return true;
}

bool IsSkeinProjectFound(const FString& InPath, FString& OutProjectRoot, FString& OutProjectName)
{
	OutProjectRoot = FindSkeinProjectRoot(InPath);
	if (OutProjectRoot.IsEmpty())
		return false;
	
	// SKEIN_TODO: grab project name from skein.yml
	OutProjectName = "SKEIN_TODO";

	return true;
}

bool RunCommand(const FString& InCommand, const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, TArray<FString>& OutResults, TArray<FString>& OutErrorMessages)
{
	bool bResult = true;

	if (InFiles.Num() > SkeinSourceControlConstants::MaxFilesPerBatch)
	{
		// Batch files up so we dont exceed command-line limits
		int32 FileCount = 0;
		while (FileCount < InFiles.Num())
		{
			TArray<FString> FilesInBatch;
			for (int32 FileIndex = 0; FileCount < InFiles.Num() && FileIndex < SkeinSourceControlConstants::MaxFilesPerBatch; FileIndex++, FileCount++)
			{
				FilesInBatch.Add(InFiles[FileCount]);
			}

			TArray<FString> BatchResults;
			TArray<FString> BatchErrors;
			bResult &= RunCommandInternal(InCommand, InSkeinBinaryPath, InSkeinProjectRoot, InParameters, FilesInBatch, BatchResults, BatchErrors);
			OutResults += BatchResults;
			OutErrorMessages += BatchErrors;
		}
	}
	else
	{
		bResult &= RunCommandInternal(InCommand, InSkeinBinaryPath, InSkeinProjectRoot, InParameters, InFiles, OutResults, OutErrorMessages);
	}

	return bResult;
}
	
bool RunUpdateStatus(const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InFiles, TArray<FString>& OutErrorMessages, TArray<FSkeinSourceControlState>& OutStates)
{
	return false;
}

}