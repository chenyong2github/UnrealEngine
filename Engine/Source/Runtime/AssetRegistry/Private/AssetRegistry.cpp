// Copyright Epic Games, Inc. All Rights Reserved.

#include "AssetRegistry.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/ArrayReader.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/UObjectHash.h"
#include "UObject/UObjectIterator.h"
#include "UObject/MetaData.h"
#include "UObject/CoreRedirects.h"
#include "Blueprint/BlueprintSupport.h"
#include "AssetRegistryPrivate.h"
#include "ARFilter.h"
#include "DependsNode.h"
#include "PackageReader.h"
#include "GenericPlatform/GenericPlatformChunkInstall.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/CoreDelegates.h"
#include "UObject/ConstructorHelpers.h"
#include "Misc/RedirectCollector.h"
#include "AssetRegistryModule.h"
#include "Serialization/LargeMemoryReader.h"
#include "Misc/ScopeExit.h"

#if WITH_EDITOR
#include "IDirectoryWatcher.h"
#include "DirectoryWatcherModule.h"
#include "HAL/ThreadHeartBeat.h"
#include "HAL/PlatformProcess.h"
#endif // WITH_EDITOR

// Caching is permanently enabled in editor because memory is not that constrained, disabled by default otherwise
#define ASSETREGISTRY_CACHE_ALWAYS_ENABLED (WITH_EDITOR)

DEFINE_LOG_CATEGORY(LogAssetRegistry);

/** Returns the appropriate ChunkProgressReportingType for the given Asset enum */
EChunkProgressReportingType::Type GetChunkAvailabilityProgressType(EAssetAvailabilityProgressReportingType::Type ReportType)
{
	EChunkProgressReportingType::Type ChunkReportType;
	switch (ReportType)
	{
	case EAssetAvailabilityProgressReportingType::ETA:
		ChunkReportType = EChunkProgressReportingType::ETA;
		break;
	case EAssetAvailabilityProgressReportingType::PercentageComplete:
		ChunkReportType = EChunkProgressReportingType::PercentageComplete;
		break;
	default:
		ChunkReportType = EChunkProgressReportingType::PercentageComplete;
		UE_LOG(LogAssetRegistry, Error, TEXT("Unsupported assetregistry report type: %i"), (int)ReportType);
		break;
	}
	return ChunkReportType;
}

UAssetRegistry::UAssetRegistry(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

#include "Misc/PreLoadFile.h"
static FPreLoadFile GPreLoadAssetRegistry(TEXT("{PROJECT}AssetRegistry.bin"));

UAssetRegistryImpl::UAssetRegistryImpl(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	const double StartupStartTime = FPlatformTime::Seconds();

	bInitialSearchCompleted = true;
	AmortizeStartTime = 0;
	TotalAmortizeTime = 0;

	MaxSecondsPerFrame = 0.04;

	// By default update the disk cache once on asset load, to incorporate changes made in PostLoad. This only happens in editor builds
	bUpdateDiskCacheAfterLoad = true;

	bIsTempCachingAlwaysEnabled = ASSETREGISTRY_CACHE_ALWAYS_ENABLED;
	bIsTempCachingEnabled = bIsTempCachingAlwaysEnabled;
	bIsTempCachingUpToDate = false;
	
	// The initial value doesn't matter since caching has not yet been computed
	TempCachingRegisteredClassesVersionNumber = 0;

	// Collect all code generator classes (currently BlueprintCore-derived ones)
	CollectCodeGeneratorClasses();

	// Read default serialization options
	InitializeSerializationOptionsFromIni(SerializationOptions, FString());

	// If in the editor, we scan all content right now
	// If in the game, we expect user to make explicit sync queries using ScanPathsSynchronous
	// If in a commandlet, we expect the commandlet to decide when to perform a synchronous scan
	if (GIsEditor && !IsRunningCommandlet())
	{
		bInitialSearchCompleted = false;
		SearchAllAssets(false);
	}
	// for platforms that require cooked data, we attempt to load a premade asset registry
	else if (FPlatformProperties::RequiresCookedData())
	{
		// load the cooked data
		int64 Size;
		void* PreloadedData = GPreLoadAssetRegistry.TakeOwnershipOfLoadedData(&Size);

		if (SerializationOptions.bSerializeAssetRegistry)
		{
			if (PreloadedData != nullptr)
			{
				FLargeMemoryReader SerializedAssetData((uint8*)PreloadedData, Size, ELargeMemoryReaderFlags::TakeOwnership);
				// serialize the data with the memory reader (will convert FStrings to FNames, etc)
				Serialize(SerializedAssetData);
			}
		}
		else
		{
			FMemory::Free(PreloadedData);
		}

		TArray<TSharedRef<IPlugin>> ContentPlugins = IPluginManager::Get().GetEnabledPluginsWithContent();
		for (TSharedRef<IPlugin> ContentPlugin : ContentPlugins)
		{
			if (ContentPlugin->CanContainContent())
			{
				FArrayReader SerializedAssetData;
				FString PluginAssetRegistry = ContentPlugin->GetBaseDir() / TEXT("AssetRegistry.bin");
				if (IFileManager::Get().FileExists(*PluginAssetRegistry) && FFileHelper::LoadFileToArray(SerializedAssetData, *PluginAssetRegistry))
				{
					SerializedAssetData.Seek(0);
					Serialize(SerializedAssetData);
				}
			}
		}
	}

	// Report startup time. This does not include DirectoryWatcher startup time.
	UE_LOG(LogAssetRegistry, Log, TEXT("FAssetRegistry took %0.4f seconds to start up"), FPlatformTime::Seconds() - StartupStartTime);

#if WITH_EDITOR
	// In-game doesn't listen for directory changes
	if (GIsEditor)
	{
		FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();

		if (DirectoryWatcher)
		{
			TArray<FString> RootContentPaths;
			FPackageName::QueryRootContentPaths(RootContentPaths);
			for (TArray<FString>::TConstIterator RootPathIt(RootContentPaths); RootPathIt; ++RootPathIt)
			{
				const FString& RootPath = *RootPathIt;
				const FString& ContentFolder = FPackageName::LongPackageNameToFilename(RootPath);

				// A missing directory here could be due to a plugin that specifies it contains content, yet has no content yet. PluginManager
				// Mounts these folders anyway which results in them being returned from QueryRootContentPaths
				if (IFileManager::Get().DirectoryExists(*ContentFolder))
				{
					FDelegateHandle NewHandle;
					DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(ContentFolder, IDirectoryWatcher::FDirectoryChanged::CreateUObject(this, &UAssetRegistryImpl::OnDirectoryChanged),
																			  NewHandle, IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges);
					OnDirectoryChangedDelegateHandles.Add(ContentFolder, NewHandle);
				}
			}
		}
	}



	if (GConfig)
	{
		GConfig->GetBool(TEXT("AssetRegistry"), TEXT("bUpdateDiskCacheAfterLoad"), bUpdateDiskCacheAfterLoad, GEngineIni);
	}

	if (bUpdateDiskCacheAfterLoad)
	{
		FCoreUObjectDelegates::OnAssetLoaded.AddUObject(this, &UAssetRegistryImpl::OnAssetLoaded);
	}
#endif // WITH_EDITOR

	// Listen for new content paths being added or removed at runtime.  These are usually plugin-specific asset paths that
	// will be loaded a bit later on.
	FPackageName::OnContentPathMounted().AddUObject(this, &UAssetRegistryImpl::OnContentPathMounted);
	FPackageName::OnContentPathDismounted().AddUObject(this, &UAssetRegistryImpl::OnContentPathDismounted);

	// If we were called before engine has fully initialized, refresh classes on initialize. If not this won't do anything as it already happened
	FCoreDelegates::OnPostEngineInit.AddUObject(this, &UAssetRegistryImpl::RefreshNativeClasses);

	InitRedirectors();
}

bool UAssetRegistryImpl::ResolveRedirect(const FString& InPackageName, FString& OutPackageName)
{
	int32 DotIndex = InPackageName.Find(TEXT("."), ESearchCase::CaseSensitive);

	FString ContainerPackageName; 
	const FString* PackageNamePtr = &InPackageName; // don't return this
	if (DotIndex != INDEX_NONE)
	{
		ContainerPackageName = InPackageName.Left(DotIndex);
		PackageNamePtr = &ContainerPackageName;
	}
	const FString& PackageName = *PackageNamePtr;

	for (const FAssetRegistryPackageRedirect& PackageRedirect : PackageRedirects)
	{
		if (PackageName.Compare(PackageRedirect.SourcePackageName) == 0)
		{
			OutPackageName = InPackageName.Replace(*PackageRedirect.SourcePackageName, *PackageRedirect.DestPackageName);
			return true;
		}
	}
	return false;
}

void UAssetRegistryImpl::InitRedirectors()
{
	// plugins can't initialize redirectors in the editor, it will mess up the saving of content.
	if ( GIsEditor )
	{
		return;
	}

	TArray<TSharedRef<IPlugin>> EnabledPlugins = IPluginManager::Get().GetEnabledPlugins();
	for (TSharedRef<IPlugin> Plugin : EnabledPlugins)
	{
		FString PluginConfigFilename = FString::Printf(TEXT("%s%s/%s.ini"), *FPaths::GeneratedConfigDir(), ANSI_TO_TCHAR(FPlatformProperties::PlatformName()), *Plugin->GetName() );
		
		bool bShouldRemap = false;
		
		if ( !GConfig->GetBool(TEXT("PluginSettings"), TEXT("RemapPluginContentToGame"), bShouldRemap, PluginConfigFilename) )
		{
			continue;
		}

		if (!bShouldRemap)
		{
			continue;
		}

		// if we are -game in editor build we might need to initialize the asset registry manually for this plugin
		if (!FPlatformProperties::RequiresCookedData() && IsRunningGame())
		{
			TArray<FString> PathsToSearch;
			
			FString RootPackageName = FString::Printf(TEXT("/%s/"), *Plugin->GetName());
			PathsToSearch.Add(RootPackageName);

			const bool bForceRescan = false;
			ScanPathsAndFilesSynchronous(PathsToSearch, TArray<FString>(), bForceRescan, EAssetDataCacheMode::UseModularCache);
		}
		
		FName PluginPackageName = FName(*FString::Printf(TEXT("/%s/"), *Plugin->GetName()));
		TArray<FAssetData> AssetList;
		GetAssetsByPath(PluginPackageName, AssetList, true, false);

#if 1 // new way
		for (const FAssetData& Asset : AssetList)
		{
			FString NewPackageNameString = Asset.PackageName.ToString();
			FString RootPackageName = FString::Printf(TEXT("/%s/"), *Plugin->GetName());

			FString OriginalPackageNameString = NewPackageNameString.Replace(*RootPackageName, TEXT("/Game/"));


			PackageRedirects.Add(FAssetRegistryPackageRedirect(OriginalPackageNameString, NewPackageNameString));

		}


		FCoreDelegates::FResolvePackageNameDelegate PackageResolveDelegate;
		PackageResolveDelegate.BindUObject(this, &UAssetRegistryImpl::ResolveRedirect);
		FCoreDelegates::PackageNameResolvers.Add(PackageResolveDelegate);
#else
		TArray<FCoreRedirect> PackageRedirects;

		for ( const FAssetData& Asset : AssetList )
		{
			FString NewPackageNameString = Asset.PackageName.ToString();
			FString RootPackageName = FString::Printf(TEXT("/%s/"), *Plugin->GetName());

			FString OriginalPackageNameString = NewPackageNameString.Replace(*RootPackageName, TEXT("/Game/"));


			PackageRedirects.Add( FCoreRedirect(ECoreRedirectFlags::Type_Package, OriginalPackageNameString, NewPackageNameString) );

		}


		FCoreRedirects::AddRedirectList(PackageRedirects, Plugin->GetName() );
#endif
	}
}

void UAssetRegistryImpl::InitializeSerializationOptions(FAssetRegistrySerializationOptions& Options, const FString& PlatformIniName) const
{
	if (PlatformIniName.IsEmpty())
	{
		// Use options we already loaded, the first pass for this happens at object creation time so this is always valid when queried externally
		Options = SerializationOptions;
	}
	else
	{
		InitializeSerializationOptionsFromIni(Options, PlatformIniName);
	}
}

void UAssetRegistryImpl::InitializeSerializationOptionsFromIni(FAssetRegistrySerializationOptions& Options, const FString& PlatformIniName) const
{
	FConfigFile* EngineIni = nullptr;
#if WITH_EDITOR
	// Use passed in platform, or current platform if empty
	FConfigFile PlatformEngineIni;
	FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Engine"), true, (!PlatformIniName.IsEmpty() ? *PlatformIniName : ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName())));
	EngineIni = &PlatformEngineIni;
#else
	// In cooked builds, always use the normal engine INI
	EngineIni = GConfig->FindConfigFile(GEngineIni);
#endif

	EngineIni->GetBool(TEXT("AssetRegistry"), TEXT("bSerializeAssetRegistry"), Options.bSerializeAssetRegistry);
	EngineIni->GetBool(TEXT("AssetRegistry"), TEXT("bSerializeDependencies"), Options.bSerializeDependencies);
	EngineIni->GetBool(TEXT("AssetRegistry"), TEXT("bSerializeNameDependencies"), Options.bSerializeSearchableNameDependencies);
	EngineIni->GetBool(TEXT("AssetRegistry"), TEXT("bSerializeManageDependencies"), Options.bSerializeManageDependencies);
	EngineIni->GetBool(TEXT("AssetRegistry"), TEXT("bSerializePackageData"), Options.bSerializePackageData);
	EngineIni->GetBool(TEXT("AssetRegistry"), TEXT("bUseAssetRegistryTagsWhitelistInsteadOfBlacklist"), Options.bUseAssetRegistryTagsWhitelistInsteadOfBlacklist);
	EngineIni->GetBool(TEXT("AssetRegistry"), TEXT("bFilterAssetDataWithNoTags"), Options.bFilterAssetDataWithNoTags);
	EngineIni->GetBool(TEXT("AssetRegistry"), TEXT("bFilterDependenciesWithNoTags"), Options.bFilterDependenciesWithNoTags);
	EngineIni->GetBool(TEXT("AssetRegistry"), TEXT("bFilterSearchableNames"), Options.bFilterSearchableNames);

	TArray<FString> FilterlistItems;
	if (Options.bUseAssetRegistryTagsWhitelistInsteadOfBlacklist)
	{
		EngineIni->GetArray(TEXT("AssetRegistry"), TEXT("CookedTagsWhitelist"), FilterlistItems);
	}
	else
	{
		EngineIni->GetArray(TEXT("AssetRegistry"), TEXT("CookedTagsBlacklist"), FilterlistItems);
	}

	{
		// this only needs to be done once, and only on builds using USE_COMPACT_ASSET_REGISTRY
		TArray<FString> AsFName;
		EngineIni->GetArray(TEXT("AssetRegistry"), TEXT("CookedTagsAsFName"), AsFName);
		TArray<FString> AsPathName;
		EngineIni->GetArray(TEXT("AssetRegistry"), TEXT("CookedTagsAsPathName"), AsPathName);
		TArray<FString> AsLocText;
		EngineIni->GetArray(TEXT("AssetRegistry"), TEXT("CookedTagsAsLocText"), AsLocText);
		FAssetRegistryState::IngestIniSettingsForCompact(AsFName, AsPathName, AsLocText);
	}

	// Takes on the pattern "(Class=SomeClass,Tag=SomeTag)"
	for (const FString& FilterlistItem : FilterlistItems)
	{
		FString TrimmedFilterlistItem = FilterlistItem;
		TrimmedFilterlistItem.TrimStartAndEndInline();
		if (TrimmedFilterlistItem.Left(1) == TEXT("("))
		{
			TrimmedFilterlistItem.RightChopInline(1, false);
		}
		if (TrimmedFilterlistItem.Right(1) == TEXT(")"))
		{
			TrimmedFilterlistItem.LeftChopInline(1, false);
		}

		TArray<FString> Tokens;
		TrimmedFilterlistItem.ParseIntoArray(Tokens, TEXT(","));
		FString ClassName;
		FString TagName;

		for (const FString& Token : Tokens)
		{
			FString KeyString;
			FString ValueString;
			if (Token.Split(TEXT("="), &KeyString, &ValueString))
			{
				KeyString.TrimStartAndEndInline();
				ValueString.TrimStartAndEndInline();
				if (KeyString == TEXT("Class"))
				{
					ClassName = ValueString;
				}
				else if (KeyString == TEXT("Tag"))
				{
					TagName = ValueString;
				}
			}
		}

		if (!ClassName.IsEmpty() && !TagName.IsEmpty())
		{
			FName TagFName = FName(*TagName);

			// Include subclasses if the class is in memory at this time (native classes only)
			UClass* FilterlistClass = Cast<UClass>(StaticFindObject(UClass::StaticClass(), ANY_PACKAGE, *ClassName));
			if (FilterlistClass)
			{
				Options.CookFilterlistTagsByClass.FindOrAdd(FilterlistClass->GetFName()).Add(TagFName);

				TArray<UClass*> DerivedClasses;
				GetDerivedClasses(FilterlistClass, DerivedClasses);
				for (UClass* DerivedClass : DerivedClasses)
				{
					Options.CookFilterlistTagsByClass.FindOrAdd(DerivedClass->GetFName()).Add(TagFName);
				}
			}
			else
			{
				// Class is not in memory yet. Just add an explicit filter.
				// Automatically adding subclasses of non-native classes is not supported.
				// In these cases, using Class=* is usually sufficient
				Options.CookFilterlistTagsByClass.FindOrAdd(FName(*ClassName)).Add(TagFName);
			}
		}
	}
}

void UAssetRegistryImpl::CollectCodeGeneratorClasses()
{
	// Work around the fact we don't reference Engine module directly
	UClass* BlueprintCoreClass = Cast<UClass>(StaticFindObject(UClass::StaticClass(), ANY_PACKAGE, TEXT("BlueprintCore")));
	if (BlueprintCoreClass)
	{
		ClassGeneratorNames.Add(BlueprintCoreClass->GetFName());

		TArray<UClass*> BlueprintCoreDerivedClasses;
		GetDerivedClasses(BlueprintCoreClass, BlueprintCoreDerivedClasses);
		for (UClass* BPCoreClass : BlueprintCoreDerivedClasses)
		{
			ClassGeneratorNames.Add(BPCoreClass->GetFName());
		}
	}
}

void UAssetRegistryImpl::RefreshNativeClasses()
{
	// Native classes have changed so reinitialize code generator and serialization options
	CollectCodeGeneratorClasses();

	// Read default serialization options
	InitializeSerializationOptionsFromIni(SerializationOptions, FString());
}

UAssetRegistryImpl::~UAssetRegistryImpl()
{
	// Make sure the asset search thread is closed
	if (BackgroundAssetSearch.IsValid())
	{
		BackgroundAssetSearch->EnsureCompletion();
		BackgroundAssetSearch.Reset();
	}

	// Stop listening for content mount point events
	FPackageName::OnContentPathMounted().RemoveAll(this);
	FPackageName::OnContentPathDismounted().RemoveAll(this);
	FCoreDelegates::OnPostEngineInit.RemoveAll(this);

#if WITH_EDITOR
	if (GIsEditor)
	{
		// If the directory module is still loaded, unregister any delegates
		if (FModuleManager::Get().IsModuleLoaded("DirectoryWatcher"))
		{
			FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::GetModuleChecked<FDirectoryWatcherModule>("DirectoryWatcher");
			IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();

			if (DirectoryWatcher)
			{
				TArray<FString> RootContentPaths;
				FPackageName::QueryRootContentPaths(RootContentPaths);
				for (TArray<FString>::TConstIterator RootPathIt(RootContentPaths); RootPathIt; ++RootPathIt)
				{
					const FString& RootPath = *RootPathIt;
					const FString& ContentFolder = FPackageName::LongPackageNameToFilename(RootPath);
					DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(ContentFolder, OnDirectoryChangedDelegateHandles.FindRef(ContentFolder));
					OnDirectoryChangedDelegateHandles.Remove(ContentFolder);
				}
			}
		}
	}

	if (bUpdateDiskCacheAfterLoad)
	{
		FCoreUObjectDelegates::OnAssetLoaded.RemoveAll(this);
	}
#endif // WITH_EDITOR

	// Clear all listeners
	AssetAddedEvent.Clear();
	AssetRemovedEvent.Clear();
	AssetRenamedEvent.Clear();
	InMemoryAssetCreatedEvent.Clear();
	InMemoryAssetDeletedEvent.Clear();
	FileLoadedEvent.Clear();
	FileLoadProgressUpdatedEvent.Clear();
}

UAssetRegistryImpl& UAssetRegistryImpl::Get()
{
	FAssetRegistryModule& Module = FModuleManager::GetModuleChecked<FAssetRegistryModule>("AssetRegistry");
	return static_cast<UAssetRegistryImpl&>(Module.Get());
}

void UAssetRegistryImpl::SearchAllAssets(bool bSynchronousSearch)
{
	// Mark the time before the first search started
	FullSearchStartTime = FPlatformTime::Seconds();

	// Figure out what all of the root asset directories are.  This will include Engine content, Game content, but also may include
	// mounted content directories for one or more plugins.	Also keep in mind that plugins may become loaded later on.  We'll listen
	// for that via a delegate, and add those directories to scan later as them come in.
	TArray<FString> PathsToSearch;
	FPackageName::QueryRootContentPaths(PathsToSearch);

	// Start the asset search (synchronous in commandlets)
	if (bSynchronousSearch)
	{
#if WITH_EDITOR
		if (IsLoadingAssets())
		{
			// Force a flush of the current gatherer instead
			UE_LOG(LogAssetRegistry, Log, TEXT("Flushing asset discovery search because of synchronous request, this can take several seconds..."));

			while (IsLoadingAssets())
			{
				Tick(-1.0f);

				FThreadHeartBeat::Get().HeartBeat();
				FPlatformProcess::SleepNoStats(0.0001f);
			}
		}
		else
#endif
		{
			const bool bForceRescan = false;
			ScanPathsAndFilesSynchronous(PathsToSearch, TArray<FString>(), bForceRescan, EAssetDataCacheMode::UseMonolithicCache);
		}


#if WITH_EDITOR
		if (IsRunningCommandlet())
		{
			// Update redirectors
			UpdateRedirectCollector();
		}
#endif
	}
	else if (!BackgroundAssetSearch.IsValid())
	{
		// if the BackgroundAssetSearch is already valid then we have already called it before
		BackgroundAssetSearch = MakeShareable(new FAssetDataGatherer(PathsToSearch, TArray<FString>(), bSynchronousSearch, EAssetDataCacheMode::UseMonolithicCache));
	}
}

bool UAssetRegistryImpl::HasAssets(const FName PackagePath, const bool bRecursive) const
{
	bool bHasAssets = State.HasAssets(PackagePath);

	if (!bHasAssets && bRecursive)
	{
		CachedPathTree.EnumerateSubPaths(PackagePath, [this, &bHasAssets](FName SubPath)
		{
			bHasAssets = State.HasAssets(SubPath);
			return !bHasAssets;
		});
	}

	return bHasAssets;
}

bool UAssetRegistryImpl::GetAssetsByPackageName(FName PackageName, TArray<FAssetData>& OutAssetData, bool bIncludeOnlyOnDiskAssets) const
{
	FARFilter Filter;
	Filter.PackageNames.Add(PackageName);
	Filter.bIncludeOnlyOnDiskAssets = bIncludeOnlyOnDiskAssets;
	return GetAssets(Filter, OutAssetData);
}

bool UAssetRegistryImpl::GetAssetsByPath(FName PackagePath, TArray<FAssetData>& OutAssetData, bool bRecursive, bool bIncludeOnlyOnDiskAssets) const
{
	FARFilter Filter;
	Filter.bRecursivePaths = bRecursive;
	Filter.PackagePaths.Add(PackagePath);
	Filter.bIncludeOnlyOnDiskAssets = bIncludeOnlyOnDiskAssets;
	return GetAssets(Filter, OutAssetData);
}

bool UAssetRegistryImpl::GetAssetsByClass(FName ClassName, TArray<FAssetData>& OutAssetData, bool bSearchSubClasses) const
{
	FARFilter Filter;
	Filter.ClassNames.Add(ClassName);
	Filter.bRecursiveClasses = bSearchSubClasses;
	return GetAssets(Filter, OutAssetData);
}

bool UAssetRegistryImpl::GetAssetsByTags(const TArray<FName>& AssetTags, TArray<FAssetData>& OutAssetData) const
{
	FARFilter Filter;
	for (const FName AssetTag : AssetTags)
	{
		Filter.TagsAndValues.Add(AssetTag);
	}
	return GetAssets(Filter, OutAssetData);
}

bool UAssetRegistryImpl::GetAssetsByTagValues(const TMultiMap<FName, FString>& AssetTagsAndValues, TArray<FAssetData>& OutAssetData) const
{
	FARFilter Filter;
	for (const auto& AssetTagsAndValue : AssetTagsAndValues)
	{
		Filter.TagsAndValues.Add(AssetTagsAndValue.Key, AssetTagsAndValue.Value);
	}
	return GetAssets(Filter, OutAssetData);
}

bool UAssetRegistryImpl::GetAssets(const FARFilter& InFilter, TArray<FAssetData>& OutAssetData) const
{
	const double GetAssetsStartTime = FPlatformTime::Seconds();

	const bool bResult = EnumerateAssets(InFilter, [&OutAssetData](const FAssetData& AssetData)
	{
		OutAssetData.Emplace(AssetData);
		return true;
	});

	UE_LOG(LogAssetRegistry, Verbose, TEXT("GetAssets completed in %0.4f seconds"), FPlatformTime::Seconds() - GetAssetsStartTime);

	return bResult;
}

bool UAssetRegistryImpl::EnumerateAssets(const FARFilter& InFilter, TFunctionRef<bool(const FAssetData&)> Callback) const
{
	FARCompiledFilter CompiledFilter;
	CompileFilter(InFilter, CompiledFilter);
	return EnumerateAssets(CompiledFilter, Callback);
}

bool UAssetRegistryImpl::EnumerateAssets(const FARCompiledFilter& InFilter, TFunctionRef<bool(const FAssetData&)> Callback) const
{
	// Verify filter input. If all assets are needed, use EnumerateAllAssets() instead.
	if (InFilter.IsEmpty())
	{
		return false;
	}

	if (!FAssetRegistryState::IsFilterValid(InFilter))
	{
		return false;
	}

	// Start with in memory assets
	TSet<FName> PackagesToSkip = CachedEmptyPackages;
	if (!InFilter.bIncludeOnlyOnDiskAssets)
	{
		// Reusable structures to avoid memory allocations
		TArray<UObject::FAssetRegistryTag> ObjectTags;

		auto FilterInMemoryObjectLambda = [&](const UObject* Obj, bool& OutContinue)
		{
			if (Obj->IsAsset())
			{
				UPackage* InMemoryPackage = Obj->GetOutermost();

				// Skip assets that were loaded for diffing
				if (InMemoryPackage->HasAnyPackageFlags(PKG_ForDiffing))
				{
					return;
				}

				// Package name
				const FName PackageName = InMemoryPackage->GetFName();

				PackagesToSkip.Add(PackageName);

				if (InFilter.PackageNames.Num() && !InFilter.PackageNames.Contains(PackageName))
				{
					return;
				}

				// Object Path
				if (InFilter.ObjectPaths.Num() > 0)
				{
					const FName ObjectPath = FName(*Obj->GetPathName(), FNAME_Find);
					if (!InFilter.ObjectPaths.Contains(ObjectPath))
					{
						return;
					}
				}

				// Package path
				const FName PackagePath = FName(*FPackageName::GetLongPackagePath(InMemoryPackage->GetName()));
				if (InFilter.PackagePaths.Num() > 0 && !InFilter.PackagePaths.Contains(PackagePath))
				{
					return;
				}

				// Tags and values
				check(ObjectTags.Num() == 0);
				Obj->GetAssetRegistryTags(ObjectTags);
				if (InFilter.TagsAndValues.Num() > 0)
				{
					bool bMatch = false;
					for (auto FilterTagIt = InFilter.TagsAndValues.CreateConstIterator(); FilterTagIt; ++FilterTagIt)
					{
						const FName Tag = FilterTagIt.Key();
						const TOptional<FString>& Value = FilterTagIt.Value();

						for (UObject::FAssetRegistryTag& AssetRegistryTag : ObjectTags)
						{
							if (AssetRegistryTag.Name == Tag)
							{
								if (!Value.IsSet() || AssetRegistryTag.Value == Value.GetValue())
								{
									bMatch = true;
								}

								break;
							}
						}

						if (bMatch)
						{
							break;
						}
					}

					if (!bMatch)
					{
						ObjectTags.Reset();
						return;
					}
				}

				FAssetDataTagMap TagMap;
				for (UObject::FAssetRegistryTag& AssetRegistryTag : ObjectTags)
				{
					if (AssetRegistryTag.Name != NAME_None && !AssetRegistryTag.Value.IsEmpty())
					{
						// Don't add empty tags
						TagMap.Add(AssetRegistryTag.Name, AssetRegistryTag.Value);
					}
				}
				ObjectTags.Reset();

				// This asset is in memory and passes all filters
				OutContinue = Callback(FAssetData(PackageName, PackagePath, Obj->GetFName(), Obj->GetClass()->GetFName(), MoveTemp(TagMap), InMemoryPackage->GetChunkIDs(), InMemoryPackage->GetPackageFlags()));
			}
		};

		// Iterate over all in-memory assets to find the ones that pass the filter components
		if (InFilter.ClassNames.Num() > 0)
		{
			TArray<UObject*> InMemoryObjects;
			for (FName ClassName : InFilter.ClassNames)
			{
				UClass* Class = FindObjectFast<UClass>(nullptr, ClassName, false, true, RF_NoFlags);
				if (Class != nullptr)
				{
					GetObjectsOfClass(Class, InMemoryObjects, false, RF_NoFlags);
				}
			}

			for (UObject* Object : InMemoryObjects)
			{
				bool bContinue = true;
				FilterInMemoryObjectLambda(Object, bContinue);
				if (!bContinue)
				{
					return true;
				}
			}
		}
		else
		{
			for (FObjectIterator ObjIt; ObjIt; ++ObjIt)
			{
				bool bContinue = true;
				FilterInMemoryObjectLambda(*ObjIt, bContinue);
				if (!bContinue)
				{
					return true;
				}
			}
		}
	}

	State.EnumerateAssets(InFilter, PackagesToSkip, Callback);

	return true;
}

FAssetData UAssetRegistryImpl::GetAssetByObjectPath(const FName ObjectPath, bool bIncludeOnlyOnDiskAssets) const
{
	if (!bIncludeOnlyOnDiskAssets)
	{
		UObject* Asset = FindObject<UObject>(nullptr, *ObjectPath.ToString());

		if (Asset != nullptr)
		{
			return FAssetData(Asset);
		}
	}

	const FAssetData* FoundData = State.GetAssetByObjectPath(ObjectPath);

	if (FoundData)
	{
		return *FoundData;
	}
	return FAssetData();
}

bool UAssetRegistryImpl::GetAllAssets(TArray<FAssetData>& OutAssetData, bool bIncludeOnlyOnDiskAssets) const
{
	const double GetAllAssetsStartTime = FPlatformTime::Seconds();

	const bool bResult = EnumerateAllAssets([&OutAssetData](const FAssetData& AssetData)
	{
		OutAssetData.Emplace(AssetData);
		return true;
	}, bIncludeOnlyOnDiskAssets);

	UE_LOG(LogAssetRegistry, VeryVerbose, TEXT("GetAllAssets completed in %0.4f seconds"), FPlatformTime::Seconds() - GetAllAssetsStartTime);
	
	return bResult;
}

bool UAssetRegistryImpl::EnumerateAllAssets(TFunctionRef<bool(const FAssetData&)> Callback, bool bIncludeOnlyOnDiskAssets) const
{
	TSet<FName> PackageNamesToSkip = CachedEmptyPackages;

	// All in memory assets
	if (!bIncludeOnlyOnDiskAssets)
	{
		for (FObjectIterator ObjIt; ObjIt; ++ObjIt)
		{
			if (ObjIt->IsAsset())
			{
				const FAssetData AssetData(*ObjIt);
				if (!Callback(AssetData))
				{
					return true;
				}
				PackageNamesToSkip.Add(AssetData.PackageName);
			}
		}
	}

	State.EnumerateAllAssets(PackageNamesToSkip, Callback);

	return true;
}

bool UAssetRegistryImpl::GetDependencies(const FAssetIdentifier& AssetIdentifier, TArray<FAssetIdentifier>& OutDependencies, EAssetRegistryDependencyType::Type InDependencyType) const
{
	return State.GetDependencies(AssetIdentifier, OutDependencies, InDependencyType);
}

bool UAssetRegistryImpl::GetDependencies(FName PackageName, TArray<FName>& OutDependencies, EAssetRegistryDependencyType::Type InDependencyType) const
{
	TArray<FAssetIdentifier> TempDependencies;

	if (GetDependencies(FAssetIdentifier(PackageName), TempDependencies, InDependencyType))
	{
		for (const FAssetIdentifier& AssetId : TempDependencies)
		{
			if (AssetId.PackageName != NAME_None)
			{
				OutDependencies.AddUnique(AssetId.PackageName);
			}
		}
		return true;
	}

	return false;
}

bool UAssetRegistryImpl::GetReferencers(const FAssetIdentifier& AssetIdentifier, TArray<FAssetIdentifier>& OutReferencers, EAssetRegistryDependencyType::Type InReferenceType) const
{
	return State.GetReferencers(AssetIdentifier, OutReferencers, InReferenceType);
}

bool UAssetRegistryImpl::GetReferencers(FName PackageName, TArray<FName>& OutReferencers, EAssetRegistryDependencyType::Type InReferenceType) const
{
	TArray<FAssetIdentifier> TempReferencers;

	if (GetReferencers(FAssetIdentifier(PackageName), TempReferencers, InReferenceType))
	{
		for (const FAssetIdentifier& AssetId : TempReferencers)
		{
			if (AssetId.PackageName != NAME_None)
			{
				OutReferencers.AddUnique(AssetId.PackageName);
			}
		}
		return true;
	}

	return false;
}

const FAssetPackageData* UAssetRegistryImpl::GetAssetPackageData(FName PackageName) const
{
	return State.GetAssetPackageData(PackageName);
}

FName UAssetRegistryImpl::GetRedirectedObjectPath(const FName ObjectPath) const
{
	FString RedirectedPath = ObjectPath.ToString();
	FAssetData DestinationData = GetAssetByObjectPath(ObjectPath);
	TSet<FString> SeenPaths;
	SeenPaths.Add(RedirectedPath);

	// Need to follow chain of redirectors
	while (DestinationData.IsRedirector())
	{
		if (DestinationData.GetTagValue("DestinationObject", RedirectedPath))
		{
			ConstructorHelpers::StripObjectClass(RedirectedPath);
			if (SeenPaths.Contains(RedirectedPath))
			{
				// Recursive, bail
				DestinationData = FAssetData();
			}
			else
			{
				SeenPaths.Add(RedirectedPath);
				DestinationData = GetAssetByObjectPath(FName(*RedirectedPath), true);
			}
		}
		else
		{
			// Can't extract
			DestinationData = FAssetData();
		}
	}

	return FName(*RedirectedPath);
}

void UAssetRegistryImpl::StripAssetRegistryKeyForObject(FName ObjectPath, FName Key)
{
	return State.StripAssetRegistryKeyForObject(ObjectPath, Key);
}

bool UAssetRegistryImpl::GetAncestorClassNames(FName ClassName, TArray<FName>& OutAncestorClassNames) const
{
	// Assume we found the class unless there is an error
	bool bFoundClass = true;
	UpdateTemporaryCaches();

	// Make sure the requested class is in the inheritance map
	if (!TempCachedInheritanceMap.Contains(ClassName))
	{
		bFoundClass = false;
	}
	else
	{
		// Now follow the map pairs until we cant find any more parents
		const FName* CurrentClassName = &ClassName;
		const uint32 MaxInheritanceDepth = 65536;
		uint32 CurrentInheritanceDepth = 0;
		while (CurrentInheritanceDepth < MaxInheritanceDepth && CurrentClassName != nullptr)
		{
			CurrentClassName = TempCachedInheritanceMap.Find(*CurrentClassName);

			if (CurrentClassName)
			{
				if (*CurrentClassName == NAME_None)
				{
					// No parent, we are at the root
					CurrentClassName = nullptr;
				}
				else
				{
					OutAncestorClassNames.Add(*CurrentClassName);
				}
			}
			CurrentInheritanceDepth++;
		}

		if (CurrentInheritanceDepth == MaxInheritanceDepth)
		{
			UE_LOG(LogAssetRegistry, Error, TEXT("IsChildClass exceeded max inheritance depth. There is probably an infinite loop of parent classes."));
			bFoundClass = false;
		}
	}

	ClearTemporaryCaches();
	return bFoundClass;
}

void UAssetRegistryImpl::GetDerivedClassNames(const TArray<FName>& ClassNames, const TSet<FName>& ExcludedClassNames, TSet<FName>& OutDerivedClassNames) const
{
	GetSubClasses(ClassNames, ExcludedClassNames, OutDerivedClassNames);
}

void UAssetRegistryImpl::GetAllCachedPaths(TArray<FString>& OutPathList) const
{
	EnumerateAllCachedPaths([&OutPathList](FName Path)
	{
		OutPathList.Emplace(Path.ToString());
		return true;
	});
}

void UAssetRegistryImpl::EnumerateAllCachedPaths(TFunctionRef<bool(FString)> Callback) const
{
	EnumerateAllCachedPaths([&Callback](FName Path)
	{
		return Callback(Path.ToString());
	});
}

void UAssetRegistryImpl::EnumerateAllCachedPaths(TFunctionRef<bool(FName)> Callback) const
{
	CachedPathTree.EnumerateAllPaths(Callback);
}

void UAssetRegistryImpl::GetSubPaths(const FString& InBasePath, TArray<FString>& OutPathList, bool bInRecurse) const
{
	EnumerateSubPaths(*InBasePath, [&OutPathList](FName Path)
	{
		OutPathList.Emplace(Path.ToString());
		return true;
	}, bInRecurse);
}

void UAssetRegistryImpl::EnumerateSubPaths(const FString& InBasePath, TFunctionRef<bool(FString)> Callback, bool bInRecurse) const
{
	EnumerateSubPaths(*InBasePath, [&Callback](FName Path)
	{
		return Callback(Path.ToString());
	}, bInRecurse);
}

void UAssetRegistryImpl::EnumerateSubPaths(const FName InBasePath, TFunctionRef<bool(FName)> Callback, bool bInRecurse) const
{
	CachedPathTree.EnumerateSubPaths(InBasePath, Callback, bInRecurse);
}

void UAssetRegistryImpl::RunAssetsThroughFilter(TArray<FAssetData>& AssetDataList, const FARFilter& Filter) const
{
	RunAssetsThroughFilterImpl(AssetDataList, Filter, EARFilterMode::Inclusive);
}

void UAssetRegistryImpl::UseFilterToExcludeAssets(TArray<FAssetData>& AssetDataList, const FARFilter& Filter) const
{
	RunAssetsThroughFilterImpl(AssetDataList, Filter, EARFilterMode::Exclusive);
}

bool UAssetRegistryImpl::IsAssetIncludedByFilter(const FAssetData& AssetData, const FARCompiledFilter& Filter) const
{
	return RunAssetThroughFilterImpl(AssetData, Filter, EARFilterMode::Inclusive);
}

bool UAssetRegistryImpl::IsAssetExcludedByFilter(const FAssetData& AssetData, const FARCompiledFilter& Filter) const
{
	return RunAssetThroughFilterImpl(AssetData, Filter, EARFilterMode::Exclusive);
}

bool UAssetRegistryImpl::RunAssetThroughFilterImpl(const FAssetData& AssetData, const FARCompiledFilter& Filter, const EARFilterMode FilterMode) const
{
	const bool bPassFilterValue = FilterMode == EARFilterMode::Inclusive;
	if (Filter.IsEmpty())
	{
		return bPassFilterValue;
	}

	const bool bFilterResult = RunAssetThroughFilterImpl_Unchecked(AssetData, Filter, bPassFilterValue);
	return bFilterResult == bPassFilterValue;
}

bool UAssetRegistryImpl::RunAssetThroughFilterImpl_Unchecked(const FAssetData& AssetData, const FARCompiledFilter& Filter, const bool bPassFilterValue) const
{
	// Package Names
	if (Filter.PackageNames.Num() > 0)
	{
		const bool bPassesPackageNames = Filter.PackageNames.Contains(AssetData.PackageName);
		if (bPassesPackageNames != bPassFilterValue)
		{
			return !bPassFilterValue;
		}
	}

	// Package Paths
	if (Filter.PackagePaths.Num() > 0)
	{
		const bool bPassesPackagePaths = Filter.PackagePaths.Contains(AssetData.PackagePath);
		if (bPassesPackagePaths != bPassFilterValue)
		{
			return !bPassFilterValue;
		}
	}

	// ObjectPaths
	if (Filter.ObjectPaths.Num() > 0)
	{
		const bool bPassesObjectPaths = Filter.ObjectPaths.Contains(AssetData.ObjectPath);
		if (bPassesObjectPaths != bPassFilterValue)
		{
			return !bPassFilterValue;
		}
	}

	// Classes
	if (Filter.ClassNames.Num() > 0)
	{
		const bool bPassesClasses = Filter.ClassNames.Contains(AssetData.AssetClass);
		if (bPassesClasses != bPassFilterValue)
		{
			return !bPassFilterValue;
		}
	}

	// Tags and values
	if (Filter.TagsAndValues.Num() > 0)
	{
		bool bPassesTags = false;
		for (const auto& TagsAndValuePair : Filter.TagsAndValues)
		{
			bPassesTags |= TagsAndValuePair.Value.IsSet()
				? AssetData.TagsAndValues.ContainsKeyValue(TagsAndValuePair.Key, TagsAndValuePair.Value.GetValue())
				: AssetData.TagsAndValues.Contains(TagsAndValuePair.Key);
			if (bPassesTags)
			{
				break;
			}
		}
		if (bPassesTags != bPassFilterValue)
		{
			return !bPassFilterValue;
		}
	}

	return bPassFilterValue;
}

void UAssetRegistryImpl::RunAssetsThroughFilterImpl(TArray<FAssetData>& AssetDataList, const FARFilter& Filter, const EARFilterMode FilterMode) const
{
	if (Filter.IsEmpty())
	{
		return;
	}

	FARCompiledFilter CompiledFilter;
	CompileFilter(Filter, CompiledFilter);
	if (!FAssetRegistryState::IsFilterValid(CompiledFilter))
	{
		return;
	}

	const int32 OriginalArrayCount = AssetDataList.Num();
	const bool bPassFilterValue = FilterMode == EARFilterMode::Inclusive;

	// Spin the array backwards to minimize the number of elements that are repeatedly moved down
	for (int32 AssetDataIndex = AssetDataList.Num() - 1; AssetDataIndex >= 0; --AssetDataIndex)
	{
		const bool bFilterResult = RunAssetThroughFilterImpl_Unchecked(AssetDataList[AssetDataIndex], CompiledFilter, bPassFilterValue);
		if (bFilterResult != bPassFilterValue)
		{
			AssetDataList.RemoveAt(AssetDataIndex, 1, /*bAllowShrinking*/false);
			continue;
		}
	}

	if (OriginalArrayCount > AssetDataList.Num())
	{
		AssetDataList.Shrink();
	}
}

void UAssetRegistryImpl::ExpandRecursiveFilter(const FARFilter& InFilter, FARFilter& ExpandedFilter) const
{
	FARCompiledFilter CompiledFilter;
	CompileFilter(InFilter, CompiledFilter);

	ExpandedFilter.Clear();
	ExpandedFilter.PackageNames = CompiledFilter.PackageNames.Array();
	ExpandedFilter.PackagePaths = CompiledFilter.PackagePaths.Array();
	ExpandedFilter.ObjectPaths = CompiledFilter.ObjectPaths.Array();
	ExpandedFilter.ClassNames = CompiledFilter.ClassNames.Array();
	ExpandedFilter.TagsAndValues = CompiledFilter.TagsAndValues;
	ExpandedFilter.bIncludeOnlyOnDiskAssets = CompiledFilter.bIncludeOnlyOnDiskAssets;
}

void UAssetRegistryImpl::CompileFilter(const FARFilter& InFilter, FARCompiledFilter& OutCompiledFilter) const
{
	OutCompiledFilter.Clear();
	OutCompiledFilter.PackageNames.Append(InFilter.PackageNames);
	OutCompiledFilter.PackagePaths.Append(InFilter.PackagePaths);
	OutCompiledFilter.ObjectPaths.Append(InFilter.ObjectPaths);
	OutCompiledFilter.ClassNames.Append(InFilter.ClassNames);
	OutCompiledFilter.TagsAndValues = InFilter.TagsAndValues;
	OutCompiledFilter.bIncludeOnlyOnDiskAssets = InFilter.bIncludeOnlyOnDiskAssets;

	if (InFilter.bRecursivePaths)
	{
		// Add the sub-paths of all the input paths to the expanded list
		for (const FName PackagePath : InFilter.PackagePaths)
		{
			CachedPathTree.GetSubPaths(PackagePath, OutCompiledFilter.PackagePaths);
		}
	}

	if (InFilter.bRecursiveClasses)
	{
		// Add the sub-classes of all the input classes to the expanded list, excluding any that were requested
		if (InFilter.RecursiveClassesExclusionSet.Num() > 0 && InFilter.ClassNames.Num() == 0)
		{
			TArray<FName> ClassNamesObject;
			ClassNamesObject.Add(UObject::StaticClass()->GetFName());

			GetSubClasses(ClassNamesObject, InFilter.RecursiveClassesExclusionSet, OutCompiledFilter.ClassNames);
		}
		else
		{
			GetSubClasses(InFilter.ClassNames, InFilter.RecursiveClassesExclusionSet, OutCompiledFilter.ClassNames);
		}
	}
}

EAssetAvailability::Type UAssetRegistryImpl::GetAssetAvailability(const FAssetData& AssetData) const
{
	IPlatformChunkInstall* ChunkInstall = FPlatformMisc::GetPlatformChunkInstall();

	EChunkLocation::Type BestLocation = EChunkLocation::DoesNotExist;

	// check all chunks to see which has the best locality
	for (int32 PakchunkId : AssetData.ChunkIDs)
	{
		EChunkLocation::Type ChunkLocation = ChunkInstall->GetPakchunkLocation(PakchunkId);

		// if we find one in the best location, early out
		if (ChunkLocation == EChunkLocation::BestLocation)
		{
			BestLocation = ChunkLocation;
			break;
		}

		if (ChunkLocation > BestLocation)
		{
			BestLocation = ChunkLocation;
		}
	}

	switch (BestLocation)
	{
	case EChunkLocation::LocalFast:
		return EAssetAvailability::LocalFast;
	case EChunkLocation::LocalSlow:
		return EAssetAvailability::LocalSlow;
	case EChunkLocation::NotAvailable:
		return EAssetAvailability::NotAvailable;
	case EChunkLocation::DoesNotExist:
		return EAssetAvailability::DoesNotExist;
	default:
		check(0);
		return EAssetAvailability::LocalFast;
	}
}

float UAssetRegistryImpl::GetAssetAvailabilityProgress(const FAssetData& AssetData, EAssetAvailabilityProgressReportingType::Type ReportType) const
{
	IPlatformChunkInstall* ChunkInstall = FPlatformMisc::GetPlatformChunkInstall();
	EChunkProgressReportingType::Type ChunkReportType = GetChunkAvailabilityProgressType(ReportType);

	bool IsPercentageComplete = (ChunkReportType == EChunkProgressReportingType::PercentageComplete) ? true : false;
	check(ReportType == EAssetAvailabilityProgressReportingType::PercentageComplete || ReportType == EAssetAvailabilityProgressReportingType::ETA);

	float BestProgress = MAX_FLT;

	// check all chunks to see which has the best time remaining
	for (int32 PakchunkID : AssetData.ChunkIDs)
	{
		float Progress = ChunkInstall->GetChunkProgress(PakchunkID, ChunkReportType);

		// need to flip percentage completes for the comparison
		if (IsPercentageComplete)
		{
			Progress = 100.0f - Progress;
		}

		if (Progress <= 0.0f)
		{
			BestProgress = 0.0f;
			break;
		}

		if (Progress < BestProgress)
		{
			BestProgress = Progress;
		}
	}

	// unflip percentage completes
	if (IsPercentageComplete)
	{
		BestProgress = 100.0f - BestProgress;
	}
	return BestProgress;
}

bool UAssetRegistryImpl::GetAssetAvailabilityProgressTypeSupported(EAssetAvailabilityProgressReportingType::Type ReportType) const
{
	IPlatformChunkInstall* ChunkInstall = FPlatformMisc::GetPlatformChunkInstall();
	return ChunkInstall->GetProgressReportingTypeSupported(GetChunkAvailabilityProgressType(ReportType));
}

void UAssetRegistryImpl::PrioritizeAssetInstall(const FAssetData& AssetData) const
{
	IPlatformChunkInstall* ChunkInstall = FPlatformMisc::GetPlatformChunkInstall();

	if (AssetData.ChunkIDs.Num() == 0)
	{
		return;
	}

	ChunkInstall->PrioritizePakchunk(AssetData.ChunkIDs[0], EChunkPriority::Immediate);
}

bool UAssetRegistryImpl::AddPath(const FString& PathToAdd)
{
	return AddAssetPath(FName(*PathToAdd));
}

bool UAssetRegistryImpl::RemovePath(const FString& PathToRemove)
{
	return RemoveAssetPath(FName(*PathToRemove));
}

bool UAssetRegistryImpl::PathExists(const FString& PathToTest) const
{
	return PathExists(FName(*PathToTest));
}

bool UAssetRegistryImpl::PathExists(const FName PathToTest) const
{
	return CachedPathTree.PathExists(PathToTest);
}

void UAssetRegistryImpl::ScanPathsSynchronous(const TArray<FString>& InPaths, bool bForceRescan)
{
	ScanPathsAndFilesSynchronous(InPaths, TArray<FString>(), bForceRescan, EAssetDataCacheMode::UseModularCache);
}

void UAssetRegistryImpl::ScanFilesSynchronous(const TArray<FString>& InFilePaths, bool bForceRescan)
{
	ScanPathsAndFilesSynchronous(TArray<FString>(), InFilePaths, bForceRescan, EAssetDataCacheMode::UseModularCache);
}

void UAssetRegistryImpl::PrioritizeSearchPath(const FString& PathToPrioritize)
{
	// Prioritize the background search
	if (BackgroundAssetSearch.IsValid())
	{
		BackgroundAssetSearch->PrioritizeSearchPath(PathToPrioritize);
	}

	// Also prioritize the queue of background search results
	BackgroundAssetResults.Prioritize([&PathToPrioritize](const FAssetData* BackgroundAssetResult)
	{
		return BackgroundAssetResult && BackgroundAssetResult->PackagePath.ToString().StartsWith(PathToPrioritize);
	});
	BackgroundPathResults.Prioritize([&PathToPrioritize](const FString& BackgroundPathResult)
	{
		return BackgroundPathResult.StartsWith(PathToPrioritize);
	});
}

void UAssetRegistryImpl::AssetCreated(UObject* NewAsset)
{
	if (ensure(NewAsset) && NewAsset->IsAsset())
	{
		// Add the newly created object to the package file cache because its filename can already be
		// determined by its long package name.
		// @todo AssetRegistry We are assuming it will be saved in a single asset package.
		UPackage* NewPackage = NewAsset->GetOutermost();

		// Mark this package as newly created.
		NewPackage->SetPackageFlags(PKG_NewlyCreated);

		const FString NewPackageName = NewPackage->GetName();

		// This package not empty, in case it ever was
		RemoveEmptyPackage(NewPackage->GetFName());

		// Add the path to the Path Tree, in case it wasn't already there
		AddAssetPath(*FPackageName::GetLongPackagePath(NewPackageName));

		// Let subscribers know that the new asset was added to the registry
		AssetAddedEvent.Broadcast(FAssetData(NewAsset));

		// Notify listeners that an asset was just created
		InMemoryAssetCreatedEvent.Broadcast(NewAsset);
	}
}

void UAssetRegistryImpl::AssetDeleted(UObject* DeletedAsset)
{
	if (ensure(DeletedAsset) && DeletedAsset->IsAsset())
	{
		UPackage* DeletedObjectPackage = DeletedAsset->GetOutermost();
		if (DeletedObjectPackage != nullptr)
		{
			const FString PackageName = DeletedObjectPackage->GetName();

			// Deleting the last asset in a package causes the package to be garbage collected.
			// If the UPackage object is GCed, it will be considered 'Unloaded' which will cause it to
			// be fully loaded from disk when save is invoked.
			// We want to keep the package around so we can save it empty or delete the file.
			if (UPackage::IsEmptyPackage(DeletedObjectPackage, DeletedAsset))
			{
				AddEmptyPackage(DeletedObjectPackage->GetFName());

				// If there is a package metadata object, clear the standalone flag so the package can be truly emptied upon GC
				if (UMetaData* MetaData = DeletedObjectPackage->GetMetaData())
				{
					MetaData->ClearFlags(RF_Standalone);
				}
			}
		}

		FAssetData AssetDataDeleted = FAssetData(DeletedAsset);

#if WITH_EDITOR
		if (bInitialSearchCompleted && AssetDataDeleted.IsRedirector())
		{
			// Need to remove from GRedirectCollector
			GRedirectCollector.RemoveAssetPathRedirection(AssetDataDeleted.ObjectPath);
		}
#endif

		// Let subscribers know that the asset was removed from the registry
		AssetRemovedEvent.Broadcast(AssetDataDeleted);

		// Notify listeners that an in-memory asset was just deleted
		InMemoryAssetDeletedEvent.Broadcast(DeletedAsset);
	}
}

void UAssetRegistryImpl::AssetRenamed(const UObject* RenamedAsset, const FString& OldObjectPath)
{
	if (ensure(RenamedAsset) && RenamedAsset->IsAsset())
	{
		// Add the renamed object to the package file cache because its filename can already be
		// determined by its long package name.
		// @todo AssetRegistry We are assuming it will be saved in a single asset package.
		UPackage* NewPackage = RenamedAsset->GetOutermost();
		const FString NewPackageName = NewPackage->GetName();
		const FString Filename = FPackageName::LongPackageNameToFilename(NewPackageName, FPackageName::GetAssetPackageExtension());

		RemoveEmptyPackage(NewPackage->GetFName());

		// We want to keep track of empty packages so we can properly merge cached assets with in-memory assets
		FString OldPackageName;
		FString OldAssetName;
		if (OldObjectPath.Split(TEXT("."), &OldPackageName, &OldAssetName))
		{
			UPackage* OldPackage = FindPackage(nullptr, *OldPackageName);

			if (UPackage::IsEmptyPackage(OldPackage))
			{
				AddEmptyPackage(OldPackage->GetFName());
			}
		}

		// Add the path to the Path Tree, in case it wasn't already there
		AddAssetPath(*FPackageName::GetLongPackagePath(NewPackageName));

		AssetRenamedEvent.Broadcast(FAssetData(RenamedAsset), OldObjectPath);
	}
}

void UAssetRegistryImpl::PackageDeleted(UPackage* DeletedPackage)
{
	if (ensure(DeletedPackage))
	{
		RemovePackageData(*DeletedPackage->GetName());
	}
}

bool UAssetRegistryImpl::IsLoadingAssets() const
{
	return !bInitialSearchCompleted;
}

void UAssetRegistryImpl::Tick(float DeltaTime)
{
	double TickStartTime = FPlatformTime::Seconds();

	if (DeltaTime < 0)
	{
		// Force a full flush
		TickStartTime = -1;
	}
	
	bool bOldTemporaryCachingMode = GetTemporaryCachingMode();
	SetTemporaryCachingMode(true);
	ON_SCOPE_EXIT { SetTemporaryCachingMode(bOldTemporaryCachingMode); };

	// Gather results from the background search
	bool bIsSearching = false;
	TArray<double> SearchTimes;
	int32 NumFilesToSearch = 0;
	int32 NumPathsToSearch = 0;
	bool bIsDiscoveringFiles = false;
	if (BackgroundAssetSearch.IsValid())
	{
		bIsSearching = BackgroundAssetSearch->GetAndTrimSearchResults(BackgroundAssetResults, BackgroundPathResults, BackgroundDependencyResults, BackgroundCookedPackageNamesWithoutAssetDataResults, SearchTimes, NumFilesToSearch, NumPathsToSearch, bIsDiscoveringFiles);
	}

	// Report the search times
	for (int32 SearchTimeIdx = 0; SearchTimeIdx < SearchTimes.Num(); ++SearchTimeIdx)
	{
		UE_LOG(LogAssetRegistry, Verbose, TEXT("### Background search completed in %0.4f seconds"), SearchTimes[SearchTimeIdx]);
	}

	// Add discovered paths
	if (BackgroundPathResults.Num())
	{
		PathDataGathered(TickStartTime, BackgroundPathResults);
	}

	// Process the asset results
	const bool bHadAssetsToProcess = BackgroundAssetResults.Num() > 0 || BackgroundDependencyResults.Num() > 0;
	if (BackgroundAssetResults.Num())
	{
		// Mark the first amortize time
		if (AmortizeStartTime == 0)
		{
			AmortizeStartTime = FPlatformTime::Seconds();
		}

		AssetSearchDataGathered(TickStartTime, BackgroundAssetResults);

		if (BackgroundAssetResults.Num() == 0)
		{
			TotalAmortizeTime += FPlatformTime::Seconds() - AmortizeStartTime;
			AmortizeStartTime = 0;
		}
	}

	// Add dependencies
	if (BackgroundDependencyResults.Num())
	{
		DependencyDataGathered(TickStartTime, BackgroundDependencyResults);
	}

	// Load cooked packages that do not have asset data
	if (BackgroundCookedPackageNamesWithoutAssetDataResults.Num())
	{
		CookedPackageNamesWithoutAssetDataGathered(TickStartTime, BackgroundCookedPackageNamesWithoutAssetDataResults);
	}

	// Compute total pending, plus highest pending for this run so we can show a good progress bar
	static int32 HighestPending = 0;
	const int32 NumPending = NumFilesToSearch + NumPathsToSearch + BackgroundPathResults.Num() + BackgroundAssetResults.Num() + BackgroundDependencyResults.Num() + BackgroundCookedPackageNamesWithoutAssetDataResults.Num();

	HighestPending = FMath::Max(HighestPending, NumPending);

	// Notify the status change
	if (bIsSearching || bHadAssetsToProcess)
	{
		const FFileLoadProgressUpdateData ProgressUpdateData(
			HighestPending,					// NumTotalAssets
			HighestPending - NumPending,	// NumAssetsProcessedByAssetRegistry
			NumPending / 2,					// NumAssetsPendingDataLoad, divided by 2 because assets are double counted due to dependencies
			bIsDiscoveringFiles				// bIsDiscoveringAssetFiles
		);
		FileLoadProgressUpdatedEvent.Broadcast(ProgressUpdateData);
	}

	// If completing an initial search, refresh the content browser
	if (!bIsSearching && NumPending == 0)
	{
		HighestPending = 0;

		if (!bInitialSearchCompleted)
		{
#if WITH_EDITOR
			// update redirectors
			UpdateRedirectCollector();
#endif
			UE_LOG(LogAssetRegistry, Verbose, TEXT("### Time spent amortizing search results: %0.4f seconds"), TotalAmortizeTime);
			UE_LOG(LogAssetRegistry, Log, TEXT("Asset discovery search completed in %0.4f seconds"), FPlatformTime::Seconds() - FullSearchStartTime);

			bInitialSearchCompleted = true;

			FileLoadedEvent.Broadcast();
		}
#if WITH_EDITOR
		else if (bUpdateDiskCacheAfterLoad)
		{
			ProcessLoadedAssetsToUpdateCache(TickStartTime);
		}
#endif
	}
}

void UAssetRegistryImpl::Serialize(FArchive& Ar)
{
	State.Serialize(Ar, SerializationOptions);
	CachePathsFromState(State);
}

/** Append the assets from the incoming state into our own */
void UAssetRegistryImpl::AppendState(const FAssetRegistryState& InState)
{
	State.InitializeFromExisting(InState, SerializationOptions, FAssetRegistryState::EInitializationMode::Append);
	CachePathsFromState(InState);

	InState.EnumerateAllAssets(TSet<FName>(), [this](const FAssetData& AssetData)
	{
		// Let subscribers know that the new asset was added to the registry
		AssetAddedEvent.Broadcast(AssetData);
		return true;
	});
}

void UAssetRegistryImpl::CachePathsFromState(const FAssetRegistryState& InState)
{
	// Add paths to cache
	for (const TPair<FName, FAssetData*>& AssetDataPair : InState.CachedAssetsByObjectPath)
	{
		const FAssetData* AssetData = AssetDataPair.Value;

		if (AssetData != nullptr)
		{
			AddAssetPath(AssetData->PackagePath);

			// Populate the class map if adding blueprint
			if (ClassGeneratorNames.Contains(AssetData->AssetClass))
			{
				const FString GeneratedClass = AssetData->GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);
				const FString ParentClass = AssetData->GetTagValueRef<FString>(FBlueprintTags::ParentClassPath);
				if (!GeneratedClass.IsEmpty() && !ParentClass.IsEmpty())
				{
					const FName GeneratedClassFName = *ExportTextPathToObjectName(GeneratedClass);
					const FName ParentClassFName = *ExportTextPathToObjectName(ParentClass);
					CachedBPInheritanceMap.Add(GeneratedClassFName, ParentClassFName);

					// Invalidate caching because CachedBPInheritanceMap got modified
					bIsTempCachingUpToDate = false;
				}
			}
		}
	}
}

uint32 UAssetRegistryImpl::GetAllocatedSize(bool bLogDetailed) const
{
	uint32 StateSize = State.GetAllocatedSize(bLogDetailed);

	uint32 StaticSize = sizeof(UAssetRegistryImpl) + CachedEmptyPackages.GetAllocatedSize() + CachedBPInheritanceMap.GetAllocatedSize()  + ClassGeneratorNames.GetAllocatedSize() + OnDirectoryChangedDelegateHandles.GetAllocatedSize();
	uint32 SearchSize = BackgroundAssetResults.GetAllocatedSize() + BackgroundPathResults.GetAllocatedSize() + BackgroundDependencyResults.GetAllocatedSize() + BackgroundCookedPackageNamesWithoutAssetDataResults.GetAllocatedSize() + SynchronouslyScannedPathsAndFiles.GetAllocatedSize() + CachedPathTree.GetAllocatedSize();

	if (bIsTempCachingEnabled && !bIsTempCachingAlwaysEnabled)
	{
		uint32 TempCacheMem = TempCachedInheritanceMap.GetAllocatedSize() + TempReverseInheritanceMap.GetAllocatedSize();
		StaticSize += TempCacheMem;
		UE_LOG(LogAssetRegistry, Warning, TEXT("Asset Registry Temp caching enabled, wasting memory: %dk"), TempCacheMem / 1024);
	}

	StaticSize += SerializationOptions.CookFilterlistTagsByClass.GetAllocatedSize();
	for (const TPair<FName, TSet<FName>>& Pair : SerializationOptions.CookFilterlistTagsByClass)
	{
		StaticSize += Pair.Value.GetAllocatedSize();
	}

	if (bLogDetailed)
	{
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetRegistry Static Size: %dk"), StaticSize / 1024);
		UE_LOG(LogAssetRegistry, Log, TEXT("AssetRegistry Search Size: %dk"), SearchSize / 1024);
	}

	return StateSize + StaticSize + SearchSize;
}

void UAssetRegistryImpl::LoadPackageRegistryData(FArchive& Ar, TArray<FAssetData*> &AssetDataList) const
{

	FPackageReader Reader;
	Reader.OpenPackageFile(&Ar);

	Reader.ReadAssetRegistryData(AssetDataList);

	Reader.ReadAssetDataFromThumbnailCache(AssetDataList);

	TArray<FString> CookedPackageNamesWithoutAssetDataGathered;
	Reader.ReadAssetRegistryDataIfCookedPackage(AssetDataList, CookedPackageNamesWithoutAssetDataGathered);

	//bool ReadDependencyData(FPackageDependencyData& OutDependencyData);
}

void UAssetRegistryImpl::SaveRegistryData(FArchive& Ar, TMap<FName, FAssetData*>& Data, TArray<FName>* InMaps /* = nullptr */)
{
	FAssetRegistryState TempState;
	InitializeTemporaryAssetRegistryState(TempState, SerializationOptions, false, Data);

	TempState.Serialize(Ar, SerializationOptions);
}

void UAssetRegistryImpl::LoadRegistryData(FArchive& Ar, TMap<FName, FAssetData*>& Data)
{
	FAssetRegistryState TempState;

	TempState.Serialize(Ar, SerializationOptions);

	if (Ar.IsLoading())
	{
		for (const TPair<FName, FAssetData*>& AssetDataPair : State.CachedAssetsByObjectPath)
		{
			const FAssetData* AssetData = AssetDataPair.Value;

			if (AssetData != nullptr)
			{
				FAssetData *NewAssetData = new FAssetData(*AssetData);

				Data.Add(NewAssetData->PackageName, NewAssetData);
			}
		}
	}
}

void UAssetRegistryImpl::InitializeTemporaryAssetRegistryState(FAssetRegistryState& OutState, const FAssetRegistrySerializationOptions& Options, bool bRefreshExisting, const TMap<FName, FAssetData*>& OverrideData) const
{
	const TMap<FName, FAssetData*>& DataToUse = OverrideData.Num() > 0 ? OverrideData : State.CachedAssetsByObjectPath;

	OutState.InitializeFromExisting(DataToUse, State.CachedDependsNodes, State.CachedPackageData, Options, bRefreshExisting ? FAssetRegistryState::EInitializationMode::OnlyUpdateExisting : FAssetRegistryState::EInitializationMode::Rebuild);
}

const FAssetRegistryState* UAssetRegistryImpl::GetAssetRegistryState() const
{
	return &State;
}

const TSet<FName>& UAssetRegistryImpl::GetCachedEmptyPackages() const
{
	return CachedEmptyPackages;
}

void UAssetRegistryImpl::ScanPathsAndFilesSynchronous(const TArray<FString>& InPaths, const TArray<FString>& InSpecificFiles, bool bForceRescan, EAssetDataCacheMode AssetDataCacheMode)
{
	ScanPathsAndFilesSynchronous(InPaths, InSpecificFiles, bForceRescan, AssetDataCacheMode, nullptr, nullptr);
}

void UAssetRegistryImpl::ScanPathsAndFilesSynchronous(const TArray<FString>& InPaths, const TArray<FString>& InSpecificFiles, bool bForceRescan, EAssetDataCacheMode AssetDataCacheMode, TArray<FName>* OutFoundAssets, TArray<FName>* OutFoundPaths)
{
	const double SearchStartTime = FPlatformTime::Seconds();

	// Only scan paths that were not previously synchronously scanned, unless we were asked to force rescan.
	TArray<FString> PathsToScan;
	TArray<FString> FilesToScan;
	bool bPathsRemoved = false;

	for (const FString& Path : InPaths)
	{
		bool bAlreadyScanned = false;
		FString PathWithSlash = Path;
		if (!PathWithSlash.EndsWith(TEXT("/"), ESearchCase::CaseSensitive))
		{
			// Add / if it's missing so the substring check is safe
			PathWithSlash += TEXT("/");
		}

		// Check that it starts with /
		for (const FString& ScannedPath : SynchronouslyScannedPathsAndFiles)
		{
			if (PathWithSlash.StartsWith(ScannedPath))
			{
				bAlreadyScanned = true;
				break;
			}
		}

		if (bForceRescan || !bAlreadyScanned)
		{
			PathsToScan.Add(Path);
			SynchronouslyScannedPathsAndFiles.Add(PathWithSlash);
		}
		else
		{
			bPathsRemoved = true;
		}
	}

	for (const FString& SpecificFile : InSpecificFiles)
	{
		if (bForceRescan || !SynchronouslyScannedPathsAndFiles.Contains(SpecificFile))
		{
			FilesToScan.Add(SpecificFile);
			SynchronouslyScannedPathsAndFiles.Add(SpecificFile);
		}
		else
		{
			bPathsRemoved = true;
		}
	}

	// If we removed paths, we can't use the monolithic cache as this will replace it with invalid data
	if (AssetDataCacheMode == EAssetDataCacheMode::UseMonolithicCache && bPathsRemoved)
	{
		AssetDataCacheMode = EAssetDataCacheMode::UseModularCache;
	}

	if (PathsToScan.Num() > 0 || FilesToScan.Num() > 0)
	{
		// Start the sync asset search
		FAssetDataGatherer AssetSearch(PathsToScan, FilesToScan, /*bSynchronous=*/true, AssetDataCacheMode);

		// Get the search results
		TBackgroundGatherResults<FAssetData*> AssetResults;
		TBackgroundGatherResults<FString> PathResults;
		TBackgroundGatherResults<FPackageDependencyData> DependencyResults;
		TBackgroundGatherResults<FString> CookedPackageNamesWithoutAssetDataResults;
		TArray<double> SearchTimes;
		int32 NumFilesToSearch = 0;
		int32 NumPathsToSearch = 0;
		bool bIsDiscoveringFiles = false;
		AssetSearch.GetAndTrimSearchResults(AssetResults, PathResults, DependencyResults, CookedPackageNamesWithoutAssetDataResults, SearchTimes, NumFilesToSearch, NumPathsToSearch, bIsDiscoveringFiles);

		if (OutFoundAssets)
		{
			OutFoundAssets->Reserve(OutFoundAssets->Num() + AssetResults.Num());
			for (int32 i = 0; i < AssetResults.Num(); ++i)
			{
				OutFoundAssets->Add(AssetResults[i]->ObjectPath);
			}
		}

		if (OutFoundPaths)
		{
			OutFoundPaths->Reserve(OutFoundPaths->Num() + PathResults.Num());
			for (int32 i = 0; i < PathResults.Num(); ++i)
			{
				OutFoundPaths->Add(*PathResults[i]);
			}
		}

		// Cache the search results
		const int32 NumResults = AssetResults.Num();
		AssetSearchDataGathered(-1, AssetResults);
		PathDataGathered(-1, PathResults);
		DependencyDataGathered(-1, DependencyResults);
		CookedPackageNamesWithoutAssetDataGathered(-1, CookedPackageNamesWithoutAssetDataResults);

#if WITH_EDITOR
		if (bUpdateDiskCacheAfterLoad && bInitialSearchCompleted)
		{
			ProcessLoadedAssetsToUpdateCache(-1);
		}
#endif

		// Log stats
		TArray<FString> LogPathsAndFilenames = PathsToScan;
		LogPathsAndFilenames.Append(FilesToScan);

		const FString& Path = LogPathsAndFilenames[0];
		FString PathsString;
		if (LogPathsAndFilenames.Num() > 1)
		{
			PathsString = FString::Printf(TEXT("'%s' and %d other paths/filenames"), *Path, LogPathsAndFilenames.Num() - 1);
		}
		else
		{
			PathsString = FString::Printf(TEXT("'%s'"), *Path);
		}

		UE_LOG(LogAssetRegistry, Verbose, TEXT("ScanPathsSynchronous completed scanning %s to find %d assets in %0.4f seconds"), *PathsString, NumResults, FPlatformTime::Seconds() - SearchStartTime);
	}
}

void UAssetRegistryImpl::AssetSearchDataGathered(const double TickStartTime, TBackgroundGatherResults<FAssetData*>& AssetResults)
{
	const bool bFlushFullBuffer = TickStartTime < 0;

	// Add the found assets
	while (AssetResults.Num() > 0)
	{
		FAssetData*& BackgroundResult = AssetResults.Pop();

		CA_ASSUME(BackgroundResult);

		// Try to update any asset data that may already exist
		FAssetData* AssetData = State.CachedAssetsByObjectPath.FindRef(BackgroundResult->ObjectPath);

		const FName PackagePath = BackgroundResult->PackagePath;
		if (AssetData)
		{
			// If this ensure fires then we've somehow processed the same result more than once, and that should never happen
			if (ensure(AssetData != BackgroundResult))
			{
				// The asset exists in the cache, update it
				UpdateAssetData(AssetData, *BackgroundResult);

				// Delete the result that was originally created by an FPackageReader
				delete BackgroundResult;
				BackgroundResult = nullptr;
			}
		}
		else
		{
			// The asset isn't in the cache yet, add it and notify subscribers
			AddAssetData(BackgroundResult);
		}

		// Populate the path tree
		AddAssetPath(PackagePath);

		// Check to see if we have run out of time in this tick
		if (!bFlushFullBuffer && (FPlatformTime::Seconds() - TickStartTime) > MaxSecondsPerFrame)
		{
			return;
		}
	}

	// Trim the results array
	AssetResults.Trim();
}

void UAssetRegistryImpl::PathDataGathered(const double TickStartTime, TBackgroundGatherResults<FString>& PathResults)
{
	const bool bFlushFullBuffer = TickStartTime < 0;

	while (PathResults.Num() > 0)
	{
		const FString& Path = PathResults.Pop();
		AddAssetPath(FName(*Path));

		// Check to see if we have run out of time in this tick
		if (!bFlushFullBuffer && (FPlatformTime::Seconds() - TickStartTime) > MaxSecondsPerFrame)
		{
			return;
		}
	}

	// Trim the results array
	PathResults.Trim();
}

void UAssetRegistryImpl::DependencyDataGathered(const double TickStartTime, TBackgroundGatherResults<FPackageDependencyData>& DependsResults)
{
	const bool bFlushFullBuffer = TickStartTime < 0;

	while (DependsResults.Num() > 0)
	{
		FPackageDependencyData& Result = DependsResults.Pop();

		// Update package data
		FAssetPackageData* PackageData = State.CreateOrGetAssetPackageData(Result.PackageName);
		*PackageData = Result.PackageData;

		FDependsNode* Node = State.CreateOrFindDependsNode(Result.PackageName);

		// We will populate the node dependencies below. Empty the set here in case this file was already read
		// Also remove references to all existing dependencies, those will be also repopulated below
		Node->IterateOverDependencies([Node](FDependsNode* InDependency, EAssetRegistryDependencyType::Type InDependencyType)
		{
			InDependency->RemoveReferencer(Node);
		});

		Node->ClearDependencies();

		// Don't bother registering dependencies on these packages, every package in the game will depend on them
		static TArray<FName> ScriptPackagesToSkip = TArray<FName>{ TEXT("/Script/CoreUObject"), TEXT("/Script/Engine"), TEXT("/Script/BlueprintGraph"), TEXT("/Script/UnrealEd") };

		// Determine the new package dependencies
		TMap<FName, EAssetRegistryDependencyType::Type> PackageDependencies;
		for (int32 ImportIdx = 0; ImportIdx < Result.ImportMap.Num(); ++ImportIdx)
		{
			const FName AssetReference = Result.GetImportPackageName(ImportIdx);

			// Should we skip this because it's too common?
			if (ScriptPackagesToSkip.Contains(AssetReference))
			{
				continue;
			}

			// Already processed?
			if (PackageDependencies.Contains(AssetReference))
			{
				continue;
			}

			PackageDependencies.Add(AssetReference, EAssetRegistryDependencyType::Hard);
		}

		for (FName SoftPackageName : Result.SoftPackageReferenceList)
		{
			// Already processed?
			if (PackageDependencies.Contains(SoftPackageName))
			{
				continue;
			}

			PackageDependencies.Add(SoftPackageName, EAssetRegistryDependencyType::Soft);
		}

		for (const TPair<FPackageIndex, TArray<FName>>& SearchableNameList : Result.SearchableNamesMap)
		{
			FName ObjectName;
			FName PackageName;

			// Find object and package name from linker
			FPackageIndex LinkerIndex = SearchableNameList.Key;
			if (LinkerIndex.IsExport())
			{
				// Package name has to be this package, take a guess at object name
				PackageName = Result.PackageName;
				ObjectName = FName(*FPackageName::GetLongPackageAssetName(Result.PackageName.ToString()));
			}
			else if (LinkerIndex.IsImport())
			{
				FObjectResource* Resource = &Result.ImpExp(LinkerIndex);
				FPackageIndex OuterLinkerIndex = Resource->OuterIndex;
				check(OuterLinkerIndex.IsNull() || OuterLinkerIndex.IsImport());
				if (!OuterLinkerIndex.IsNull())
				{
					ObjectName = Resource->ObjectName;
					while (!OuterLinkerIndex.IsNull())
					{
						Resource = &Result.ImpExp(OuterLinkerIndex);
						OuterLinkerIndex = Resource->OuterIndex;
						check(OuterLinkerIndex.IsNull() || OuterLinkerIndex.IsImport());
					}
				}
				PackageName = Resource->ObjectName;
			}

			for (FName NameReference : SearchableNameList.Value)
			{
				FAssetIdentifier AssetId = FAssetIdentifier(PackageName, ObjectName, NameReference);

				// Add node for all name references
				FDependsNode* DependsNode = State.CreateOrFindDependsNode(AssetId);

				if (DependsNode != nullptr)
				{
					Node->AddDependency(DependsNode, EAssetRegistryDependencyType::SearchableName);
					DependsNode->AddReferencer(Node);
				}
			}
		}

		// Doubly-link all new dependencies for this package
		for (auto NewDependsIt : PackageDependencies)
		{
			FDependsNode* DependsNode = State.CreateOrFindDependsNode(NewDependsIt.Key);

			if (DependsNode != nullptr)
			{
				const FAssetIdentifier& Identifier = DependsNode->GetIdentifier();
				if (DependsNode->GetConnectionCount() == 0 && Identifier.IsPackage())
				{
					// This was newly created, see if we need to read the script package Guid
					FString PackageName = Identifier.PackageName.ToString();

					if (FPackageName::IsScriptPackage(PackageName))
					{
						// Get the guid off the script package, this is updated when script is changed
						UPackage* Package = FindPackage(nullptr, *PackageName);

						if (Package)
						{
							FAssetPackageData* ScriptPackageData = State.CreateOrGetAssetPackageData(Identifier.PackageName);
							ScriptPackageData->PackageGuid = Package->GetGuid();
						}
					}
				}

				Node->AddDependency(DependsNode, NewDependsIt.Value);
				DependsNode->AddReferencer(Node);
			}
		}

		// Check to see if we have run out of time in this tick
		if (!bFlushFullBuffer && (FPlatformTime::Seconds() - TickStartTime) > MaxSecondsPerFrame)
		{
			return;
		}
	}

	// Trim the results array
	DependsResults.Trim();
}

void UAssetRegistryImpl::CookedPackageNamesWithoutAssetDataGathered(const double TickStartTime, TBackgroundGatherResults<FString>& CookedPackageNamesWithoutAssetDataResults)
{
	const bool bFlushFullBuffer = TickStartTime < 0;

	struct FConfigValue
	{
		FConfigValue()
		{
			if (GConfig)
			{
				GConfig->GetBool(TEXT("AssetRegistry"), TEXT("LoadCookedPackagesWithoutAssetData"), bShouldProcess, GEngineIni);
			}
		}

		bool bShouldProcess = true;
	};
	static FConfigValue ShouldProcessCookedPackages;

	// Add the found assets
	if (ShouldProcessCookedPackages.bShouldProcess)
	{
		while (CookedPackageNamesWithoutAssetDataResults.Num() > 0)
		{
			// If this data is cooked and it we couldn't find any asset in its export table then try to load the entire package 
			// Loading the entire package will make all of its assets searchable through the in-memory scanning performed by GetAsseets
			const FString& BackgroundResult = CookedPackageNamesWithoutAssetDataResults.Pop();
			LoadPackage(nullptr, *BackgroundResult, 0);

			// Check to see if we have run out of time in this tick
			if (!bFlushFullBuffer && (FPlatformTime::Seconds() - TickStartTime) > MaxSecondsPerFrame)
			{
				return;
			}
		}
	}
	else
	{
		// Do nothing will these packages. For projects which could run entirely from cooked data, this
		// process will involve opening every single package synchronously on the game thread which will
		// kill performance. We need a better way.
		CookedPackageNamesWithoutAssetDataResults.Empty();
	}

	// Trim the results array
	CookedPackageNamesWithoutAssetDataResults.Trim();
}

void UAssetRegistryImpl::AddEmptyPackage(FName PackageName)
{
	CachedEmptyPackages.Add(PackageName);
}

bool UAssetRegistryImpl::RemoveEmptyPackage(FName PackageName)
{
	return CachedEmptyPackages.Remove(PackageName) > 0;
}

bool UAssetRegistryImpl::AddAssetPath(FName PathToAdd)
{
	if (CachedPathTree.CachePath(PathToAdd))
	{
		PathAddedEvent.Broadcast(PathToAdd.ToString());
		return true;
	}

	return false;
}

bool UAssetRegistryImpl::RemoveAssetPath(FName PathToRemove, bool bEvenIfAssetsStillExist)
{
	if (!bEvenIfAssetsStillExist)
	{
		// Check if there were assets in the specified folder. You can not remove paths that still contain assets
		TArray<FAssetData> AssetsInPath;
		GetAssetsByPath(PathToRemove, AssetsInPath, true);
		if (AssetsInPath.Num() > 0)
		{
			// At least one asset still exists in the path. Fail the remove.
			return false;
		}
	}

	if (CachedPathTree.RemovePath(PathToRemove))
	{
		PathRemovedEvent.Broadcast(PathToRemove.ToString());
		return true;
	}
	else
	{
		// The folder did not exist in the tree, fail the remove
		return false;
	}
}

FString UAssetRegistryImpl::ExportTextPathToObjectName(const FString& InExportTextPath) const
{
	const FString ObjectPath = FPackageName::ExportTextPathToObjectPath(InExportTextPath);
	return FPackageName::ObjectPathToObjectName(ObjectPath);
}

void UAssetRegistryImpl::AddAssetData(FAssetData* AssetData)
{
	State.AddAssetData(AssetData);

	// Notify subscribers
	AssetAddedEvent.Broadcast(*AssetData);

	// Populate the class map if adding blueprint
	if (ClassGeneratorNames.Contains(AssetData->AssetClass))
	{
		const FString GeneratedClass = AssetData->GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);
		const FString ParentClass = AssetData->GetTagValueRef<FString>(FBlueprintTags::ParentClassPath);
		if (!GeneratedClass.IsEmpty() && !ParentClass.IsEmpty())
		{
			const FName GeneratedClassFName = *ExportTextPathToObjectName(GeneratedClass);
			const FName ParentClassFName = *ExportTextPathToObjectName(ParentClass);
			CachedBPInheritanceMap.Add(GeneratedClassFName, ParentClassFName);
			
			// Invalidate caching because CachedBPInheritanceMap got modified
			bIsTempCachingUpToDate = false;
		}
	}
}

void UAssetRegistryImpl::UpdateAssetData(FAssetData* AssetData, const FAssetData& NewAssetData)
{
	// Update the class map if updating a blueprint
	if (ClassGeneratorNames.Contains(AssetData->AssetClass))
	{
		const FString OldGeneratedClass = AssetData->GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);
		if (!OldGeneratedClass.IsEmpty())
		{
			const FName OldGeneratedClassFName = *ExportTextPathToObjectName(OldGeneratedClass);
			CachedBPInheritanceMap.Remove(OldGeneratedClassFName);

			// Invalidate caching because CachedBPInheritanceMap got modified
			bIsTempCachingUpToDate = false;
		}

		const FString NewGeneratedClass = NewAssetData.GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);
		const FString NewParentClass = NewAssetData.GetTagValueRef<FString>(FBlueprintTags::ParentClassPath);
		if (!NewGeneratedClass.IsEmpty() && !NewParentClass.IsEmpty())
		{
			const FName NewGeneratedClassFName = *ExportTextPathToObjectName(*NewGeneratedClass);
			const FName NewParentClassFName = *ExportTextPathToObjectName(*NewParentClass);
			CachedBPInheritanceMap.Add(NewGeneratedClassFName, NewParentClassFName);

			// Invalidate caching because CachedBPInheritanceMap got modified
			bIsTempCachingUpToDate = false;
		}
	}

	State.UpdateAssetData(AssetData, NewAssetData);
	
	AssetUpdatedEvent.Broadcast(*AssetData);
}

bool UAssetRegistryImpl::RemoveAssetData(FAssetData* AssetData)
{
	bool bRemoved = false;

	if (ensure(AssetData))
	{
		// Notify subscribers
		AssetRemovedEvent.Broadcast(*AssetData);

		// Remove from the class map if removing a blueprint
		if (ClassGeneratorNames.Contains(AssetData->AssetClass))
		{
			const FString OldGeneratedClass = AssetData->GetTagValueRef<FString>(FBlueprintTags::GeneratedClassPath);
			if (!OldGeneratedClass.IsEmpty())
			{
				const FName OldGeneratedClassFName = *ExportTextPathToObjectName(OldGeneratedClass);
				CachedBPInheritanceMap.Remove(OldGeneratedClassFName);

				// Invalidate caching because CachedBPInheritanceMap got modified
				bIsTempCachingUpToDate = false;
			}
		}

		bRemoved = State.RemoveAssetData(AssetData);
	}

	return bRemoved;
}

void UAssetRegistryImpl::RemovePackageData(const FName PackageName)
{
	TArray<FAssetData*>* PackageAssetsPtr = State.CachedAssetsByPackageName.Find(PackageName);
	if (PackageAssetsPtr && PackageAssetsPtr->Num() > 0)
	{
		// If there were any referencers, re-add them to a new empty dependency node, as it would be when the referencers are loaded from disk
		TArray<FName> SoftReferencers;
		TArray<FName> HardReferencers;
		GetReferencers(PackageName, SoftReferencers, EAssetRegistryDependencyType::Soft);
		GetReferencers(PackageName, HardReferencers, EAssetRegistryDependencyType::Hard);

		// Copy the array since RemoveAssetData may re-allocate it!
		TArray<FAssetData*> PackageAssets = *PackageAssetsPtr;
		for (FAssetData* PackageAsset : PackageAssets)
		{
			RemoveAssetData(PackageAsset);
		}

		// See if we have to readd the dependency now
		if (SoftReferencers.Num() > 0 || HardReferencers.Num() > 0)
		{
			FDependsNode* NewNode = State.CreateOrFindDependsNode(PackageName);

			auto ReAddDependency = [this, NewNode](const FName& Referencer, EAssetRegistryDependencyType::Type RefType) {
				FDependsNode* ReferencerNode = State.CreateOrFindDependsNode(Referencer);
				if (ReferencerNode != nullptr)
				{
					ReferencerNode->AddDependency(NewNode, RefType);
					NewNode->AddReferencer(ReferencerNode);
				}
			};

			for (const FName& SoftRef : SoftReferencers)
			{
				ReAddDependency(SoftRef, EAssetRegistryDependencyType::Soft);
			}

			for (const FName& HardRef : HardReferencers)
			{
				ReAddDependency(HardRef, EAssetRegistryDependencyType::Hard);
			}
		}
	}
}

void UAssetRegistryImpl::AddPathToSearch(const FString& Path)
{
	if (BackgroundAssetSearch.IsValid())
	{
		BackgroundAssetSearch->AddPathToSearch(Path);
	}
}

void UAssetRegistryImpl::AddFilesToSearch(const TArray<FString>& Files)
{
	if (BackgroundAssetSearch.IsValid())
	{
		BackgroundAssetSearch->AddFilesToSearch(Files);
	}
}

#if WITH_EDITOR

void UAssetRegistryImpl::OnDirectoryChanged(const TArray<FFileChangeData>& FileChanges)
{
	// Take local copy of FileChanges array as we wish to collapse pairs of 'Removed then Added' FileChangeData
	// entries into a single 'Modified' entry.
	TArray<FFileChangeData> FileChangesProcessed(FileChanges);

	for (int32 FileEntryIndex = 0; FileEntryIndex < FileChangesProcessed.Num(); FileEntryIndex++)
	{
		if (FileChangesProcessed[FileEntryIndex].Action == FFileChangeData::FCA_Added)
		{
			// Search back through previous entries to see if this Added can be paired with a previous Removed
			const FString& FilenameToCompare = FileChangesProcessed[FileEntryIndex].Filename;
			for (int32 SearchIndex = FileEntryIndex - 1; SearchIndex >= 0; SearchIndex--)
			{
				if (FileChangesProcessed[SearchIndex].Action == FFileChangeData::FCA_Removed &&
					FileChangesProcessed[SearchIndex].Filename == FilenameToCompare)
				{
					// Found a Removed which matches the Added - change the Added file entry to be a Modified...
					FileChangesProcessed[FileEntryIndex].Action = FFileChangeData::FCA_Modified;

					// ...and remove the Removed entry
					FileChangesProcessed.RemoveAt(SearchIndex);
					FileEntryIndex--;
					break;
				}
			}
		}
	}

	TArray<FString> NewFiles;
	TArray<FString> ModifiedFiles;

	for (int32 FileIdx = 0; FileIdx < FileChangesProcessed.Num(); ++FileIdx)
	{
		FString LongPackageName;
		const FString File = FString(FileChangesProcessed[FileIdx].Filename);
		const bool bIsPackageFile = FPackageName::IsPackageExtension(*FPaths::GetExtension(File, true));
		const bool bIsValidPackageName = FPackageName::TryConvertFilenameToLongPackageName(File, LongPackageName);
		const bool bIsValidPackage = bIsPackageFile && bIsValidPackageName;

		if (bIsValidPackage)
		{
			switch (FileChangesProcessed[FileIdx].Action)
			{
			case FFileChangeData::FCA_Added:
				// This is a package file that was created on disk. Mark it to be scanned for asset data.
				NewFiles.AddUnique(File);
				UE_LOG(LogAssetRegistry, Verbose, TEXT("File was added to content directory: %s"), *File);
				break;

			case FFileChangeData::FCA_Modified:
				// This is a package file that changed on disk. Mark it to be scanned immediately for new or removed asset data.
				ModifiedFiles.AddUnique(File);
				UE_LOG(LogAssetRegistry, Verbose, TEXT("File changed in content directory: %s"), *File);
				break;

			case FFileChangeData::FCA_Removed:
				// This file was deleted. Remove all assets in the package from the registry.
				RemovePackageData(*LongPackageName);
				UE_LOG(LogAssetRegistry, Verbose, TEXT("File was removed from content directory: %s"), *File);
				break;
			}
		}
		else if (bIsValidPackageName)
		{
			// This could be a directory or possibly a file with no extension or a wrong extension.
			// No guaranteed way to know at this point since it may have been deleted.
			switch (FileChangesProcessed[FileIdx].Action)
			{
			case FFileChangeData::FCA_Added:
			{
				if (FPaths::DirectoryExists(File) && LongPackageName != TEXT("/Game/Collections"))
				{
					AddPath(LongPackageName);
					UE_LOG(LogAssetRegistry, Verbose, TEXT("Directory was added to content directory: %s"), *File);
					AddPathToSearch(LongPackageName);
				}
				break;
			}
			case FFileChangeData::FCA_Removed:
			{
				RemoveAssetPath(*LongPackageName);
				UE_LOG(LogAssetRegistry, Verbose, TEXT("Directory was removed from content directory: %s"), *File);
				break;
			}
			default:
				break;
			}
		}
	}

	if (NewFiles.Num())
	{
		AddFilesToSearch(NewFiles);
	}

	ScanModifiedAssetFiles(ModifiedFiles);
}

void UAssetRegistryImpl::OnAssetLoaded(UObject *AssetLoaded)
{
	LoadedAssetsToProcess.Add(AssetLoaded);
}

void UAssetRegistryImpl::ProcessLoadedAssetsToUpdateCache(const double TickStartTime)
{
	check(bInitialSearchCompleted && bUpdateDiskCacheAfterLoad);

	const bool bFlushFullBuffer = TickStartTime < 0;

	if (bFlushFullBuffer)
	{
		// Retry the previous failures on a flush
		LoadedAssetsToProcess.Append(LoadedAssetsThatDidNotHaveCachedData);
		LoadedAssetsThatDidNotHaveCachedData.Reset();
	}

	// Add the found assets
	int32 LoadedAssetIndex = 0;
	for (LoadedAssetIndex = 0; LoadedAssetIndex < LoadedAssetsToProcess.Num(); ++LoadedAssetIndex)
	{
		UObject* LoadedAsset = LoadedAssetsToProcess[LoadedAssetIndex].Get();

		if (!LoadedAsset)
		{
			// This could be null, in which case it already got freed, ignore
			continue;
		}

		const FName ObjectPath = FName(*LoadedAsset->GetPathName());
		if (AssetDataObjectPathsUpdatedOnLoad.Contains(ObjectPath))
		{
			// Already processed once, don't process again even if it loads a second time
			continue;
		}

		UPackage* InMemoryPackage = LoadedAsset->GetOutermost();
		if (InMemoryPackage->IsDirty())
		{
			// Package is dirty, which means it has temporary changes other than just a PostLoad, ignore
			continue;
		}

		FAssetData** CachedData = State.CachedAssetsByObjectPath.Find(ObjectPath);
		if (!CachedData)
		{
			// Not scanned, can't process right now but try again on next synchronous scan
			LoadedAssetsThatDidNotHaveCachedData.Add(LoadedAsset);
			continue;
		}

		AssetDataObjectPathsUpdatedOnLoad.Add(ObjectPath);

		FAssetData NewAssetData = FAssetData(LoadedAsset);

		if (NewAssetData.TagsAndValues.GetMap() != (*CachedData)->TagsAndValues.GetMap())
		{
			// We need to actually update disk cache
			UpdateAssetData(*CachedData, NewAssetData);
		}

		// Check to see if we have run out of time in this tick
		if (!bFlushFullBuffer && (FPlatformTime::Seconds() - TickStartTime) > MaxSecondsPerFrame)
		{
			// Increment the index to properly trim the buffer below
			++LoadedAssetIndex;
			break;
		}
	}

	// Trim the results array
	if (LoadedAssetIndex > 0)
	{
		LoadedAssetsToProcess.RemoveAt(0, LoadedAssetIndex);
	}
}

void UAssetRegistryImpl::UpdateRedirectCollector()
{
	// Look for all redirectors in list
	const TArray<const FAssetData*>& RedirectorAssets = State.GetAssetsByClassName(UObjectRedirector::StaticClass()->GetFName());

	for (const FAssetData* AssetData : RedirectorAssets)
	{
		FName Destination = GetRedirectedObjectPath(AssetData->ObjectPath);

		if (Destination != AssetData->ObjectPath)
		{
			GRedirectCollector.AddAssetPathRedirection(AssetData->ObjectPath, Destination);
		}
	}
}

#endif // WITH_EDITOR

void UAssetRegistryImpl::ScanModifiedAssetFiles(const TArray<FString>& InFilePaths)
{
	if (InFilePaths.Num() > 0)
	{
		// Convert all the filenames to package names
		TArray<FString> ModifiedPackageNames;
		ModifiedPackageNames.Reserve(InFilePaths.Num());
		for (const FString& File : InFilePaths)
		{
			ModifiedPackageNames.Add(FPackageName::FilenameToLongPackageName(File));
		}

		// Get the assets that are currently inside the package
		TArray<TArray<FAssetData*>> ExistingFilesAssetData;
		ExistingFilesAssetData.Reserve(InFilePaths.Num());
		for (const FString& PackageName : ModifiedPackageNames)
		{
			TArray<FAssetData*>* PackageAssetsPtr = State.CachedAssetsByPackageName.Find(*PackageName);
			if (PackageAssetsPtr && PackageAssetsPtr->Num() > 0)
			{
				ExistingFilesAssetData.Add(*PackageAssetsPtr);
			}
			else
			{
				ExistingFilesAssetData.AddDefaulted();
			}
		}

		// Re-scan and update the asset registry with the new asset data
		TArray<FName> FoundAssets;
		ScanPathsAndFilesSynchronous(TArray<FString>(), InFilePaths, true, EAssetDataCacheMode::NoCache, &FoundAssets, nullptr);

		// Remove any assets that are no longer present in the package
		for (const TArray<FAssetData*>& OldPackageAssets : ExistingFilesAssetData)
		{
			for (FAssetData* OldPackageAsset : OldPackageAssets)
			{
				if (!FoundAssets.Contains(OldPackageAsset->ObjectPath))
				{
					RemoveAssetData(OldPackageAsset);
				}
			}
		}
	}
}

void UAssetRegistryImpl::OnContentPathMounted(const FString& InAssetPath, const FString& FileSystemPath)
{
	// Sanitize
	FString AssetPath = InAssetPath;
	if (AssetPath.EndsWith(TEXT("/")) == false)
	{
		// We actually want a trailing slash here so the path can be properly converted while searching for assets
		AssetPath = AssetPath + TEXT("/");
	}

	// Add this to our list of root paths to process
	AddPathToSearch(AssetPath);

	// Listen for directory changes in this content path
#if WITH_EDITOR
	// In-game doesn't listen for directory changes
	if (GIsEditor)
	{
		FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();
		if (DirectoryWatcher)
		{
			// If the path doesn't exist on disk, make it so the watcher will work.
			IFileManager::Get().MakeDirectory(*FileSystemPath);
			DirectoryWatcher->RegisterDirectoryChangedCallback_Handle(FileSystemPath, IDirectoryWatcher::FDirectoryChanged::CreateUObject(this, &UAssetRegistryImpl::OnDirectoryChanged), 
																	  OnContentPathMountedOnDirectoryChangedDelegateHandle, IDirectoryWatcher::WatchOptions::IncludeDirectoryChanges);
		}
	}
#endif // WITH_EDITOR

}

void UAssetRegistryImpl::OnContentPathDismounted(const FString& InAssetPath, const FString& FileSystemPath)
{
	// Sanitize
	FString AssetPath = InAssetPath;
	if (AssetPath.EndsWith(TEXT("/"), ESearchCase::CaseSensitive))
	{
		// We don't want a trailing slash here as it could interfere with RemoveAssetPath
		AssetPath.LeftChopInline(1, false);
	}

	// Remove all cached assets found at this location
	{
		TArray<FAssetData*> AllAssetDataToRemove;
		TArray<FString> PathList;
		const bool bRecurse = true;
		GetSubPaths(AssetPath, PathList, bRecurse);
		PathList.Add(AssetPath);
		for (const FString& Path : PathList)
		{
			TArray<FAssetData*>* AssetsInPath = State.CachedAssetsByPath.Find(FName(*Path));
			if (AssetsInPath)
			{
				AllAssetDataToRemove.Append(*AssetsInPath);
			}
		}

		for (FAssetData* AssetData : AllAssetDataToRemove)
		{
			RemoveAssetData(AssetData);
		}
	}

	// Remove the root path
	{
		const bool bEvenIfAssetsStillExist = true;
		RemoveAssetPath(FName(*AssetPath), bEvenIfAssetsStillExist);
	}

	// Stop listening for directory changes in this content path
#if WITH_EDITOR
	// In-game doesn't listen for directory changes
	if (GIsEditor)
	{
		FDirectoryWatcherModule& DirectoryWatcherModule = FModuleManager::LoadModuleChecked<FDirectoryWatcherModule>(TEXT("DirectoryWatcher"));
		IDirectoryWatcher* DirectoryWatcher = DirectoryWatcherModule.Get();
		if (DirectoryWatcher)
		{
			DirectoryWatcher->UnregisterDirectoryChangedCallback_Handle(FileSystemPath, OnContentPathMountedOnDirectoryChangedDelegateHandle);
		}
	}
#endif // WITH_EDITOR

}

void UAssetRegistryImpl::SetTemporaryCachingMode(bool bEnable)
{
	if (bIsTempCachingAlwaysEnabled || bEnable == bIsTempCachingEnabled)
	{
		return;
	}

	if (bEnable)
	{
		bIsTempCachingEnabled = true;
		bIsTempCachingUpToDate = false;
	}
	else
	{
		bIsTempCachingEnabled = false;
		ClearTemporaryCaches();
	}
}

bool UAssetRegistryImpl::GetTemporaryCachingMode() const
{
	return bIsTempCachingEnabled;
}

void UAssetRegistryImpl::ClearTemporaryCaches() const
{
	if (!bIsTempCachingEnabled && !bIsTempCachingAlwaysEnabled)
	{
		// We clear these as much as possible to get back memory
		TempCachedInheritanceMap.Empty();
		TempReverseInheritanceMap.Empty();
		bIsTempCachingUpToDate = false;
	}
}

void UAssetRegistryImpl::UpdateTemporaryCaches() const
{
	if (bIsTempCachingEnabled && bIsTempCachingUpToDate && TempCachingRegisteredClassesVersionNumber == GetRegisteredClassesVersionNumber())
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE(UAssetRegistryImpl::UpdateTemporaryCaches)

	TempCachedInheritanceMap = CachedBPInheritanceMap;
	TempReverseInheritanceMap.Reset();
	TempCachingRegisteredClassesVersionNumber = GetRegisteredClassesVersionNumber();
	for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
	{
		UClass* Class = *ClassIt;

		if (!Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			FName ClassName = Class->GetFName();
			if (Class->GetSuperClass())
			{
				FName SuperClassName = Class->GetSuperClass()->GetFName();
				TSet<FName>& ChildClasses = TempReverseInheritanceMap.FindOrAdd(SuperClassName);
				ChildClasses.Add(ClassName);

				TempCachedInheritanceMap.Add(ClassName, SuperClassName);
			}
			else
			{
				// This should only be true for a small number of CoreUObject classes
				TempCachedInheritanceMap.Add(ClassName, NAME_None);
			}

			// Add any implemented interfaces to the reverse inheritance map, but not to the forward map
			for (int32 i = 0; i < Class->Interfaces.Num(); ++i)
			{
				UClass* InterfaceClass = Class->Interfaces[i].Class;
				if (InterfaceClass) // could be nulled out by ForceDelete of a blueprint interface
				{
					TSet<FName>& ChildClasses = TempReverseInheritanceMap.FindOrAdd(InterfaceClass->GetFName());
					ChildClasses.Add(ClassName);
				}
			}
		}
	}

	// Add non-native classes to reverse map
	for (const TPair<FName, FName>& Kvp : CachedBPInheritanceMap)
	{
		const FName& ParentClassName = Kvp.Value;
		if (ParentClassName != NAME_None)
		{
			TSet<FName>& ChildClasses = TempReverseInheritanceMap.FindOrAdd(ParentClassName);
			ChildClasses.Add(Kvp.Key);
		}
	}

	bIsTempCachingUpToDate = true;
}

void UAssetRegistryImpl::GetSubClasses(const TArray<FName>& InClassNames, const TSet<FName>& ExcludedClassNames, TSet<FName>& SubClassNames) const
{
	UpdateTemporaryCaches();

	TSet<FName> ProcessedClassNames;
	for (FName ClassName : InClassNames)
	{
		// Now find all subclass names
		GetSubClasses_Recursive(ClassName, SubClassNames, ProcessedClassNames, TempReverseInheritanceMap, ExcludedClassNames);
	}

	ClearTemporaryCaches();
}

void UAssetRegistryImpl::GetSubClasses_Recursive(FName InClassName, TSet<FName>& SubClassNames, TSet<FName>& ProcessedClassNames, const TMap<FName, TSet<FName>>& ReverseInheritanceMap, const TSet<FName>& ExcludedClassNames) const
{
	if (ExcludedClassNames.Contains(InClassName))
	{
		// This class is in the exclusion list. Exclude it.
	}
	else if (ProcessedClassNames.Contains(InClassName))
	{
		// This class has already been processed. Ignore it.
	}
	else
	{
		SubClassNames.Add(InClassName);
		ProcessedClassNames.Add(InClassName);

		const TSet<FName>* FoundSubClassNames = ReverseInheritanceMap.Find(InClassName);
		if (FoundSubClassNames)
		{
			for (FName ClassName : (*FoundSubClassNames))
			{
				GetSubClasses_Recursive(ClassName, SubClassNames, ProcessedClassNames, ReverseInheritanceMap, ExcludedClassNames);
			}
		}
	}
}

void UAssetRegistryImpl::SetManageReferences(const TMultiMap<FAssetIdentifier, FAssetIdentifier>& ManagerMap, bool bClearExisting, EAssetRegistryDependencyType::Type RecurseType, ShouldSetManagerPredicate ShouldSetManager)
{
	TSet<FDependsNode*> ExistingManagedNodes;

	// Set default predicate if needed
	if (!ShouldSetManager)
	{
		ShouldSetManager = [](const FAssetIdentifier& Manager, const FAssetIdentifier& Source, const FAssetIdentifier& Target, EAssetRegistryDependencyType::Type DependencyType, EAssetSetManagerFlags::Type Flags)
		{
			return EAssetSetManagerResult::SetButDoNotRecurse;
		};
	}

	// Find all nodes with incoming manage dependencies
	for (const TPair<FAssetIdentifier, FDependsNode*>& Pair : State.CachedDependsNodes)
	{
		Pair.Value->IterateOverDependencies([&ExistingManagedNodes](FDependsNode* TestNode, EAssetRegistryDependencyType::Type DependencyType)
		{
			ExistingManagedNodes.Add(TestNode);
		}, EAssetRegistryDependencyType::Manage);
	}

	if (bClearExisting)
	{
		// Clear them
		for (FDependsNode* NodeToClear : ExistingManagedNodes)
		{
			NodeToClear->RemoveManageReferencesToNode();
		}
		ExistingManagedNodes.Empty();
	}

	TMap<FDependsNode*, TArray<FDependsNode *>> ExplicitMap; // Reverse of ManagerMap, specifies what relationships to add to each node

	for (const TPair<FAssetIdentifier, FAssetIdentifier>& Pair : ManagerMap)
	{
		FDependsNode* ManagedNode = State.FindDependsNode(Pair.Value);

		if (!ManagedNode)
		{
			UE_LOG(LogAssetRegistry, Error, TEXT("Cannot set %s to manage asset %s because it does not exist!"), *Pair.Key.ToString(), *Pair.Value.ToString());
			continue;
		}

		TArray<FDependsNode*>& ManagerList = ExplicitMap.FindOrAdd(ManagedNode);

		FDependsNode* ManagerNode = State.CreateOrFindDependsNode(Pair.Key);

		ManagerList.Add(ManagerNode);
	}

	TSet<FDependsNode*> Visited;
	TMap<FDependsNode*, EAssetRegistryDependencyType::Type> NodesToManage;
	TArray<FDependsNode*> NodesToHardReference;
	TArray<FDependsNode*> NodesToRecurse;

	// For each explicitly set asset
	for (const TPair<FDependsNode*, TArray<FDependsNode *>>& ExplicitPair : ExplicitMap)
	{
		FDependsNode* BaseManagedNode = ExplicitPair.Key;
		const TArray<FDependsNode*>& ManagerNodes = ExplicitPair.Value;

		for (FDependsNode* ManagerNode : ManagerNodes)
		{
			Visited.Reset();
			NodesToManage.Reset();
			NodesToRecurse.Reset();

			FDependsNode* SourceNode = ManagerNode;

			auto IterateFunction = [&ManagerNode, &SourceNode, &ShouldSetManager, &NodesToManage, &NodesToHardReference, &NodesToRecurse, &Visited, &ExplicitMap, &ExistingManagedNodes](FDependsNode* TargetNode, EAssetRegistryDependencyType::Type DependencyType)
			{
				// Only recurse if we haven't already visited, and this node passes recursion test
				if (!Visited.Contains(TargetNode))
				{
					EAssetSetManagerFlags::Type Flags = (EAssetSetManagerFlags::Type)((SourceNode == ManagerNode ? EAssetSetManagerFlags::IsDirectSet : 0)
						| (ExistingManagedNodes.Contains(TargetNode) ? EAssetSetManagerFlags::TargetHasExistingManager : 0)
						| (ExplicitMap.Find(TargetNode) && SourceNode != ManagerNode ? EAssetSetManagerFlags::TargetHasDirectManager : 0));

					EAssetSetManagerResult::Type Result = ShouldSetManager(ManagerNode->GetIdentifier(), SourceNode->GetIdentifier(), TargetNode->GetIdentifier(), DependencyType, Flags);

					if (Result == EAssetSetManagerResult::DoNotSet)
					{
						return;
					}

					EAssetRegistryDependencyType::Type ManageType = (Flags & EAssetSetManagerFlags::IsDirectSet) ? EAssetRegistryDependencyType::HardManage : EAssetRegistryDependencyType::SoftManage;
					NodesToManage.Add(TargetNode, ManageType);

					if (Result == EAssetSetManagerResult::SetAndRecurse)
					{
						NodesToRecurse.Push(TargetNode);
					}
				}
			};

			// Check initial node
			IterateFunction(BaseManagedNode, EAssetRegistryDependencyType::Manage);

			// Do all recursion first, but only if we have a recurse type
			if (RecurseType)
			{
				while (NodesToRecurse.Num())
				{
					// Pull off end of array, order doesn't matter
					SourceNode = NodesToRecurse.Pop();

					Visited.Add(SourceNode);

					SourceNode->IterateOverDependencies(IterateFunction, RecurseType);
				}
			}

			for (TPair<FDependsNode*, EAssetRegistryDependencyType::Type>& ManagePair : NodesToManage)
			{
				ManagePair.Key->AddReferencer(ManagerNode);
				ManagerNode->AddDependency(ManagePair.Key, ManagePair.Value);
			}
		}
	}
}

bool UAssetRegistryImpl::SetPrimaryAssetIdForObjectPath(const FName ObjectPath, FPrimaryAssetId PrimaryAssetId)
{
	FAssetData** FoundAssetData = State.CachedAssetsByObjectPath.Find(ObjectPath);

	if (!FoundAssetData)
	{
		return false;
	}

	FAssetData* AssetData = *FoundAssetData;

	FAssetDataTagMap TagsAndValues = AssetData->TagsAndValues.GetMap();
	TagsAndValues.Add(FPrimaryAssetId::PrimaryAssetTypeTag, PrimaryAssetId.PrimaryAssetType.ToString());
	TagsAndValues.Add(FPrimaryAssetId::PrimaryAssetNameTag, PrimaryAssetId.PrimaryAssetName.ToString());

	FAssetData NewAssetData = FAssetData(AssetData->PackageName, AssetData->PackagePath, AssetData->AssetName, AssetData->AssetClass, TagsAndValues, AssetData->ChunkIDs, AssetData->PackageFlags);

	UpdateAssetData(AssetData, NewAssetData);

	return true;
}

const FAssetData* UAssetRegistryImpl::GetCachedAssetDataForObjectPath(const FName ObjectPath) const
{
	return State.GetAssetByObjectPath(ObjectPath);
}