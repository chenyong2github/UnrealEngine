// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenerateNaniteDisplacedMeshCommandlet.h"

#include "CollectionManagerModule.h"
#include "CollectionManagerTypes.h"
#include "ICollectionManager.h"
#include "NaniteDisplacedMesh.h"
#include "NaniteDisplacedMeshLog.h"
#include "NaniteDisplacedMeshEditorModule.h"
#include "NaniteDisplacedMeshFactory.h"
#include "PackageSourceControlHelper.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Level.h"
#include "HAL/FileManager.h"
#include "UObject/GCObjectScopeGuard.h"
#include "Algo/Transform.h"

int32 UGenerateNaniteDisplacedMeshCommandlet::Main(const FString& CmdLineParams)
{
	int32 ExitCode = 0;

	// Process the arguments
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> Params;
	ParseCommandLine(*CmdLineParams, Tokens, Switches, Params);
	const FString CollectionFilter = Params.FindRef(TEXT("GNDMCollectionFilter"));
	const bool DeleteUnused = Switches.Contains(TEXT("GNDMDeleteUnused"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UWorld::StaticClass()->GetClassPathName());
	if (!CollectionFilter.IsEmpty())
	{
		UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("CollectionFilter: %s"), *CollectionFilter);
		const ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();
		CollectionManager.GetObjectsInCollection(FName(*CollectionFilter), ECollectionShareType::CST_All, Filter.SoftObjectPaths, ECollectionRecursionFlags::SelfAndChildren);
	}

	IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();

	if (IsRunningCommandlet())
	{
		UE_LOG(LogNaniteDisplacedMesh, Display, TEXT("SearchAllAssets..."));
		// This is automatically called in the regular editor but not when running a commandlet (unless cooking)
		// Must also search synchronously because AssetRegistry.IsLoadingAssets() won't account for this search
		AssetRegistry.SearchAllAssets(true);
	}

	TArray<FAssetData> LevelAssets;
	AssetRegistry.GetAssets(Filter, LevelAssets);

	FNaniteDisplacedMeshEditorModule& Module = FNaniteDisplacedMeshEditorModule::GetModule();
	Module.OnLinkDisplacedMeshOverride.BindUObject(this, &UGenerateNaniteDisplacedMeshCommandlet::OnLinkDisplacedMesh);

	const int32 LevelCount = LevelAssets.Num();
	for (int LevelIndex = 0; LevelIndex < LevelCount; ++LevelIndex)
	{
		const FAssetData& LevelAsset = LevelAssets[LevelIndex];
		UE_LOG(LogNaniteDisplacedMesh, Display, TEXT("-------------------------------------------------------------------"));
		UE_LOG(LogNaniteDisplacedMesh, Display, TEXT("Level: %s (%d/%d)"), *LevelAsset.GetSoftObjectPath().ToString(), LevelIndex + 1, LevelCount);
		LoadLevel(LevelAsset);
	}

	Module.OnLinkDisplacedMeshOverride.Unbind();
	UE_LOG(LogNaniteDisplacedMesh, Display, TEXT("==================================================================="));
	UE_LOG(LogNaniteDisplacedMesh, Display, TEXT("All levels processed"));
	UE_LOG(LogNaniteDisplacedMesh, Display, TEXT("==================================================================="));

	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("Collecting existing packages from %d folder(s):%s"), GeneratedFolders.Num(), *SetToString(GeneratedFolders));
	TSet<FString> ExistingPackages;
	for (const FString& Folder : GeneratedFolders)
	{
		GetPackagesInFolder(Folder, ExistingPackages);
	}

	const TSet<FString> AddedPackages = GeneratedPackages.Difference(ExistingPackages);
	const TSet<FString> UnusedPackages = ExistingPackages.Difference(GeneratedPackages);

	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("-------------------------------------------------------------------"));
	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("Detected %d existing package(s):%s"), ExistingPackages.Num(), *SetToString(ExistingPackages));
	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("-------------------------------------------------------------------"));
	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("Detected %d generated package(s):%s"), GeneratedPackages.Num(), *SetToString(GeneratedPackages));
	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("-------------------------------------------------------------------"));
	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("Detected %d added package(s):%s"), AddedPackages.Num(), *SetToString(AddedPackages));
	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("-------------------------------------------------------------------"));
	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("Detected %d unused package(s):%s"), UnusedPackages.Num(), *SetToString(UnusedPackages));

	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("==================================================================="));
	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("All packages detected"));
	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("==================================================================="));

	FPackageSourceControlHelper SourceControlHelper;
	if (SourceControlHelper.UseSourceControl())
	{
		UE_LOG(LogNaniteDisplacedMesh, Display, TEXT("Adding %d new package(s) to source control..."), AddedPackages.Num());

		if (!SourceControlHelper.AddToSourceControl(AddedPackages.Array()))
		{
			UE_LOG(LogNaniteDisplacedMesh, Error, TEXT("Unable to add one or more packages to source control!"));
			ExitCode = 1;
		}
	}

	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("DeleteUnused: %s"), DeleteUnused ? TEXT("true") : TEXT("false"));
	if (DeleteUnused)
	{
		UE_LOG(LogNaniteDisplacedMesh, Display, TEXT("Deleting %d unused package(s) from disk and source control (if enabled)..."), UnusedPackages.Num());
		if (!SourceControlHelper.Delete(UnusedPackages.Array()))
		{
			UE_LOG(LogNaniteDisplacedMesh, Error, TEXT("Unable to delete one or more packages from disk and source control (if enabled)!"));
			ExitCode = 1;
		}
	}

	UE_LOG(LogNaniteDisplacedMesh, Verbose, TEXT("ExitCode: %d"), ExitCode);
	return ExitCode;
}

UNaniteDisplacedMesh* UGenerateNaniteDisplacedMeshCommandlet::OnLinkDisplacedMesh(const FNaniteDisplacedMeshParams& Parameters, const FString& Folder)
{
	FNaniteDisplacedMeshEditorModule& Module = FNaniteDisplacedMeshEditorModule::GetModule();
	Module.OnLinkDisplacedMeshOverride.Unbind();

	/**
	 * This will force the saving of a new asset.
	 */
	UNaniteDisplacedMesh* NaniteDisplacedMesh = LinkDisplacedMeshAsset(nullptr, Parameters, Folder, ELinkDisplacedMeshAssetSetting::LinkAgainstPersistentAsset);
	if (NaniteDisplacedMesh != nullptr)
	{
		const FString PackageName = NaniteDisplacedMesh->GetPackage()->GetPathName();
		UE_LOG(LogNaniteDisplacedMesh, Display, TEXT("LinkedDisplacedMeshAsset: %s"), *PackageName);

		GeneratedPackages.Add(PackageName);
		GeneratedFolders.Add(Folder);
	}

	Module.OnLinkDisplacedMeshOverride.BindUObject(this, &UGenerateNaniteDisplacedMeshCommandlet::OnLinkDisplacedMesh);
	return NaniteDisplacedMesh;
}

void UGenerateNaniteDisplacedMeshCommandlet::LoadLevel(const FAssetData& AssetData)
{
	if (AssetData.GetClass() != UWorld::StaticClass())
	{
		return;
	}

	UWorld* World = Cast<UWorld>(AssetData.GetAsset());
	if (World == nullptr)
	{
		return;
	}

	World->AddToRoot();

	// Load the external actors (we should look with the open world team to see if there is a better way to do this)
	if (const ULevel* PersistantLevel = World->PersistentLevel)
	{
		if (PersistantLevel->bUseExternalActors || PersistantLevel->bIsPartitioned)
		{
			const FString ExternalActorsPath = ULevel::GetExternalActorsPath(AssetData.PackageName.ToString());
			const FString ExternalActorsFilePath = FPackageName::LongPackageNameToFilename(ExternalActorsPath);

			if (IFileManager::Get().DirectoryExists(*ExternalActorsFilePath))
			{
				IFileManager::Get().IterateDirectoryRecursively(*ExternalActorsFilePath, [](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
					{
						if (!bIsDirectory)
						{
							const FString Filename(FilenameOrDirectory);
							if (Filename.EndsWith(FPackageName::GetAssetPackageExtension()))
							{
								const FString PackageName = FPackageName::FilenameToLongPackageName(*Filename);
								LoadPackage(nullptr, *Filename, LOAD_None, nullptr, nullptr);
							}
						}

						return true;
					});
			}
		}
	}

	World->RemoveFromRoot();

	CollectGarbage(RF_NoFlags);
}

void UGenerateNaniteDisplacedMeshCommandlet::GetPackagesInFolder(const FString& InFolder, TSet<FString>& OutAssets)
{
	const IAssetRegistry& AssetRegistry = FAssetRegistryModule::GetRegistry();

	TArray<FAssetData> Assets;
	AssetRegistry.GetAssetsByPath(*InFolder, Assets, true, true);

	Algo::Transform(Assets, OutAssets, [](const FAssetData& Asset) { return Asset.PackageName.ToString(); });
}

FString UGenerateNaniteDisplacedMeshCommandlet::SetToString(const TSet<FString>& Set)
{
	FString Result;
	for (const FString& Element : Set)
	{
		Result += TEXT("\n - ") + Element; 
	}
	return Result;
}

namespace UE::GenerateNaniteDisplacedMesh::Private
{
	void RunCommandlet(const TArray<FString>& Args)
	{
		TArray<FString> CmdLineArgs;
		if (Args.IsValidIndex(0)) CmdLineArgs.Add(FString::Printf(TEXT("-GNDMCollectionFilter=\"%s\""), *Args[0]));
		if (Args.IsValidIndex(1) && Args[1].Equals(TEXT("true"), ESearchCase::IgnoreCase)) CmdLineArgs.Add(TEXT("-GNDMDeleteUnused"));
		const FString CmdLineParams = FString::Join(CmdLineArgs, TEXT(" "));

		UE_LOG(LogNaniteDisplacedMesh, Display, TEXT("Run commandlet GenerateNaniteDisplacedMesh: %s"), *CmdLineParams);

		UGenerateNaniteDisplacedMeshCommandlet* Commandlet = NewObject<UGenerateNaniteDisplacedMeshCommandlet>();
		FGCObjectScopeGuard ScopeGuard(Commandlet);
		Commandlet->Main(CmdLineParams);
	}

	static FAutoConsoleCommand ConsoleCommand = FAutoConsoleCommand(
		TEXT("GenerateNaniteDisplacedMesh"),
		TEXT("Generate nanite displacement mesh assets"),
		FConsoleCommandWithArgsDelegate::CreateStatic(&RunCommandlet)
		);
}
