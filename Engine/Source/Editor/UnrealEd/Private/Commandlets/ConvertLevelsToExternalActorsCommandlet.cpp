// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
 ConvertLevelsToExternalActorsCommandlet: Commandlet used to convert levels uses external actors in batch
=============================================================================*/

#include "Commandlets/ConvertLevelsToExternalActorsCommandlet.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
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

bool UConvertLevelsToExternalActorsCommandlet::AddPackageToSourceControl(UPackage* Package)
{
	if (UseSourceControl())
	{
		FString PackageFilename = SourceControlHelpers::PackageFilename(Package);
		FSourceControlStatePtr SourceControlState = GetSourceControlProvider().GetState(PackageFilename, EStateCacheUsage::ForceUpdate);

		if (SourceControlState.IsValid() && !SourceControlState->IsSourceControlled())
		{
			UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Log, TEXT("Adding package %s to source control"), *PackageFilename);
			if (GetSourceControlProvider().Execute(ISourceControlOperation::Create<FMarkForAdd>(), Package) != ECommandResult::Succeeded)
			{
				UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Error, TEXT("Error adding %s to source control."), *PackageFilename);
				return false;
			}
		}
	}

	return true;
}

bool UConvertLevelsToExternalActorsCommandlet::SavePackage(UPackage* Package)
{
	FString PackageFileName = SourceControlHelpers::PackageFilename(Package);
	if (!UPackage::SavePackage(Package, nullptr, RF_Standalone, *PackageFileName, GError, nullptr, false, true, SAVE_None))
	{
		UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Error, TEXT("Error saving %s"), *PackageFileName);
		return false;
	}

	return true;
}

bool UConvertLevelsToExternalActorsCommandlet::CheckoutPackage(UPackage* Package)
{
	if (UseSourceControl())
	{
		FString PackageFilename = SourceControlHelpers::PackageFilename(Package);
		FSourceControlStatePtr SourceControlState = GetSourceControlProvider().GetState(PackageFilename, EStateCacheUsage::ForceUpdate);

		if (SourceControlState.IsValid())
		{
			FString OtherCheckedOutUser;
			if (SourceControlState->IsCheckedOutOther(&OtherCheckedOutUser))
			{
				UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Error, TEXT("Overwriting package %s already checked out by %s, will not submit"), *PackageFilename, *OtherCheckedOutUser);
				return false;
			}
			else if (!SourceControlState->IsCurrent())
			{
				UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Error, TEXT("Overwriting package %s (not at head revision), will not submit"), *PackageFilename);
				return false;
			}
			else if (SourceControlState->IsCheckedOut() || SourceControlState->IsAdded())
			{
				UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Log, TEXT("Skipping package %s (already checked out)"), *PackageFilename);
				return true;
			}
			else if (SourceControlState->IsSourceControlled())
			{
				UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Log, TEXT("Checking out package %s from source control"), *PackageFilename);
				return GetSourceControlProvider().Execute(ISourceControlOperation::Create<FCheckOut>(), Package) == ECommandResult::Succeeded;
			}
		}
	}
	else
	{
		FString PackageFilename = SourceControlHelpers::PackageFilename(Package);
		if (IPlatformFile::GetPlatformPhysical().FileExists(*PackageFilename))
		{
			if (!IPlatformFile::GetPlatformPhysical().SetReadOnly(*PackageFilename, false))
			{
				UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Error, TEXT("Error setting %s writable"), *PackageFilename);
				return false;
			}
		}
	}

	return true;
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

	// This will convert imcomplete package name to a fully qualifed path
	if (!FPackageName::SearchForPackageOnDisk(Tokens[0], &Tokens[0]))
	{
		UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Error, TEXT("Unknown level '%s'"), *Tokens[0]);
		return 1;
	}

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

	for(ULevel* Level : LevelsToConvert)
	{
		if (!Level->bContainsStableActorGUIDs)
		{
			UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Error, TEXT("Unable to convert level '%s' with non-stable actor GUIDs. Resave the level before converting."), *Level->GetPackage()->GetName());
			return 1;
		}
	}
	
	TArray<UPackage*> PackagesToSave;
	for(ULevel* Level : LevelsToConvert)
	{
		Level->SetUseExternalActors(bConvertToExternal);
		Level->ConvertAllActorsToPackaging(bConvertToExternal);
		UPackage* LevelPackage = Level->GetPackage();
		PackagesToSave.Add(LevelPackage);
		PackagesToSave.Append(Level->GetLoadedExternalActorPackages());
	}

	for (UPackage* PackageToSave : PackagesToSave)
	{
		if(!CheckoutPackage(PackageToSave))
		{
			return 1;
		}
	}

	// Save packages
	UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Log, TEXT("Saving %d packages."), PackagesToSave.Num());
	for (UPackage* PackageToSave : PackagesToSave)
	{
		if (!SavePackage(PackageToSave))
		{
			return 1;
		}
	}

	// Add new packages to source control
	for (UPackage* PackageToSave : PackagesToSave)
	{
		if(!AddPackageToSourceControl(PackageToSave))
		{
			return 1;
		}
	}

	UE_LOG(LogConvertLevelsToExternalActorsCommandlet, Log, TEXT("Conversion took %.2f seconds"), ConversionTimer.GetTime());
	return 0;
}