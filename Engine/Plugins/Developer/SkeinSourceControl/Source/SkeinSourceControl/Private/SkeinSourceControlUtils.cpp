// Copyright Epic Games, Inc. All Rights Reserved.

#include "SkeinSourceControlUtils.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ScopeLock.h"
#include "ISourceControlModule.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"

namespace SkeinSourceControlConstants
{
	/** The maximum number of files we submit in a single Skein command */
	const int32 MaxFilesPerBatch = 1;
}

namespace SkeinSourceControlUtils
{

// Convert an array of FJsonObject statuses to FSkeinSourceControlStates
static void ParseStatusOutput(const FString& InSkeinProjectRoot, const TArray<FString>& InFiles, const TSharedPtr<FJsonObject> InStates, TArray<FSkeinSourceControlState>& OutStates)
{
	const FDateTime Now = FDateTime::Now();

	// Build map of all states received
	TMap<FString, FString> FileStates;

	const TArray<TSharedPtr<FJsonValue>>& Entries = InStates->GetArrayField("Data");
	for (const TSharedPtr<FJsonValue>& Entry : Entries)
	{
		const TSharedPtr<FJsonObject>* PathStatus;
		if (Entry->TryGetObject(PathStatus))
		{
			FString RelativePath = (*PathStatus)->GetStringField(TEXT("file_path"));
			FString AbsolutePath = FPaths::Combine(InSkeinProjectRoot, RelativePath);
			FString State = (*PathStatus)->GetStringField(TEXT("file_state"));

			FPaths::NormalizeFilename(AbsolutePath);

			FileStates.Add(AbsolutePath, State);
		}
	}

	// Iterate on all files explicitly listed in the command
	for (const auto& File : InFiles)
	{
		FSkeinSourceControlState FileState(File);
		FileState.TimeStamp = Now;
		FileState.State = ESkeinState::Unknown;

		FString* State = FileStates.Find(File);
		if (State != nullptr)
		{
			if (*State == "unknown")
			{
				FileState.State = ESkeinState::Unknown;
			}
			else if (*State == "add")
			{
				FileState.State = ESkeinState::Added;
			}
			else if (*State == "remove")
			{
				FileState.State = ESkeinState::Deleted;
			}
			else if (*State == "modified")
			{
				FileState.State = ESkeinState::Modified;
			}
			else if (*State == "untracked")
			{
				FileState.State = ESkeinState::NotControlled;
			}
			else if (*State == "unchanged")
			{
				FileState.State = ESkeinState::Unchanged;
			}
			else
			{
				checkNoEntry();
			}
		}

		OutStates.Add(FileState);
	}
}

// The Skein command line process returns 'instance already running' when invoked in parallel.
static FCriticalSection RunCriticalSection;

// Launch the Skein command line process and extract its results & errors
static bool RunCommandInternalRaw(const FString& InCommand, const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, FString& OutMessage, TSharedPtr<FJsonObject>& OutData)
{
	FScopeLock Lock(&RunCriticalSection);

	int32 ReturnCode = 0;
	int32 StatusCode = 0;

	FString FullCommand;
	FString LoggableCommand; // short version of the command for logging purpose

	// Append the Skein command itself
	LoggableCommand += InCommand;

	// Append the "--json" param to indicate we want Json output
	LoggableCommand += TEXT(" ");
	LoggableCommand += TEXT("--json");

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

	UE_LOG(LogSourceControl, VeryVerbose, TEXT("RunCommandInternalRaw: 'skein %s'"), *LoggableCommand);

	FString Results;
	FString Errors;

	FPlatformProcess::ExecProcess(*InSkeinBinaryPath, *FullCommand, &ReturnCode, &Results, &Errors, *InSkeinProjectRoot);

	if (!Results.IsEmpty())
	{
		UE_LOG(LogSourceControl, VeryVerbose, TEXT("RunCommandInternalRaw(%s): Results=\n%s"), *InCommand, *Results);
	}
	if (!Errors.IsEmpty())
	{
		UE_LOG(LogSourceControl, VeryVerbose, TEXT("RunCommandInternalRaw(%s): Errors=\n%s"), *InCommand, *Errors);
	}

	// Try to parse either the StdOut or StdErr stream as Json
	FString OutputToParse = Results.IsEmpty() ? Errors : Results;
	if (!OutputToParse.IsEmpty())
	{
		TSharedPtr<FJsonObject> JsonObject;
		TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(*OutputToParse);
		if (FJsonSerializer::Deserialize(JsonReader, JsonObject) && JsonObject.IsValid())
		{
			OutMessage = JsonObject->GetStringField("Message");
			OutData = JsonObject;
			StatusCode = JsonObject->GetIntegerField("Code");
		}
	}

	// If unsuccessful, construct a JsonObject to return
	if (!OutData.IsValid())
	{
		if (Errors.IsEmpty())
		{
			OutMessage = FString::Format(TEXT("Internal error ('{0}')"), { ReturnCode });
		}
		else
		{
			OutMessage = Errors;
		}

		OutData = MakeShared<FJsonObject>();
		OutData->SetBoolField(TEXT("OK"), false);
		OutData->SetNumberField(TEXT("Code"), 500);
		OutData->SetStringField(TEXT("Message"), OutMessage);
		OutData->SetField(TEXT("Data"), MakeShared<FJsonValueNull>());
	}

 	return ReturnCode == 0 && StatusCode == 200;
}

static bool RunCommandInternal(const FString& InCommand, const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, FString& OutMessage, TSharedPtr<FJsonObject>& OutData)
{
	return RunCommandInternalRaw(InCommand, InSkeinBinaryPath, InSkeinProjectRoot, InParameters, InFiles, OutMessage, OutData);
}

static bool RunCommandInternal(const FString& InCommand, const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, FString& OutMessage)
{
	TSharedPtr<FJsonObject> RawData = MakeShared<FJsonObject>();
	return RunCommandInternalRaw(InCommand, InSkeinBinaryPath, InSkeinProjectRoot, InParameters, InFiles, OutMessage, RawData);
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

	FString Marker = TEXT("name:");
	FString Filename = FPaths::Combine(OutProjectRoot, "skein.yml");

	TArray<FString> Lines;
	FFileHelper::LoadFileToStringArrayWithPredicate(Lines, *Filename,
		[&] (const FString& Line)
		{
			return Line.StartsWith(Marker);
		}
	);

	if (Lines.Num() == 1)
	{
		OutProjectName = Lines[0].RightChop(Marker.Len()).TrimStartAndEnd();
	}
	else
	{
		OutProjectName = "Unknown";
	}

	return true;
}

template <typename FunctionType>
bool RunCommandBatched(const FString& InCommand, const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, FunctionType& OutCallback)
{
	int NumErrors = 0;

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

			FString BatchMessage;
			TSharedPtr<FJsonObject> BatchData;
			if (RunCommandInternal(InCommand, InSkeinBinaryPath, InSkeinProjectRoot, InParameters, FilesInBatch, BatchMessage, BatchData))
			{
				OutCallback(true, FilesInBatch, BatchMessage, BatchData);
			}
			else
			{
				OutCallback(false, FilesInBatch, BatchMessage, BatchData);
				++NumErrors;
			}
		}
	}
	else
	{
		FString Message;
		TSharedPtr<FJsonObject> Data;
		if (RunCommandInternal(InCommand, InSkeinBinaryPath, InSkeinProjectRoot, InParameters, InFiles, Message, Data))
		{
			OutCallback(true, InFiles, Message, Data);
		}
		else
		{
			OutCallback(false, InFiles, Message, Data);
			++NumErrors;
		}
	}

	return (NumErrors == 0);
}

bool RunCommand(const FString& InCommand, const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InParameters, const TArray<FString>& InFiles, TArray<FString>& OutResults, TArray<FString>& OutErrors)
{
	auto Callback = 
		[&] (bool bBatchResult, const TArray<FString>& BatchFiles, const FString& BatchMessage, const TSharedPtr<FJsonObject>& BatchData)
		{
			if (bBatchResult)
			{
				OutResults.Add(BatchMessage);
			}
			else
			{
				OutErrors.Add(BatchMessage);
			}
		};

	return RunCommandBatched(InCommand, InSkeinBinaryPath, InSkeinProjectRoot, InParameters, InFiles, Callback);
}
	
bool RunUpdateStatus(const FString& InSkeinBinaryPath, const FString& InSkeinProjectRoot, const TArray<FString>& InFiles, TArray<FString>& OutErrors, TArray<FSkeinSourceControlState>& OutStates)
{
	auto Callback = 
		[&] (bool bBatchResult, const TArray<FString>& BatchFiles, const FString& BatchMessage, const TSharedPtr<FJsonObject>& BatchData)
		{
			if (bBatchResult)
			{
				ParseStatusOutput(InSkeinProjectRoot, InFiles, BatchData, OutStates);
			}
			else
			{
				OutErrors.Add(BatchMessage);
			}
		};

	TArray<FString> Paths;
	Paths.Add(InSkeinProjectRoot);

	return RunCommandBatched(TEXT("projects status"), InSkeinBinaryPath, InSkeinProjectRoot, TArray<FString>(), Paths, Callback);
}
	
}