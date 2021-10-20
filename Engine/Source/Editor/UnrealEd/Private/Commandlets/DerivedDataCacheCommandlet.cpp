// Copyright Epic Games, Inc. All Rights Reserved.

/*=============================================================================
DerivedDataCacheCommandlet.cpp: Commandlet for DDC maintenence
=============================================================================*/
#include "Commandlets/DerivedDataCacheCommandlet.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/Package.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "PackageHelperFunctions.h"
#include "DerivedDataCacheInterface.h"
#include "GlobalShader.h"
#include "Interfaces/ITargetPlatform.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "ShaderCompiler.h"
#include "DistanceFieldAtlas.h"
#include "Misc/RedirectCollector.h"
#include "Engine/Texture.h"
#include "CookOnTheSide/CookOnTheFlyServer.h"
#include "Algo/RemoveIf.h"
#include "Algo/Transform.h"
#include "Settings/ProjectPackagingSettings.h"

DEFINE_LOG_CATEGORY_STATIC(LogDerivedDataCacheCommandlet, Log, All);

class UDerivedDataCacheCommandlet::FObjectReferencer : public FGCObject
{
public:
	FObjectReferencer(TMap<UObject*, double>& InReferencedObjects)
		: ReferencedObjects(InReferencedObjects)
	{
	}

private:
	void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AllowEliminatingReferences(false);
		Collector.AddReferencedObjects(ReferencedObjects);
		Collector.AllowEliminatingReferences(true);
	}

	FString GetReferencerName() const override
	{
		return TEXT("UDerivedDataCacheCommandlet");
	}

	FString ReferencerName;
	TMap<UObject*, double>& ReferencedObjects;
};

class UDerivedDataCacheCommandlet::FPackageListener : public FUObjectArray::FUObjectCreateListener, public FUObjectArray::FUObjectDeleteListener
{
public:
	FPackageListener()
	{
		GUObjectArray.AddUObjectDeleteListener(this);
		GUObjectArray.AddUObjectCreateListener(this);

		// We might be late to the party, check if some UPackage already have been created
		for (TObjectIterator<UPackage> PackageIter; PackageIter; ++PackageIter)
		{
			NewPackages.Add(*PackageIter);
		}
	}

	~FPackageListener()
	{
		GUObjectArray.RemoveUObjectDeleteListener(this);
		GUObjectArray.RemoveUObjectCreateListener(this);
	}

	TSet<UPackage*>& GetNewPackages()
	{
		return NewPackages;
	}

private:
	void NotifyUObjectCreated(const class UObjectBase* Object, int32 Index) override
	{
		if (Object->GetClass() == UPackage::StaticClass())
		{
			NewPackages.Add(const_cast<UPackage*>(static_cast<const UPackage*>(Object)));
		}
	}

	void NotifyUObjectDeleted(const class UObjectBase* Object, int32 Index) override
	{
		if (Object->GetClass() == UPackage::StaticClass())
		{
			NewPackages.Remove(const_cast<UPackage*>(static_cast<const UPackage*>(Object)));
		}
	}

	void OnUObjectArrayShutdown() override
	{
		GUObjectArray.RemoveUObjectDeleteListener(this);
		GUObjectArray.RemoveUObjectCreateListener(this);
	}

	TSet<UPackage*> NewPackages;
};

UDerivedDataCacheCommandlet::UDerivedDataCacheCommandlet(FVTableHelper& Helper)
	: Super(Helper)
{
}

UDerivedDataCacheCommandlet::UDerivedDataCacheCommandlet(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	LogToConsole = false;
}

void UDerivedDataCacheCommandlet::MaybeMarkPackageAsAlreadyLoaded(UPackage* Package)
{
	if (ProcessedPackages.Contains(Package->GetFName()))
	{
		UE_LOG(LogDerivedDataCacheCommandlet, Verbose, TEXT("Marking %s already loaded."), *Package->GetName());
		Package->SetPackageFlags(PKG_ReloadingForCooker);
	}
}

static void WaitForCurrentShaderCompilationToFinish(bool& bInOutHadActivity)
{
	if (GShaderCompilingManager->IsCompiling())
	{
		bInOutHadActivity = true;
		int32 CachedShaderCount = GShaderCompilingManager->GetNumRemainingJobs();
		if (CachedShaderCount > 0)
		{
			UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("Waiting for %d shaders to finish."), CachedShaderCount);
		}
		int32 NumCompletedShadersSinceLastLog = 0;
		while (GShaderCompilingManager->IsCompiling())
		{
			const int32 CurrentShaderCount = GShaderCompilingManager->GetNumRemainingJobs();
			NumCompletedShadersSinceLastLog += (CachedShaderCount - CurrentShaderCount);
			CachedShaderCount = CurrentShaderCount;

			if (NumCompletedShadersSinceLastLog >= 1000)
			{
				UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("Waiting for %d shaders to finish."), CachedShaderCount);
				NumCompletedShadersSinceLastLog = 0;
			}

			// Process any asynchronous shader compile results that are ready, limit execution time
			GShaderCompilingManager->ProcessAsyncResults(true, false);
			GDistanceFieldAsyncQueue->ProcessAsyncTasks();
		}
		GShaderCompilingManager->FinishAllCompilation(); // Final blocking check as IsCompiling() may be non-deterministic
		UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("Done waiting for shaders to finish."));
	}

	// these shouldn't be predicated on whether the shaders were being compiled
	GDistanceFieldAsyncQueue->BlockUntilAllBuildsComplete();
}

static void WaitForCurrentTextureBuildingToFinish(bool& bInOutHadActivity)
{
	for (TObjectIterator<UTexture> Texture; Texture; ++Texture)
	{
		Texture->FinishCachePlatformData();
	}
};

static void PumpAsync(bool* bInOutHadActivity = nullptr)
{
	bool bHadActivity = false;
	WaitForCurrentShaderCompilationToFinish(bHadActivity);
	WaitForCurrentTextureBuildingToFinish(bHadActivity);
	if (bInOutHadActivity)
	{
		*bInOutHadActivity = *bInOutHadActivity || bHadActivity;
	}
}

void UDerivedDataCacheCommandlet::CacheLoadedPackages(UPackage* CurrentPackage, uint8 PackageFilter, const TArray<ITargetPlatform*>& Platforms)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UDerivedDataCacheCommandlet::CacheLoadedPackages);

	const double BeginCacheTimeStart = FPlatformTime::Seconds();

	// We will only remove what we process from the list to avoid unprocessed package being forever forgotten.
	TSet<UPackage*>& NewPackages = PackageListener->GetNewPackages();

	TArray<UObject*> ObjectsWithOuter;
	for (auto NewPackageIt = NewPackages.CreateIterator(); NewPackageIt; ++NewPackageIt)
	{
		UPackage* NewPackage = *NewPackageIt;
		const FName NewPackageName = NewPackage->GetFName();
		if (!ProcessedPackages.Contains(NewPackageName))
		{
			if ((PackageFilter & NORMALIZE_ExcludeEnginePackages) != 0 && NewPackage->GetName().StartsWith(TEXT("/Engine")))
			{
				UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("Skipping %s as Engine package"), *NewPackageName.ToString());

				// Add it so we don't convert the FName to a string everytime we encounter this package
				ProcessedPackages.Add(NewPackageName);
				NewPackageIt.RemoveCurrent();
			}
			else if (NewPackage == CurrentPackage || !PackagesToProcess.Contains(NewPackageName))
			{
				UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("Processing %s"), *NewPackageName.ToString());

				ProcessedPackages.Add(NewPackageName);
				NewPackageIt.RemoveCurrent();

				ObjectsWithOuter.Reset();
				GetObjectsWithOuter(NewPackage, ObjectsWithOuter, true /* bIncludeNestedObjects */, RF_ClassDefaultObject /* ExclusionFlags */);
				for (UObject* Object : ObjectsWithOuter)
				{
					for (auto Platform : Platforms)
					{
						Object->BeginCacheForCookedPlatformData(Platform);
					}

					CachingObjects.Add(Object);
				}
			}
		}
		else
		{
			NewPackageIt.RemoveCurrent();
		}
	}

	BeginCacheTime += FPlatformTime::Seconds() - BeginCacheTimeStart;

	ProcessCachingObjects(Platforms);
}

bool UDerivedDataCacheCommandlet::ProcessCachingObjects(const TArray<ITargetPlatform*>& Platforms)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(UDerivedDataCacheCommandlet::ProcessCachingObjects);

	bool bHadActivity = false;
	if (CachingObjects.Num() > 0)
	{
		PumpAsync();

		double CurrentTime = FPlatformTime::Seconds();
		for (auto It = CachingObjects.CreateIterator(); It; ++It)
		{
			// Call IsCachedCookedPlatformDataLoaded once a second per object since it can be quite expensive
			if (CurrentTime - It->Value > 1.0)
			{
				UObject* Object = It->Key;
				bool bIsFinished = true;

				for (auto Platform : Platforms)
				{
					// IsCachedCookedPlatformDataLoaded can be quite slow for some objects
					// Do not call it if bIsFinished is already false
					bIsFinished = bIsFinished && Object->IsCachedCookedPlatformDataLoaded(Platform);
				}

				if (bIsFinished)
				{
					bHadActivity = true;
					Object->WillNeverCacheCookedPlatformDataAgain();
					Object->ClearAllCachedCookedPlatformData();
					It.RemoveCurrent();
				}
				else
				{
					It->Value = CurrentTime;
				}
			}
		}
	}

	return bHadActivity;
}

void UDerivedDataCacheCommandlet::FinishCachingObjects(const TArray<ITargetPlatform*>& Platforms)
{
	// Timing variables
	double DDCCommandletMaxWaitSeconds = 60. * 10.;
	GConfig->GetDouble(TEXT("CookSettings"), TEXT("DDCCommandletMaxWaitSeconds"), DDCCommandletMaxWaitSeconds, GEditorIni);

	const double FinishCacheTimeStart = FPlatformTime::Seconds();
	double LastActivityTime = FinishCacheTimeStart;

	while (CachingObjects.Num() > 0)
	{
		bool bHadActivity = ProcessCachingObjects(Platforms);

		double CurrentTime = FPlatformTime::Seconds();
		if (!bHadActivity)
		{
			PumpAsync(&bHadActivity);
		}
		if (!bHadActivity)
		{
			if (CurrentTime - LastActivityTime >= DDCCommandletMaxWaitSeconds)
			{
				UObject* Object = CachingObjects.CreateIterator()->Key;
				UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("Timed out for %.2lfs waiting for %d objects to finish caching. First object: %s."),
					DDCCommandletMaxWaitSeconds, CachingObjects.Num(), *Object->GetFullName());
				break;
			}
			else
			{
				const double WaitingForCacheSleepTime = 0.050;
				FPlatformProcess::Sleep(WaitingForCacheSleepTime);
			}
		}
		else
		{
			LastActivityTime = CurrentTime;
		}
	}

	FinishCacheTime += FPlatformTime::Seconds() - FinishCacheTimeStart;
}

int32 UDerivedDataCacheCommandlet::Main( const FString& Params )
{
	// Avoid putting those directly in the constructor because we don't
	// want the CDO to have a second copy of these being active.
	PackageListener  = MakeUnique<FPackageListener>();
	ObjectReferencer = MakeUnique<FObjectReferencer>(CachingObjects);

	TArray<FString> Tokens, Switches;
	ParseCommandLine(*Params, Tokens, Switches);

	bool bFillCache = Switches.Contains("FILL");   // do the equivalent of a "loadpackage -all" to fill the DDC
	bool bStartupOnly = Switches.Contains("STARTUPONLY");   // regardless of any other flags, do not iterate packages

	// Subsets for parallel processing
	uint32 SubsetMod = 0;
	uint32 SubsetTarget = MAX_uint32;
	FParse::Value(*Params, TEXT("SubsetMod="), SubsetMod);
	FParse::Value(*Params, TEXT("SubsetTarget="), SubsetTarget);
	bool bDoSubset = SubsetMod > 0 && SubsetTarget < SubsetMod;

	double FindProcessedPackagesTime = 0.0;
	double GCTime = 0.0;
	FinishCacheTime = 0.;
	BeginCacheTime = 0.;

	if (!bStartupOnly && bFillCache)
	{
		FCoreUObjectDelegates::PackageCreatedForLoad.AddUObject(this, &UDerivedDataCacheCommandlet::MaybeMarkPackageAsAlreadyLoaded);

		Tokens.Empty(2);
		Tokens.Add(FString("*") + FPackageName::GetAssetPackageExtension());

		FString MapList;
		if(FParse::Value(*Params, TEXT("Map="), MapList))
		{
			for(int StartIdx = 0; StartIdx < MapList.Len();)
			{
				int EndIdx = StartIdx;
				while(EndIdx < MapList.Len() && MapList[EndIdx] != '+')
				{
					EndIdx++;
				}
				Tokens.Add(MapList.Mid(StartIdx, EndIdx - StartIdx) + FPackageName::GetMapPackageExtension());
				StartIdx = EndIdx + 1;
			}
		}
		else
		{
			Tokens.Add(FString("*") + FPackageName::GetMapPackageExtension());
		}

		uint8 PackageFilter = NORMALIZE_DefaultFlags;
		if ( Switches.Contains(TEXT("MAPSONLY")) )
		{
			PackageFilter |= NORMALIZE_ExcludeContentPackages;
		}

		if ( Switches.Contains(TEXT("PROJECTONLY")) )
		{
			PackageFilter |= NORMALIZE_ExcludeEnginePackages;
		}

		if ( !Switches.Contains(TEXT("DEV")) )
		{
			PackageFilter |= NORMALIZE_ExcludeDeveloperPackages;
		}

		if ( !Switches.Contains(TEXT("NOREDIST")) )
		{
			PackageFilter |= NORMALIZE_ExcludeNoRedistPackages;
		}

		// assume the first token is the map wildcard/pathname
		TSet<FString> FilesInPath;
		TArray<FString> Unused;
		TArray<FString> TokenFiles;
		for ( int32 TokenIndex = 0; TokenIndex < Tokens.Num(); TokenIndex++ )
		{
			TokenFiles.Reset();
			if ( !NormalizePackageNames( Unused, TokenFiles, Tokens[TokenIndex], PackageFilter) )
			{
				UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("No packages found for parameter %i: '%s'"), TokenIndex, *Tokens[TokenIndex]);
				continue;
			}

			FilesInPath.Append(TokenFiles);
		}

		TArray<TPair<FString, FName>> PackagePaths;
		PackagePaths.Reserve(FilesInPath.Num());
		for (FString& Filename : FilesInPath)
		{
			FString PackageName;
			FString FailureReason;
			if (!FPackageName::TryConvertFilenameToLongPackageName(Filename, PackageName, &FailureReason))
			{
				UE_LOG(LogDerivedDataCacheCommandlet, Warning, TEXT("Unable to resolve filename %s to package name because: %s"), *Filename, *FailureReason);
				continue;
			}
			PackagePaths.Emplace(MoveTemp(Filename), FName(*PackageName));
		}

		// Respect settings that instruct us not to enumerate some paths
		TArray<FString> LocalDirsToNotSearch;
		const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
		for (const FDirectoryPath& DirToNotSearch : PackagingSettings->TestDirectoriesToNotSearch)
		{
			FString LocalPath;
			if (FPackageName::TryConvertGameRelativePackagePathToLocalPath(DirToNotSearch.Path, LocalPath))
			{
				LocalDirsToNotSearch.Add(LocalPath);
			}
			else
			{
				UE_LOG(LogCook, Warning, TEXT("'ProjectSettings -> Project -> Packaging -> Test directories to not search' has invalid element '%s'"), *DirToNotSearch.Path);
			}
		}

		TArray<FString> LocalFilenamesToSkip;
		if (FPackageName::FindPackagesInDirectories(LocalFilenamesToSkip, LocalDirsToNotSearch))
		{
			TSet<FName> PackageNamesToSkip;
			Algo::Transform(LocalFilenamesToSkip, PackageNamesToSkip, [](const FString& Filename)
				{
					FString PackageName;
					if (FPackageName::TryConvertFilenameToLongPackageName(Filename, PackageName))
					{
						return FName(*PackageName);
					}
					return FName(NAME_None);
				});

			int32 NewNum = Algo::StableRemoveIf(PackagePaths, [&PackageNamesToSkip](const TPair<FString,FName>& PackagePath) { return PackageNamesToSkip.Contains(PackagePath.Get<1>()); });
			PackagePaths.SetNum(NewNum);
		}

		ITargetPlatformManagerModule* TPM = GetTargetPlatformManager();
		const TArray<ITargetPlatform*>& Platforms = TPM->GetActiveTargetPlatforms();

		for (int32 Index = 0; Index < Platforms.Num(); Index++)
		{
			TArray<FName> DesiredShaderFormats;
			Platforms[Index]->GetAllTargetedShaderFormats(DesiredShaderFormats);

			for (int32 FormatIndex = 0; FormatIndex < DesiredShaderFormats.Num(); FormatIndex++)
			{
				const EShaderPlatform ShaderPlatform = ShaderFormatToLegacyShaderPlatform(DesiredShaderFormats[FormatIndex]);
				// Kick off global shader compiles for each target platform. Note that shader platform alone is not sufficient to distinguish between WindowsEditor and WindowsClient, which after UE 4.25 have different DDC
				CompileGlobalShaderMap(ShaderPlatform, Platforms[Index], false);
			}
		}

		const int32 GCInterval = 100;
		int32 NumProcessedSinceLastGC = 0;
		bool bLastPackageWasMap = false;

		if (PackagePaths.Num() == 0)
		{
			UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("No packages found to load."));
		}
		else
		{
			UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("%d packages to load..."), PackagePaths.Num());
		}

		// Gather the list of packages to process
		PackagesToProcess.Empty(PackagePaths.Num());
		for (int32 PackageIndex = PackagePaths.Num() - 1; PackageIndex >= 0; PackageIndex--)
		{
			PackagesToProcess.Add(PackagePaths[PackageIndex].Get<1>());
		}

		// Process each package
		for (int32 PackageIndex = PackagePaths.Num() - 1; PackageIndex >= 0; PackageIndex-- )
		{
			TPair<FString, FName>& PackagePath = PackagePaths[PackageIndex];
			const FString& Filename = PackagePath.Get<0>();
			FName PackageFName = PackagePath.Get<1>();
			check(!ProcessedPackages.Contains(PackageFName));

			// If work is distributed, skip packages that are meant to be process by other machines
			if (bDoSubset)
			{
				FString PackageName = PackageFName.ToString();
				if (FCrc::StrCrc_DEPRECATED(*PackageName.ToUpper()) % SubsetMod != SubsetTarget)
				{
					continue;
				}
			}

			UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("Loading (%d) %s"), FilesInPath.Num() - PackageIndex, *Filename);

			UPackage* Package = LoadPackage(NULL, *Filename, LOAD_None);
			if (Package == NULL)
			{
				UE_LOG(LogDerivedDataCacheCommandlet, Error, TEXT("Error loading %s!"), *Filename);
				bLastPackageWasMap = false;
			}
			else
			{
				bLastPackageWasMap = Package->ContainsMap();
				NumProcessedSinceLastGC++;
			}

			// even if the load failed this could be the first time through the loop so it might have all the startup packages to resolve
			GRedirectCollector.ResolveAllSoftObjectPaths();

			// Find any new packages and cache all the objects in each package
			CacheLoadedPackages(Package, PackageFilter, Platforms);

			// Perform a GC if conditions are met
			if (NumProcessedSinceLastGC >= GCInterval || PackageIndex < 0 || bLastPackageWasMap)
			{
				const double StartGCTime = FPlatformTime::Seconds();
				if (NumProcessedSinceLastGC >= GCInterval || PackageIndex < 0)
				{
					UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("GC (Full)..."));
					CollectGarbage(RF_NoFlags);
					NumProcessedSinceLastGC = 0;
				}
				else
				{
					UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("GC..."));
					CollectGarbage(RF_Standalone);
				}
				GCTime += FPlatformTime::Seconds() - StartGCTime;

				bLastPackageWasMap = false;
			}
		}
	}

	FinishCachingObjects(GetTargetPlatformManager()->GetActiveTargetPlatforms());

	GetDerivedDataCacheRef().WaitForQuiescence(true);

	UE_LOG(LogDerivedDataCacheCommandlet, Display, TEXT("BeginCacheTime=%.2lfs, FinishCacheTime=%.2lfs, GCTime=%.2lfs."), BeginCacheTime, FinishCacheTime, GCTime);

	return 0;
}