// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Commandlets/AssetRegistryGenerator.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/ArrayWriter.h"
#include "Misc/App.h"
#include "Serialization/JsonTypes.h"
#include "Serialization/JsonReader.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Settings/ProjectPackagingSettings.h"
#include "CollectionManagerTypes.h"
#include "ICollectionManager.h"
#include "CollectionManagerModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "AssetRegistryModule.h"
#include "GameDelegates.h"
#include "Commandlets/ChunkDependencyInfo.h"
#include "IPlatformFileSandboxWrapper.h"
#include "Misc/ConfigCacheIni.h"
#include "Stats/StatsMisc.h"
#include "Templates/UniquePtr.h"
#include "Engine/AssetManager.h"

#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY_STATIC(LogAssetRegistryGenerator, Log, All);

#define LOCTEXT_NAMESPACE "AssetRegistryGenerator"

#if WITH_EDITOR
#include "HAL/ThreadHeartBeat.h"
#endif

//////////////////////////////////////////////////////////////////////////
// Static functions
FName GetPackageNameFromDependencyPackageName(const FName RawPackageFName)
{
	FName PackageFName = RawPackageFName;
	if ((FPackageName::IsValidLongPackageName(RawPackageFName.ToString()) == false) &&
		(FPackageName::IsScriptPackage(RawPackageFName.ToString()) == false))
	{
		FText OutReason;
		if (!FPackageName::IsValidLongPackageName(RawPackageFName.ToString(), true, &OutReason))
		{
			const FText FailMessage = FText::Format(LOCTEXT("UnableToGeneratePackageName", "Unable to generate long package name for {0}. {1}"),
				FText::FromString(RawPackageFName.ToString()), OutReason);

			UE_LOG(LogAssetRegistryGenerator, Warning, TEXT("%s"), *(FailMessage.ToString()));
			return NAME_None;
		}


		FString LongPackageName;
		if (FPackageName::SearchForPackageOnDisk(RawPackageFName.ToString(), &LongPackageName) == false)
		{
			return NAME_None;
		}
		PackageFName = FName(*LongPackageName);
	}

	// don't include script packages in dependencies as they are always in memory
	if (FPackageName::IsScriptPackage(PackageFName.ToString()))
	{
		// no one likes script packages
		return NAME_None;
	}
	return PackageFName;
}


//////////////////////////////////////////////////////////////////////////
// FAssetRegistryGenerator

FAssetRegistryGenerator::FAssetRegistryGenerator(const ITargetPlatform* InPlatform)
	: AssetRegistry(FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get())
	, TargetPlatform(InPlatform)
	, bGenerateChunks(false)
	, bUseAssetManager(false)
	, HighestChunkId(0)
{
	DependencyInfo = GetMutableDefault<UChunkDependencyInfo>();

	bool bOnlyHardReferences = false;
	const UProjectPackagingSettings* const PackagingSettings = GetDefault<UProjectPackagingSettings>();
	if (PackagingSettings)
	{
		bOnlyHardReferences = PackagingSettings->bChunkHardReferencesOnly;
	}	

	DependencyType = bOnlyHardReferences ? EAssetRegistryDependencyType::Hard : EAssetRegistryDependencyType::Packages;

	if (UAssetManager::IsValid() && !FGameDelegates::Get().GetAssignStreamingChunkDelegate().IsBound() && !FGameDelegates::Get().GetGetPackageDependenciesForManifestGeneratorDelegate().IsBound())
	{
		bUseAssetManager = true;

		UAssetManager::Get().UpdateManagementDatabase();
	}

	InitializeChunkIdPakchunkIndexMapping();
}

FAssetRegistryGenerator::~FAssetRegistryGenerator()
{
	for (auto ChunkSet : ChunkManifests)
	{
		delete ChunkSet;
	}
	ChunkManifests.Empty();
	for (auto ChunkSet : FinalChunkManifests)
	{
		delete ChunkSet;
	}
	FinalChunkManifests.Empty();
}

bool FAssetRegistryGenerator::CleanTempPackagingDirectory(const FString& Platform) const
{
	FString TmpPackagingDir = GetTempPackagingDirectoryForPlatform(Platform);
	if (IFileManager::Get().DirectoryExists(*TmpPackagingDir))
	{
		if (!IFileManager::Get().DeleteDirectory(*TmpPackagingDir, false, true))
		{
			UE_LOG(LogAssetRegistryGenerator, Error, TEXT("Failed to delete directory: %s"), *TmpPackagingDir);
			return false;
		}
	}

	FString ChunkListDir = FPaths::Combine(*FPaths::ProjectLogDir(), TEXT("ChunkLists"));
	if (IFileManager::Get().DirectoryExists(*ChunkListDir))
	{
		if (!IFileManager::Get().DeleteDirectory(*ChunkListDir, false, true))
		{
			UE_LOG(LogAssetRegistryGenerator, Error, TEXT("Failed to delete directory: %s"), *ChunkListDir);
			return false;
		}
	}
	return true;
}

bool FAssetRegistryGenerator::ShouldPlatformGenerateStreamingInstallManifest(const ITargetPlatform* Platform) const
{
	if (Platform)
	{
		FConfigFile PlatformIniFile;
		FConfigCacheIni::LoadLocalIniFile(PlatformIniFile, TEXT("Game"), true, *Platform->IniPlatformName());
		FString ConfigString;
		if (PlatformIniFile.GetString(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("bGenerateChunks"), ConfigString))
		{
			return FCString::ToBool(*ConfigString);
		}
	}

	return false;
}


int64 FAssetRegistryGenerator::GetMaxChunkSizePerPlatform(const ITargetPlatform* Platform) const
{
	if ( Platform )
	{
		FConfigFile PlatformIniFile;
		FConfigCacheIni::LoadLocalIniFile(PlatformIniFile, TEXT("Game"), true, *Platform->IniPlatformName());
		FString ConfigString;
		if (PlatformIniFile.GetString(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("MaxChunkSize"), ConfigString))
		{
			return FCString::Atoi64(*ConfigString);
		}
	}

	return -1;
}

class FPackageFileSizeVisitor : public IPlatformFile::FDirectoryStatVisitor
{
	TMap<FString, int64>& PackageFileSizes;
public:
	FPackageFileSizeVisitor(TMap<FString, int64>& InFileSizes)
		: PackageFileSizes(InFileSizes)
	{}
	virtual bool Visit(const TCHAR* FilenameOrDirectory, const FFileStatData& StatData)
	{
		const TCHAR* Extensions[] = { TEXT(".uexp"), TEXT(".uasset"), TEXT(".ubulk"), TEXT(".ufont"), TEXT(".umap"), TEXT(".uptnl") };

		if (StatData.bIsDirectory)
			return true;

		const TCHAR* Extension = FCString::Strrchr(FilenameOrDirectory, '.');
		if (!Extension)
			return true;

		int32 ExtIndex = 0;
		for (; ExtIndex < UE_ARRAY_COUNT(Extensions); ++ExtIndex)
		{
			if (0 == FCString::Stricmp(Extension, Extensions[ExtIndex]))
				break;
		}

		if (ExtIndex >= UE_ARRAY_COUNT(Extensions))
			return true;

		int32 LengthWithoutExtension = Extension - FilenameOrDirectory;
		FString FilenameWithoutExtension(LengthWithoutExtension, FilenameOrDirectory);

		if (int64* CurrentPackageSize = PackageFileSizes.Find(FilenameWithoutExtension))
		{
			int64& TotalPackageSize = *CurrentPackageSize;
			TotalPackageSize += StatData.FileSize;
		}
		else
		{
			PackageFileSizes.Add(FilenameWithoutExtension, StatData.FileSize);
		}

		return true;
	}
};

static void ParseChunkLayerAssignment(TArray<FString> ChunkLayerAssignmentArray, TMap<int32, int32>& OutChunkLayerAssignment)
{
	OutChunkLayerAssignment.Empty();

	const TCHAR* PropertyChunkId = TEXT("ChunkId=");
	const TCHAR* PropertyLayerId = TEXT("Layer=");
	for (FString& Entry : ChunkLayerAssignmentArray)
	{
		// Remove parentheses
		Entry.TrimStartAndEndInline();
		Entry.ReplaceInline(TEXT("("), TEXT(""));
		Entry.ReplaceInline(TEXT(")"), TEXT(""));

		int32 ChunkId = -1;
		int32 LayerId = -1;
		FParse::Value(*Entry, PropertyChunkId, ChunkId);
		FParse::Value(*Entry, PropertyLayerId, LayerId);

		if (ChunkId >= 0 && LayerId >= 0 && !OutChunkLayerAssignment.Contains(ChunkId))
		{
			OutChunkLayerAssignment.Add(ChunkId, LayerId);
		}
	}
}

static void AssignLayerChunkDelegate(const FAssignLayerChunkMap* ChunkManifest, const FString& Platform, const int32 ChunkIndex, int32& OutChunkLayer)
{
	OutChunkLayer = 0;

	FConfigFile PlatformIniFile;
	FConfigCacheIni::LoadLocalIniFile(PlatformIniFile, TEXT("Game"), true, *Platform);
	TArray<FString> ChunkLayerAssignmentArray;
	PlatformIniFile.GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("ChunkLayerAssignment"), ChunkLayerAssignmentArray);

	TMap<int32, int32> ChunkLayerAssignment;
	ParseChunkLayerAssignment(ChunkLayerAssignmentArray, ChunkLayerAssignment);

	int32* LayerId = ChunkLayerAssignment.Find(ChunkIndex);
	if (LayerId)
	{
		OutChunkLayer = *LayerId;
	}
}

bool FAssetRegistryGenerator::GenerateStreamingInstallManifest(int64 InExtraFlavorChunkSize, FSandboxPlatformFile* InSandboxFile)
{
	const FString Platform = TargetPlatform->PlatformName();

	// empty out the current paklist directory
	FString TmpPackagingDir = GetTempPackagingDirectoryForPlatform(Platform);

	int64 MaxChunkSize = GetMaxChunkSizePerPlatform( TargetPlatform );

	if (InExtraFlavorChunkSize > 0)
	{
		TmpPackagingDir /= TEXT("ExtraFlavor");
		MaxChunkSize = InExtraFlavorChunkSize;
	}

	if (!IFileManager::Get().MakeDirectory(*TmpPackagingDir, true))
	{
		UE_LOG(LogAssetRegistryGenerator, Error, TEXT("Failed to create directory: %s"), *TmpPackagingDir);
		return false;
	}
	
	// open a file for writing the list of pak file lists that we've generated
	FString PakChunkListFilename = TmpPackagingDir / TEXT("pakchunklist.txt");
	TUniquePtr<FArchive> PakChunkListFile(IFileManager::Get().CreateFileWriter(*PakChunkListFilename));

	if (!PakChunkListFile)
	{
		UE_LOG(LogAssetRegistryGenerator, Error, TEXT("Failed to open output pakchunklist file %s"), *PakChunkListFilename);
		return false;
	}

	FString PakChunkLayerInfoFilename = FString::Printf(TEXT("%s/pakchunklayers.txt"), *TmpPackagingDir);
	TUniquePtr<FArchive> ChunkLayerFile(IFileManager::Get().CreateFileWriter(*PakChunkLayerInfoFilename));

	TArray<FString> CompressedChunkWildcards;
	{
		// never screw with server pak files.  This hack only cares about client platforms
		if (!TargetPlatform->IsServerOnly())
		{
			FConfigFile PlatformIniFile;
			FConfigCacheIni::LoadLocalIniFile(PlatformIniFile, TEXT("Game"), true, *TargetPlatform->IniPlatformName());
			FString ConfigString;
			PlatformIniFile.GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("CompressedChunkWildcard"), CompressedChunkWildcards);
		}
	}

	// Add manifests for any staging-time only groups
	if (bUseAssetManager && !TargetPlatform->HasSecurePackageFormat())
	{
		UE_LOG(LogAssetRegistryGenerator, Log, TEXT("Updating stage-only encryption manifests for platform %s"), *TargetPlatform->IniPlatformName());
		FContentEncryptionConfig ContentEncryptionConfig;
		UAssetManager::Get().GetContentEncryptionConfig(ContentEncryptionConfig);
		const FContentEncryptionConfig::TGroupMap& EncryptedNonUFSFileGroups = ContentEncryptionConfig.GetPackageGroupMap();
		
		for (const FContentEncryptionConfig::TGroupMap::ElementType& Element : EncryptedNonUFSFileGroups)
		{
			if (Element.Value.bStageTimeOnly)
			{
				UE_LOG(LogAssetRegistryGenerator, Log, TEXT("Adding stage-time only manifest for group '%s'"), *Element.Key.ToString());

				FName GroupName = Element.Key;
				const TSet<FName>& PackageNames = Element.Value.PackageNames;
				
				FChunkPackageSet* NewManifest = new FChunkPackageSet();
				for (FName PackageName : PackageNames)
				{
					NewManifest->Add(PackageName, FPackageName::LongPackageNameToFilename(PackageName.ToString()));
				}

				int32 ChunkID = UAssetManager::Get().GetContentEncryptionGroupChunkID(GroupName);
				int32 PakchunkIndex = GetPakchunkIndex(ChunkID);
				if (PakchunkIndex >= FinalChunkManifests.Num())
				{
					FinalChunkManifests.AddDefaulted(PakchunkIndex - FinalChunkManifests.Num() + 1);
				}
				checkf(PakchunkIndex < FinalChunkManifests.Num(), TEXT("Chunk %i out of range. %i manifests available"), PakchunkIndex, FinalChunkManifests.Num() - 1);
				checkf(FinalChunkManifests[PakchunkIndex] == nullptr || FinalChunkManifests[PakchunkIndex]->Num() == 0, TEXT("Manifest already exists for chunk %i"), PakchunkIndex);

				if (FinalChunkManifests[PakchunkIndex])
				{
					delete FinalChunkManifests[PakchunkIndex];
				}

				FinalChunkManifests[PakchunkIndex] = NewManifest;
			}
		}
	}

	TMap<FString, int64> PackageFileSizes;
	if (MaxChunkSize > 0)
	{
		FString SandboxPath = InSandboxFile->GetSandboxDirectory();
		SandboxPath.ReplaceInline(TEXT("[Platform]"), *Platform);
		FPackageFileSizeVisitor PackageSearch(PackageFileSizes);
		IFileManager::Get().IterateDirectoryStatRecursively(*SandboxPath, PackageSearch);
	}

	// generate per-chunk pak list files
	for (int32 PakchunkIndex = 0; PakchunkIndex < FinalChunkManifests.Num(); ++PakchunkIndex)
	{
		// Serialize chunk layers whether chunk is empty or not
		int32 TargetLayer = 0;
		FGameDelegates::Get().GetAssignLayerChunkDelegate().ExecuteIfBound(FinalChunkManifests[PakchunkIndex], Platform, PakchunkIndex, TargetLayer);

		FString LayerString = FString::Printf(TEXT("%d\r\n"), TargetLayer);
		ChunkLayerFile->Serialize(TCHAR_TO_ANSI(*LayerString), LayerString.Len());

		// Is this chunk empty?
		if (!FinalChunkManifests[PakchunkIndex])
		{
			continue;
		}

		int32 FilenameIndex = 0;
		TArray<FString> ChunkFilenames;
		FinalChunkManifests[PakchunkIndex]->GenerateValueArray(ChunkFilenames);
		bool bFinishedAllFiles = false;
		for (int32 SubChunkIndex = 0; !bFinishedAllFiles; ++SubChunkIndex)
		{
			const FString PakChunkFilename = (SubChunkIndex > 0)
				? FString::Printf(TEXT("pakchunk%d_s%d.txt"), PakchunkIndex, SubChunkIndex)
				: FString::Printf(TEXT("pakchunk%d.txt"), PakchunkIndex);

			const FString PakListFilename = FString::Printf(TEXT("%s/%s"), *TmpPackagingDir, *PakChunkFilename);
			TUniquePtr<FArchive> PakListFile(IFileManager::Get().CreateFileWriter(*PakListFilename));

			if (!PakListFile)
			{
				UE_LOG(LogAssetRegistryGenerator, Error, TEXT("Failed to open output paklist file %s"), *PakListFilename);
				return false;
			}

			FString PakChunkOptions;
			for (const FString& CompressedChunkWildcard : CompressedChunkWildcards)
			{
				if (PakChunkFilename.MatchesWildcard(CompressedChunkWildcard))
				{
					PakChunkOptions += " compressed";
					break;
				}
			}

			if (bUseAssetManager)
			{
				// For encryption chunks, PakchunkIndex equals ChunkID
				FGuid Guid = UAssetManager::Get().GetChunkEncryptionKeyGuid(PakchunkIndex);
				if (Guid.IsValid())
				{
					PakChunkOptions += TEXT(" encryptionkeyguid=") + Guid.ToString();

					// If this chunk has a seperate unique asset registry, add it to first subchunk's manifest here
					if (SubChunkIndex == 0)
					{
						// For chunks with unique asset registry name, pakchunkIndex should equal chunkid
						FName RegistryName = UAssetManager::Get().GetUniqueAssetRegistryName(PakchunkIndex);
						if (RegistryName != NAME_None)
						{
							FString AssetRegistryFilename = FString::Printf(TEXT("%s%sAssetRegistry%s.bin"), *InSandboxFile->GetSandboxDirectory(), *InSandboxFile->GetGameSandboxDirectoryName(), *RegistryName.ToString());
							ChunkFilenames.Add(AssetRegistryFilename);
						}
					}
				}
			}

			if (bUseAssetManager && SubChunkIndex == 0)
			{
				// Sort so the order is consistent. If load order is important then it should be specified as a load order file to UnrealPak
				ChunkFilenames.Sort();
			}

			int64 CurrentPakSize = 0;
			bFinishedAllFiles = true;
			for (; FilenameIndex < ChunkFilenames.Num(); ++FilenameIndex)
			{
				FString Filename = ChunkFilenames[FilenameIndex];
				FString PakListLine = FPaths::ConvertRelativePathToFull(Filename.Replace(TEXT("[Platform]"), *Platform));
				if (MaxChunkSize > 0)
				{
					const int64* PackageFileSize = PackageFileSizes.Find(PakListLine);
					CurrentPakSize += PackageFileSize ? *PackageFileSize : 0;
					if (MaxChunkSize < CurrentPakSize)
					{
						// early out if we are over memory limit
						bFinishedAllFiles = false;
						break;
					}
				}
				
				PakListLine.ReplaceInline(TEXT("/"), TEXT("\\"));
				PakListLine += TEXT("\r\n");
				PakListFile->Serialize(TCHAR_TO_ANSI(*PakListLine), PakListLine.Len());
			}

			PakListFile->Close();

			// add this pakfilelist to our master list of pakfilelists
			FString PakChunkListLine = FString::Printf(TEXT("%s%s\r\n"), *PakChunkFilename, *PakChunkOptions);
			PakChunkListFile->Serialize(TCHAR_TO_ANSI(*PakChunkListLine), PakChunkListLine.Len());
		}
	}

	ChunkLayerFile->Close();
	PakChunkListFile->Close();

	return true;
}

void FAssetRegistryGenerator::GenerateChunkManifestForPackage(const FName& PackageFName, const FString& PackagePathName, const FString& SandboxFilename, const FString& LastLoadedMapName, FSandboxPlatformFile* InSandboxFile)
{
	TArray<int32> TargetChunks;
	TArray<int32> ExistingChunkIDs;

	if (!bGenerateChunks)
	{
		TargetChunks.AddUnique(0);
		ExistingChunkIDs.AddUnique(0);
	}

	if (bGenerateChunks)
	{
		// Collect all chunk IDs associated with this package from the asset registry
		TArray<int32> RegistryChunkIDs = GetAssetRegistryChunkAssignments(PackageFName);

		ExistingChunkIDs = GetExistingPackageChunkAssignments(PackageFName);
		if (bUseAssetManager)
		{
			// No distinction between source of existing chunks for new flow
			RegistryChunkIDs.Append(ExistingChunkIDs);

			UAssetManager::Get().GetPackageChunkIds(PackageFName, TargetPlatform, RegistryChunkIDs, TargetChunks);
		}
		else
		{
			// Try to call game-specific delegate to determine the target chunk ID
			// FString Name = Package->GetPathName();
			if (FGameDelegates::Get().GetAssignStreamingChunkDelegate().IsBound())
			{
				FGameDelegates::Get().GetAssignStreamingChunkDelegate().ExecuteIfBound(PackagePathName, LastLoadedMapName, RegistryChunkIDs, ExistingChunkIDs, TargetChunks);
			}
			else
			{
				//Take asset registry assignments and existing assignments
				TargetChunks.Append(RegistryChunkIDs);
				TargetChunks.Append(ExistingChunkIDs);
			}
		}
	}

	// if the delegate requested a specific chunk assignment, add them package to it now.
	for (const auto& PackageChunk : TargetChunks)
	{
		AddPackageToManifest(SandboxFilename, PackageFName, PackageChunk);
	}
	// If the delegate requested to remove the package from any chunk, remove it now
	for (const auto& PackageChunk : ExistingChunkIDs)
	{
		if (!TargetChunks.Contains(PackageChunk))
		{
			RemovePackageFromManifest(PackageFName, PackageChunk);
		}
	}

}


void FAssetRegistryGenerator::CleanManifestDirectories()
{
	CleanTempPackagingDirectory(TargetPlatform->PlatformName());
}

bool FAssetRegistryGenerator::LoadPreviousAssetRegistry(const FString& Filename)
{
	// First try development asset registry
	FArrayReader SerializedAssetData;

	if (IFileManager::Get().FileExists(*Filename) && FFileHelper::LoadFileToArray(SerializedAssetData, *Filename))
	{
		FAssetRegistrySerializationOptions Options;
		Options.ModifyForDevelopment();

		return PreviousState.Serialize(SerializedAssetData, Options);
	}

	return false;
}

void FAssetRegistryGenerator::InjectEncryptionData(FAssetRegistryState& TargetState)
{
	if (bUseAssetManager)
	{
		UAssetManager& AssetManager = UAssetManager::Get();

		TMap<int32, FGuid> GuidCache;
		FContentEncryptionConfig EncryptionConfig;
		AssetManager.GetContentEncryptionConfig(EncryptionConfig);

		for (FContentEncryptionConfig::TGroupMap::ElementType EncryptedAssetSetElement : EncryptionConfig.GetPackageGroupMap())
		{
			FName SetName = EncryptedAssetSetElement.Key;
			TSet<FName>& EncryptedRootAssets = EncryptedAssetSetElement.Value.PackageNames;

			for (FName EncryptedRootPackageName : EncryptedRootAssets)
			{
				const TArray<const FAssetData*>& PackageAssets = TargetState.GetAssetsByPackageName(EncryptedRootPackageName);

				for (const FAssetData* PackageAsset : PackageAssets)
				{
					FAssetData* AssetData = const_cast<FAssetData*>(PackageAsset);

					if (AssetData)
					{
						FString GuidString;

						if (AssetData->ChunkIDs.Num() > 1)
						{
							UE_LOG(LogAssetRegistryGenerator, Error, TEXT("Encrypted root asset '%s' exists in two chunks. Only secondary assets should be shared between chunks."), *AssetData->ObjectPath.ToString());
						}
						else if (AssetData->ChunkIDs.Num() == 1)
						{
							int32 ChunkID = AssetData->ChunkIDs[0];
							FGuid Guid;

							if (GuidCache.Contains(ChunkID))
							{
								Guid = GuidCache[ChunkID];
							}
							else
							{
								Guid = GuidCache.Add(ChunkID, AssetManager.GetChunkEncryptionKeyGuid(ChunkID));
							}

							if (Guid.IsValid())
							{
								FAssetDataTagMap TagsAndValues = AssetData->TagsAndValues.GetMap();
								TagsAndValues.Add(UAssetManager::GetEncryptionKeyAssetTagName(), Guid.ToString());
								FAssetData NewAssetData = FAssetData(AssetData->PackageName, AssetData->PackagePath, AssetData->AssetName, AssetData->AssetClass, TagsAndValues, AssetData->ChunkIDs, AssetData->PackageFlags);
								TargetState.UpdateAssetData(AssetData, NewAssetData);
							}
						}
					}
				}
			}
		}
	}
}

bool FAssetRegistryGenerator::SaveManifests(FSandboxPlatformFile* InSandboxFile, int64 InExtraFlavorChunkSize)
{
	// Always do package dependency work, is required to modify asset registry
	FixupPackageDependenciesForChunks(InSandboxFile);

	if (bGenerateChunks)
	{	
		if (!GenerateStreamingInstallManifest(InExtraFlavorChunkSize, InSandboxFile))
		{
			return false;
		}

		// Generate map for the platform abstraction
		TMultiMap<FString, int32> PakchunkMap;	// asset -> ChunkIDs map
		TSet<int32> PakchunkIndicesInUse;
		const FString PlatformName = TargetPlatform->PlatformName();

		// Collect all unique chunk indices and map all files to their chunks
		for (int32 PakchunkIndex = 0; PakchunkIndex < FinalChunkManifests.Num(); ++PakchunkIndex)
		{
			check(FinalChunkManifests[PakchunkIndex]);
			if (FinalChunkManifests[PakchunkIndex]->Num())
			{
				PakchunkIndicesInUse.Add(PakchunkIndex);
				for (auto& Filename : *FinalChunkManifests[PakchunkIndex])
				{
					FString PlatFilename = Filename.Value.Replace(TEXT("[Platform]"), *PlatformName);
					PakchunkMap.Add(PlatFilename, PakchunkIndex);
				}
			}
		}

		// Sort our chunk IDs and file paths
		PakchunkMap.KeySort(TLess<FString>());
		PakchunkIndicesInUse.Sort(TLess<int32>());

		// Platform abstraction will generate any required platform-specific files for the chunks
		if (!TargetPlatform->GenerateStreamingInstallManifest(PakchunkMap, PakchunkIndicesInUse))
		{
			return false;
		}

		if (!bUseAssetManager)
		{
			// In new flow, this is written later
			GenerateAssetChunkInformationCSV(FPaths::Combine(*FPaths::ProjectLogDir(), TEXT("ChunkLists")), true);
		}
	}

	return true;
}

bool FAssetRegistryGenerator::ContainsMap(const FName& PackageName) const
{
	return PackagesContainingMaps.Contains(PackageName);
}

FAssetPackageData* FAssetRegistryGenerator::GetAssetPackageData(const FName& PackageName)
{
	return State.CreateOrGetAssetPackageData(PackageName);
}

void FAssetRegistryGenerator::UpdateKeptPackagesDiskData(const TArray<FName>& InKeptPackages)
{
	for (const FName& PackageName : InKeptPackages)
	{
		// Get mutable PackageData without creating it when it does not exist
		FAssetPackageData* PackageData =
			State.GetAssetPackageData(PackageName) ?
			State.CreateOrGetAssetPackageData(PackageName) :
			nullptr;
		const FAssetPackageData* PreviousPackageData =
			PreviousState.GetAssetPackageData(PackageName);

		if (!PackageData || !PreviousPackageData)
		{
			continue;
		}

		if (PackageData->PackageGuid != PreviousPackageData->PackageGuid)
		{
			continue;
		}

		PackageData->CookedHash = PreviousPackageData->CookedHash;
		PackageData->DiskSize = PreviousPackageData->DiskSize;
	}
}

void FAssetRegistryGenerator::UpdateKeptPackagesAssetData()
{
	for (FName PackageName : KeptPackages)
	{
		const TArray<const FAssetData*>& PreviousAssetDatas = PreviousState.GetAssetsByPackageName(PackageName);
		for (int I = 0; I < PreviousAssetDatas.Num(); ++I)
		{
			State.UpdateAssetData(*PreviousAssetDatas[I]);
		}
	}
}

void FAssetRegistryGenerator::UpdateCollectionAssetData()
{
	// Read out the per-platform settings use to build the list of collections to tag
	bool bTagAllCollections = false;
	TArray<FString> CollectionsToIncludeOrExclude;
	{
		const FString PlatformIniName = TargetPlatform->IniPlatformName();

		FConfigFile PlatformEngineIni;
		FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Engine"), true, (!PlatformIniName.IsEmpty() ? *PlatformIniName : ANSI_TO_TCHAR(FPlatformProperties::IniPlatformName())));

		// The list of collections will either be a inclusive or a exclusive depending on the value of bTagAllCollections
		PlatformEngineIni.GetBool(TEXT("AssetRegistry"), TEXT("bTagAllCollections"), bTagAllCollections);
		PlatformEngineIni.GetArray(TEXT("AssetRegistry"), bTagAllCollections ? TEXT("CollectionsToExcludeAsTags") : TEXT("CollectionsToIncludeAsTags"), CollectionsToIncludeOrExclude);
	}

	// Build the list of collections we should tag for each asset
	TMap<FName, TArray<FName>> AssetPathNamesToCollectionTags;
	{
		ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

		TArray<FCollectionNameType> CollectionNamesToTag;
		CollectionManager.GetCollections(CollectionNamesToTag);
		if (bTagAllCollections)
		{
			CollectionNamesToTag.RemoveAll([&CollectionsToIncludeOrExclude](const FCollectionNameType& CollectionNameAndType)
			{
				return CollectionsToIncludeOrExclude.Contains(CollectionNameAndType.Name.ToString());
			});
		}
		else
		{
			CollectionNamesToTag.RemoveAll([&CollectionsToIncludeOrExclude](const FCollectionNameType& CollectionNameAndType)
			{
				return !CollectionsToIncludeOrExclude.Contains(CollectionNameAndType.Name.ToString());
			});
		}
		
		TArray<FName> TmpAssetPathNames;
		for (const FCollectionNameType& CollectionNameToTag : CollectionNamesToTag)
		{
			const FName CollectionTagName = *FString::Printf(TEXT("%s%s"), FAssetData::GetCollectionTagPrefix(), *CollectionNameToTag.Name.ToString());

			TmpAssetPathNames.Reset();
			CollectionManager.GetAssetsInCollection(CollectionNameToTag.Name, CollectionNameToTag.Type, TmpAssetPathNames);

			for (const FName AssetPathName : TmpAssetPathNames)
			{
				TArray<FName>& CollectionTagsForAsset = AssetPathNamesToCollectionTags.FindOrAdd(AssetPathName);
				CollectionTagsForAsset.AddUnique(CollectionTagName);
			}
		}
	}

	// Apply the collection tags to the asset registry state
	for (const auto& AssetPathNameToCollectionTagsPair : AssetPathNamesToCollectionTags)
	{
		const FName AssetPathName = AssetPathNameToCollectionTagsPair.Key;
		const TArray<FName>& CollectionTagsForAsset = AssetPathNameToCollectionTagsPair.Value;

		const FAssetData* AssetData = State.GetAssetByObjectPath(AssetPathName);
		if (AssetData)
		{
			FAssetDataTagMap TagsAndValues = AssetData->TagsAndValues.GetMap();
			for (const FName CollectionTagName : CollectionTagsForAsset)
			{
				TagsAndValues.Add(CollectionTagName, FString()); // TODO: Does this need a value to avoid being trimmed?
			}
			State.UpdateAssetData(FAssetData(AssetData->PackageName, AssetData->PackagePath, AssetData->AssetName, AssetData->AssetClass, TagsAndValues, AssetData->ChunkIDs, AssetData->PackageFlags));
		}
	}
}

void FAssetRegistryGenerator::Initialize(const TArray<FName> &InStartupPackages)
{
	StartupPackages.Append(InStartupPackages);

	FAssetRegistrySerializationOptions SaveOptions;

	// If the asset registry is still doing it's background scan, we need to wait for it to finish and tick it so that the results are flushed out
	while (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.Tick(-1.0f);
		FThreadHeartBeat::Get().HeartBeat();
		FPlatformProcess::SleepNoStats(0.0001f);
	}

	ensureMsgf(!AssetRegistry.IsLoadingAssets(), TEXT("Cannot initialize asset registry generator while asset registry is still scanning source assets "));

	AssetRegistry.InitializeSerializationOptions(SaveOptions, TargetPlatform->IniPlatformName());

	AssetRegistry.InitializeTemporaryAssetRegistryState(State, SaveOptions);

	FGameDelegates::Get().GetAssignLayerChunkDelegate() = FAssignLayerChunkDelegate::CreateStatic(AssignLayerChunkDelegate);
}

void FAssetRegistryGenerator::ComputePackageDifferences(TSet<FName>& ModifiedPackages, TSet<FName>& NewPackages, TSet<FName>& RemovedPackages, TSet<FName>& IdenticalCookedPackages, TSet<FName>& IdenticalUncookedPackages, bool bRecurseModifications, bool bRecurseScriptModifications)
{
	TArray<FName> ModifiedScriptPackages;

	for (const TPair<FName, const FAssetPackageData*>& PackagePair : State.GetAssetPackageDataMap())
	{
		FName PackageName = PackagePair.Key;
		const FAssetPackageData* CurrentPackageData = PackagePair.Value;

		const FAssetPackageData* PreviousPackageData = PreviousState.GetAssetPackageData(PackageName);

		if (!PreviousPackageData)
		{
			NewPackages.Add(PackageName);
		}
		else if (CurrentPackageData->PackageGuid == PreviousPackageData->PackageGuid)
		{
			if (PreviousPackageData->DiskSize < 0)
			{
				IdenticalUncookedPackages.Add(PackageName);
			}
			else
			{
				IdenticalCookedPackages.Add(PackageName);
			}
		}
		else
		{
			if (FPackageName::IsScriptPackage(PackageName.ToString()))
			{
				ModifiedScriptPackages.Add(PackageName);
			}
			else
			{
			ModifiedPackages.Add(PackageName);
		}
	}
	}

	for (const TPair<FName, const FAssetPackageData*>& PackagePair : PreviousState.GetAssetPackageDataMap())
	{
		FName PackageName = PackagePair.Key;
		const FAssetPackageData* PreviousPackageData = PackagePair.Value;

		const FAssetPackageData* CurrentPackageData = State.GetAssetPackageData(PackageName);

		if (!CurrentPackageData)
		{
			RemovedPackages.Add(PackageName);
		}
	}

	if (bRecurseModifications)
	{
		// Recurse modified packages to their dependencies. This is needed because we only compare package guids
		TArray<FName> ModifiedPackagesToRecurse = ModifiedPackages.Array();

		if (bRecurseScriptModifications)
		{
			ModifiedPackagesToRecurse.Append(ModifiedScriptPackages);
		}

		for (int32 RecurseIndex = 0; RecurseIndex < ModifiedPackagesToRecurse.Num(); RecurseIndex++)
		{
			FName ModifiedPackage = ModifiedPackagesToRecurse[RecurseIndex];
			TArray<FAssetIdentifier> Referencers;
			State.GetReferencers(ModifiedPackage, Referencers, EAssetRegistryDependencyType::Hard);

			for (const FAssetIdentifier& Referencer : Referencers)
			{
				FName ReferencerPackage = Referencer.PackageName;
				if (!ModifiedPackages.Contains(ReferencerPackage) && (IdenticalCookedPackages.Contains(ReferencerPackage) || IdenticalUncookedPackages.Contains(ReferencerPackage)))
				{
					// Remove from identical list
					IdenticalCookedPackages.Remove(ReferencerPackage);
					IdenticalUncookedPackages.Remove(ReferencerPackage);

					ModifiedPackages.Add(ReferencerPackage);
					ModifiedPackagesToRecurse.Add(ReferencerPackage);
				}
			}
		}
	}
}

void FAssetRegistryGenerator::UpdateKeptPackages(const TArray<FName>& InKeptPackages)
{
	KeptPackages.Append(InKeptPackages);
	// Update disk data right away, disk data is only updated when packages are saved, and kept packages are never saved
	UpdateKeptPackagesDiskData(InKeptPackages);
	// Delay update of AssetData with TagsAndValues, this data may be modified up until serialization in SaveAssetRegistry
}

void FAssetRegistryGenerator::BuildChunkManifest(const TSet<FName>& InCookedPackages, const TSet<FName>& InDevelopmentOnlyPackages, FSandboxPlatformFile* InSandboxFile, bool bGenerateStreamingInstallManifest)
{
	// If we were asked to generate a streaming install manifest explicitly and we did not have bGenerateNoChunks set, we will generate chunks.
	// Otherwise, we will defer to the config settings for the platform.
	const UProjectPackagingSettings* PackagingSettings = Cast<UProjectPackagingSettings>(UProjectPackagingSettings::StaticClass()->GetDefaultObject());
	if (PackagingSettings->bGenerateNoChunks)
	{
		bGenerateChunks = false;
	}
	else
	{
		if (bGenerateStreamingInstallManifest)
		{
			bGenerateChunks = true;
		}
		else
		{
			bGenerateChunks = ShouldPlatformGenerateStreamingInstallManifest(TargetPlatform);
		}
	}

	CookedPackages = InCookedPackages;
	DevelopmentOnlyPackages = InDevelopmentOnlyPackages;

	TSet<FName> AllPackages;
	AllPackages.Append(CookedPackages);
	AllPackages.Append(DevelopmentOnlyPackages);

	// Prune our asset registry to cooked + dev only list
	FAssetRegistrySerializationOptions DevelopmentSaveOptions;
	AssetRegistry.InitializeSerializationOptions(DevelopmentSaveOptions, TargetPlatform->IniPlatformName());
	DevelopmentSaveOptions.ModifyForDevelopment();
	State.PruneAssetData(AllPackages, TSet<FName>(), DevelopmentSaveOptions);

	// Mark development only packages as explicitly -1 size to indicate it was not cooked
	for (FName DevelopmentOnlyPackage : DevelopmentOnlyPackages)
	{
		FAssetPackageData* PackageData = State.CreateOrGetAssetPackageData(DevelopmentOnlyPackage);
		PackageData->DiskSize = -1;
	}

	// initialize FoundIDList, PackageChunkIDMap
	const TMap<FName, const FAssetData*>& ObjectToDataMap = State.GetObjectPathToAssetDataMap();

	for (const TPair<FName, const FAssetData*>& Pair : ObjectToDataMap)
	{
		// Chunk Ids are safe to modify in place so do a const cast
		FAssetData& AssetData = *const_cast<FAssetData*>(Pair.Value);
		for (auto ChunkIt = AssetData.ChunkIDs.CreateConstIterator(); ChunkIt; ++ChunkIt)
		{
			int32 ChunkID = *ChunkIt;
			if (ChunkID < 0)
			{
				UE_LOG(LogAssetRegistryGenerator, Warning, TEXT("Out of range ChunkID: %d"), ChunkID);
				ChunkID = 0;
			}
			
			auto* FoundIDList = PackageChunkIDMap.Find(AssetData.PackageName);
			if (!FoundIDList)
			{
				FoundIDList = &PackageChunkIDMap.Add(AssetData.PackageName);
			}
			FoundIDList->AddUnique(ChunkID);
		}

			// Now clear the original chunk id list. We will fill it with real IDs when cooking.
			AssetData.ChunkIDs.Empty();

		// Update whether the owner package contains a map
		if (AssetData.GetClass()->IsChildOf(UWorld::StaticClass()) || AssetData.GetClass()->IsChildOf(ULevel::StaticClass()))
		{
			PackagesContainingMaps.Add(AssetData.PackageName);
		}
	}

	// add all the packages to the unassigned package list
	for (FName CookedPackage : CookedPackages)
	{
		const FString SandboxPath = InSandboxFile->ConvertToAbsolutePathForExternalAppForWrite(*FPackageName::LongPackageNameToFilename(CookedPackage.ToString()));

		AllCookedPackageSet.Add(CookedPackage, SandboxPath);
		UnassignedPackageSet.Add(CookedPackage, SandboxPath);
	}

	TArray<FName> UnassignedPackageList;

	// Old path has map specific code, new code doesn't care about map or load order
	if (!bUseAssetManager)
	{
		// Assign startup packages, these will generally end up in chunk 0
		FString StartupPackageMapName(TEXT("None"));
		for (FName CookedPackage : StartupPackages)
		{
			const FString SandboxPath = InSandboxFile->ConvertToAbsolutePathForExternalAppForWrite(*FPackageName::LongPackageNameToFilename(CookedPackage.ToString()));
			const FString PackagePathName = CookedPackage.ToString();
			AllCookedPackageSet.Add(CookedPackage, SandboxPath);
			GenerateChunkManifestForPackage(CookedPackage, PackagePathName, SandboxPath, StartupPackageMapName, InSandboxFile);
		}

		// Capture list at start as it may change during iteration
		UnassignedPackageSet.GenerateKeyArray(UnassignedPackageList);

		// assign chunks for all the map packages
		for (FName MapFName : UnassignedPackageList)
		{
			if (ContainsMap(MapFName) == false)
			{
				continue;
			}

			// get all the dependencies for this map
			TArray<FName> MapDependencies;
			ensure(GatherAllPackageDependencies(MapFName, MapDependencies));

			for (const auto& RawPackageFName : MapDependencies)
			{
				const FName PackageFName = GetPackageNameFromDependencyPackageName(RawPackageFName);

				if (PackageFName == NAME_None)
				{
					continue;
				}

				const FString PackagePathName = PackageFName.ToString();
				const FString MapName = MapFName.ToString();
				const FString* SandboxFilenamePtr = AllCookedPackageSet.Find(PackageFName);
				if (!SandboxFilenamePtr)
				{
					const FString SandboxPath = InSandboxFile->ConvertToAbsolutePathForExternalAppForWrite(*FPackageName::LongPackageNameToFilename(PackagePathName));

					AllCookedPackageSet.Add(PackageFName, SandboxPath);

					SandboxFilenamePtr = AllCookedPackageSet.Find(PackageFName);
					check(SandboxFilenamePtr);
				}
				const FString& SandboxFilename = *SandboxFilenamePtr;

				GenerateChunkManifestForPackage(PackageFName, PackagePathName, SandboxFilename, MapName, InSandboxFile);
			}
		}
	}

	// Capture list at start as it may change during iteration
	UnassignedPackageSet.GenerateKeyArray(UnassignedPackageList);

	// process the remaining unassigned packages
	for (FName PackageFName : UnassignedPackageList)
	{
		const FString& SandboxFilename = AllCookedPackageSet.FindChecked(PackageFName);
		const FString PackagePathName = PackageFName.ToString();

		GenerateChunkManifestForPackage(PackageFName, PackagePathName, SandboxFilename, FString(), InSandboxFile);
	}

	// anything that remains in the UnAssignedPackageSet will be put in chunk0 when we save the asset registry

}

void FAssetRegistryGenerator::PreSave(const TSet<FName>& InCookedPackages)
{
	if (bUseAssetManager)
	{
		UAssetManager::Get().PreSaveAssetRegistry(TargetPlatform, InCookedPackages);
	}
}

void FAssetRegistryGenerator::PostSave()
{
	if (bUseAssetManager)
	{
		UAssetManager::Get().PostSaveAssetRegistry();
	}
}

void FAssetRegistryGenerator::AddAssetToFileOrderRecursive(const FName& InPackageName, TArray<FName>& OutFileOrder, TSet<FName>& OutEncounteredNames, const TSet<FName>& InPackageNameSet, const TSet<FName>& InTopLevelAssets)
{
	if (!OutEncounteredNames.Contains(InPackageName))
	{
		OutEncounteredNames.Add(InPackageName);

		TArray<FName> Dependencies;
		AssetRegistry.GetDependencies(InPackageName, Dependencies, EAssetRegistryDependencyType::Hard);

		for (FName DependencyName : Dependencies)
		{
			if (InPackageNameSet.Contains(DependencyName))
			{
				if (!InTopLevelAssets.Contains(DependencyName))
				{
					AddAssetToFileOrderRecursive(DependencyName, OutFileOrder, OutEncounteredNames, InPackageNameSet, InTopLevelAssets);
				}
			}
		}

		OutFileOrder.Add(InPackageName);
	}
}

bool FAssetRegistryGenerator::SaveAssetRegistry(const FString& SandboxPath, bool bSerializeDevelopmentAssetRegistry, bool bForceNoFilter)
{
	UE_LOG(LogAssetRegistryGenerator, Display, TEXT("Saving asset registry."));
	const TMap<FName, const FAssetData*>& ObjectToDataMap = State.GetObjectPathToAssetDataMap();
	
	// Write development first, this will always write
	FAssetRegistrySerializationOptions DevelopmentSaveOptions;
	AssetRegistry.InitializeSerializationOptions(DevelopmentSaveOptions, TargetPlatform->IniPlatformName());
	DevelopmentSaveOptions.ModifyForDevelopment();

	// Write runtime registry, this can be excluded per game/platform
	FAssetRegistrySerializationOptions SaveOptions;
	AssetRegistry.InitializeSerializationOptions(SaveOptions, TargetPlatform->IniPlatformName());

	if (bForceNoFilter)
	{
		DevelopmentSaveOptions.DisableFilters();
		SaveOptions.DisableFilters();
	}

	// First flush the asset registry and make sure the asset data is in sync, as it may have been updated during cook
	AssetRegistry.Tick(-1.0f);
	AssetRegistry.InitializeTemporaryAssetRegistryState(State, SaveOptions, true);
	// Then possibly apply AssetData with TagsAndValues from a previous AssetRegistry for packages kept from a previous cook
	UpdateKeptPackagesAssetData();
	UpdateCollectionAssetData();

	if (DevelopmentSaveOptions.bSerializeAssetRegistry && bSerializeDevelopmentAssetRegistry)
	{
		// Create development registry data, used for incremental cook and editor viewing
		FArrayWriter SerializedAssetRegistry;

		State.Serialize(SerializedAssetRegistry, DevelopmentSaveOptions);

		// Save the generated registry
		FString PlatformSandboxPath = SandboxPath.Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());
		PlatformSandboxPath.ReplaceInline(TEXT("AssetRegistry.bin"), TEXT("Metadata/DevelopmentAssetRegistry.bin"));
		FFileHelper::SaveArrayToFile(SerializedAssetRegistry, *PlatformSandboxPath);

		if (bGenerateChunks && bUseAssetManager)
		{
			FString ChunkListsPath = PlatformSandboxPath.Replace(TEXT("/DevelopmentAssetRegistry.bin"), TEXT(""));

			// Write out CSV file with chunking information
			GenerateAssetChunkInformationCSV(ChunkListsPath, false);
		}
	}

	if (SaveOptions.bSerializeAssetRegistry)
	{
		TMap<int32, FString> ChunkBucketNames;
		TMap<int32, TSet<int32>> ChunkBuckets;
		const int32 GenericChunkBucket = -1;
		ChunkBucketNames.Add(GenericChunkBucket, FString());

		// When chunk manifests have been generated (e.g. cook by the book) serialize 
		// an asset registry for each chunk.
		if (FinalChunkManifests.Num() > 0)
		{
			// Pass over all chunks and build a mapping of chunk index to asset registry name. All chunks that don't have a unique registry are assigned to the "generic bucket"
			// which will be written to the master asset registry in chunk 0
			for (int32 PakchunkIndex = 0; PakchunkIndex < FinalChunkManifests.Num(); ++PakchunkIndex)
			{
				FChunkPackageSet* Manifest = FinalChunkManifests[PakchunkIndex];
				if (Manifest == nullptr)
				{
					continue;
				}

				bool bAddToGenericBucket = true;

				if (bUseAssetManager)
				{
					// For chunks with unique asset registry name, pakchunkIndex should equal chunkid
					FName RegistryName = UAssetManager::Get().GetUniqueAssetRegistryName(PakchunkIndex);
					if (RegistryName != NAME_None)
					{
						ChunkBuckets.FindOrAdd(PakchunkIndex).Add(PakchunkIndex);
						ChunkBucketNames.FindOrAdd(PakchunkIndex) = RegistryName.ToString();
						bAddToGenericBucket = false;
					}
				}

				if (bAddToGenericBucket)
				{
					ChunkBuckets.FindOrAdd(GenericChunkBucket).Add(PakchunkIndex);
				}
			}

			FString SandboxPathWithoutExtension = FPaths::ChangeExtension(SandboxPath, TEXT(""));
			FString SandboxPathExtension = FPaths::GetExtension(SandboxPath);

			for (TMap<int32, TSet<int32>>::ElementType& ChunkBucketElement : ChunkBuckets)
			{
				// Prune out the development only packages, and any assets that belong in a different chunk asset registry
				FAssetRegistryState NewState;
				NewState.InitializeFromExistingAndPrune(State, CookedPackages, TSet<FName>(), ChunkBucketElement.Value, SaveOptions);

				InjectEncryptionData(NewState);

				// Create runtime registry data
				FArrayWriter SerializedAssetRegistry;
				SerializedAssetRegistry.SetFilterEditorOnly(true);

				NewState.Serialize(SerializedAssetRegistry, SaveOptions);

				// Save the generated registry
				FString PlatformSandboxPath = SandboxPathWithoutExtension.Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());
				PlatformSandboxPath += ChunkBucketNames[ChunkBucketElement.Key] + TEXT(".") + SandboxPathExtension;

				FFileHelper::SaveArrayToFile(SerializedAssetRegistry, *PlatformSandboxPath);

				FString FilenameForLog;
				if (ChunkBucketElement.Key != GenericChunkBucket)
				{
					check(ChunkBucketElement.Key < FinalChunkManifests.Num());
					FChunkPackageSet* ChunkPackageSet = FinalChunkManifests[ChunkBucketElement.Key];
					check(ChunkPackageSet);
					FilenameForLog = FString::Printf(TEXT("[chunkbucket %i] "), ChunkBucketElement.Key);
				}
				UE_LOG(LogAssetRegistryGenerator, Display, TEXT("Generated asset registry %snum assets %d, size is %5.2fkb"), *FilenameForLog, NewState.GetNumAssets(), (float)SerializedAssetRegistry.Num() / 1024.f);
			}
		}
		// If no chunk manifests have been generated (e.g. cook on the fly)
		else
		{
			// Prune out the development only packages
			State.PruneAssetData(CookedPackages, TSet<FName>(), SaveOptions);

			// Create runtime registry data
			FArrayWriter SerializedAssetRegistry;
			SerializedAssetRegistry.SetFilterEditorOnly(true);

			State.Serialize(SerializedAssetRegistry, SaveOptions);

			// Save the generated registry
			FString PlatformSandboxPath = SandboxPath.Replace(TEXT("[Platform]"), *TargetPlatform->PlatformName());
			FFileHelper::SaveArrayToFile(SerializedAssetRegistry, *PlatformSandboxPath);
			UE_LOG(LogAssetRegistryGenerator, Display, TEXT("Generated asset registry num assets %d, size is %5.2fkb"), ObjectToDataMap.Num(), (float)SerializedAssetRegistry.Num() / 1024.f);
		}
	}

	UE_LOG(LogAssetRegistryGenerator, Display, TEXT("Done saving asset registry."));

	return true;
}

bool FAssetRegistryGenerator::WriteCookerOpenOrder()
{
	TSet<FName> PackageNameSet;
	TSet<FName> MapList;
	const TMap<FName, const FAssetData*>& ObjectToDataMap = State.GetObjectPathToAssetDataMap();
	for (const TPair<FName, const FAssetData*>& Pair : ObjectToDataMap)
	{
		FAssetData* AssetData = const_cast<FAssetData*>(Pair.Value);
		PackageNameSet.Add(AssetData->PackageName);

		// REPLACE WITH PRIORITY

		if (ContainsMap(AssetData->PackageName))
		{
			MapList.Add(AssetData->PackageName);
		}
	}

	FString CookerFileOrderString;
	{
		TArray<FName> TopLevelMapPackageNames;
		TArray<FName> TopLevelPackageNames;

		for (FName PackageName : PackageNameSet)
		{
			TArray<FName> Referencers;
			AssetRegistry.GetReferencers(PackageName, Referencers, EAssetRegistryDependencyType::Hard);

			bool bIsTopLevel = true;
			bool bIsMap = MapList.Contains(PackageName);

			if (!bIsMap && Referencers.Num() > 0)
			{
				for (auto ReferencerName : Referencers)
				{
					if (PackageNameSet.Contains(ReferencerName))
					{
						bIsTopLevel = false;
						break;
					}
				}
			}

			if (bIsTopLevel)
			{
				if (bIsMap)
				{
					TopLevelMapPackageNames.Add(PackageName);
				}
				else
				{
					TopLevelPackageNames.Add(PackageName);
				}
			}
		}

		TArray<FName> FileOrder;
		TSet<FName> EncounteredNames;
		for (FName PackageName : TopLevelPackageNames)
		{
			AddAssetToFileOrderRecursive(PackageName, FileOrder, EncounteredNames, PackageNameSet, MapList);
		}

		for (FName PackageName : TopLevelMapPackageNames)
		{
			AddAssetToFileOrderRecursive(PackageName, FileOrder, EncounteredNames, PackageNameSet, MapList);
		}

		int32 CurrentIndex = 0;
		for (FName PackageName : FileOrder)
		{
			bool bIsMap = MapList.Contains(PackageName);
			FString Filename = FPackageName::LongPackageNameToFilename(PackageName.ToString(), bIsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension());
			FString Line = FString::Printf(TEXT("\"%s\" %i\n"), *Filename, CurrentIndex++);
			CookerFileOrderString.Append(Line);
		}
	}

	if (CookerFileOrderString.Len())
	{
		FString OpenOrderFilename = FString::Printf(TEXT("%sBuild/%s/FileOpenOrder/CookerOpenOrder.log"), *FPaths::ProjectDir(), *TargetPlatform->PlatformName());
		FFileHelper::SaveStringToFile(CookerFileOrderString, *OpenOrderFilename);
	}

	return true;
}

bool FAssetRegistryGenerator::GetPackageDependencyChain(FName SourcePackage, FName TargetPackage, TSet<FName>& VisitedPackages, TArray<FName>& OutDependencyChain)
{	
	//avoid crashing from circular dependencies.
	if (VisitedPackages.Contains(SourcePackage))
	{		
		return false;
	}
	VisitedPackages.Add(SourcePackage);

	if (SourcePackage == TargetPackage)
	{		
		OutDependencyChain.Add(SourcePackage);
		return true;
	}

	TArray<FName> SourceDependencies;
	if (GetPackageDependencies(SourcePackage, SourceDependencies, DependencyType) == false)
	{		
		return false;
	}

	int32 DependencyCounter = 0;
	while (DependencyCounter < SourceDependencies.Num())
	{		
		const FName& ChildPackageName = SourceDependencies[DependencyCounter];
		if (GetPackageDependencyChain(ChildPackageName, TargetPackage, VisitedPackages, OutDependencyChain))
		{
			OutDependencyChain.Add(SourcePackage);
			return true;
		}
		++DependencyCounter;
	}
	
	return false;
}

bool FAssetRegistryGenerator::GetPackageDependencies(FName PackageName, TArray<FName>& DependentPackageNames, EAssetRegistryDependencyType::Type InDependencyType)
{	
	if (FGameDelegates::Get().GetGetPackageDependenciesForManifestGeneratorDelegate().IsBound())
	{
		return FGameDelegates::Get().GetGetPackageDependenciesForManifestGeneratorDelegate().Execute(PackageName, DependentPackageNames, InDependencyType);
	}
	else
	{
		return AssetRegistry.GetDependencies(PackageName, DependentPackageNames, InDependencyType);
	}
}

bool FAssetRegistryGenerator::GatherAllPackageDependencies(FName PackageName, TArray<FName>& DependentPackageNames)
{	
	if (GetPackageDependencies(PackageName, DependentPackageNames, DependencyType) == false)
	{
		return false;
	}

	TSet<FName> VisitedPackages;
	VisitedPackages.Append(DependentPackageNames);

	int32 DependencyCounter = 0;
	while (DependencyCounter < DependentPackageNames.Num())
	{
		const FName& ChildPackageName = DependentPackageNames[DependencyCounter];
		++DependencyCounter;
		TArray<FName> ChildDependentPackageNames;
		if (GetPackageDependencies(ChildPackageName, ChildDependentPackageNames, DependencyType) == false)
		{
			return false;
		}

		for (const auto& ChildDependentPackageName : ChildDependentPackageNames)
		{
			if (!VisitedPackages.Contains(ChildDependentPackageName))
			{
				DependentPackageNames.Add(ChildDependentPackageName);
				VisitedPackages.Add(ChildDependentPackageName);
			}
		}
	}

	return true;
}

bool FAssetRegistryGenerator::GenerateAssetChunkInformationCSV(const FString& OutputPath, bool bWriteIndividualFiles)
{
	FString TmpString, TmpStringChunks;
	ANSICHAR HeaderText[] = "ChunkID, Package Name, Class Type, Hard or Soft Chunk, File Size, Other Chunks\n";

	const TMap<FName, const FAssetData*>& ObjectToDataMap = State.GetObjectPathToAssetDataMap();
	TArray<const FAssetData*> AssetDataList;
	for (const TPair<FName, const FAssetData*> Pair : ObjectToDataMap)
	{
		AssetDataList.Add(Pair.Value);
	}

	// Sort list so it's consistent over time
	AssetDataList.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.ObjectPath.LexicalLess(B.ObjectPath);
	});

	// Create file for all chunks
	TUniquePtr<FArchive> AllChunksFile(IFileManager::Get().CreateFileWriter(*FPaths::Combine(*OutputPath, TEXT("AllChunksInfo.csv"))));
	if (!AllChunksFile.IsValid())
	{
		return false;
	}

	AllChunksFile->Serialize(HeaderText, sizeof(HeaderText)-1);

	// Create file for each chunk if needed
	TArray<TUniquePtr<FArchive>> ChunkFiles;
	if (bWriteIndividualFiles)
	{
		for (int32 PakchunkIndex = 0; PakchunkIndex < FinalChunkManifests.Num(); ++PakchunkIndex)
		{
			FArchive* ChunkFile = IFileManager::Get().CreateFileWriter(*FPaths::Combine(*OutputPath, *FString::Printf(TEXT("Chunks%dInfo.csv"), PakchunkIndex)));
			if (ChunkFile == nullptr)
			{
				return false;
			}
			ChunkFile->Serialize(HeaderText, sizeof(HeaderText)-1);
			ChunkFiles.Add(TUniquePtr<FArchive>(ChunkFile));
		}
	}

	for (const FAssetData* AssetDataPtr : AssetDataList)
	{
		const FAssetData& AssetData = *AssetDataPtr;
		const FAssetPackageData* PackageData = State.GetAssetPackageData(AssetData.PackageName);

		// Add only assets that have actually been cooked and belong to any chunk and that have a file size
		if (AssetData.ChunkIDs.Num() > 0 && PackageData->DiskSize > 0)
		{
			for (int32 PakchunkIndex : AssetData.ChunkIDs)
			{
				const int64 FileSize = PackageData->DiskSize;
				FString SoftChain;
				bool bHardChunk = false;
				if (PakchunkIndex < ChunkManifests.Num())
				{
					bHardChunk = ChunkManifests[PakchunkIndex] && ChunkManifests[PakchunkIndex]->Contains(AssetData.PackageName);

					if (!bHardChunk)
					{
						SoftChain = GetShortestReferenceChain(AssetData.PackageName, PakchunkIndex);
					}
				}
				if (SoftChain.IsEmpty())
				{
					SoftChain = TEXT("Soft: Possibly Unassigned Asset");
				}

				// Build "other chunks" string or None if not part of
				TmpStringChunks.Empty(64);
				for (const auto& OtherChunk : AssetData.ChunkIDs)
				{
					if (OtherChunk != PakchunkIndex)
					{
						TmpString = FString::Printf(TEXT("%d "), OtherChunk);
					}
				}

				// Build csv line
				TmpString = FString::Printf(TEXT("%d,%s,%s,%s,%lld,%s\n"),
					PakchunkIndex,
					*AssetData.PackageName.ToString(),
					*AssetData.AssetClass.ToString(),
					bHardChunk ? TEXT("Hard") : *SoftChain,
					FileSize,
					AssetData.ChunkIDs.Num() == 1 ? TEXT("None") : *TmpStringChunks
				);

				// Write line to all chunks file and individual chunks files if requested
				{
					auto Src = StringCast<ANSICHAR>(*TmpString, TmpString.Len());
					AllChunksFile->Serialize((ANSICHAR*)Src.Get(), Src.Length() * sizeof(ANSICHAR));
					
					if (bWriteIndividualFiles)
					{
						ChunkFiles[PakchunkIndex]->Serialize((ANSICHAR*)Src.Get(), Src.Length() * sizeof(ANSICHAR));
					}
				}
			}
		}
	}

	return true;
}

void FAssetRegistryGenerator::AddPackageToManifest(const FString& PackageSandboxPath, FName PackageName, int32 ChunkId)
{
	HighestChunkId = ChunkId > HighestChunkId ? ChunkId : HighestChunkId;
	int32 PakchunkIndex = GetPakchunkIndex(ChunkId);

	while (PakchunkIndex >= ChunkManifests.Num())
	{
		ChunkManifests.Add(nullptr);
	}
	if (!ChunkManifests[PakchunkIndex])
	{
		ChunkManifests[PakchunkIndex] = new FChunkPackageSet();
	}
	ChunkManifests[PakchunkIndex]->Add(PackageName, PackageSandboxPath);
	//Safety check, it the package happens to exist in the unassigned list remove it now.
	UnassignedPackageSet.Remove(PackageName);
}


void FAssetRegistryGenerator::RemovePackageFromManifest(FName PackageName, int32 ChunkId)
{
	int32 PakchunkIndex = GetPakchunkIndex(ChunkId);

	if (ChunkManifests[PakchunkIndex])
	{
		ChunkManifests[PakchunkIndex]->Remove(PackageName);
	}
}

void FAssetRegistryGenerator::ResolveChunkDependencyGraph(const FChunkDependencyTreeNode& Node, const TSet<FName>& BaseAssetSet, TArray<TArray<FName>>& OutPackagesMovedBetweenChunks)
{
	if (FinalChunkManifests.Num() > Node.ChunkID && FinalChunkManifests[Node.ChunkID])
	{
		for (auto It = BaseAssetSet.CreateConstIterator(); It; ++It)
		{
			// Remove any assets belonging to our parents.			
			if (FinalChunkManifests[Node.ChunkID]->Remove(*It) > 0)
			{
				OutPackagesMovedBetweenChunks[Node.ChunkID].Add(*It);
				UE_LOG(LogAssetRegistryGenerator, Verbose, TEXT("Removed %s from chunk %i because it is duplicated in another chunk."), *It->ToString(), Node.ChunkID);
			}
		}
		
		TSet<FName> ModifiedAssetSet;
		
		// Add the current Chunk's assets
		for (auto It = FinalChunkManifests[Node.ChunkID]->CreateConstIterator(); It; ++It)//for (const auto It : *(FinalChunkManifests[Node.ChunkID]))
		{
			if (!ModifiedAssetSet.Num())
			{
				ModifiedAssetSet = BaseAssetSet;
			}
			
			ModifiedAssetSet.Add(It.Key());
		}
		
		const auto& AssetSet = ModifiedAssetSet.Num() ? ModifiedAssetSet : BaseAssetSet;
		for (const auto It : Node.ChildNodes)
		{
			ResolveChunkDependencyGraph(It, AssetSet, OutPackagesMovedBetweenChunks);
		}
	}
}

bool FAssetRegistryGenerator::CheckChunkAssetsAreNotInChild(const FChunkDependencyTreeNode& Node)
{
	for (const auto It : Node.ChildNodes)
	{
		if (!CheckChunkAssetsAreNotInChild(It))
		{
			return false;
		}
	}

	if (!(FinalChunkManifests.Num() > Node.ChunkID && FinalChunkManifests[Node.ChunkID]))
	{
		return true;
	}

	for (const auto ChildIt : Node.ChildNodes)
	{
		if(FinalChunkManifests.Num() > ChildIt.ChunkID && FinalChunkManifests[ChildIt.ChunkID])
		{
			for (auto It = FinalChunkManifests[Node.ChunkID]->CreateConstIterator(); It; ++It)
			{
				if (FinalChunkManifests[ChildIt.ChunkID]->Find(It.Key()))
				{
					return false;
				}
			}
		}
	}

	return true;
}

void FAssetRegistryGenerator::AddPackageAndDependenciesToChunk(FChunkPackageSet* ThisPackageSet, FName InPkgName, const FString& InSandboxFile, int32 PakchunkIndex, FSandboxPlatformFile* SandboxPlatformFile)
{
	FChunkPackageSet* InitialPackageSetForThisChunk = ChunkManifests.IsValidIndex(PakchunkIndex) ? ChunkManifests[PakchunkIndex] : nullptr;

	//Add this asset
	ThisPackageSet->Add(InPkgName, InSandboxFile);

	// Only gather dependencies the slow way if we're chunking or not using asset manager
	if (!bGenerateChunks || bUseAssetManager)
	{
		return;
	}

	//now add any dependencies
	TArray<FName> DependentPackageNames;
	if (GatherAllPackageDependencies(InPkgName, DependentPackageNames))
	{
		for (const auto& PkgName : DependentPackageNames)
		{
			bool bSkip = false;
			if (PakchunkIndex != 0 && FinalChunkManifests[0])
			{
				// Do not add if this asset was assigned to the 0 chunk. These assets always exist on disk
				bSkip = FinalChunkManifests[0]->Contains(PkgName);
			}
			if (!bSkip)
			{
				const FName FilteredPackageName = GetPackageNameFromDependencyPackageName(PkgName);
				if (FilteredPackageName == NAME_None)
				{
					continue;
				}
				FString DependentSandboxFile = SandboxPlatformFile->ConvertToAbsolutePathForExternalAppForWrite(*FPackageName::LongPackageNameToFilename(*FilteredPackageName.ToString()));
				if (!ThisPackageSet->Contains(FilteredPackageName))
				{
					if ((InitialPackageSetForThisChunk != nullptr) && InitialPackageSetForThisChunk->Contains(PkgName))
					{
						// Don't print anything out; it was pre-assigned to this chunk but we haven't gotten to it yet in the calling loop; we'll go ahead and grab it now
					}
					else
					{
						if (UE_LOG_ACTIVE(LogAssetRegistryGenerator, Verbose))
						{
							// It was not assigned to this chunk and we're forcing it to be dragged in, let the user known
							UE_LOG(LogAssetRegistryGenerator, Verbose, TEXT("Adding %s to chunk %i because %s depends on it."), *FilteredPackageName.ToString(), PakchunkIndex, *InPkgName.ToString());

							TSet<FName> VisitedPackages;
							TArray<FName> DependencyChain;
							GetPackageDependencyChain(InPkgName, PkgName, VisitedPackages, DependencyChain);
							for (const auto& ChainName : DependencyChain)
							{
								UE_LOG(LogAssetRegistryGenerator, Verbose, TEXT("\tchain: %s"), *ChainName.ToString());
							}
						}
					}
				}
				ThisPackageSet->Add(FilteredPackageName, DependentSandboxFile);
				UnassignedPackageSet.Remove(PkgName);
			}
		}
	}
}

void FAssetRegistryGenerator::FixupPackageDependenciesForChunks(FSandboxPlatformFile* InSandboxFile)
{
	UE_LOG(LogAssetRegistryGenerator, Log, TEXT("Starting FixupPackageDependenciesForChunks..."));
	SCOPE_LOG_TIME_IN_SECONDS(TEXT("... FixupPackageDependenciesForChunks complete."), nullptr);

	// Clear any existing manifests from the final array
	for (FChunkPackageSet* Manifest : FinalChunkManifests)
	{
		delete Manifest;
	}
	FinalChunkManifests.Empty();

	for (int32 PakchunkIndex = 0, MaxPakchunk = ChunkManifests.Num(); PakchunkIndex < MaxPakchunk; ++PakchunkIndex)
	{
		FinalChunkManifests.Add(new FChunkPackageSet());
		if (!ChunkManifests[PakchunkIndex])
		{
			continue;
		}
		
		for (auto It = ChunkManifests[PakchunkIndex]->CreateConstIterator(); It; ++It)
		{
			AddPackageAndDependenciesToChunk(FinalChunkManifests[PakchunkIndex], It.Key(), It.Value(), PakchunkIndex, InSandboxFile);
		}
	}

	FConfigFile PlatformIniFile;
	FConfigCacheIni::LoadLocalIniFile(PlatformIniFile, TEXT("Engine"), true, *TargetPlatform->IniPlatformName());
	bool bSkipResolveChunkDependencyGraph = false;
	PlatformIniFile.GetBool(TEXT("Script/UnrealEd.ChunkDependencyInfo"), TEXT("bSkipResolveChunkDependencyGraph"), bSkipResolveChunkDependencyGraph);

	const FChunkDependencyTreeNode* ChunkDepGraph = DependencyInfo->GetOrBuildChunkDependencyGraph(!bSkipResolveChunkDependencyGraph ? HighestChunkId : 0);

	//Once complete, Add any remaining assets (that are not assigned to a chunk) to the first chunk.
	if (FinalChunkManifests.Num() == 0)
	{
		FinalChunkManifests.Add(new FChunkPackageSet());
	}
	check(FinalChunkManifests[0]);
	
	// Copy the remaining assets
	auto RemainingAssets = UnassignedPackageSet;
	for (auto It = RemainingAssets.CreateConstIterator(); It; ++It)
	{
		AddPackageAndDependenciesToChunk(FinalChunkManifests[0], It.Key(), It.Value(), 0, InSandboxFile);
	}

	if (!CheckChunkAssetsAreNotInChild(*ChunkDepGraph))
	{
		UE_LOG(LogAssetRegistryGenerator, Log, TEXT("Initial scan of chunks found duplicate assets in graph children"));
	}
		
	TArray<TArray<FName>> PackagesRemovedFromChunks;
	PackagesRemovedFromChunks.AddDefaulted(ChunkManifests.Num());

	//Finally, if the previous step may added any extra packages to the 0 chunk. Pull them out of other chunks and save space
	ResolveChunkDependencyGraph(*ChunkDepGraph, TSet<FName>(), PackagesRemovedFromChunks);

	for (int32 PakchunkIndex = 0; PakchunkIndex < ChunkManifests.Num(); ++PakchunkIndex)
	{
		if (!bUseAssetManager)
		{
			FName CollectionName(*FString::Printf(TEXT("PackagesRemovedFromChunk%i"), PakchunkIndex));
			if (CreateOrEmptyCollection(CollectionName))
			{
				WriteCollection(CollectionName, PackagesRemovedFromChunks[PakchunkIndex]);
			}
		}
	}

	for (int32 PakchunkIndex = 0, MaxPakchunk = ChunkManifests.Num(); PakchunkIndex < MaxPakchunk; ++PakchunkIndex)
	{
		const int32 ChunkManifestNum = ChunkManifests[PakchunkIndex] ? ChunkManifests[PakchunkIndex]->Num() : 0;
		const int32 FinalChunkManifestNum = FinalChunkManifests[PakchunkIndex]->Num();
		UE_LOG(LogAssetRegistryGenerator, Log, TEXT("Chunk: %i, Started with %i packages, Final after dependency resolve: %i"), PakchunkIndex, ChunkManifestNum, FinalChunkManifestNum);
	}
	
	// Fix up the asset registry to reflect this chunk layout
	for (int32 PakchunkIndex = 0 ; PakchunkIndex < FinalChunkManifests.Num(); ++PakchunkIndex)
	{
		if (PakchunkIndex >= FinalChunkManifests.Num())
		{
			continue;
		}
		check(FinalChunkManifests[PakchunkIndex]);
		for (const TPair<FName, FString>& Asset : *FinalChunkManifests[PakchunkIndex])
		{
			const TArray<const FAssetData*> AssetIndexArray = State.GetAssetsByPackageName(Asset.Key);
			for (const FAssetData* AssetData : AssetIndexArray)
			{
				// Chunk Ids are safe to modify in place
				const_cast<FAssetData*>(AssetData)->ChunkIDs.AddUnique(PakchunkIndex);
			}
		}
	}
}

void FAssetRegistryGenerator::FindShortestReferenceChain(TArray<FReferencePair> PackageNames, int32 PakchunkIndex, uint32& OutParentIndex, FString& OutChainPath)
{
	TArray<FReferencePair> ReferencesToCheck;
	uint32 Index = 0;
	for (const auto& Pkg : PackageNames)
	{
		if (ChunkManifests[PakchunkIndex] && ChunkManifests[PakchunkIndex]->Contains(Pkg.PackageName))
		{
			OutChainPath += TEXT("Soft: ");
			OutChainPath += Pkg.PackageName.ToString();
			OutParentIndex = Pkg.ParentNodeIndex;
			return;
		}
		TArray<FName> AssetReferences;
		AssetRegistry.GetReferencers(Pkg.PackageName, AssetReferences);
		for (const auto& Ref : AssetReferences)
		{
			if (!InspectedNames.Contains(Ref))
			{
				ReferencesToCheck.Add(FReferencePair(Ref, Index));
				InspectedNames.Add(Ref);
			}
		}

		++Index;
	}

	if (ReferencesToCheck.Num() > 0)
	{
		uint32 ParentIndex = INDEX_NONE;
		FindShortestReferenceChain(ReferencesToCheck, PakchunkIndex, ParentIndex, OutChainPath);

		if (ParentIndex < (uint32)PackageNames.Num())
		{
			OutChainPath += TEXT("->");
			OutChainPath += PackageNames[ParentIndex].PackageName.ToString();
			OutParentIndex = PackageNames[ParentIndex].ParentNodeIndex;
		}
	}
	else if (PackageNames.Num() > 0)
	{
		//best guess
		OutChainPath += TEXT("Soft From Unassigned Package? Best Guess: ");
		OutChainPath += PackageNames[0].PackageName.ToString();
		OutParentIndex = PackageNames[0].ParentNodeIndex;
	}
}

FString FAssetRegistryGenerator::GetShortestReferenceChain(FName PackageName, int32 PakchunkIndex)
{
	FString StringChain;
	TArray<FReferencePair> ReferencesToCheck;
	uint32 ParentIndex;
	ReferencesToCheck.Add(FReferencePair(PackageName, 0));
	InspectedNames.Empty();
	InspectedNames.Add(PackageName);
	FindShortestReferenceChain(ReferencesToCheck, PakchunkIndex, ParentIndex, StringChain);

	return StringChain;
}


bool FAssetRegistryGenerator::CreateOrEmptyCollection(FName CollectionName)
{
	ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();

	if (CollectionManager.CollectionExists(CollectionName, ECollectionShareType::CST_Local))
	{
		return CollectionManager.EmptyCollection(CollectionName, ECollectionShareType::CST_Local);
	}
	else if (CollectionManager.CreateCollection(CollectionName, ECollectionShareType::CST_Local, ECollectionStorageMode::Static))
	{
		return true;
	}

	return false;
}

void FAssetRegistryGenerator::WriteCollection(FName CollectionName, const TArray<FName>& PackageNames)
{
	if (CreateOrEmptyCollection(CollectionName))
	{
		TArray<FName> AssetNames = PackageNames;

		// Convert package names to asset names
		for (FName& Name : AssetNames)
		{
			FString PackageName = Name.ToString();
			int32 LastPathDelimiter;
			if (PackageName.FindLastChar(TEXT('/'), /*out*/ LastPathDelimiter))
			{
				const FString AssetName = PackageName.Mid(LastPathDelimiter + 1);
				PackageName = PackageName + FString(TEXT(".")) + AssetName;
				Name = *PackageName;
			}
		}

		ICollectionManager& CollectionManager = FCollectionManagerModule::GetModule().Get();
		CollectionManager.AddToCollection(CollectionName, ECollectionShareType::CST_Local, AssetNames);

		UE_LOG(LogAssetRegistryGenerator, Log, TEXT("Updated collection %s"), *CollectionName.ToString());
	}
	else
	{
		UE_LOG(LogAssetRegistryGenerator, Warning, TEXT("Failed to update collection %s"), *CollectionName.ToString());
	}
}

int32 FAssetRegistryGenerator::GetPakchunkIndex(int32 ChunkId)
{
	if (ChunkIdPakchunkIndexMapping.Contains(ChunkId))
	{
		int32 NewChunkId = ChunkIdPakchunkIndexMapping[ChunkId];
		check(NewChunkId >= 0);
		return NewChunkId;
	}

	return ChunkId;
}

void FAssetRegistryGenerator::InitializeChunkIdPakchunkIndexMapping()
{
	FConfigFile PlatformIniFile;
	FConfigCacheIni::LoadLocalIniFile(PlatformIniFile, TEXT("Game"), true, *TargetPlatform->IniPlatformName());
	TArray<FString> ChunkMapping;
	PlatformIniFile.GetArray(TEXT("/Script/UnrealEd.ProjectPackagingSettings"), TEXT("ChunkIdPakchunkIndexMapping"), ChunkMapping);

	FPlatformMisc::ParseChunkIdPakchunkIndexMapping(ChunkMapping, ChunkIdPakchunkIndexMapping);

	// Validate ChunkIdPakchunkIndexMapping
	TArray<int32> AllChunkIDs;
	ChunkIdPakchunkIndexMapping.GetKeys(AllChunkIDs);
	for (int32 ChunkID : AllChunkIDs)
	{
		if(UAssetManager::Get().GetChunkEncryptionKeyGuid(ChunkID).IsValid()
			|| UAssetManager::Get().GetUniqueAssetRegistryName(ChunkID) != NAME_None)
		{
			UE_LOG(LogAssetRegistryGenerator, Error, TEXT("Chunks with encryption key guid or unique assetregistry name (Chunk %d) can not be mapped with ChunkIdPakchunkIndexMapping.  Mapping is removed."), ChunkID);
			ChunkIdPakchunkIndexMapping.Remove(ChunkID);
		}
	}
}
#undef LOCTEXT_NAMESPACE
