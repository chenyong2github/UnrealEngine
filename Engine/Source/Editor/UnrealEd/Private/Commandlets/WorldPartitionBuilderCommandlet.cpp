// Copyright Epic Games, Inc. All Rights Reserved.

#include "Commandlets/WorldPartitionBuilderCommandlet.h"

#include "CoreMinimal.h"
#include "EngineUtils.h"
#include "EditorWorldUtils.h"
#include "Logging/LogMacros.h"
#include "Misc/CommandLine.h"
#include "HAL/PlatformFileManager.h"
#include "ProfilingDebugging/ScopedTimers.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionBuilder.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Trace/Trace.h"

#include "CollectionManagerModule.h"
#include "ICollectionManager.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldPartitionBuilderCommandlet, All, All);

UWorldPartitionBuilderCommandlet::UWorldPartitionBuilderCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{}

int32 UWorldPartitionBuilderCommandlet::Main(const FString& Params)
{
	FPackageSourceControlHelper PackageHelper;

	TRACE_CPUPROFILER_EVENT_SCOPE(UWorldPartitionBuilderCommandlet::Main);

	UE_SCOPED_TIMER(TEXT("Execution"), LogWorldPartitionBuilderCommandlet, Display);

	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	if (Tokens.Num() != 1)
	{
		UE_LOG(LogWorldPartitionBuilderCommandlet, Error, TEXT("Missing world name"));
		return 1;
	}

	if (Switches.Contains(TEXT("Verbose")))
	{
		LogWorldPartitionBuilderCommandlet.SetVerbosity(ELogVerbosity::Verbose);
	}

	if (Switches.Contains(TEXT("RunningFromUnrealEd")))
	{
		ShowErrorCount = false;	// This has the side effect of making the process return code match the return code of the commandlet
		FastExit = true;		// Faster exit which avoids crash during shutdown. The engine isn't shutdown cleanly.
	}

	ICollectionManager& CollectionManager = FModuleManager::LoadModuleChecked<FCollectionManagerModule>("CollectionManager").Get();
	TArray<FString> MapPackagesNames;

	FAssetRegistryModule* AssetRegistryModule = &FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	// Parse map name or maps collection
	if (FPackageName::SearchForPackageOnDisk(Tokens[0]))
	{
		MapPackagesNames = { Tokens[0] };
	}	
	else if (CollectionManager.CollectionExists(FName(Tokens[0]), ECollectionShareType::CST_All))
	{
		TArray<FSoftObjectPath> AssetsPaths;
		CollectionManager.GetAssetsInCollection(FName(Tokens[0]), ECollectionShareType::CST_All, AssetsPaths, ECollectionRecursionFlags::SelfAndChildren);

		for (const auto& AssetPath : AssetsPaths)
		{
			const bool bIncludeOnlyOnDiskAssets = true;
			FAssetData AssetData = AssetRegistryModule->Get().GetAssetByObjectPath(AssetPath, bIncludeOnlyOnDiskAssets);
			const bool bIsWorldAsset = AssetData.IsValid() && (AssetData.AssetClassPath == UWorld::StaticClass()->GetClassPathName());
			if (bIsWorldAsset)
			{
				MapPackagesNames.Add(AssetPath.GetAssetPathString());
			}
		}

		if (MapPackagesNames.IsEmpty())
		{
			UE_LOG(LogWorldPartitionBuilderCommandlet, Warning, TEXT("Found no maps to process in collection %s), exiting"), *Tokens[0]);
			return 0;
		}
	}
	else
	{
		UE_LOG(LogWorldPartitionBuilderCommandlet, Error, TEXT("Missing world(s) as the first argument to the commandlet. Either supply the world name directly (WorldName or /Path/To/WorldName), or provide a collection name to have the builder operate on a set of maps."));
		return 1;
	}

	// Parse builder class name
	FString BuilderClassName;
	if (!FParse::Value(FCommandLine::Get(), TEXT("Builder="), BuilderClassName, false))
	{
		UE_LOG(LogWorldPartitionBuilderCommandlet, Error, TEXT("Invalid builder name."));
		return 1;
	}

	// Find builder class
	TSubclassOf<UWorldPartitionBuilder> BuilderClass = FindFirstObject<UClass>(*BuilderClassName, EFindFirstObjectOptions::EnsureIfAmbiguous);
	if (!BuilderClass)
	{
		UE_LOG(LogWorldPartitionBuilderCommandlet, Error, TEXT("Unknown/invalid world partition builder class: %s."), *BuilderClassName);
		return 1;
	}	

	// Run the builder on the provided map(s)
	for (const FString& MapPackageName : MapPackagesNames)
	{
		if (!RunBuilder(BuilderClass, MapPackageName))
		{
			return false;
		}
	}

	return true;
}

bool UWorldPartitionBuilderCommandlet::RunBuilder(TSubclassOf<UWorldPartitionBuilder> InBuilderClass, const FString& InWorldPackageName)
{
	// This will convert incomplete package name to a fully qualified path
	FString WorldLongPackageName;
	FString WorldFilename;
	if (!FPackageName::SearchForPackageOnDisk(InWorldPackageName, &WorldLongPackageName, &WorldFilename))
	{
		UE_LOG(LogWorldPartitionBuilderCommandlet, Error, TEXT("Package '%s' not found"), *InWorldPackageName);
		return false;
	}

	// Load the world package
	UPackage* WorldPackage = LoadWorldPackageForEditor(WorldLongPackageName);
	if (!WorldPackage)
	{
		UE_LOG(LogWorldPartitionBuilderCommandlet, Error, TEXT("Couldn't load package %s."), *WorldLongPackageName);
		return false;
	}

	// Find the world in the given package
	UWorld* World = UWorld::FindWorldInPackage(WorldPackage);
	if (!World)
	{
		UE_LOG(LogWorldPartitionBuilderCommandlet, Error, TEXT("No world in specified package %s."), *WorldLongPackageName);
		return false;
	}

	// Load configuration file
	FString WorldConfigFilename = FPackageName::LongPackageNameToFilename(World->GetPackage()->GetName(), TEXT(".ini"));
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*WorldConfigFilename))
	{
		LoadConfig(GetClass(), *WorldConfigFilename);
	}

	// Create builder instance
	UWorldPartitionBuilder* Builder = NewObject<UWorldPartitionBuilder>(GetTransientPackage(), InBuilderClass);
	if (!Builder)
	{
		UE_LOG(LogWorldPartitionBuilderCommandlet, Error, TEXT("Failed to create builder."));
		return false;
	}

	bool bResult;
	{
		FGCObjectScopeGuard BuilderGuard(Builder);
		bResult = Builder->RunBuilder(World);
	}

	// Save configuration file
	if (!FPlatformFileManager::Get().GetPlatformFile().FileExists(*WorldConfigFilename) ||
		!FPlatformFileManager::Get().GetPlatformFile().IsReadOnly(*WorldConfigFilename))
	{
		SaveConfig(CPF_Config, *WorldConfigFilename);
	}

	return bResult;
}
