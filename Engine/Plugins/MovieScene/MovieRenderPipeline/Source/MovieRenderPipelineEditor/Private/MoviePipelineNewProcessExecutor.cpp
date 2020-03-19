// Copyright Epic Games, Inc. All Rights Reserved.
#include "MoviePipelineNewProcessExecutor.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/Formatters/JsonArchiveOutputFormatter.h"
#include "Serialization/StructuredArchive.h"
#include "MoviePipelineQueue.h"
#include "ObjectTools.h"
#include "MoviePipelineInProcessExecutorSettings.h"
#include "MovieRenderPipelineCoreModule.h"
#include "FileHelpers.h"
#include "MovieRenderPipelineSettings.h"
#include "PackageHelperFunctions.h"
#include "UnrealEdMisc.h"
#include "HAL/PlatformFilemanager.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"

#define LOCTEXT_NAMESPACE "MoviePipelineNewProcessExecutor"

void UMoviePipelineNewProcessExecutor::ExecuteImpl(UMoviePipelineQueue* InPipelineQueue)
{
	if (InPipelineQueue->GetJobs().Num() == 0)
	{
		OnExecutorFinishedImpl();
		return;
	}

	// Because it's run in a new process it will load packages from disk, thus we have to save changes to see them.
	bool bPromptUserToSave = true;
	bool bSaveMapPackages = true;
	bool bSaveContentPackages = true;
	if (!FEditorFileUtils::SaveDirtyPackages(bPromptUserToSave, bSaveMapPackages, bSaveContentPackages))
	{
		OnExecutorFinishedImpl();
		return;
	}

	// Make sure all of the maps in the queue exist on disk somewhere, otherwise the remote process boots up and then fails.
	bool bHasValidMap = true;
	for (const UMoviePipelineExecutorJob* Job : InPipelineQueue->GetJobs())
	{
		FString PackageName = Job->Map.GetLongPackageName();
		if (!FPackageName::IsValidLongPackageName(PackageName))
		{
			bHasValidMap = false;
			break;
		}
	}

	if (!bHasValidMap)
	{
		FText FailureReason = LOCTEXT("UnsavedMapFailureDialog", "One or more jobs in the queue have an unsaved map as their target map. These unsaved maps cannot be loaded by an external process, and the render has been aborted.");
		FMessageDialog::Open(EAppMsgType::Ok, FailureReason);

		OnExecutorFinishedImpl();
		return;
	}


	if (!ensureMsgf(!ProcessHandle.IsValid(), TEXT("Attempted to start another New Process Executor without the last one quitting. Force killing. This executor only supports one at a time.")))
	{
		FPlatformProcess::TerminateProc(ProcessHandle, true);
	}

	// Place the Queue in a package and serialize it to disk so we can pass their dynamic object
	// to another process without having to save/check in/etc.
	FString InFileName = TEXT("QueueManifest");
	FString InPackagePath = TEXT("/Engine/MovieRenderPipeline/Editor/Transient");

	FString FixedAssetName = ObjectTools::SanitizeObjectName(InFileName);
	FString NewPackageName = FPackageName::GetLongPackagePath(InPackagePath) + TEXT("/") + FixedAssetName; 

	// If there's already a package with this name, rename it so that the newly created one can always get a fixed name.
	// The fixed name is important because in the new process it'll start the unique name count over.
	if (UPackage* OldPackage = FindObject<UPackage>(nullptr, *NewPackageName))
	{
		FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UPackage::StaticClass(), "DEAD_NewProcessExecutor_SerializedPackage");
		OldPackage->Rename(*UniqueName.ToString());
		OldPackage->SetFlags(RF_Transient);
	}

	UPackage* NewPackage = CreatePackage(nullptr, *NewPackageName);

	// Duplicate the Queue into this package as we don't want to just rename the existing that belongs to the editor subsystem.
	UMoviePipelineQueue* DuplicatedQueue = CastChecked<UMoviePipelineQueue>(StaticDuplicateObject(InPipelineQueue, NewPackage));
	DuplicatedQueue->SetFlags(RF_Public | RF_Transactional | RF_Standalone);

	// Save the package to disk.
	FString ManifestFileName = TEXT("MovieRenderPipeline/QueueManifest") + FPackageName::GetTextAssetPackageExtension();
	FString ManifestFilePath = FPaths::ProjectSavedDir() / ManifestFileName;
	if (!SavePackageHelper(NewPackage, *ManifestFilePath))
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("Could not save manifest package to disk. Path: %s"), *ManifestFilePath);
		OnExecutorFinishedImpl();
		return;
	}
	NewPackage->SetFlags(RF_Transient);
	NewPackage->ClearFlags(RF_Standalone);
	DuplicatedQueue->SetFlags(RF_Transient);
	DuplicatedQueue->ClearFlags(RF_Public | RF_Transactional | RF_Standalone);

	// Arguments to pass to the executable. This can be modified by settings in the event a setting needs to be applied early. In the format of -foo -bar
	FString CommandLineArgs;
	// In the format of ?arg=1?arg=2. This is appended after the the map name.
	FString UnrealURLParams;

	// Append all of our inherited command line arguments from the editor.
	const UMoviePipelineInProcessExecutorSettings* ExecutorSettings = GetDefault<UMoviePipelineInProcessExecutorSettings>();

	CommandLineArgs += ExecutorSettings->InheritedCommandLineArguments;
	CommandLineArgs += TEXT(" ") + ExecutorSettings->AdditionalCommandLineArguments;

	// Provide our own default arguments
	CommandLineArgs += FString::Printf(TEXT(" -messaging -SessionName=\"%s\""), TEXT("NewProcess Movie Render"));
	CommandLineArgs += TEXT(" -nohmd");
	CommandLineArgs += TEXT(" -windowed");
	CommandLineArgs += FString::Printf(TEXT(" -ResX=%d -ResY=%d"), 1280, 720);

	// Loop through our settings in the job and let them modify the command line arguments/params. Because we could have multiple jobs,
	// we go through all jobs and all settings and hope the user doesn't have conflicting settings.
	for (const UMoviePipelineExecutorJob* Job : DuplicatedQueue->GetJobs())
	{
		Job->GetConfiguration()->InitializeTransientSettings();
		for (const UMoviePipelineSetting* Setting : Job->GetConfiguration()->GetAllSettings())
		{
			Setting->BuildNewProcessCommandLine(UnrealURLParams, CommandLineArgs);
		}
	}

	FString GameNameOrProjectFile;
	if (FPaths::IsProjectFilePathSet())
	{
		GameNameOrProjectFile = FString::Printf(TEXT("\"%s\""), *FPaths::GetProjectFilePath());
	}
	else
	{
		GameNameOrProjectFile = FApp::GetProjectName();
	}

	FString MoviePipelineArgs;
	{
		FString PipelineConfig;
#if 0
		if (true)
		{
			// Due to API limitations we can't convert package -> text directly and instead need to re-load it, escape it, and then put it onto the command line :-)
			FString OutString;
			if (FFileHelper::LoadFileToString(OutString, *ManifestFilePath))
			{
				// Sanitize the string so we can pass it on the command line. We wrap the whole thing with quotes too in case newlines.
				PipelineConfig = FString::Printf(TEXT("\"%s\""), *OutString.ReplaceCharWithEscapedChar());
			}
			else
			{
				UE_LOG(LogMovieRenderPipeline, Error, TEXT("Failed to load manifest file from path: %s"), *ManifestFilePath);
				return;
			}
		}
		else
#endif
		{
			// We will pass the path to the saved manifest file on the command line and parse it on the other end from disk.
			PipelineConfig = ManifestFilePath;
		}

		// Because the Queue has multiple jobs in it, we don't need to pass which sequence to render. That's only needed if you're rendering a 
		// specific sequence with a specific master config.
		MoviePipelineArgs = FString::Printf(TEXT("-MoviePipelineConfig=\"%s\""), *PipelineConfig); // -MoviePipeline=\"%s\" -MoviePipelineLocalExecutorClass=\"%s\" -MoviePipelineClass=\"%s\""),
	}

	TMap<FString, FStringFormatArg> NamedArguments;
	NamedArguments.Add(TEXT("GameNameOrProjectFile"), GameNameOrProjectFile);
	NamedArguments.Add(TEXT("PlayWorld"), DuplicatedQueue->GetJobs()[0]->Map.GetAssetPathString()); // Boot up on the first job's intended map.
	NamedArguments.Add(TEXT("UnrealURL"), UnrealURLParams); // Boot up on the first job's intended map.
	NamedArguments.Add(TEXT("SubprocessCommandLine"), FCommandLine::GetSubprocessCommandline());
	NamedArguments.Add(TEXT("CommandLineParams"), CommandLineArgs);
	NamedArguments.Add(TEXT("MoviePipelineArgs"), MoviePipelineArgs);

	FString FinalCommandLine = FString::Format(
		TEXT("{GameNameOrProjectFile} {PlayWorld}{UnrealURL} -game {SubprocessCommandLine} {CommandLineParams} {MoviePipelineArgs}"), NamedArguments);

	// Prefer the -Cmd version of the executable if possible, gracefully falling back to the normal one. This is to help
	// with user education about the -Cmd version which allows piping the output log to the cmd window that launched it.
	const FString ExecutablePath = FUnrealEdMisc::Get().GetExecutableForCommandlets();
		
	uint32 ProcessID = 0;
	const bool bLaunchDetatched = true;
	const bool bLaunchMinimized = false;
	const bool bLaunchWindowHidden = false;
	const uint32 PriorityModifier = 0;

	UE_LOG(LogMovieRenderPipeline, Log, TEXT("Launching a new process to render with the following command line:"));
	UE_LOG(LogMovieRenderPipeline, Log, TEXT("%s %s"), *ExecutablePath, *FinalCommandLine);
	
	ProcessHandle = FPlatformProcess::CreateProc(
		*ExecutablePath, *FinalCommandLine, bLaunchDetatched,
		bLaunchMinimized, bLaunchWindowHidden, &ProcessID,
		PriorityModifier, nullptr, nullptr, nullptr);

	if (!ProcessHandle.IsValid())
	{
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Failed to launch executable for new process render. Executable Path: \"%s\" Command Line: \"%s\""), FPlatformProcess::ExecutablePath(), *FinalCommandLine);
		// OnPipelineErrored(nullptr, true, LOCTEXT("ProcessFailedToLaunch", "New Process failed to launch. See log for command line argument used."));
		// OnIndividualPipelineFinished(nullptr);
	}
	else
	{
		if (ExecutorSettings->bCloseEditor)
		{
			FPlatformMisc::RequestExit(false);
		}
		else
		{
			// Register a tick handler to listen every frame to see if the process shut down gracefully, we'll use return codes to tell success vs cancel.
			FCoreDelegates::OnBeginFrame.AddUObject(this, &UMoviePipelineNewProcessExecutor::CheckForProcessFinished);
		}
	}
}

void UMoviePipelineNewProcessExecutor::CheckForProcessFinished()
{
	if (!ensureMsgf(ProcessHandle.IsValid(), TEXT("CheckForProcessFinished called without a valid process handle. This should only be called if the process was originally valid!")))
	{
		return;
	}

	int32 ReturnCode;
	if (FPlatformProcess::GetProcReturnCode(ProcessHandle, &ReturnCode))
	{
		ProcessHandle.Reset();
		FCoreDelegates::OnBeginFrame.RemoveAll(this);

		// Log an error for now
		if (ReturnCode != 0)
		{
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("Process exited with non-success return code. Return Code; %d"), ReturnCode);
			// OnPipelineErrored(nullptr, true, LOCTEXT("ProcessNonZeroReturn", "Non-success return code returned. See log for Return Code."));
		}

		OnExecutorFinishedImpl();
	}
	else
	{
		// Process is still running, spin wheels.
	}
}
#undef LOCTEXT_NAMESPACE // "MoviePipelineNewProcessExecutor"
