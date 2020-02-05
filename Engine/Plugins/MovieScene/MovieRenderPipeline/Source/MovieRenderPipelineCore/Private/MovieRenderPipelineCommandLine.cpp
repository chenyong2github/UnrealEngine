// Copyright Epic Games, Inc. All Rights Reserved.

#include "MovieRenderPipelineCoreModule.h"
#include "Modules/ModuleInterface.h"
#include "Misc/CommandLine.h"
#include "Modules/ModuleManager.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipeline.h"
#include "MoviePipelineInProcessExecutor.h"
#include "LevelSequence.h"
#include "MoviePipelineMasterConfig.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MoviePipelineQueue.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "UObject/UObjectHash.h"
#include "ObjectTools.h"

#if WITH_EDITOR
//#include "Editor.h"
#endif

void FMovieRenderPipelineCoreModule::InitializeCommandLineMovieRender()
{
#if WITH_EDITOR
	//const bool bIsGameMode = !GEditor;
	//if (!bIsGameMode)
	//{
	//	UE_LOG(LogMovieRenderPipeline, Fatal, TEXT("Command Line Renders must be performed in -game mode, otherwise use the editor ui/python and PIE. Add -game to your command line arguments."));
	//	FPlatformMisc::RequestExitWithStatus(false, MoviePipelineErrorCodes::Critical);
	//	return;
	//}
#endif

	// Attempt to convert their command line arguments into the required objects.
	UMoviePipelineExecutorBase* ExecutorBase = nullptr;
	UMoviePipelineQueue* Queue = nullptr;

	uint8 ReturnCode = ParseMovieRenderData(SequenceAssetValue, SettingsAssetValue, MoviePipelineLocalExecutorClassType, MoviePipelineClassType, 
		/*Out*/ Queue, /*Out*/ ExecutorBase);
	if (!ensureMsgf(ExecutorBase && Queue, TEXT("There was a failure parsing the command line and a movie render cannot be started. Check the log for more details.")))
	{
		// Take the failure return code from the detection of our command line arguments.
		FPlatformMisc::RequestExitWithStatus(/*Force*/ false, /*ReturnCode*/ ReturnCode);
		return;
	}
	else
	{
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("Successfully detected and loaded required movie arguments. Rendering will begin once the map is loaded."));
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("NumJobs: %d ExecutorClass: %s PipelineClass: %s"), Queue->GetJobs().Num(), *ExecutorBase->GetPathName(), *MoviePipelineClassType);
	}

	// We add the Executor to the root set. It will own all of the configuration data so this keeps it nicely in memory until finished,
	// and means we only have to add/remove one thing from the root set, everything else uses normal outer ownership.
	ExecutorBase->AddToRoot();
	ExecutorBase->OnExecutorFinished().AddRaw(this, &FMovieRenderPipelineCoreModule::OnCommandLineMovieRenderCompleted);
	ExecutorBase->OnExecutorErrored().AddRaw(this, &FMovieRenderPipelineCoreModule::OnCommandLineMovieRenderErrored);

	ExecutorBase->Execute(Queue);
}

void FMovieRenderPipelineCoreModule::OnCommandLineMovieRenderCompleted(UMoviePipelineExecutorBase* InExecutor, bool bSuccess)
{
	if (InExecutor)
	{
		InExecutor->RemoveFromRoot();
	}

	// Return a success code. Ideally any errors detected during rendering will return different codes.
	FPlatformMisc::RequestExitWithStatus(/*Force*/ false, /*ReturnCode*/ 0);
}

void FMovieRenderPipelineCoreModule::OnCommandLineMovieRenderErrored(UMoviePipelineExecutorBase* InExecutor, UMoviePipeline* InPipelineWithError, bool bIsFatal, FText ErrorText)
{

}


UClass* GetLocalExecutorClass(const FString& MoviePipelineLocalExecutorClassType, const FString ExecutorClassFormatString)
{
	if (MoviePipelineLocalExecutorClassType.Len() > 0)
	{
		UMoviePipelineExecutorBase* LoadedExecutor = LoadObject<UMoviePipelineExecutorBase>(GetTransientPackage(), *MoviePipelineLocalExecutorClassType);
		if (LoadedExecutor)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Loaded explicitly specified Movie Pipeline Executor %s."), *MoviePipelineLocalExecutorClassType);
			return LoadedExecutor->GetClass();
		}
		else
		{
			// They explicitly specified an object, but that couldn't be loaded so it's an error.
			UE_LOG(LogMovieRenderPipeline, Fatal, TEXT("Failed to load specified Local Executor class. Executor Class: %s"), *MoviePipelineLocalExecutorClassType);
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("%s"), *ExecutorClassFormatString);
			return nullptr;
		}
	}
	else
	{
		// Fall back to our provided one. This is okay because it doesn't come from a user preference,
		// since it needs an different executor than the editor provided New Process
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("Using default Movie Pipeline Executor %s. See '-MoviePipelineLocalExecutorClass' if you need to override this."), *MoviePipelineLocalExecutorClassType);
		return UMoviePipelineInProcessExecutor::StaticClass();
	}
}

UClass* GetMoviePipelineClass(const FString& MoviePipelineLocalExecutorClassType, const FString ExecutorClassFormatString)
{
	if (MoviePipelineLocalExecutorClassType.Len() > 0)
	{
		UMoviePipelineExecutorBase* LoadedExecutor = LoadObject<UMoviePipelineExecutorBase>(GetTransientPackage(), *MoviePipelineLocalExecutorClassType);
		if (LoadedExecutor)
		{
			UE_LOG(LogMovieRenderPipeline, Log, TEXT("Loaded explicitly specified Movie Pipeline Executor %s."), *MoviePipelineLocalExecutorClassType);
			return LoadedExecutor->GetClass();
		}
		else
		{
			// They explicitly specified an object, but that couldn't be loaded so it's an error.
			UE_LOG(LogMovieRenderPipeline, Fatal, TEXT("Failed to load specified Local Executor class. Executor Class: %s"), *MoviePipelineLocalExecutorClassType);
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("%s"), *ExecutorClassFormatString);
			return nullptr;
		}
	}
	else
	{
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("Using default Movie Pipeline %s. See '-MoviePipelineClass' if you need to override this."), *MoviePipelineLocalExecutorClassType);
		return UMoviePipeline::StaticClass();
	}
}


bool FMovieRenderPipelineCoreModule::IsTryingToRenderMovieFromCommandLine(FString& OutSequenceAssetPath, FString& OutConfigAssetPath, FString& OutExecutorType, FString& OutPipelineType) const
{
	// Look to see if they've specified a Movie Pipeline to run. This should be in the format:
	// "/Game/MySequences/MySequence.MySequence"
	FParse::Value(FCommandLine::Get(), TEXT("-MoviePipeline="), OutSequenceAssetPath);

	// Look to see if they've specified a configuration to use. This should be in the format:
	// "/Game/MyRenderSettings/MyHighQualitySetting.MyHighQualitySetting" or an absolute path 
	// to a exported json file. Or the contents of a serialized package .utxt
	FParse::Value(FCommandLine::Get(), TEXT("-MoviePipelineConfig="), OutConfigAssetPath);

	// The user may want to override the executor. By default, we use the one specified in the Project
	// Settings, but we allow them to override it on the command line (for render farms, etc.) in case
	// the one used when a human is running the box isn't appropriate. This should be in the format:
	// "/Script/ModuleName.ClassNameNoUPrefix"
	FParse::Value(FCommandLine::Get(), TEXT("-MoviePipelineLocalExecutorClass="), OutExecutorType);

	// The user may want to override the Movie Pipeline itself. By default we use the one specified in the Project
	// Setting, but we allow overriding it for consistency's sake really. This should be in the format:
	// "/Script/ModuleName.ClassNameNoUPrefix"
	FParse::Value(FCommandLine::Get(), TEXT("-MoviePipelineClass="), OutPipelineType);

	return OutSequenceAssetPath.Len() > 0 || OutConfigAssetPath.Len() > 0;
}

uint8 FMovieRenderPipelineCoreModule::ParseMovieRenderData(const FString& InSequenceAssetPath, const FString& InConfigAssetPath, const FString& InExecutorType, const FString& InPipelineType,
	UMoviePipelineQueue*& OutQueue, UMoviePipelineExecutorBase*& OutExecutor) const
{
	// Store off the messages that print the expected format since they're printed in a couple places.
	const FString SequenceAssetFormatString = TEXT("Movie Pipeline Asset should be specified in the format '-MoviePipeline=\"/Game/MySequences/MyPipelineAsset\", or a relative (to executable)/absolute path to a JSON file in the format '-MoviePipeline=\"D:/MyMoviePipelineConfig.json\".");
	const FString ConfigAssetFormatString	= TEXT("Pipeline Config Asset should be specified in the format '-MoviePipelineConfig=\"/Game/MyRenderSettings/MyHighQualitySetting.MyHighQualitySetting\"'");
	const FString PipelineClassFormatString = TEXT("Movie Pipeline Class should be specified in the format '-MoviePipelineClass=\"/Script/ModuleName.ClassNameNoUPrefix\"'");
	const FString ExecutorClassFormatString = TEXT("Pipeline Executor Class should be specified in the format '-MoviePipelineLocalExecutorClass=\"/Script/ModuleName.ClassNameNoUPrefix\"'");

	// They need both an asset and a configuration so early out if we don't have that.
	if (InConfigAssetPath.Len() == 0)
	{
		UE_LOG(LogMovieRenderPipeline, Fatal, TEXT("No Movie Pipeline Config was specified to use as settings."));
		UE_LOG(LogMovieRenderPipeline, Warning, TEXT("%s"), *ConfigAssetFormatString);
		return MoviePipelineErrorCodes::NoConfig;
	}

	OutQueue = nullptr;
	OutExecutor = nullptr;

	// Try to detect executor/class overrides 
	UClass* ExecutorClass = GetLocalExecutorClass(MoviePipelineLocalExecutorClassType, ExecutorClassFormatString);
	UClass* PipelineClass = GetMoviePipelineClass(MoviePipelineClassType, PipelineClassFormatString);

	if (!ensureMsgf(PipelineClass && ExecutorClass, TEXT("Attempted to render a movie pipeline but could not load executor or pipeline class, nor fall back to defaults.")))
	{
		return MoviePipelineErrorCodes::Critical;
	}

	// If they're just trying to render a specific sequence, parse that now.
	ULevelSequence* TargetSequence = nullptr;
	if(InSequenceAssetPath.Len() > 0)
	// Locate and load the level sequence they wish to render.
	{
		// Convert it to a soft object path and use that load to ensure it follows redirectors, etc.
		FSoftObjectPath AssetPath = FSoftObjectPath(InSequenceAssetPath);
		TargetSequence = CastChecked<ULevelSequence>(AssetPath.TryLoad());

		if (!TargetSequence)
		{
			UE_LOG(LogMovieRenderPipeline, Fatal, TEXT("Failed to find Movie Pipeline sequence asset to render. Please note that the /Content/ part of the on-disk structure is omitted. Looked for: %s"), *InSequenceAssetPath);
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("%s"), *SequenceAssetFormatString);
			return MoviePipelineErrorCodes::NoAsset;
		}
	}

	// Now look for the configuration file to see how to render it.
	if (InConfigAssetPath.StartsWith("/Game/"))
	{
		// Convert it to a soft object path and use that load to ensure it follows redirectors, etc.
		FSoftObjectPath AssetPath = FSoftObjectPath(InConfigAssetPath);
		if (UMoviePipelineQueue* AssetAsQueue = Cast<UMoviePipelineQueue>(AssetPath.TryLoad()))
		{
			OutQueue = AssetAsQueue;
		}
		else if (UMoviePipelineMasterConfig* AssetAsConfig = Cast<UMoviePipelineMasterConfig>(AssetPath.TryLoad()))
		{
			OutQueue = NewObject<UMoviePipelineQueue>();
			UMoviePipelineExecutorJob* NewJob = OutQueue->AllocateNewJob();
			NewJob->Sequence = FSoftObjectPath(InSequenceAssetPath);

			// Duplicate the configuration in case we modify it and in case multiple jobs will use it.
			// we don't ave packages in game mode but if another job uses it we don't want to have modified.
			UMoviePipelineMasterConfig* NewConfig = NewObject<UMoviePipelineMasterConfig>(NewJob);
			NewConfig->CopyFrom(AssetAsConfig);
			NewJob->SetConfiguration(AssetAsConfig);
		}

		if (!OutQueue)
		{
			UE_LOG(LogMovieRenderPipeline, Fatal, TEXT("Failed to find Pipeline Configuration asset to render. Please note that the /Content/ part of the on-disk structure is omitted. Looked for: %s"), *InConfigAssetPath);
			UE_LOG(LogMovieRenderPipeline, Warning, TEXT("%s"), *ConfigAssetFormatString);
			return MoviePipelineErrorCodes::NoConfig;
		}
	}
	else
	{
		// If they didn't pass a path that started with /Game/, we'll try to see if it is a manifest file.
		if (InConfigAssetPath.EndsWith(FPackageName::GetTextAssetPackageExtension()))
		{
			FString InFileName = TEXT("QueueManifest");
			FString InPackagePath = TEXT("/Engine/MovieRenderPipeline/Editor/Transient");

			FString NewPackageName = FPackageName::GetLongPackagePath(InPackagePath) + TEXT("/") + InFileName;

			UPackage* OuterPackage = CreatePackage(nullptr, *NewPackageName);
			UPackage* QueuePackage = LoadPackage(OuterPackage, *InConfigAssetPath, LOAD_None);
			if (QueuePackage)
			{
				OutQueue = Cast<UMoviePipelineQueue>((UObject*)FindObjectWithOuter(QueuePackage, UMoviePipelineQueue::StaticClass()));
			}
			
			if(!OutQueue)
			{
				UE_LOG(LogMovieRenderPipeline, Fatal, TEXT("Could not parse text asset package."));
				return MoviePipelineErrorCodes::NoConfig;
			}
		}
		else
		{
			UE_LOG(LogMovieRenderPipeline, Fatal, TEXT("Unknown Config Asset Path. Path: %s"), *InConfigAssetPath);
			return MoviePipelineErrorCodes::NoConfig;
		}
#if 0
		else
		{
			// Due to API limitations we need to save this to a file and then call LoadPackage()
			FGuid FileNameGuid = FGuid::NewGuid();

			FString ManifestFileName = TEXT("MovieRenderPipeline/") + FileNameGuid.ToString() + FPackageName::GetTextAssetPackageExtension();
			FString ManifestFilePath = FPaths::ProjectSavedDir() / ManifestFileName;

			if (!FFileHelper::SaveStringToFile(InConfigAssetPath, *ManifestFilePath))
			{
				UE_LOG(LogMovieRenderPipeline, Fatal, TEXT("Failed to find Pipeline Configuration asset to render. Please note that the /Content/ part of the on-disk structure is omitted. Looked for: %s"), *InConfigAssetPath);
				return MoviePipelineErrorCodes::NoConfig;
			}

			UPackage* NewPackage = LoadPackage(nullptr, *ManifestFilePath, LOAD_None);
			if (NewPackage)
			{
			}
			else
			{
				UE_LOG(LogMovieRenderPipeline, Fatal, TEXT("Could not parse text asset package."));
				return MoviePipelineErrorCodes::NoConfig;
			}
				 
		}
#endif

	}


	// By this time, we know what assets you want to render, how to process the array of assets, and what runs an individual render. First we will create an executor.
	OutExecutor = NewObject<UMoviePipelineExecutorBase>(GetTransientPackage(), ExecutorClass);
	OutExecutor->SetMoviePipelineClass(PipelineClass);

	// Rename our Queue to belong to the Executor
	OutQueue->Rename(nullptr, OutExecutor);

	return MoviePipelineErrorCodes::Success;
}
