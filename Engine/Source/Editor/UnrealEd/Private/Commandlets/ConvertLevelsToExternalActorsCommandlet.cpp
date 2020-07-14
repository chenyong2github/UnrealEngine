// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
 ConvertLevelsToExternalActorsCommandlet: Commandlet used to convert levels uses external actors in batch
=============================================================================*/

#include "Commandlets/ConvertLevelsToExternalActorsCommandlet.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "Misc/ConfigCacheIni.h"
#include "Editor.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Engine/LevelStreaming.h"
#include "UObject/UObjectHash.h"
#include "PackageHelperFunctions.h"
#include "ISourceControlOperation.h"
#include "SourceControlOperations.h"
#include "SourceControlHelpers.h"
#include "ISourceControlModule.h"
#include "UObject/MetaData.h"
#include "ProfilingDebugging/ScopedTimers.h"

DEFINE_LOG_CATEGORY_STATIC(LogConvertLevelsToExternalActorsCommandlet, All, All);

UConvertLevelsToExternalActorsCommandlet::UConvertLevelsToExternalActorsCommandlet(const FObjectInitializer& ObjectInitializer)
: Super(ObjectInitializer)
{
}

ULevel* UConvertLevelsToExternalActorsCommandlet::LoadLevel(const FString& LevelToLoad) const
{
	SET_WARN_COLOR(COLOR_WHITE);
	UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Log, TEXT("Loading level %s."), *LevelToLoad);
	CLEAR_WARN_COLOR();

	FString MapLoadCommand = FString::Printf(TEXT("MAP LOAD FILE=%s TEMPLATE=0 SHOWPROGRESS=0 FEATURELEVEL=3"), *LevelToLoad);
	GEditor->Exec(nullptr, *MapLoadCommand, *GError);
	FlushAsyncLoading();

	UPackage* MapPackage = FindPackage(nullptr, *LevelToLoad);
	UWorld* World = MapPackage ? UWorld::FindWorldInPackage(MapPackage) : nullptr;
	return World ? World->PersistentLevel : nullptr;
}

void UConvertLevelsToExternalActorsCommandlet::GetSubLevelsToConvert(ULevel* MainLevel, TSet<ULevel*>& SubLevels, bool bRecursive)
{
	UWorld* World = MainLevel->GetTypedOuter<UWorld>();
	for(ULevelStreaming* StreamingLevel: World->GetStreamingLevels())
	{
		if (ULevel* SubLevel = StreamingLevel->GetLoadedLevel())
		{
			SubLevels.Add(SubLevel);
			if (bRecursive)
			{
				// Recursively obtain sub levels to convert
				GetSubLevelsToConvert(SubLevel, SubLevels, bRecursive);
			}
		}
	}
}

int32 UConvertLevelsToExternalActorsCommandlet::Main(const FString& Params)
{
	FAutoScopedDurationTimer ConversionTimer;

	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	// Need at least the level to convert
	if (Tokens.Num() < 1)
	{
		UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Error, TEXT("ConvertLevelToExternalActors bad parameters"));
		return 1;
	}

	bool bNoSourceControl = Switches.Contains(TEXT("nosourcecontrol"));
	bool bConvertSubLevel = Switches.Contains(TEXT("convertsublevels"));
	bool bRecursiveSubLevel = Switches.Contains(TEXT("recursive"));
	bool bConvertToExternal = !Switches.Contains(TEXT("internal"));

	FScopedSourceControl SourceControl;
	SourceControlProvider = bNoSourceControl ? nullptr : &ISourceControlModule::Get().GetProvider();

	// Load persistent level
	ULevel* MainLevel = LoadLevel(Tokens[0]);
	if (!MainLevel)
	{
		UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Error, TEXT("Unable to load level '%s'"), *Tokens[0]);
		return 1;
	}

	UWorld* MainWorld = MainLevel->GetWorld();
	UPackage* MainPackage = MainLevel->GetPackage();

	TSet<ULevel*> LevelsToConvert;
	LevelsToConvert.Add(MainLevel);
	if (bConvertSubLevel)
	{
		GetSubLevelsToConvert(MainLevel, LevelsToConvert, bRecursiveSubLevel);
	}
	
	TArray<UPackage*> PackagesToSave;
	for(ULevel* Level : LevelsToConvert)
	{
		Level->SetUseExternalActors(bConvertToExternal);
		Level->ConvertAllActorsToPackaging(bConvertToExternal);
		UPackage* LevelPackage = Level->GetPackage();
		PackagesToSave.Add(LevelPackage);
		PackagesToSave.Append(Level->GetExternalActorPackages());
	}

	if (UseSourceControl())
	{
		FEditorFileUtils::CheckoutPackages(PackagesToSave, nullptr, false);
	}
	else
	{
		for (UPackage* Package : PackagesToSave)
		{
			FString PackageFilename = SourceControlHelpers::PackageFilename(Package);
			if (IPlatformFile::GetPlatformPhysical().FileExists(*PackageFilename))
			{
				if (!IPlatformFile::GetPlatformPhysical().SetReadOnly(*PackageFilename, false))
				{
					UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Error, TEXT("Error setting %s writable"), *PackageFilename);
					return 1;
				}
			}
		}
	}
	FEditorFileUtils::PromptForCheckoutAndSave(PackagesToSave, false, false, nullptr, true, false);
	UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Log, TEXT("Conversion took %.2f seconds"), ConversionTimer.GetTime());

	return 0;
}