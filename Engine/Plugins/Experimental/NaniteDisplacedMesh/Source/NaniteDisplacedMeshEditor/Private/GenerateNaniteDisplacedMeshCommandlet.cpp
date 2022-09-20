// Copyright Epic Games, Inc. All Rights Reserved.

#include "GenerateNaniteDisplacedMeshCommandlet.h"

#include "NaniteDisplacedMesh.h"
#include "NaniteDisplacedMeshEditorModule.h"
#include "NaniteDisplacedMeshFactory.h"

#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Level.h"
#include "HAL/FileManager.h"
#include "Modules/ModuleManager.h"

int32 UGenerateNaniteDisplacedMeshCommandlet::Main(const FString& Params)
{
	// Process the arguments
	//TArray<FString> Tokens, Switches;
	//ParseCommandLine(*Params, Tokens, Switches);

	FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
	FDelegateHandle OnAssetAddedHandle = AssetRegistry.OnAssetAdded().AddUObject(this, &UGenerateNaniteDisplacedMeshCommandlet::ProcessAssetData);

	TArray<FAssetData> WorldsAssetData;
	AssetRegistry.GetAssetsByClass(UWorld::StaticClass()->GetClassPathName(), WorldsAssetData);

	FNaniteDisplacedMeshEditorModule::GetModule().OnLinkDisplacedMeshOverride.BindUObject(this, &UGenerateNaniteDisplacedMeshCommandlet::OnLinkDisplacedMesh);


	for (const FAssetData& WorldAssetData : WorldsAssetData)
	{
		ProcessAssetData(WorldAssetData);
	}

	while (AssetRegistry.IsLoadingAssets())
	{
		CommandletHelpers::TickEngine();
	}

	AssetRegistry.OnAssetAdded().Remove(OnAssetAddedHandle);
	FNaniteDisplacedMeshEditorModule::GetModule().OnLinkDisplacedMeshOverride.Unbind();

	return 0;
}

UNaniteDisplacedMesh* UGenerateNaniteDisplacedMeshCommandlet::OnLinkDisplacedMesh(const FNaniteDisplacedMeshParams& InParameters, const FString& DisplacedMeshFolder)
{
	FNaniteDisplacedMeshEditorModule::GetModule().OnLinkDisplacedMeshOverride.Unbind();

	/**
	 * This will force the saving of a new asset.
	 */
	UNaniteDisplacedMesh* NaniteDisplacedMesh = LinkDisplacedMeshAsset(nullptr, InParameters, DisplacedMeshFolder, ELinkDisplacedMeshAssetSetting::LinkAgainstPersistentAsset);

	FNaniteDisplacedMeshEditorModule::GetModule().OnLinkDisplacedMeshOverride.BindUObject(this, &UGenerateNaniteDisplacedMeshCommandlet::OnLinkDisplacedMesh);

	return NaniteDisplacedMesh;
}

void UGenerateNaniteDisplacedMeshCommandlet::ProcessAssetData(const FAssetData& InAssetData)
{
	if (UClass* AssetClass = InAssetData.GetClass())
	{
		if (AssetClass->IsChildOf<UWorld>())
		{
			if (UWorld* World = Cast<UWorld>(InAssetData.GetAsset()))
			{
				World->AddToRoot();

				// Load the external actors (we should look with the open world team to see if there is a better way to do this)
				if (ULevel* PersistantLevel = World->PersistentLevel)
				{
					if (PersistantLevel->bUseExternalActors || PersistantLevel->bIsPartitioned)
					{
						FString ExternalActorsPath = ULevel::GetExternalActorsPath(InAssetData.PackageName.ToString());
						FString ExternalActorsFilePath = FPackageName::LongPackageNameToFilename(ExternalActorsPath);

						if (IFileManager::Get().DirectoryExists(*ExternalActorsFilePath))
						{
							bool bResult = IFileManager::Get().IterateDirectoryRecursively(*ExternalActorsFilePath, [](const TCHAR* FilenameOrDirectory, bool bIsDirectory)
								{
									if (!bIsDirectory)
									{
										FString Filename(FilenameOrDirectory);
										if (Filename.EndsWith(FPackageName::GetAssetPackageExtension()))
										{
											AActor* MainPackageActor = nullptr;
											AActor* PotentialMainPackageActor = nullptr;

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


				CommandletHelpers::TickEngine();
			}
		}
	}
}
