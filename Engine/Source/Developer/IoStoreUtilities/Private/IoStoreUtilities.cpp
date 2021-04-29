// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreUtilities.h"

#if !UE_BUILD_SHIPPING

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Hash/CityHash.h"
#include "Interfaces/IPluginManager.h"
#include "Interfaces/ITargetPlatformManagerModule.h"
#include "Interfaces/ITargetPlatform.h"
#include "IO/IoDispatcher.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/Base64.h"
#include "Misc/AES.h"
#include "Misc/CoreDelegates.h"
#include "Misc/KeyChainUtilities.h"
#include "Modules/ModuleManager.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/BufferWriter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/AsyncLoading2.h"
#include "Serialization/ArrayReader.h"
#include "Serialization/PackageStore.h"
#include "UObject/Class.h"
#include "UObject/NameBatchSerialization.h"
#include "UObject/PackageFileSummary.h"
#include "UObject/ObjectResource.h"
#include "UObject/Package.h"
#include "UObject/UObjectHash.h"
#include "Algo/Find.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeLock.h"
#include "Async/ParallelFor.h"
#include "Async/AsyncFileHandle.h"
#include "Async/Async.h"
#include "RSA.h"
#include "Misc/AssetRegistryInterface.h"
#include "AssetRegistryState.h"
#include "Misc/OutputDeviceFile.h"
#include "Misc/FeedbackContext.h"
#include "Serialization/LargeMemoryReader.h"
#include "Misc/StringBuilder.h"
#include "Async/Future.h"
#include "Algo/MaxElement.h"
#include "PackageStoreOptimizer.h"

//PRAGMA_DISABLE_OPTIMIZATION

IMPLEMENT_MODULE(FDefaultModuleImpl, IoStoreUtilities);

DEFINE_LOG_CATEGORY_STATIC(LogIoStore, Log, All);

#define IOSTORE_CPU_SCOPE(NAME) TRACE_CPUPROFILER_EVENT_SCOPE(IoStore##NAME);
#define IOSTORE_CPU_SCOPE_DATA(NAME, DATA) TRACE_CPUPROFILER_EVENT_SCOPE(IoStore##NAME);

#define OUTPUT_CHUNKID_DIRECTORY 0

static const FName DefaultCompressionMethod = NAME_Zlib;
static const uint64 DefaultCompressionBlockSize = 64 << 10;
static const uint64 DefaultCompressionBlockAlignment = 64 << 10;
static const uint64 DefaultMemoryMappingAlignment = 16 << 10;

struct FReleasedPackages
{
	TSet<FName> PackageNames;
	TMap<FPackageId, FName> PackageIdToName;
};

static void LoadKeyChain(const TCHAR* CmdLine, FKeyChain& OutCryptoSettings)
{
	OutCryptoSettings.SigningKey = InvalidRSAKeyHandle;
	OutCryptoSettings.EncryptionKeys.Empty();

	// First, try and parse the keys from a supplied crypto key cache file
	FString CryptoKeysCacheFilename;
	if (FParse::Value(CmdLine, TEXT("cryptokeys="), CryptoKeysCacheFilename))
	{
		UE_LOG(LogIoStore, Display, TEXT("Parsing crypto keys from a crypto key cache file '%s'"), *CryptoKeysCacheFilename);
		KeyChainUtilities::LoadKeyChainFromFile(CryptoKeysCacheFilename, OutCryptoSettings);
	}
	else if (FParse::Param(CmdLine, TEXT("encryptionini")))
	{
		FString ProjectDir, EngineDir, Platform;

		if (FParse::Value(CmdLine, TEXT("projectdir="), ProjectDir, false)
			&& FParse::Value(CmdLine, TEXT("enginedir="), EngineDir, false)
			&& FParse::Value(CmdLine, TEXT("platform="), Platform, false))
		{
			UE_LOG(LogIoStore, Warning, TEXT("A legacy command line syntax is being used for crypto config. Please update to using the -cryptokey parameter as soon as possible as this mode is deprecated"));

			FConfigFile EngineConfig;

			FConfigCacheIni::LoadExternalIniFile(EngineConfig, TEXT("Engine"), *FPaths::Combine(EngineDir, TEXT("Config\\")), *FPaths::Combine(ProjectDir, TEXT("Config/")), true, *Platform);
			bool bDataCryptoRequired = false;
			EngineConfig.GetBool(TEXT("PlatformCrypto"), TEXT("PlatformRequiresDataCrypto"), bDataCryptoRequired);

			if (!bDataCryptoRequired)
			{
				return;
			}

			FConfigFile ConfigFile;
			FConfigCacheIni::LoadExternalIniFile(ConfigFile, TEXT("Crypto"), *FPaths::Combine(EngineDir, TEXT("Config\\")), *FPaths::Combine(ProjectDir, TEXT("Config/")), true, *Platform);
			bool bSignPak = false;
			bool bEncryptPakIniFiles = false;
			bool bEncryptPakIndex = false;
			bool bEncryptAssets = false;
			bool bEncryptPak = false;

			if (ConfigFile.Num())
			{
				UE_LOG(LogIoStore, Display, TEXT("Using new format crypto.ini files for crypto configuration"));

				static const TCHAR* SectionName = TEXT("/Script/CryptoKeys.CryptoKeysSettings");

				ConfigFile.GetBool(SectionName, TEXT("bEnablePakSigning"), bSignPak);
				ConfigFile.GetBool(SectionName, TEXT("bEncryptPakIniFiles"), bEncryptPakIniFiles);
				ConfigFile.GetBool(SectionName, TEXT("bEncryptPakIndex"), bEncryptPakIndex);
				ConfigFile.GetBool(SectionName, TEXT("bEncryptAssets"), bEncryptAssets);
				bEncryptPak = bEncryptPakIniFiles || bEncryptPakIndex || bEncryptAssets;

				if (bSignPak)
				{
					FString PublicExpBase64, PrivateExpBase64, ModulusBase64;
					ConfigFile.GetString(SectionName, TEXT("SigningPublicExponent"), PublicExpBase64);
					ConfigFile.GetString(SectionName, TEXT("SigningPrivateExponent"), PrivateExpBase64);
					ConfigFile.GetString(SectionName, TEXT("SigningModulus"), ModulusBase64);

					TArray<uint8> PublicExp, PrivateExp, Modulus;
					FBase64::Decode(PublicExpBase64, PublicExp);
					FBase64::Decode(PrivateExpBase64, PrivateExp);
					FBase64::Decode(ModulusBase64, Modulus);

					OutCryptoSettings.SigningKey = FRSA::CreateKey(PublicExp, PrivateExp, Modulus);

					UE_LOG(LogIoStore, Display, TEXT("Parsed signature keys from config files."));
				}

				if (bEncryptPak)
				{
					FString EncryptionKeyString;
					ConfigFile.GetString(SectionName, TEXT("EncryptionKey"), EncryptionKeyString);

					if (EncryptionKeyString.Len() > 0)
					{
						TArray<uint8> Key;
						FBase64::Decode(EncryptionKeyString, Key);
						check(Key.Num() == sizeof(FAES::FAESKey::Key));
						FNamedAESKey NewKey;
						NewKey.Name = TEXT("Default");
						NewKey.Guid = FGuid();
						FMemory::Memcpy(NewKey.Key.Key, &Key[0], sizeof(FAES::FAESKey::Key));
						OutCryptoSettings.EncryptionKeys.Add(NewKey.Guid, NewKey);
						UE_LOG(LogIoStore, Display, TEXT("Parsed AES encryption key from config files."));
					}
				}
			}
			else
			{
				static const TCHAR* SectionName = TEXT("Core.Encryption");

				UE_LOG(LogIoStore, Display, TEXT("Using old format encryption.ini files for crypto configuration"));

				FConfigCacheIni::LoadExternalIniFile(ConfigFile, TEXT("Encryption"), *FPaths::Combine(EngineDir, TEXT("Config\\")), *FPaths::Combine(ProjectDir, TEXT("Config/")), true, *Platform);
				ConfigFile.GetBool(SectionName, TEXT("SignPak"), bSignPak);
				ConfigFile.GetBool(SectionName, TEXT("EncryptPak"), bEncryptPak);

				if (bSignPak)
				{
					FString RSAPublicExp, RSAPrivateExp, RSAModulus;
					ConfigFile.GetString(SectionName, TEXT("rsa.publicexp"), RSAPublicExp);
					ConfigFile.GetString(SectionName, TEXT("rsa.privateexp"), RSAPrivateExp);
					ConfigFile.GetString(SectionName, TEXT("rsa.modulus"), RSAModulus);

					//TODO: Fix me!
					//OutSigningKey.PrivateKey.Exponent.Parse(RSAPrivateExp);
					//OutSigningKey.PrivateKey.Modulus.Parse(RSAModulus);
					//OutSigningKey.PublicKey.Exponent.Parse(RSAPublicExp);
					//OutSigningKey.PublicKey.Modulus = OutSigningKey.PrivateKey.Modulus;

					UE_LOG(LogIoStore, Display, TEXT("Parsed signature keys from config files."));
				}

				if (bEncryptPak)
				{
					FString EncryptionKeyString;
					ConfigFile.GetString(SectionName, TEXT("aes.key"), EncryptionKeyString);
					FNamedAESKey NewKey;
					NewKey.Name = TEXT("Default");
					NewKey.Guid = FGuid();
					if (EncryptionKeyString.Len() == 32 && TCString<TCHAR>::IsPureAnsi(*EncryptionKeyString))
					{
						for (int32 Index = 0; Index < 32; ++Index)
						{
							NewKey.Key.Key[Index] = (uint8)EncryptionKeyString[Index];
						}
						OutCryptoSettings.EncryptionKeys.Add(NewKey.Guid, NewKey);
						UE_LOG(LogIoStore, Display, TEXT("Parsed AES encryption key from config files."));
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(LogIoStore, Display, TEXT("Using command line for crypto configuration"));

		FString EncryptionKeyString;
		FParse::Value(CmdLine, TEXT("aes="), EncryptionKeyString, false);

		if (EncryptionKeyString.Len() > 0)
		{
			UE_LOG(LogIoStore, Warning, TEXT("A legacy command line syntax is being used for crypto config. Please update to using the -cryptokey parameter as soon as possible as this mode is deprecated"));

			FNamedAESKey NewKey;
			NewKey.Name = TEXT("Default");
			NewKey.Guid = FGuid();
			const uint32 RequiredKeyLength = sizeof(NewKey.Key);

			// Error checking
			if (EncryptionKeyString.Len() < RequiredKeyLength)
			{
				UE_LOG(LogIoStore, Fatal, TEXT("AES encryption key must be %d characters long"), RequiredKeyLength);
			}

			if (EncryptionKeyString.Len() > RequiredKeyLength)
			{
				UE_LOG(LogIoStore, Warning, TEXT("AES encryption key is more than %d characters long, so will be truncated!"), RequiredKeyLength);
				EncryptionKeyString.LeftInline(RequiredKeyLength);
			}

			if (!FCString::IsPureAnsi(*EncryptionKeyString))
			{
				UE_LOG(LogIoStore, Fatal, TEXT("AES encryption key must be a pure ANSI string!"));
			}

			ANSICHAR* AsAnsi = TCHAR_TO_ANSI(*EncryptionKeyString);
			check(TCString<ANSICHAR>::Strlen(AsAnsi) == RequiredKeyLength);
			FMemory::Memcpy(NewKey.Key.Key, AsAnsi, RequiredKeyLength);
			OutCryptoSettings.EncryptionKeys.Add(NewKey.Guid, NewKey);
			UE_LOG(LogIoStore, Display, TEXT("Parsed AES encryption key from command line."));
		}
	}

	FString EncryptionKeyOverrideGuidString;
	FGuid EncryptionKeyOverrideGuid;
	if (FParse::Value(CmdLine, TEXT("EncryptionKeyOverrideGuid="), EncryptionKeyOverrideGuidString))
	{
		UE_LOG(LogIoStore, Display, TEXT("Using encryption key override '%s'"), *EncryptionKeyOverrideGuidString);
		FGuid::Parse(EncryptionKeyOverrideGuidString, EncryptionKeyOverrideGuid);
	}
	OutCryptoSettings.MasterEncryptionKey = OutCryptoSettings.EncryptionKeys.Find(EncryptionKeyOverrideGuid);
}

struct FContainerSourceFile 
{
	FString NormalizedPath;
	FString DestinationPath;
	bool bNeedsCompression = false;
	bool bNeedsEncryption = false;
};

struct FContainerSourceSpec
{
	FName Name;
	FString OutputPath;
	TArray<FContainerSourceFile> SourceFiles;
	FString PatchTargetFile;
	TArray<FString> PatchSourceContainerFiles;
	FGuid EncryptionKeyOverrideGuid;
	bool bGenerateDiffPatch = false;
};

struct FCookedFileStatData
{
	enum EFileExt { UMap, UAsset, UExp, UBulk, UPtnl, UMappedBulk };
	enum EFileType { PackageHeader, PackageData, BulkData };

	int64 FileSize = 0;
	EFileType FileType = PackageHeader;
	EFileExt FileExt = UMap;

	TArray<FFileRegion> FileRegions;
};

using FCookedFileStatMap = TMap<FString, FCookedFileStatData>;

struct FContainerTargetSpec;

struct FLegacyCookedPackage
{
	FPackageId GlobalPackageId;
	FName PackageName;
	FName RedirectFromPackageName;
	FString FileName;
	uint64 UAssetSize = 0;
	uint64 UExpSize = 0;
	uint64 DiskLayoutOrder = -1;
	FPackageStorePackage* OptimizedPackage = nullptr;
};

struct FContainerTargetFile
{
	FContainerTargetSpec* ContainerTarget = nullptr;
	FLegacyCookedPackage* Package = nullptr;
	FString NormalizedSourcePath;
	FString TargetPath;
	FString DestinationPath;
	uint64 SourceSize = 0;
	uint64 IdealOrder = 0;
	FIoChunkId ChunkId;
	TArray<uint8> PackageHeaderData;
	bool bIsBulkData = false;
	bool bIsOptionalBulkData = false;
	bool bIsMemoryMappedBulkData = false;
	bool bForceUncompressed = false;

	TArray<FFileRegion> FileRegions;
};

struct FFileOrderMap
{
	TMap<FName, uint64> PackageNameToOrder;
	FString Name;
};

struct FIoStoreArguments
{
	FString GlobalContainerPath;
	FString CookedDir;
	ITargetPlatform* TargetPlatform = nullptr;
	FString MetaInputDir;
	FString MetaOutputDir;
	TArray<FContainerSourceSpec> Containers;
	FCookedFileStatMap CookedFileStatMap;
	TArray<FFileOrderMap> OrderMaps;
	FKeyChain KeyChain;
	FKeyChain PatchKeyChain;
	FString DLCPluginPath;
	FString DLCName;
	FString BasedOnReleaseVersionPath;
	FAssetRegistryState ReleaseAssetRegistry;
	FReleasedPackages ReleasedPackages;
	bool bSign = false;
	bool bRemapPluginContentToGame = false;
	bool bCreateDirectoryIndex = true;

	bool ShouldCreateContainers() const
	{
		return GlobalContainerPath.Len() > 0 || DLCPluginPath.Len() > 0;
	}

	bool IsDLC() const
	{
		return DLCPluginPath.Len() > 0;
	}
};

struct FContainerTargetSpec
{
	FIoContainerId ContainerId;
	FContainerHeader Header;
	FName Name;
	FGuid EncryptionKeyGuid;
	FString OutputPath;
	FIoStoreWriter* IoStoreWriter;
	TArray<FContainerTargetFile> TargetFiles;
	TArray<TUniquePtr<FIoStoreReader>> PatchSourceReaders;
	EIoContainerFlags ContainerFlags = EIoContainerFlags::None;
	uint32 PackageCount = 0;
	bool bGenerateDiffPatch = false;
};

using FPackageNameMap = TMap<FName, FLegacyCookedPackage*>;
using FPackageIdMap = TMap<FPackageId, FLegacyCookedPackage*>;

#if OUTPUT_CHUNKID_DIRECTORY
class FChunkIdCsv
{
public:

	~FChunkIdCsv()
	{
		if (OutputArchive)
		{
			OutputArchive->Flush();
		}
	}

	void CreateOutputFile(const FString& RootPath)
	{
		const FString OutputFilename = RootPath / TEXT("chunkid_directory.csv");
		OutputArchive.Reset(IFileManager::Get().CreateFileWriter(*OutputFilename));
		if (OutputArchive)
		{
			const ANSICHAR* Output = "NameIndex,NameNumber,ChunkIndex,ChunkType,ChunkIdHash,DebugString\n";
			OutputArchive->Serialize((void*)Output, FPlatformString::Strlen(Output));
		}
	}

	void AddChunk(uint32 NameIndex, uint32 NameNumber, uint16 ChunkIndex, uint8 ChunkType, uint32 ChunkIdHash, const TCHAR* DebugString)
	{
		ANSICHAR Buffer[MAX_SPRINTF + 1] = { 0 };
		int32 NumOfCharacters = FCStringAnsi::Sprintf(Buffer, "%u,%u,%u,%u,%u,%s\n", NameIndex, NameNumber, ChunkIndex, ChunkType, ChunkIdHash, TCHAR_TO_ANSI(DebugString));
		OutputArchive->Serialize(Buffer, NumOfCharacters);
	}	

private:
	TUniquePtr<FArchive> OutputArchive;
};
FChunkIdCsv ChunkIdCsv;

#endif

static FIoChunkId CreateChunkId(FPackageId GlobalPackageId, uint16 ChunkIndex, EIoChunkType ChunkType, const TCHAR* DebugString)
{
	FIoChunkId ChunkId = CreateIoChunkId(GlobalPackageId.Value(), ChunkIndex, ChunkType);
#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.AddChunk(GlobalPackageId, 0, ChunkIndex, (uint8)ChunkType, GetTypeHash(ChunkId), DebugString);
#endif
	return ChunkId;
}

static void AssignPackagesDiskOrder(
	const TArray<FLegacyCookedPackage*>& Packages,
	const TArray<FFileOrderMap>& OrderMaps,
		const FPackageIdMap& PackageIdMap)
{
	struct FCluster
	{
		TArray<FLegacyCookedPackage*> Packages;
	};

	TArray<FCluster*> Clusters;
	TSet<FLegacyCookedPackage*> AssignedPackages;
	TArray<FLegacyCookedPackage*> ProcessStack;

	struct FPackageAndOrder
	{
		FLegacyCookedPackage* Package = nullptr;
		uint64 Order = MAX_uint64;
		const FFileOrderMap* BlameOrderMap = nullptr;

		bool operator<(const FPackageAndOrder& Other) const
		{
			if (Order != Other.Order)
			{
				return Order < Other.Order;
			}
			// Fallback to reverse bundle order (so that packages are considered before their imports)
			return Package->OptimizedPackage->GetLoadOrder() > Other.Package->OptimizedPackage->GetLoadOrder();
		}
	};

	TArray<FPackageAndOrder> SortedPackages;
	SortedPackages.Reserve(Packages.Num());
	for (FLegacyCookedPackage* Package : Packages)
	{
		if (!Package->OptimizedPackage->GetExportBundleCount())
		{
			continue;
		}
		FPackageAndOrder& Entry = SortedPackages.AddDefaulted_GetRef();
		Entry.Package = Package;
		Entry.Order = MAX_uint64;

		for (const FFileOrderMap& OrderMap : OrderMaps)
		{
			if (const uint64* Order = OrderMap.PackageNameToOrder.Find(Package->PackageName))
			{
				Entry.Order = *Order;
				Entry.BlameOrderMap = &OrderMap;
				break;
			}
		}
	}
	const FFileOrderMap* LastBlameOrderMap = OrderMaps.Num() ? &OrderMaps[0] : nullptr;
	int32 LastAssignedCount = 0;
	Algo::Sort(SortedPackages);
	for (FPackageAndOrder& Entry : SortedPackages)
	{
		if (Entry.BlameOrderMap != LastBlameOrderMap)
		{
			UE_LOG(LogIoStore, Display, TEXT("Ordered %d/%d packages using order file %s"), AssignedPackages.Num() - LastAssignedCount, Packages.Num(), *LastBlameOrderMap->Name);
			LastAssignedCount = AssignedPackages.Num();
			LastBlameOrderMap = Entry.BlameOrderMap;
		}
		if (!AssignedPackages.Contains(Entry.Package))
		{
			FCluster* Cluster = new FCluster();
			Clusters.Add(Cluster);
			ProcessStack.Push(Entry.Package);

			while (ProcessStack.Num())
			{
				FLegacyCookedPackage* PackageToProcess = ProcessStack.Pop(false);
				if (!AssignedPackages.Contains(PackageToProcess))
				{
					AssignedPackages.Add(PackageToProcess);
					if (PackageToProcess->OptimizedPackage->GetExportBundleCount())
					{
						Cluster->Packages.Add(PackageToProcess);
					}
					TArray<FPackageId> AllReferencedPackageIds;
					AllReferencedPackageIds.Append(PackageToProcess->OptimizedPackage->GetImportedPackageIds());
					AllReferencedPackageIds.Append(PackageToProcess->OptimizedPackage->GetImportedRedirectedPackageIds().Array());
					for (const FPackageId& ReferencedPackageId : AllReferencedPackageIds)
					{
						FLegacyCookedPackage* FindReferencedPackage = PackageIdMap.FindRef(ReferencedPackageId);
						if (FindReferencedPackage)
						{
							ProcessStack.Push(FindReferencedPackage);
						}
					}
				}
			}
		}
	}
	UE_LOG(LogIoStore, Display, TEXT("Ordered %d packages using fallback bundle order"), AssignedPackages.Num() - LastAssignedCount);

	check(AssignedPackages.Num() == Packages.Num());
	
	for (FCluster* Cluster : Clusters)
	{
		Algo::Sort(Cluster->Packages, [](const FLegacyCookedPackage* A, const FLegacyCookedPackage* B)
		{
			return A->OptimizedPackage->GetLoadOrder() < B->OptimizedPackage->GetLoadOrder();
		});
	}

	uint64 LayoutIndex = 0;
	for (FCluster* Cluster : Clusters)
	{
		for (FLegacyCookedPackage* Package : Cluster->Packages)
		{
			Package->DiskLayoutOrder = LayoutIndex++;
		}
		delete Cluster;
	}
}

static void CreateDiskLayout(
	const TArray<FContainerTargetSpec*>& ContainerTargets,
	const TArray<FLegacyCookedPackage*>& Packages,
	const TArray<FFileOrderMap>& OrderMaps,
	const FPackageIdMap& PackageIdMap)
{
	IOSTORE_CPU_SCOPE(CreateDiskLayout);

	AssignPackagesDiskOrder(Packages, OrderMaps, PackageIdMap);

	for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
	{
		TArray<FContainerTargetFile*> SortedTargetFiles;
		SortedTargetFiles.Reserve(ContainerTarget->TargetFiles.Num());
		for (FContainerTargetFile& TargetFile : ContainerTarget->TargetFiles)
		{
			SortedTargetFiles.Add(&TargetFile);
		}
		Algo::Sort(SortedTargetFiles, [](const FContainerTargetFile* A, const FContainerTargetFile* B)
		{
			if (A->bIsMemoryMappedBulkData != B->bIsMemoryMappedBulkData)
			{
				// Put all memory mapped bulkdata last
				return B->bIsMemoryMappedBulkData;
			}
			else if (A->bIsBulkData != B->bIsBulkData)
			{
				// Put packages before bulkdata
				return B->bIsBulkData;
			}
			else if (A->Package != B->Package)
			{
				return A->Package->DiskLayoutOrder < B->Package->DiskLayoutOrder;
			}
			else if (A->bIsOptionalBulkData != B->bIsOptionalBulkData)
			{
				// Put each ubulk right before it's corresponding uptnl
				return B->bIsOptionalBulkData;
			}
			check(A == B)
			return false;
		});
		uint64 IdealOrder = 0;
		for (FContainerTargetFile* TargetFile : SortedTargetFiles)
		{
			TargetFile->IdealOrder = IdealOrder++;
		}
	}
}

FContainerTargetSpec* AddContainer(
	FName Name,
	TArray<FContainerTargetSpec*>& Containers)
{
	FIoContainerId ContainerId = FIoContainerId::FromName(Name);
	for (FContainerTargetSpec* ExistingContainer : Containers)
	{
		if (ExistingContainer->Name == Name)
		{
			UE_LOG(LogIoStore, Fatal, TEXT("Duplicate container name: '%s'"), *Name.ToString());
			return nullptr;
		}
		if (ExistingContainer->ContainerId == ContainerId)
		{
			UE_LOG(LogIoStore, Fatal, TEXT("Hash collision for container names: '%s' and '%s'"), *Name.ToString(), *ExistingContainer->Name.ToString());
			return nullptr;
		}
	}
	
	FContainerTargetSpec* ContainerTargetSpec = new FContainerTargetSpec();
	ContainerTargetSpec->Name = Name;
	ContainerTargetSpec->ContainerId = ContainerId;
	Containers.Add(ContainerTargetSpec);
	return ContainerTargetSpec;
}

FLegacyCookedPackage* FindOrAddPackage(
	const FIoStoreArguments& Arguments,
	const TCHAR* RelativeFileName,
	TArray<FLegacyCookedPackage*>& Packages,
	FPackageNameMap& PackageNameMap,
	FPackageIdMap& PackageIdMap,
	FPackageStoreOptimizer& PackageStoreOptimizer)
{
	FString PackageName;
	FString ErrorMessage;
	if (!FPackageName::TryConvertFilenameToLongPackageName(RelativeFileName, PackageName, &ErrorMessage))
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed to obtain package name from file name '%s'"), *ErrorMessage);
		return nullptr;
	}

	FName PackageFName = *PackageName;

	FLegacyCookedPackage* Package = PackageNameMap.FindRef(PackageFName);
	if (!Package)
	{
		FPackageId PackageId = FPackageId::FromName(PackageFName);
		if (FLegacyCookedPackage* FindById = PackageIdMap.FindRef(PackageId))
		{
			UE_LOG(LogIoStore, Fatal, TEXT("Package name hash collision \"%s\" and \"%s"), *FindById->PackageName.ToString(), *PackageFName.ToString());
		}

		if (const FName* ReleasedPackageName = Arguments.ReleasedPackages.PackageIdToName.Find(PackageId))
		{
			UE_LOG(LogIoStore, Fatal, TEXT("Package name hash collision \"%s\" and \"%s"), *ReleasedPackageName->ToString(), *PackageFName.ToString());
		}

		Package = new FLegacyCookedPackage();
		Package->PackageName = PackageFName;
		Package->GlobalPackageId = PackageId;
		Packages.Add(Package);
		PackageNameMap.Add(PackageFName, Package);
		PackageIdMap.Add(PackageId, Package);
	}

	return Package;
}

static void ParsePackageAssets(TArray<FLegacyCookedPackage*>& Packages, const FPackageStoreOptimizer& PackageStoreOptimizer)
{
	IOSTORE_CPU_SCOPE(ParsePackageAssets);
	UE_LOG(LogIoStore, Display, TEXT("Parsing packages..."));

	TAtomic<int32> ReadCount {0};
	TAtomic<int32> ParseCount {0};
	const int32 TotalPackageCount = Packages.Num();

	TArray<FPackageFileSummary> PackageFileSummaries;
	PackageFileSummaries.SetNum(TotalPackageCount);

	uint8* UAssetMemory = nullptr;
	TArray<uint8*> PackageAssetBuffers;
	PackageAssetBuffers.SetNum(TotalPackageCount);

	UE_LOG(LogIoStore, Display, TEXT("Reading package assets..."));
	{
		IOSTORE_CPU_SCOPE(ReadUAssetFiles);

		uint64 TotalUAssetSize = 0;
		for (const FLegacyCookedPackage* Package : Packages)
		{
			TotalUAssetSize += Package->UAssetSize;
		}
		UAssetMemory = reinterpret_cast<uint8*>(FMemory::Malloc(TotalUAssetSize));
		uint8* UAssetMemoryPtr = UAssetMemory;
		for (int32 Index = 0; Index < TotalPackageCount; ++Index)
		{
			PackageAssetBuffers[Index] = UAssetMemoryPtr;
			UAssetMemoryPtr += Packages[Index]->UAssetSize;
		}

		TAtomic<uint64> CurrentFileIndex{ 0 };
		ParallelFor(TotalPackageCount, [&ReadCount, &PackageAssetBuffers, &Packages, &CurrentFileIndex](int32 Index)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(ReadUAssetFile);
			FLegacyCookedPackage* Package = Packages[Index];
			uint8* Buffer = PackageAssetBuffers[Index];
			IFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenRead(*Package->FileName);
			if (FileHandle)
			{
				bool bSuccess = FileHandle->Read(Buffer, Package->UAssetSize);
				UE_CLOG(!bSuccess, LogIoStore, Warning, TEXT("Failed reading file '%s'"), *Package->FileName);
				delete FileHandle;
			}
			else
			{
				UE_LOG(LogIoStore, Warning, TEXT("Couldn't open file '%s'"), *Package->FileName);
			}
			uint64 LocalFileIndex = CurrentFileIndex.IncrementExchange() + 1;
			UE_CLOG(LocalFileIndex % 1000 == 0, LogIoStore, Display, TEXT("Reading %d/%d: '%s'"), LocalFileIndex, Packages.Num(), *Package->FileName);
		}, EParallelForFlags::Unbalanced);
	}

	{
		IOSTORE_CPU_SCOPE(SerializeSummaries);

		ParallelFor(TotalPackageCount, [
				&ReadCount,
				&PackageAssetBuffers,
				&PackageFileSummaries,
				&Packages,
				&PackageStoreOptimizer](int32 Index)
		{
			uint8* PackageBuffer = PackageAssetBuffers[Index];
			FLegacyCookedPackage* Package = Packages[Index];

			FIoBuffer CookedHeaderBuffer = FIoBuffer(FIoBuffer::Wrap, PackageBuffer, Package->UAssetSize);
			Package->OptimizedPackage = PackageStoreOptimizer.CreatePackageFromCookedHeader(Package->PackageName, CookedHeaderBuffer);
			check(Package->OptimizedPackage->GetId() == Package->GlobalPackageId);
		}, EParallelForFlags::Unbalanced);
	}

	FMemory::Free(UAssetMemory);
}

TUniquePtr<FIoStoreReader> CreateIoStoreReader(const TCHAR* Path, const FKeyChain& KeyChain)
{
	TUniquePtr<FIoStoreReader> IoStoreReader(new FIoStoreReader());

	TMap<FGuid, FAES::FAESKey> DecryptionKeys;
	for (const auto& KV : KeyChain.EncryptionKeys)
	{
		DecryptionKeys.Add(KV.Key, KV.Value.Key);
	}
	FIoStatus Status = IoStoreReader->Initialize(*FPaths::ChangeExtension(Path, TEXT("")), DecryptionKeys);
	if (Status.IsOk())
	{
		return IoStoreReader;
	}
	else
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed creating IoStore reader '%s' [%s]"), Path, *Status.ToString())
		return nullptr;
	}
}

TArray<TUniquePtr<FIoStoreReader>> CreatePatchSourceReaders(const TArray<FString>& Files, const FIoStoreArguments& Arguments)
{
	TArray<TUniquePtr<FIoStoreReader>> Readers;
	for (const FString& PatchSourceContainerFile : Files)
	{
		TUniquePtr<FIoStoreReader> Reader = CreateIoStoreReader(*PatchSourceContainerFile, Arguments.PatchKeyChain);
		if (Reader.IsValid())
		{
			UE_LOG(LogIoStore, Display, TEXT("Loaded patch source container '%s'"), *PatchSourceContainerFile);
			Readers.Add(MoveTemp(Reader));
		}
	}
	return Readers;
}

void InitializeContainerTargetsAndPackages(
	const FIoStoreArguments& Arguments,
	TArray<FLegacyCookedPackage*>& Packages,
	FPackageNameMap& PackageNameMap,
	FPackageIdMap& PackageIdMap,
	TArray<FContainerTargetSpec*>& ContainerTargets,
	FPackageStoreOptimizer& PackageStoreOptimizer)
{
	FString ProjectName = FApp::GetProjectName();
	FString RelativeEnginePath = FPaths::GetRelativePathToRoot();
	FString RelativeProjectPath = FPaths::ProjectDir();
	int32 CookedEngineDirLen = Arguments.CookedDir.Len() + 1;;
	int32 CookedProjectDirLen = CookedEngineDirLen + ProjectName.Len() + 1;

	auto ConvertCookedPathToRelativePath = [
		&ProjectName,
		&CookedEngineDirLen,
		&CookedProjectDirLen,
		&RelativeEnginePath,
		&RelativeProjectPath]
		(const FString& CookedFile) -> FString
	{
		FString RelativeFileName;
		const TCHAR* FileName = *CookedFile + CookedEngineDirLen;
		if (FCString::Strncmp(FileName, *ProjectName, ProjectName.Len()))
		{
			int32 FileNameLen = CookedFile.Len() - CookedEngineDirLen;
			RelativeFileName.Reserve(RelativeEnginePath.Len() + FileNameLen);
			RelativeFileName = RelativeEnginePath;
			RelativeFileName.AppendChars(*CookedFile + CookedEngineDirLen, FileNameLen);
		}
		else
		{
			FileName = *CookedFile + CookedProjectDirLen;
			int32 FileNameLen = CookedFile.Len() - CookedProjectDirLen;
			RelativeFileName.Reserve(RelativeProjectPath.Len() + FileNameLen);
			RelativeFileName = RelativeProjectPath;
			RelativeFileName.AppendChars(*CookedFile + CookedProjectDirLen, FileNameLen);
		}
		return RelativeFileName;
	};

	for (const FContainerSourceSpec& ContainerSource : Arguments.Containers)
	{
		FContainerTargetSpec* ContainerTarget = AddContainer(ContainerSource.Name, ContainerTargets);
		ContainerTarget->OutputPath = ContainerSource.OutputPath;
		ContainerTarget->bGenerateDiffPatch = ContainerSource.bGenerateDiffPatch;
		if (Arguments.bSign)
		{
			ContainerTarget->ContainerFlags |= EIoContainerFlags::Signed;
		}

		if (!ContainerTarget->EncryptionKeyGuid.IsValid())
		{
			ContainerTarget->EncryptionKeyGuid = ContainerSource.EncryptionKeyOverrideGuid;
		}

		ContainerTarget->PatchSourceReaders = CreatePatchSourceReaders(ContainerSource.PatchSourceContainerFiles, Arguments);

		{
			IOSTORE_CPU_SCOPE(ProcessSourceFiles);
			for (const FContainerSourceFile& SourceFile : ContainerSource.SourceFiles)
			{
				const FCookedFileStatData* OriginalCookedFileStatData = Arguments.CookedFileStatMap.Find(SourceFile.NormalizedPath);
				if (!OriginalCookedFileStatData)
				{
					UE_LOG(LogIoStore, Warning, TEXT("File not found: '%s'"), *SourceFile.NormalizedPath);
					continue;
				}
				const FCookedFileStatData* CookedFileStatData = OriginalCookedFileStatData;
				FString NormalizedSourcePath = SourceFile.NormalizedPath;
				if (CookedFileStatData->FileType == FCookedFileStatData::PackageHeader)
				{
					NormalizedSourcePath = FPaths::ChangeExtension(SourceFile.NormalizedPath, TEXT(".uexp"));
					CookedFileStatData = Arguments.CookedFileStatMap.Find(NormalizedSourcePath);
					if (!CookedFileStatData)
					{
						UE_LOG(LogIoStore, Warning, TEXT("File not found: '%s'"), *NormalizedSourcePath);
						continue;
					}
				}

				FString RelativeFileName = ConvertCookedPathToRelativePath(SourceFile.NormalizedPath);
				FLegacyCookedPackage* Package = nullptr;
				const bool bIsMemoryMappedBulkData = CookedFileStatData->FileExt == FCookedFileStatData::EFileExt::UMappedBulk;

				if (bIsMemoryMappedBulkData)
				{
					FString TmpFileName = FString(RelativeFileName.Len() - 8, GetData(RelativeFileName)) + TEXT(".ubulk");
					Package = FindOrAddPackage(Arguments, *TmpFileName, Packages, PackageNameMap, PackageIdMap, PackageStoreOptimizer);
				}
				else
				{
					Package = FindOrAddPackage(Arguments, *RelativeFileName, Packages, PackageNameMap, PackageIdMap, PackageStoreOptimizer);
				}

				if (Package)
				{
					FContainerTargetFile& TargetFile = ContainerTarget->TargetFiles.AddDefaulted_GetRef();
					TargetFile.ContainerTarget = ContainerTarget;
					TargetFile.SourceSize = uint64(CookedFileStatData->FileSize);
					TargetFile.NormalizedSourcePath = NormalizedSourcePath;
					TargetFile.TargetPath = MoveTemp(RelativeFileName);
					TargetFile.DestinationPath = SourceFile.DestinationPath;
					TargetFile.Package = Package;
					if (SourceFile.bNeedsCompression)
					{
						ContainerTarget->ContainerFlags |= EIoContainerFlags::Compressed;
					}
					else
					{
						TargetFile.bForceUncompressed = true;
					}
					if (SourceFile.bNeedsEncryption)
					{
						ContainerTarget->ContainerFlags |= EIoContainerFlags::Encrypted;
					}

					if (CookedFileStatData->FileType == FCookedFileStatData::BulkData)
					{
						TargetFile.bIsBulkData = true;
						if (CookedFileStatData->FileExt == FCookedFileStatData::UPtnl)
						{
							TargetFile.bIsOptionalBulkData = true;
							TargetFile.ChunkId = CreateChunkId(Package->GlobalPackageId, 0, EIoChunkType::OptionalBulkData, *TargetFile.TargetPath);
						}
						else if (CookedFileStatData->FileExt == FCookedFileStatData::UMappedBulk)
						{
							TargetFile.bIsMemoryMappedBulkData = true;
							TargetFile.bForceUncompressed = true;
							TargetFile.ChunkId = CreateChunkId(Package->GlobalPackageId, 0, EIoChunkType::MemoryMappedBulkData, *TargetFile.TargetPath);
						}
						else
						{
							TargetFile.ChunkId = CreateChunkId(Package->GlobalPackageId, 0, EIoChunkType::BulkData, *TargetFile.TargetPath);
						}
						if (Package->FileName.IsEmpty())
						{
							Package->FileName = FPaths::ChangeExtension(SourceFile.NormalizedPath, TEXT(".uasset"));
						}
					}
					else
					{
						check(CookedFileStatData->FileType == FCookedFileStatData::PackageData);

						++ContainerTarget->PackageCount;

						Package->FileName = SourceFile.NormalizedPath; // .uasset path
						Package->UAssetSize = OriginalCookedFileStatData->FileSize;
						Package->UExpSize = CookedFileStatData->FileSize;
						TargetFile.ChunkId = CreateChunkId(Package->GlobalPackageId, 0, EIoChunkType::ExportBundleData, *TargetFile.TargetPath);
					}

					if (TargetFile.bForceUncompressed && !SourceFile.bNeedsEncryption)
					{
						// Only keep the regions for the file if neither compression nor encryption are enabled, otherwise the regions will be meaningless.
						TargetFile.FileRegions = CookedFileStatData->FileRegions;
					}
				}
			}
		}
	}

	Algo::Sort(Packages, [](const FLegacyCookedPackage* A, const FLegacyCookedPackage* B)
	{
		return A->GlobalPackageId < B->GlobalPackageId;
	});
};

void LogWriterResults(const TArray<FIoStoreWriterResult>& Results)
{
	UE_LOG(LogIoStore, Display, TEXT("--------------------------------------------------- IoDispatcher --------------------------------------------------------"));
	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("%-30s %10s %15s %15s %15s %25s"),
		TEXT("Container"), TEXT("Flags"), TEXT("TOC Size (KB)"), TEXT("TOC Entries"), TEXT("Size (MB)"), TEXT("Compressed (MB)"));
	UE_LOG(LogIoStore, Display, TEXT("-------------------------------------------------------------------------------------------------------------------------"));
	uint64 TotalTocSize = 0;
	uint64 TotalTocEntryCount = 0;
	uint64 TotalUncompressedContainerSize = 0;
	uint64 TotalPaddingSize = 0;
	for (const FIoStoreWriterResult& Result : Results)
	{
		FString CompressionInfo = TEXT("-");

		if (Result.CompressionMethod != NAME_None)
		{
			double Procentage = (double(Result.UncompressedContainerSize - Result.CompressedContainerSize) / double(Result.UncompressedContainerSize)) * 100.0;
			CompressionInfo = FString::Printf(TEXT("%.2lf (%.2lf%% %s)"),
				(double)Result.CompressedContainerSize / 1024.0 / 1024.0,
				Procentage,
				*Result.CompressionMethod.ToString());
		}

		FString ContainerSettings = FString::Printf(TEXT("%s/%s/%s/%s"),
			EnumHasAnyFlags(Result.ContainerFlags, EIoContainerFlags::Compressed) ? TEXT("C") : TEXT("-"),
			EnumHasAnyFlags(Result.ContainerFlags, EIoContainerFlags::Encrypted) ? TEXT("E") : TEXT("-"),
			EnumHasAnyFlags(Result.ContainerFlags, EIoContainerFlags::Signed) ? TEXT("S") : TEXT("-"),
			EnumHasAnyFlags(Result.ContainerFlags, EIoContainerFlags::Indexed) ? TEXT("I") : TEXT("-"));

		UE_LOG(LogIoStore, Display, TEXT("%-30s %10s %15.2lf %15llu %15.2lf %25s"),
			*Result.ContainerName,
			*ContainerSettings,
			(double)Result.TocSize / 1024.0,
			Result.TocEntryCount,
			(double)Result.UncompressedContainerSize / 1024.0 / 1024.0,
			*CompressionInfo);


		TotalTocSize += Result.TocSize;
		TotalTocEntryCount += Result.TocEntryCount;
		TotalUncompressedContainerSize += Result.UncompressedContainerSize;
		TotalPaddingSize += Result.PaddingSize;
	}

	UE_LOG(LogIoStore, Display, TEXT("%-30s %10s %15.2lf %15llu %15.2lf %25s"),
		TEXT("TOTAL"),
		TEXT(""),
		(double)TotalTocSize / 1024.0,
		TotalTocEntryCount,
		(double)TotalUncompressedContainerSize / 1024.0 / 1024.0,
		TEXT("-"));

	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("** Flags: (C)ompressed / (E)ncrypted / (S)igned) / (I)ndexed) **"));
	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("Compression block padding: %8.2lf MB"), (double)TotalPaddingSize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT(""));

	UE_LOG(LogIoStore, Display, TEXT("-------------------------------------------- Container Directory Index --------------------------------------------------"));
	UE_LOG(LogIoStore, Display, TEXT("%-30s %15s"), TEXT("Container"), TEXT("Size (KB)"));
	for (const FIoStoreWriterResult& Result : Results)
	{
		UE_LOG(LogIoStore, Display, TEXT("%-30s %15.2lf"), *Result.ContainerName, double(Result.DirectoryIndexSize) / 1024.0);
	}

	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("---------------------------------------------- Container Patch Report ---------------------------------------------------"));
	UE_LOG(LogIoStore, Display, TEXT("%-30s %16s %16s %16s %16s %16s"), TEXT("Container"), TEXT("Total (count)"), TEXT("Modified (count)"), TEXT("Added (count)"), TEXT("Modified (MB)"), TEXT("Added (MB)"));
	for (const FIoStoreWriterResult& Result : Results)
	{
		UE_LOG(LogIoStore, Display, TEXT("%-30s %16d %16d %16d %16.2lf %16.2lf"), *Result.ContainerName, Result.TocEntryCount, Result.ModifiedChunksCount, Result.AddedChunksCount, Result.ModifiedChunksSize / 1024.0 / 1024.0, Result.AddedChunksSize / 1024.0 / 1024.0);
	}
}

void LogContainerPackageInfo(const TArray<FContainerTargetSpec*>& ContainerTargets)
{
	uint64 TotalStoreSize = 0;
	uint64 TotalPackageCount = 0;
	uint64 TotalLocalizedPackageCount = 0;

	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("--------------------------------------------------- PackageStore (KB) ---------------------------------------------------"));
	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("%-30s %20s %20s %20s"),
		TEXT("Container"),
		TEXT("Store Size"),
		TEXT("Packages"),
		TEXT("Localized"));
	UE_LOG(LogIoStore, Display, TEXT("-------------------------------------------------------------------------------------------------------------------------"));

	for (const FContainerTargetSpec* ContainerTarget : ContainerTargets)
	{
		uint64 StoreSize = ContainerTarget->Header.StoreEntries.Num();
		uint64 PackageCount = ContainerTarget->PackageCount;
		uint64 LocalizedPackageCount = 0;

		for (const auto& KV : ContainerTarget->Header.CulturePackageMap)
		{
			LocalizedPackageCount += KV.Value.Num();
		}

		UE_LOG(LogIoStore, Display, TEXT("%-30s %20.0lf %20llu %20llu"),
			*ContainerTarget->Name.ToString(),
			(double)StoreSize / 1024.0,
			PackageCount,
			LocalizedPackageCount);

		TotalStoreSize += StoreSize;
		TotalPackageCount += PackageCount;
		TotalLocalizedPackageCount += LocalizedPackageCount;
	}
	UE_LOG(LogIoStore, Display, TEXT("%-30s %20.0lf %20llu %20llu"),
		TEXT("TOTAL"),
		(double)TotalStoreSize / 1024.0,
		TotalPackageCount,
		TotalLocalizedPackageCount);


	uint64 TotalHeaderSize = 0;
	uint64 TotalSummarySize = 0;
	uint64 TotalUGraphSize = 0;
	uint64 TotalImportMapSize = 0;
	uint64 TotalExportMapSize = 0;
	uint64 TotalNameMapSize = 0;

	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("--------------------------------------------------- PackageHeader (KB) --------------------------------------------------"));
	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("%-30s %13s %13s %13s %13s %13s %13s"),
		TEXT("Container"),
		TEXT("Header"),
		TEXT("Summary"),
		TEXT("Graph"),
		TEXT("ImportMap"),
		TEXT("ExportMap"),
		TEXT("NameMap"));
	UE_LOG(LogIoStore, Display, TEXT("-------------------------------------------------------------------------------------------------------------------------"));
	for (const FContainerTargetSpec* ContainerTarget : ContainerTargets)
	{
		uint64 HeaderSize = 0;
		uint64 SummarySize = ContainerTarget->PackageCount * sizeof(FPackageSummary);
		uint64 UGraphSize = 0;
		uint64 ImportMapSize = 0;
		uint64 ExportMapSize = 0;
		uint64 NameMapSize = 0;

		for (const FContainerTargetFile& TargetFile : ContainerTarget->TargetFiles)
		{
			if (TargetFile.bIsBulkData)
			{
				continue;
			}

			UGraphSize += TargetFile.Package->OptimizedPackage->GetExportBundleArcsSize();
			ImportMapSize += TargetFile.Package->OptimizedPackage->GetImportMapSize();
			ExportMapSize += TargetFile.Package->OptimizedPackage->GetExportMapSize();
			NameMapSize += TargetFile.Package->OptimizedPackage->GetNameMapSize();
		}

		HeaderSize = SummarySize + UGraphSize + ImportMapSize + ExportMapSize + NameMapSize;

		UE_LOG(LogIoStore, Display, TEXT("%-30s %13.0lf %13.0lf %13.0lf %13.0lf %13.0lf %13.0lf"),
			*ContainerTarget->Name.ToString(),
			(double)HeaderSize / 1024.0,
			(double)SummarySize / 1024.0,
			(double)UGraphSize / 1024.0,
			(double)ImportMapSize / 1024.0,
			(double)ExportMapSize / 1024.0,
			(double)NameMapSize / 1024.0);

		TotalHeaderSize += HeaderSize;
		TotalSummarySize += SummarySize;
		TotalUGraphSize += UGraphSize;
		TotalImportMapSize += ImportMapSize;
		TotalExportMapSize += ExportMapSize;
		TotalNameMapSize += NameMapSize;
	}

	UE_LOG(LogIoStore, Display, TEXT("%-30s %13.0lf %13.0lf %13.0lf %13.0lf %13.0lf %13.0lf"),
		TEXT("TOTAL"),
		(double)TotalHeaderSize / 1024.0,
		(double)TotalSummarySize / 1024.0,
		(double)TotalUGraphSize / 1024.0,
		(double)TotalImportMapSize / 1024.0,
		(double)TotalExportMapSize / 1024.0,
		(double)TotalNameMapSize / 1024.0);

	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT(""));
}

class FIoStoreWriteRequestManager
{
public:
	FIoStoreWriteRequestManager(FPackageStoreOptimizer& InPackageStoreOptimizer)
		: PackageStoreOptimizer(InPackageStoreOptimizer)
		, MemoryAvailableEvent(FPlatformProcess::GetSynchEventFromPool(false))
	{
		InitiatorThread = Async(EAsyncExecution::Thread, [this]() { InitiatorThreadFunc(); });
		RetirerThread = Async(EAsyncExecution::Thread, [this]() { RetirerThreadFunc(); });
	}

	~FIoStoreWriteRequestManager()
	{
		InitiatorQueue.CompleteAdding();
		RetirerQueue.CompleteAdding();
		InitiatorThread.Wait();
		RetirerThread.Wait();
		FPlatformProcess::ReturnSynchEventToPool(MemoryAvailableEvent);
	}

	IIoStoreWriteRequest* Read(const FContainerTargetFile& InTargetFile)
	{
		return new FWriteContainerTargetFileRequest(*this, InTargetFile);
	}

private:
	class FWriteContainerTargetFileRequest
		: public IIoStoreWriteRequest
	{
	public:
		FWriteContainerTargetFileRequest(FIoStoreWriteRequestManager& InManager, const FContainerTargetFile& InTargetFile)
			: Manager(InManager)
			, TargetFile(InTargetFile)
			, FileRegions(TargetFile.FileRegions)
		{
		}

		~FWriteContainerTargetFileRequest()
		{
		}

		const FContainerTargetFile& GetTargetFile() const
		{
			return TargetFile;
		}

		void PrepareSourceBufferAsync(FGraphEventRef InCompletionEvent) override
		{
			CompletionEvent = InCompletionEvent;
			Manager.Schedule(this);
		}

		const FIoBuffer* GetSourceBuffer() override
		{
			return SourceBuffer;
		}

		void FreeSourceBuffer() override
		{
			delete SourceBuffer;
			SourceBuffer = nullptr;
			Manager.OnBufferMemoryFreed(TargetFile.SourceSize);
		}

		uint64 GetOrderHint() override
		{
			return TargetFile.IdealOrder;
		}

		TArrayView<const FFileRegion> GetRegions() override
		{
			return FileRegions;
		}

		FIoBuffer& PrepareSourceBuffer()
		{
			check(!SourceBuffer);
			SourceBuffer = new FIoBuffer(TargetFile.SourceSize);
			return *SourceBuffer;
		}

		void AsyncReadCallback()
		{
			//TRACE_CPUPROFILER_EVENT_SCOPE(ReadCallback);
			if (!TargetFile.bIsBulkData)
			{
				*SourceBuffer = Manager.PackageStoreOptimizer.CreatePackageBuffer(TargetFile.Package->OptimizedPackage, *SourceBuffer, bHasUpdatedExportBundleRegions ? nullptr : &FileRegions);
				bHasUpdatedExportBundleRegions = true;
			}
			TArray<FBaseGraphTask*> NewTasks;
			CompletionEvent->DispatchSubsequents(NewTasks);
		}

	private:
		FIoStoreWriteRequestManager& Manager;
		const FContainerTargetFile& TargetFile;
		TArray<FFileRegion> FileRegions;
		FGraphEventRef CompletionEvent;
		FIoBuffer* SourceBuffer = nullptr;
		bool bHasUpdatedExportBundleRegions = false;
	};

	struct FQueueEntry
	{
		FQueueEntry* Next = nullptr;
		IAsyncReadFileHandle* FileHandle = nullptr;
		IAsyncReadRequest* ReadRequest = nullptr;
		FWriteContainerTargetFileRequest* WriteRequest = nullptr;
	};

	class FQueue
	{
	public:
		FQueue()
			: Event(FPlatformProcess::GetSynchEventFromPool(false))
		{ }

		~FQueue()
		{
			check(Head == nullptr && Tail == nullptr);
			FPlatformProcess::ReturnSynchEventToPool(Event);
		}

		void Enqueue(FQueueEntry* Entry)
		{
			check(!bIsDoneAdding);
			{
				FScopeLock _(&CriticalSection);

				if (!Tail)
				{
					Head = Tail = Entry;
				}
				else
				{
					Tail->Next = Entry;
					Tail = Entry;
				}
				Entry->Next = nullptr;
			}

			Event->Trigger();
		}

		FQueueEntry* DequeueOrWait()
		{
			for (;;)
			{
				{
					FScopeLock _(&CriticalSection);
					if (Head)
					{
						FQueueEntry* Entry = Head;
						Head = Tail = nullptr;
						return Entry;
					}
				}

				if (bIsDoneAdding)
				{
					break;
				}

				Event->Wait();
			}

			return nullptr;
		}

		void CompleteAdding()
		{
			bIsDoneAdding = true;
			Event->Trigger();
		}

	private:
		FCriticalSection CriticalSection;
		FEvent* Event = nullptr;
		FQueueEntry* Head = nullptr;
		FQueueEntry* Tail = nullptr;
		TAtomic<bool> bIsDoneAdding{ false };
	};

	void Schedule(FWriteContainerTargetFileRequest* WriteRequest)
	{
		FQueueEntry* QueueEntry = new FQueueEntry();
		QueueEntry->WriteRequest = WriteRequest;
		InitiatorQueue.Enqueue(QueueEntry);
	}

	void Start(FQueueEntry* QueueEntry)
	{
		check(!QueueEntry->ReadRequest);
		check(!QueueEntry->FileHandle);
		const FContainerTargetFile& TargetFile = QueueEntry->WriteRequest->GetTargetFile();

		uint64 LocalUsedBufferMemory = UsedBufferMemory.Load();
		while (LocalUsedBufferMemory > 0 && LocalUsedBufferMemory + TargetFile.SourceSize > BufferMemoryLimit)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(WaitForBufferMemory);
			MemoryAvailableEvent->Wait();
			LocalUsedBufferMemory = UsedBufferMemory.Load();
		}
		UsedBufferMemory.AddExchange(TargetFile.SourceSize);

		IAsyncReadFileHandle* FileHandle = FPlatformFileManager::Get().GetPlatformFile().OpenAsyncRead(*TargetFile.NormalizedSourcePath);
		FAsyncFileCallBack Callback = [this, FileHandle, QueueEntry](bool, IAsyncReadRequest* ReadRequest)
		{
			QueueEntry->WriteRequest->AsyncReadCallback();
			QueueEntry->FileHandle = FileHandle;
			QueueEntry->ReadRequest = ReadRequest;
			RetirerQueue.Enqueue(QueueEntry);
		};
		FileHandle->ReadRequest(0, TargetFile.SourceSize, AIOP_Normal, &Callback, QueueEntry->WriteRequest->PrepareSourceBuffer().Data());
	}

	void Retire(FQueueEntry* QueueEntry)
	{
		check(QueueEntry->ReadRequest);
		check(QueueEntry->FileHandle);
		QueueEntry->ReadRequest->WaitCompletion();
		delete QueueEntry->ReadRequest;
		delete QueueEntry->FileHandle;
		delete QueueEntry;
	}

	void OnBufferMemoryFreed(uint64 Count)
	{
		uint64 OldValue = UsedBufferMemory.SubExchange(Count);
		check(OldValue >= Count);
		MemoryAvailableEvent->Trigger();
	}

	void InitiatorThreadFunc()
	{
		for (;;)
		{
			FQueueEntry* QueueEntry = InitiatorQueue.DequeueOrWait();
			if (!QueueEntry)
			{
				return;
			}
			while (QueueEntry)
			{
				FQueueEntry* Next = QueueEntry->Next;
				Start(QueueEntry);
				QueueEntry = Next;
			}
		}
	}

	void RetirerThreadFunc()
	{
		for (;;)
		{
			FQueueEntry* QueueEntry = RetirerQueue.DequeueOrWait();
			if (!QueueEntry)
			{
				return;
			}
			while (QueueEntry)
			{
				FQueueEntry* Next = QueueEntry->Next;
				Retire(QueueEntry);
				QueueEntry = Next;
			}
		}
	}

	FPackageStoreOptimizer& PackageStoreOptimizer;
	TFuture<void> InitiatorThread;
	TFuture<void> RetirerThread;
	FQueue InitiatorQueue;
	FQueue RetirerQueue;
	TAtomic<uint64> UsedBufferMemory { 0 };
	FEvent* MemoryAvailableEvent;

	static constexpr uint64 BufferMemoryLimit = 2ull << 30;
};

int32 CreateTarget(const FIoStoreArguments& Arguments, const FIoStoreWriterSettings& GeneralIoWriterSettings)
{
	TGuardValue<int32> GuardAllowUnversionedContentInEditor(GAllowUnversionedContentInEditor, 1);

#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.CreateOutputFile(CookedDir);
#endif

	TArray<FLegacyCookedPackage*> Packages;
	FPackageNameMap PackageNameMap;
	FPackageIdMap PackageIdMap;

	FPackageStoreOptimizer PackageStoreOptimizer;
	PackageStoreOptimizer.Initialize(Arguments.TargetPlatform);
	FIoStoreWriteRequestManager WriteRequestManager(PackageStoreOptimizer);

	TArray<FContainerTargetSpec*> ContainerTargets;
	UE_LOG(LogIoStore, Display, TEXT("Creating container targets..."));
	{
		IOSTORE_CPU_SCOPE(CreateContainerTargets);
		InitializeContainerTargetsAndPackages(Arguments, Packages, PackageNameMap, PackageIdMap, ContainerTargets, PackageStoreOptimizer);
	}

	TUniquePtr<FIoStoreWriterContext> IoStoreWriterContext(new FIoStoreWriterContext());
	TArray<FIoStoreWriter*> IoStoreWriters;
	FIoStoreWriter* GlobalIoStoreWriter = nullptr;
	{
		IOSTORE_CPU_SCOPE(InitializeIoStoreWriters);
		if (!Arguments.IsDLC())
		{
			GlobalIoStoreWriter = new FIoStoreWriter(*Arguments.GlobalContainerPath);
			IoStoreWriters.Add(GlobalIoStoreWriter);
		}
		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			check(ContainerTarget->ContainerId.IsValid());
			if (!ContainerTarget->OutputPath.IsEmpty())
			{
				ContainerTarget->IoStoreWriter = new FIoStoreWriter(*ContainerTarget->OutputPath);
				IoStoreWriters.Add(ContainerTarget->IoStoreWriter);
			}
		}
		FIoStatus IoStatus = IoStoreWriterContext->Initialize(GeneralIoWriterSettings);
		check(IoStatus.IsOk());

		FIoContainerSettings GlobalContainerSettings;
		if (Arguments.bSign)
		{
			GlobalContainerSettings.SigningKey = Arguments.KeyChain.SigningKey;
			GlobalContainerSettings.ContainerFlags |= EIoContainerFlags::Signed;
		}
		if (GlobalIoStoreWriter)
		{
			IoStatus = GlobalIoStoreWriter->Initialize(*IoStoreWriterContext, GlobalContainerSettings);
		}
		check(IoStatus.IsOk());
		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			if (ContainerTarget->IoStoreWriter)
			{
				FIoContainerSettings ContainerSettings;
				ContainerSettings.ContainerId = ContainerTarget->ContainerId;
				if (Arguments.bCreateDirectoryIndex)
				{
					ContainerSettings.ContainerFlags = ContainerTarget->ContainerFlags | EIoContainerFlags::Indexed;
				}
				if (EnumHasAnyFlags(ContainerTarget->ContainerFlags, EIoContainerFlags::Encrypted))
				{
					const FNamedAESKey* Key = Arguments.KeyChain.EncryptionKeys.Find(ContainerTarget->EncryptionKeyGuid);
					check(Key);
					ContainerSettings.EncryptionKeyGuid = ContainerTarget->EncryptionKeyGuid;
					ContainerSettings.EncryptionKey = Key->Key;
				}
				if (EnumHasAnyFlags(ContainerTarget->ContainerFlags, EIoContainerFlags::Signed))
				{
					ContainerSettings.SigningKey = Arguments.KeyChain.SigningKey;
					ContainerSettings.ContainerFlags |= EIoContainerFlags::Signed;
				}
				ContainerSettings.bGenerateDiffPatch = ContainerTarget->bGenerateDiffPatch;
				IoStatus = ContainerTarget->IoStoreWriter->Initialize(*IoStoreWriterContext, ContainerSettings);
				check(IoStatus.IsOk());
				ContainerTarget->IoStoreWriter->EnableDiskLayoutOrdering(ContainerTarget->PatchSourceReaders);
			}
		}
	}

	ParsePackageAssets(Packages, PackageStoreOptimizer);

	if (Arguments.IsDLC() && Arguments.bRemapPluginContentToGame)
	{
		for (FLegacyCookedPackage* Package : Packages)
		{
			const int32 DLCNameLen = Arguments.DLCName.Len() + 1;
			FString PackageNameStr = Package->PackageName.ToString();
			FString RedirectedPackageNameStr = TEXT("/Game");
			RedirectedPackageNameStr.AppendChars(*PackageNameStr + DLCNameLen, PackageNameStr.Len() - DLCNameLen);
			FName RedirectedPackageName = FName(*RedirectedPackageNameStr);
			if (Arguments.ReleasedPackages.PackageNames.Contains(RedirectedPackageName))
			{
				Package->OptimizedPackage->RedirectFrom(RedirectedPackageName);
			}
			else
			{
				// TODO: Should NAME_None be the default source package name instead?
				// RemapLocalizationPathIfNeeded sets it to the original
				// package name when not remapping plugin content
				Package->OptimizedPackage->RedirectFrom(NAME_None);
			}
		}
	}


	for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
	{
		if (ContainerTarget->IoStoreWriter)
		{
			for (FContainerTargetFile& TargetFile : ContainerTarget->TargetFiles)
			{
				if (TargetFile.bIsBulkData)
				{
					FIoWriteOptions WriteOptions;
					WriteOptions.DebugName = *TargetFile.TargetPath;
					WriteOptions.bForceUncompressed = TargetFile.bForceUncompressed;
					WriteOptions.bIsMemoryMapped = TargetFile.bIsMemoryMappedBulkData;
					WriteOptions.FileName = TargetFile.DestinationPath;
					ContainerTarget->IoStoreWriter->Append(TargetFile.ChunkId, WriteRequestManager.Read(TargetFile), WriteOptions);
				}
			}
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Optimizing packages..."));
	for (FLegacyCookedPackage* Package : Packages)
	{
		check(Package->OptimizedPackage);
		PackageStoreOptimizer.BeginResolvePackage(Package->OptimizedPackage);
	}
	PackageStoreOptimizer.Flush(true);

	UE_LOG(LogIoStore, Display, TEXT("Creating disk layout..."));
	CreateDiskLayout(ContainerTargets, Packages, Arguments.OrderMaps, PackageIdMap);

	for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
	{
		if (ContainerTarget->IoStoreWriter)
		{
			TArray<FPackageStoreContainerHeaderEntry> ContainerHeaderEntries;
			for (FContainerTargetFile& TargetFile : ContainerTarget->TargetFiles)
			{
				check(TargetFile.Package);
				if (!TargetFile.bIsBulkData)
				{
					FIoWriteOptions WriteOptions;
					WriteOptions.DebugName = *TargetFile.TargetPath;
					WriteOptions.bForceUncompressed = TargetFile.bForceUncompressed;
					WriteOptions.FileName = TargetFile.DestinationPath;
					ContainerTarget->IoStoreWriter->Append(TargetFile.ChunkId, WriteRequestManager.Read(TargetFile), WriteOptions);
					ContainerHeaderEntries.Add(PackageStoreOptimizer.CreateContainerHeaderEntry(TargetFile.Package->OptimizedPackage));
				}
			}

			ContainerTarget->Header = PackageStoreOptimizer.CreateContainerHeader(ContainerTarget->ContainerId, ContainerHeaderEntries);
			FLargeMemoryWriter HeaderAr(0, true);
			HeaderAr << ContainerTarget->Header;
			int64 DataSize = HeaderAr.TotalSize();
			FIoBuffer ContainerHeaderBuffer(FIoBuffer::AssumeOwnership, HeaderAr.ReleaseOwnership(), DataSize);
			
			FIoWriteOptions WriteOptions;
			WriteOptions.DebugName = TEXT("ContainerHeader");
			ContainerTarget->IoStoreWriter->Append(
				CreateIoChunkId(ContainerTarget->ContainerId.Value(), 0, EIoChunkType::ContainerHeader),
				ContainerHeaderBuffer,
				WriteOptions);
		}

		// Check if we need to dump the final order of the packages. Useful, to debug packing.
		if (FParse::Param(FCommandLine::Get(), TEXT("writefinalorder")))
		{
			FString FinalContainerOrderFile = FPaths::GetPath(ContainerTarget->OutputPath) + FPaths::GetBaseFilename(ContainerTarget->OutputPath) + TEXT("-order.txt");
			TUniquePtr<FArchive> IoOrderListArchive(IFileManager::Get().CreateFileWriter(*FinalContainerOrderFile));
			if (IoOrderListArchive)
			{
				IoOrderListArchive->SetIsTextFormat(true);

				for (const FContainerTargetFile& TargetFile : ContainerTarget->TargetFiles)
				{
					if (TargetFile.Package)
					{
						FString Line = FString::Printf(TEXT("%s"), *TargetFile.Package->FileName);
						IoOrderListArchive->Logf(TEXT("%s"), *Line);
					}
				}

				IoOrderListArchive->Close();
			}
		}

	}

	uint64 InitialLoadSize = 0;
	if (GlobalIoStoreWriter)
	{
		InitialLoadSize = PackageStoreOptimizer.WriteScriptObjects(GlobalIoStoreWriter);
	}

	UE_LOG(LogIoStore, Display, TEXT("Serializing container(s)..."));

	TArray<FIoStoreWriterResult> IoStoreWriterResults;
	IoStoreWriterResults.Reserve(IoStoreWriters.Num());
	for (FIoStoreWriter* IoStoreWriter : IoStoreWriters)
	{
		TFuture<FIoStoreWriterResult> FlushTask = Async(EAsyncExecution::ThreadPool, [IoStoreWriter]()
		{
			return IoStoreWriter->Flush().ConsumeValueOrDie();
		});

		while (!FlushTask.IsReady())
		{
			FlushTask.WaitFor(FTimespan::FromSeconds(2.0));
			FIoStoreWriterContext::FProgress Progress = IoStoreWriterContext->GetProgress();
			UE_LOG(LogIoStore, Display, TEXT("Hashed, Compressed, Serialized: %lld, %lld, %lld / %lld"), Progress.HashedChunksCount, Progress.CompressedChunksCount, Progress.SerializedChunksCount, Progress.TotalChunksCount);
		}
		IoStoreWriterResults.Emplace(FlushTask.Get());
		delete IoStoreWriter;
	}
	IoStoreWriters.Empty();

	IOSTORE_CPU_SCOPE(CalculateStats);

	UE_LOG(LogIoStore, Display, TEXT("Calculating stats..."));
	uint64 UExpSize = 0;
	uint64 UAssetSize = 0;
	uint64 ImportedPackagesCount = 0;
	uint64 NoImportedPackagesCount = 0;
	uint64 NameMapCount = 0;
	
	for (const FLegacyCookedPackage* Package : Packages)
	{
		UExpSize += Package->UExpSize;
		UAssetSize += Package->UAssetSize;
		NameMapCount += Package->OptimizedPackage->GetNameCount();
		int32 PackageImportedPackagesCount = Package->OptimizedPackage->GetImportedPackageIds().Num();
		ImportedPackagesCount += PackageImportedPackagesCount;
		NoImportedPackagesCount += PackageImportedPackagesCount == 0;
	}

	LogWriterResults(IoStoreWriterResults);
	LogContainerPackageInfo(ContainerTargets);
	
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8.2lf MB UExp"), (double)UExpSize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8.2lf MB UAsset"), (double)UAssetSize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8d Packages"), Packages.Num());
	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("Output: %8llu Export bundles"), PackageStoreOptimizer.GetTotalExportBundleCount());
	UE_LOG(LogIoStore, Display, TEXT("Output: %8llu Export bundle entries"), PackageStoreOptimizer.GetTotalExportBundleEntryCount());
	UE_LOG(LogIoStore, Display, TEXT("Output: %8llu Export bundle arcs"), PackageStoreOptimizer.GetTotalExportBundleArcCount());
	UE_LOG(LogIoStore, Display, TEXT("Output: %8llu Name map entries"), NameMapCount);
	UE_LOG(LogIoStore, Display, TEXT("Output: %8llu Imported package entries"), ImportedPackagesCount);
	UE_LOG(LogIoStore, Display, TEXT("Output: %8llu Packages without imports"), NoImportedPackagesCount);
	UE_LOG(LogIoStore, Display, TEXT("Output: %8d Public runtime script objects"), PackageStoreOptimizer.GetTotalScriptObjectCount());
	UE_LOG(LogIoStore, Display, TEXT("Output: %8.2lf MB InitialLoadData"), (double)InitialLoadSize / 1024.0 / 1024.0);

	return 0;
}

int32 CreateContentPatch(const FIoStoreArguments& Arguments, const FIoStoreWriterSettings& GeneralIoWriterSettings)
{
	UE_LOG(LogIoStore, Display, TEXT("Building patch..."));
	TUniquePtr<FIoStoreWriterContext> IoStoreWriterContext(new FIoStoreWriterContext());
	FIoStatus IoStatus = IoStoreWriterContext->Initialize(GeneralIoWriterSettings);
	check(IoStatus.IsOk());
	TArray<FIoStoreWriterResult> Results;
	for (const FContainerSourceSpec& Container : Arguments.Containers)
	{
		TArray<TUniquePtr<FIoStoreReader>> SourceReaders = CreatePatchSourceReaders(Container.PatchSourceContainerFiles, Arguments);
		TUniquePtr<FIoStoreReader> TargetReader = CreateIoStoreReader(*Container.PatchTargetFile, Arguments.KeyChain);
		if (!TargetReader.IsValid())
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed loading target container"));
			return -1;
		}

		FIoStoreWriter IoStoreWriter(*Container.OutputPath);
		
		EIoContainerFlags TargetContainerFlags = TargetReader->GetContainerFlags();

		FIoContainerSettings ContainerSettings;
		if (Arguments.bCreateDirectoryIndex)
		{
			ContainerSettings.ContainerFlags |= EIoContainerFlags::Indexed;
		}

		ContainerSettings.ContainerId = TargetReader->GetContainerId();
		if (Arguments.bSign || EnumHasAnyFlags(TargetContainerFlags, EIoContainerFlags::Signed))
		{
			ContainerSettings.SigningKey = Arguments.KeyChain.SigningKey;
			ContainerSettings.ContainerFlags |= EIoContainerFlags::Signed;
		}

		if (EnumHasAnyFlags(TargetContainerFlags, EIoContainerFlags::Encrypted))
		{
			ContainerSettings.ContainerFlags |= EIoContainerFlags::Encrypted;
			const FNamedAESKey* Key = Arguments.KeyChain.EncryptionKeys.Find(TargetReader->GetEncryptionKeyGuid());
			if (!Key)
			{
				UE_LOG(LogIoStore, Error, TEXT("Missing encryption key for target container"));
				return -1;
			}
			ContainerSettings.EncryptionKeyGuid = Key->Guid;
			ContainerSettings.EncryptionKey = Key->Key;
		}

		IoStatus = IoStoreWriter.Initialize(*IoStoreWriterContext, ContainerSettings);
		check(IoStatus.IsOk());

		TMap<FIoChunkId, FIoChunkHash> SourceHashByChunkId;
		for (const TUniquePtr<FIoStoreReader>& SourceReader : SourceReaders)
		{
			SourceReader->EnumerateChunks([&SourceHashByChunkId](const FIoStoreTocChunkInfo& ChunkInfo)
			{
				SourceHashByChunkId.Add(ChunkInfo.Id, ChunkInfo.Hash);
				return true;
			});
		}

		TMap<FIoChunkId, FString> ChunkFileNamesMap;
		TargetReader->GetDirectoryIndexReader().IterateDirectoryIndex(FIoDirectoryIndexHandle::RootDirectory(), TEXT(""),
		[&ChunkFileNamesMap, &TargetReader](FString Filename, uint32 TocEntryIndex) -> bool
		{
			TIoStatusOr<FIoStoreTocChunkInfo> ChunkInfo = TargetReader->GetChunkInfo(TocEntryIndex);
			if (ChunkInfo.IsOk())
			{
				ChunkFileNamesMap.Add(ChunkInfo.ValueOrDie().Id, Filename);
			}
			return true;
		});

		TargetReader->EnumerateChunks([&TargetReader, &SourceHashByChunkId, &IoStoreWriter, &ChunkFileNamesMap](const FIoStoreTocChunkInfo& ChunkInfo)
		{
			FIoChunkHash* FindSourceHash = SourceHashByChunkId.Find(ChunkInfo.Id);
			if (!FindSourceHash || *FindSourceHash != ChunkInfo.Hash)
			{
				FIoReadOptions ReadOptions;
				TIoStatusOr<FIoBuffer> ChunkBuffer = TargetReader->Read(ChunkInfo.Id, ReadOptions);
				FIoWriteOptions WriteOptions;
				FString* FindFileName = ChunkFileNamesMap.Find(ChunkInfo.Id);
				if (FindFileName)
				{
					WriteOptions.FileName = *FindFileName;
					if (FindSourceHash)
					{
						UE_LOG(LogIoStore, Display, TEXT("Modified: %s"), **FindFileName);
					}
					else
					{
						UE_LOG(LogIoStore, Display, TEXT("Added: %s"), **FindFileName);
					}
				}
				WriteOptions.bIsMemoryMapped = ChunkInfo.bIsMemoryMapped;
				WriteOptions.bForceUncompressed = ChunkInfo.bForceUncompressed; 
				IoStoreWriter.Append(ChunkInfo.Id, ChunkBuffer.ConsumeValueOrDie(), WriteOptions);
			}
			return true;
		});


		Results.Emplace(IoStoreWriter.Flush().ConsumeValueOrDie());
	}

	LogWriterResults(Results);

	return 0;
}

using DirectoryIndexVisitorFunction = TFunctionRef<bool(FString, const uint32)>;

bool IterateDirectoryIndex(FIoDirectoryIndexHandle Directory, const FString& Path, const FIoDirectoryIndexReader& Reader, DirectoryIndexVisitorFunction Visit)
{
	FIoDirectoryIndexHandle File = Reader.GetFile(Directory);
	while (File.IsValid())
	{
		const uint32 TocEntryIndex = Reader.GetFileData(File);
		FStringView FileName = Reader.GetFileName(File);
		FString FilePath = Reader.GetMountPoint() / Path / FString(FileName);

		if (!Visit(MoveTemp(FilePath), TocEntryIndex))
		{
			return false;
		}

		File = Reader.GetNextFile(File);
	}

	FIoDirectoryIndexHandle ChildDirectory = Reader.GetChildDirectory(Directory);
	while (ChildDirectory.IsValid())
	{
		FStringView DirectoryName = Reader.GetDirectoryName(ChildDirectory);
		FString ChildDirectoryPath = Path / FString(DirectoryName);

		if (!IterateDirectoryIndex(ChildDirectory, ChildDirectoryPath, Reader, Visit))
		{
			return false;
		}

		ChildDirectory = Reader.GetNextDirectory(ChildDirectory);
	}

	return true;
}

int32 ListContainer(
	const FIoStoreArguments& Arguments,
	const FString& ContainerPathOrWildcard,
	const FString& CsvPath)
{
	TArray<FString> ContainerFilePaths;

	if (IFileManager::Get().FileExists(*ContainerPathOrWildcard))
	{
		ContainerFilePaths.Add(ContainerPathOrWildcard);
	}
	else
	{
		FString Directory = FPaths::GetPath(ContainerPathOrWildcard);
		FPaths::NormalizeDirectoryName(Directory);

		TArray<FString> FoundContainerFiles;
		IFileManager::Get().FindFiles(FoundContainerFiles, *ContainerPathOrWildcard, true, false);

		for (const FString& Filename : FoundContainerFiles)
		{
			ContainerFilePaths.Emplace(Directory / Filename);
		}
	}

	if (ContainerFilePaths.Num() == 0)
	{
		UE_LOG(LogIoStore, Error, TEXT("Container '%s' doesn't exist and no container matches wildcard."), *ContainerPathOrWildcard);
		return -1;
	}

	TArray<FString> CsvLines;
	
	CsvLines.Add(TEXT("PackageId, PackageName, Filename, ContainerName, Offset, Size, CompressedSize, Hash"));

	for (const FString& ContainerFilePath : ContainerFilePaths)
	{
		TUniquePtr<FIoStoreReader> Reader = CreateIoStoreReader(*ContainerFilePath, Arguments.KeyChain);
		if (!Reader.IsValid())
		{
			UE_LOG(LogIoStore, Warning, TEXT("Failed to read container '%s'"), *ContainerFilePath);
			continue;
		}

		if (!EnumHasAnyFlags(Reader->GetContainerFlags(), EIoContainerFlags::Indexed))
		{
			UE_LOG(LogIoStore, Warning, TEXT("Missing directory index for container '%s'"), *ContainerFilePath);
		}

		UE_LOG(LogIoStore, Display, TEXT("Listing container '%s'"), *ContainerFilePath);

		FString ContainerName = FPaths::GetBaseFilename(ContainerFilePath);
		const FIoDirectoryIndexReader& IndexReader = Reader->GetDirectoryIndexReader();
		TMap<FIoChunkId, FString> ChunkFileNamesMap;
		IterateDirectoryIndex(
			FIoDirectoryIndexHandle::RootDirectory(),
			TEXT(""),
			IndexReader,
			[&ChunkFileNamesMap, &Reader](FString Filename, uint32 TocEntryIndex) -> bool
		{
			TIoStatusOr<FIoStoreTocChunkInfo> ChunkInfo = Reader->GetChunkInfo(TocEntryIndex);
			if (ChunkInfo.IsOk())
			{
				ChunkFileNamesMap.Add(ChunkInfo.ValueOrDie().Id, Filename);
			}
			return true;
		});

		Reader->EnumerateChunks([&CsvLines, &ChunkFileNamesMap, &ContainerName](const FIoStoreTocChunkInfo& ChunkInfo)
		{
			FString PackageName;
			FString* FindFileName = ChunkFileNamesMap.Find(ChunkInfo.Id);
			if (FindFileName)
			{
				FPackageName::TryConvertFilenameToLongPackageName(*FindFileName, PackageName, nullptr);
			}
			FPackageId PackageId = PackageName.Len() > 0
				? FPackageId::FromName(FName(*PackageName))
				: FPackageId();

			CsvLines.Emplace(
				FString::Printf(TEXT("0x%llX, %s, %s, %s, %lld, %lld, %lld, 0x%s"),
					PackageId.ValueForDebugging(),
					*PackageName,
					FindFileName ? **FindFileName : TEXT(""),
					*ContainerName,
					ChunkInfo.Offset,
					ChunkInfo.Size,
					ChunkInfo.CompressedSize,
					*ChunkInfo.Hash.ToString()));

			return true;
		});
	}

	if (CsvLines.Num())
	{
		UE_LOG(LogIoStore, Display, TEXT("Saving '%d' file entries to '%s'"), CsvLines.Num(), *CsvPath);
		FFileHelper::SaveStringArrayToFile(CsvLines, *CsvPath);
	}
	else
	{
		UE_LOG(LogIoStore, Warning, TEXT("No file entries to save from '%s'"), *ContainerPathOrWildcard);
	}

	return 0;
}

int32 Describe(
	const FString& GlobalContainerPath,
	const FKeyChain& KeyChain,
	const FString& PackageFilter,
	const FString& OutPath,
	bool bIncludeExportHashes)
{
	struct FPackageDesc;

	struct FPackageRedirect
	{
		FName Culture;
		FPackageDesc* Source = nullptr;
		FPackageDesc* Target = nullptr;
	};

	struct FContainerDesc
	{
		FName Name;
		FIoContainerId Id;
		FGuid EncryptionKeyGuid;
		TArray<FPackageRedirect> PackageRedirects;
		bool bCompressed;
		bool bSigned;
		bool bEncrypted;
		bool bIndexed;
	};

	struct FPackageLocation
	{
		FContainerDesc* Container = nullptr;
		uint64 Offset = -1;
	};

	struct FExportDesc
	{
		FPackageDesc* Package = nullptr;
		FName Name;
		FName FullName;
		FPackageObjectIndex OuterIndex;
		FPackageObjectIndex ClassIndex;
		FPackageObjectIndex SuperIndex;
		FPackageObjectIndex TemplateIndex;
		FPackageObjectIndex GlobalImportIndex;
		uint64 SerialOffset = 0;
		uint64 SerialSize = 0;
		FSHAHash Hash;
	};

	struct FExportBundleEntryDesc
	{
		FExportBundleEntry::EExportCommandType CommandType = FExportBundleEntry::ExportCommandType_Count;
		int32 LocalExportIndex = -1;
		FExportDesc* Export = nullptr;
	};

	struct FImportDesc
	{
		FName Name;
		FPackageObjectIndex GlobalImportIndex;
		FExportDesc* Export = nullptr;
	};

	struct FScriptObjectDesc
	{
		FName Name;
		FName FullName;
		FPackageObjectIndex GlobalImportIndex;
		FPackageObjectIndex OuterIndex;
	};

	struct FPackageDesc
	{
		FPackageId PackageId;
		FName PackageName;
		uint64 Size = 0;
		uint32 LoadOrder = uint32(-1);
		uint32 PackageFlags = 0;
		int32 NameCount = -1;
		int32 ExportBundleCount = -1;
		TArray<FPackageLocation, TInlineAllocator<1>> Locations;
		TArray<FImportDesc> Imports;
		TArray<FExportDesc> Exports;
		TArray<TArray<FExportBundleEntryDesc>, TInlineAllocator<1>> ExportBundles;
	};

	if (!IFileManager::Get().FileExists(*GlobalContainerPath))
	{
		UE_LOG(LogIoStore, Error, TEXT("Global container '%s' doesn't exist."), *GlobalContainerPath);
		return -1;
	}

	TUniquePtr<FIoStoreReader> GlobalReader = CreateIoStoreReader(*GlobalContainerPath, KeyChain);
	if (!GlobalReader.IsValid())
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed reading global container '%s'"), *GlobalContainerPath);
		return -1;
	}

	UE_LOG(LogIoStore, Display, TEXT("Loading global name map..."));

	TIoStatusOr<FIoBuffer> GlobalNamesIoBuffer = GlobalReader->Read(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNames), FIoReadOptions());
	if (!GlobalNamesIoBuffer.IsOk())
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed reading names chunk from global container '%s'"), *GlobalContainerPath);
		return -1;
	}

	TIoStatusOr<FIoBuffer> GlobalNameHashesIoBuffer = GlobalReader->Read(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNameHashes), FIoReadOptions());
	if (!GlobalNameHashesIoBuffer.IsOk())
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed reading name hashes chunk from global container '%s'"), *GlobalContainerPath);
		return -1;
	}

	TArray<FNameEntryId> GlobalNameMap;
	LoadNameBatch(
		GlobalNameMap,
		TArrayView<const uint8>(GlobalNamesIoBuffer.ValueOrDie().Data(), GlobalNamesIoBuffer.ValueOrDie().DataSize()),
		TArrayView<const uint8>(GlobalNameHashesIoBuffer.ValueOrDie().Data(), GlobalNameHashesIoBuffer.ValueOrDie().DataSize()));

	UE_LOG(LogIoStore, Display, TEXT("Loading script imports..."));

	TIoStatusOr<FIoBuffer> InitialLoadIoBuffer = GlobalReader->Read(CreateIoChunkId(0, 0, EIoChunkType::LoaderInitialLoadMeta), FIoReadOptions());
	if (!InitialLoadIoBuffer.IsOk())
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed reading initial load meta chunk from global container '%s'"), *GlobalContainerPath);
		return -1;
	}

	TMap<FPackageObjectIndex, FScriptObjectDesc> ScriptObjectByGlobalIdMap;
	FLargeMemoryReader InitialLoadArchive(InitialLoadIoBuffer.ValueOrDie().Data(), InitialLoadIoBuffer.ValueOrDie().DataSize());
	int32 NumScriptObjects = 0;
	InitialLoadArchive << NumScriptObjects;
	const FScriptObjectEntry* ScriptObjectEntries = reinterpret_cast<const FScriptObjectEntry*>(InitialLoadIoBuffer.ValueOrDie().Data() + InitialLoadArchive.Tell());
	for (int32 ScriptObjectIndex = 0; ScriptObjectIndex < NumScriptObjects; ++ScriptObjectIndex)
	{
		const FScriptObjectEntry& ScriptObjectEntry = ScriptObjectEntries[ScriptObjectIndex];
		const FMappedName& MappedName = FMappedName::FromMinimalName(ScriptObjectEntry.ObjectName);
		check(MappedName.IsGlobal());
		FScriptObjectDesc& ScriptObjectDesc = ScriptObjectByGlobalIdMap.Add(ScriptObjectEntry.GlobalIndex);
		ScriptObjectDesc.Name = FName::CreateFromDisplayId(GlobalNameMap[MappedName.GetIndex()], MappedName.GetNumber());
		ScriptObjectDesc.GlobalImportIndex = ScriptObjectEntry.GlobalIndex;
		ScriptObjectDesc.OuterIndex = ScriptObjectEntry.OuterIndex;
	}
	for (auto& KV : ScriptObjectByGlobalIdMap)
	{
		FScriptObjectDesc& ScriptObjectDesc = KV.Get<1>();
		if (ScriptObjectDesc.FullName.IsNone())
		{
			TArray<FScriptObjectDesc*> ScriptObjectStack;
			FScriptObjectDesc* Current = &ScriptObjectDesc;
			FString FullName;
			while (Current)
			{
				if (!Current->FullName.IsNone())
				{
					FullName = Current->FullName.ToString();
					break;
				}
				ScriptObjectStack.Push(Current);
				Current = ScriptObjectByGlobalIdMap.Find(Current->OuterIndex);
			}
			while (ScriptObjectStack.Num() > 0)
			{
				Current = ScriptObjectStack.Pop();
				FullName /= Current->Name.ToString();
				Current->FullName = FName(FullName);
			}
		}
	}

	FString Directory = FPaths::GetPath(GlobalContainerPath);
	FPaths::NormalizeDirectoryName(Directory);

	TArray<FString> FoundContainerFiles;
	IFileManager::Get().FindFiles(FoundContainerFiles, *(Directory / TEXT("*.utoc")), true, false);
	TArray<FString> ContainerFilePaths;
	for (const FString& Filename : FoundContainerFiles)
	{
		ContainerFilePaths.Emplace(Directory / Filename);
	}

	UE_LOG(LogIoStore, Display, TEXT("Loading containers..."));

	TArray<TUniquePtr<FIoStoreReader>> Readers;

	struct FLoadContainerHeaderJob
	{
		FName ContainerName;
		FContainerDesc* ContainerDesc = nullptr;
		TArray<FPackageDesc*> Packages;
		FIoStoreReader* Reader = nullptr;
		FCulturePackageMap RawCulturePackageMap;
		TArray<TPair<FPackageId, FPackageId>> RawPackageRedirects;
	};

	TArray<FLoadContainerHeaderJob> LoadContainerHeaderJobs;

	for (const FString& ContainerFilePath : ContainerFilePaths)
	{
		TUniquePtr<FIoStoreReader> Reader = CreateIoStoreReader(*ContainerFilePath, KeyChain);
		if (!Reader.IsValid())
		{
			UE_LOG(LogIoStore, Warning, TEXT("Failed to read container '%s'"), *ContainerFilePath);
			continue;
		}

		FLoadContainerHeaderJob& LoadContainerHeaderJob = LoadContainerHeaderJobs.AddDefaulted_GetRef();
		LoadContainerHeaderJob.Reader = Reader.Get();
		LoadContainerHeaderJob.ContainerName = FName(FPaths::GetBaseFilename(ContainerFilePath));
		
		Readers.Emplace(MoveTemp(Reader));
	}
	
	TAtomic<int32> TotalPackageCount{ 0 };
	ParallelFor(LoadContainerHeaderJobs.Num(), [&LoadContainerHeaderJobs, &TotalPackageCount](int32 Index)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LoadContainerHeader);

		FLoadContainerHeaderJob& Job = LoadContainerHeaderJobs[Index];

		FContainerDesc* ContainerDesc = new FContainerDesc();
		ContainerDesc->Name = Job.ContainerName;
		ContainerDesc->Id = Job.Reader->GetContainerId();
		ContainerDesc->EncryptionKeyGuid = Job.Reader->GetEncryptionKeyGuid();
		EIoContainerFlags Flags = Job.Reader->GetContainerFlags();
		ContainerDesc->bCompressed = bool(Flags & EIoContainerFlags::Compressed);
		ContainerDesc->bEncrypted = bool(Flags & EIoContainerFlags::Encrypted);
		ContainerDesc->bSigned = bool(Flags & EIoContainerFlags::Signed);
		ContainerDesc->bIndexed = bool(Flags & EIoContainerFlags::Indexed);
		Job.ContainerDesc = ContainerDesc;

		TIoStatusOr<FIoBuffer> IoBuffer = Job.Reader->Read(CreateIoChunkId(Job.Reader->GetContainerId().Value(), 0, EIoChunkType::ContainerHeader), FIoReadOptions());
		if (IoBuffer.IsOk())
		{
			FMemoryReaderView Ar(MakeArrayView(IoBuffer.ValueOrDie().Data(), IoBuffer.ValueOrDie().DataSize()));
			FContainerHeader ContainerHeader;
			Ar << ContainerHeader;

			Job.RawCulturePackageMap = ContainerHeader.CulturePackageMap;
			Job.RawPackageRedirects = ContainerHeader.PackageRedirects;

			TArrayView<FPackageStoreEntry> StoreEntries(reinterpret_cast<FPackageStoreEntry*>(ContainerHeader.StoreEntries.GetData()), ContainerHeader.PackageCount);

			int32 PackageIndex = 0;
			Job.Packages.Reserve(StoreEntries.Num());
			for (FPackageStoreEntry& ContainerEntry : StoreEntries)
			{
				const FPackageId& PackageId = ContainerHeader.PackageIds[PackageIndex++];
				FPackageDesc* PackageDesc = new FPackageDesc();
				PackageDesc->PackageId = PackageId;
				PackageDesc->Size = ContainerEntry.ExportBundlesSize;
				PackageDesc->Exports.SetNum(ContainerEntry.ExportCount);
				PackageDesc->ExportBundleCount = ContainerEntry.ExportBundleCount;
				PackageDesc->LoadOrder = ContainerEntry.LoadOrder;
				Job.Packages.Add(PackageDesc);
				++TotalPackageCount;
			}
		}
	}, EParallelForFlags::Unbalanced);

	struct FLoadPackageSummaryJob
	{
		FPackageDesc* PackageDesc = nullptr;
		FIoChunkId ChunkId;
		TArray<FLoadContainerHeaderJob*, TInlineAllocator<1>> Containers;
	};

	TArray<FLoadPackageSummaryJob> LoadPackageSummaryJobs;

	TArray<FContainerDesc*> Containers;
	TArray<FPackageDesc*> Packages;
	TMap<FPackageId, FPackageDesc*> PackageByIdMap;
	TMap<FPackageId, FLoadPackageSummaryJob*> PackageJobByIdMap;
	Containers.Reserve(LoadContainerHeaderJobs.Num());
	Packages.Reserve(TotalPackageCount);
	PackageByIdMap.Reserve(TotalPackageCount);
	PackageJobByIdMap.Reserve(TotalPackageCount);
	LoadPackageSummaryJobs.Reserve(TotalPackageCount);
	for (FLoadContainerHeaderJob& LoadContainerHeaderJob : LoadContainerHeaderJobs)
	{
		Containers.Add(LoadContainerHeaderJob.ContainerDesc);
		for (FPackageDesc* PackageDesc : LoadContainerHeaderJob.Packages)
		{
			FLoadPackageSummaryJob*& UniquePackageJob = PackageJobByIdMap.FindOrAdd(PackageDesc->PackageId);
			if (!UniquePackageJob)
			{
				Packages.Add(PackageDesc);
				PackageByIdMap.Add(PackageDesc->PackageId, PackageDesc);
				FLoadPackageSummaryJob& LoadPackageSummaryJob = LoadPackageSummaryJobs.AddDefaulted_GetRef();
				LoadPackageSummaryJob.PackageDesc = PackageDesc;
				LoadPackageSummaryJob.ChunkId = CreateIoChunkId(PackageDesc->PackageId.Value(), 0, EIoChunkType::ExportBundleData);
				UniquePackageJob = &LoadPackageSummaryJob;
			}
			UniquePackageJob->Containers.Add(&LoadContainerHeaderJob);
		}
	}
	for (FLoadContainerHeaderJob& LoadContainerHeaderJob : LoadContainerHeaderJobs)
	{
		for (const auto& RedirectPair : LoadContainerHeaderJob.RawPackageRedirects)
		{
			FPackageRedirect& PackageRedirect = LoadContainerHeaderJob.ContainerDesc->PackageRedirects.AddDefaulted_GetRef();
			PackageRedirect.Source = PackageByIdMap.FindRef(RedirectPair.Get<0>());
			PackageRedirect.Target = PackageByIdMap.FindRef(RedirectPair.Get<1>());
		}
		for (const auto& CultureRedirectsPair : LoadContainerHeaderJob.RawCulturePackageMap)
		{
			FName Culture(CultureRedirectsPair.Get<0>());
			for (const auto& RedirectPair : CultureRedirectsPair.Get<1>())
			{
				FPackageRedirect& PackageRedirect = LoadContainerHeaderJob.ContainerDesc->PackageRedirects.AddDefaulted_GetRef();
				PackageRedirect.Source = PackageByIdMap.FindRef(RedirectPair.Get<0>());
				PackageRedirect.Target = PackageByIdMap.FindRef(RedirectPair.Get<1>());
				PackageRedirect.Culture = Culture;
			}
		}
	}
	
	ParallelFor(LoadPackageSummaryJobs.Num(), [&LoadPackageSummaryJobs, bIncludeExportHashes](int32 Index)
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(LoadPackageSummary);

		FLoadPackageSummaryJob& Job = LoadPackageSummaryJobs[Index];
		for (FLoadContainerHeaderJob* LoadContainerHeaderJob : Job.Containers)
		{
			TIoStatusOr<FIoStoreTocChunkInfo> ChunkInfo = LoadContainerHeaderJob->Reader->GetChunkInfo(Job.ChunkId);
			check(ChunkInfo.IsOk());
			FPackageLocation& Location = Job.PackageDesc->Locations.AddDefaulted_GetRef();
			Location.Container = LoadContainerHeaderJob->ContainerDesc;
			Location.Offset = ChunkInfo.ValueOrDie().Offset;
		}

		FIoStoreReader* Reader = Job.Containers[0]->Reader;
		FIoReadOptions ReadOptions;
		if (!bIncludeExportHashes)
		{
			ReadOptions.SetRange(0, 16 << 10);
		}
		TIoStatusOr<FIoBuffer> IoBuffer = Reader->Read(Job.ChunkId, ReadOptions);
		check(IoBuffer.IsOk());
		const uint8* PackageSummaryData = IoBuffer.ValueOrDie().Data();
		const FPackageSummary* PackageSummary = reinterpret_cast<const FPackageSummary*>(PackageSummaryData);
		const uint64 PackageSummarySize = PackageSummary->GraphDataOffset + PackageSummary->GraphDataSize;
		if (PackageSummarySize > IoBuffer.ValueOrDie().DataSize())
		{
			ReadOptions.SetRange(0, PackageSummarySize);
			IoBuffer = Reader->Read(Job.ChunkId, ReadOptions);
			PackageSummaryData = IoBuffer.ValueOrDie().Data();
			PackageSummary = reinterpret_cast<const FPackageSummary*>(PackageSummaryData);
		}

		TArray<FNameEntryId> PackageNameMap;
		if (PackageSummary->NameMapNamesSize)
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(LoadNameBatch);
			const uint8* NameMapNamesData = PackageSummaryData + PackageSummary->NameMapNamesOffset;
			const uint8* NameMapHashesData = PackageSummaryData + PackageSummary->NameMapHashesOffset;
			LoadNameBatch(
				PackageNameMap,
				TArrayView<const uint8>(NameMapNamesData, PackageSummary->NameMapNamesSize),
				TArrayView<const uint8>(NameMapHashesData, PackageSummary->NameMapHashesSize));
		}

		Job.PackageDesc->PackageName = FName::CreateFromDisplayId(PackageNameMap[PackageSummary->Name.GetIndex()], PackageSummary->Name.GetNumber());
		Job.PackageDesc->PackageFlags = PackageSummary->PackageFlags;
		Job.PackageDesc->NameCount = PackageNameMap.Num();

		const FPackageObjectIndex* ImportMap = reinterpret_cast<const FPackageObjectIndex*>(PackageSummaryData + PackageSummary->ImportMapOffset);
		Job.PackageDesc->Imports.SetNum((PackageSummary->ExportMapOffset - PackageSummary->ImportMapOffset) / sizeof(FPackageObjectIndex));
		for (int32 ImportIndex = 0; ImportIndex < Job.PackageDesc->Imports.Num(); ++ImportIndex)
		{
			FImportDesc& ImportDesc = Job.PackageDesc->Imports[ImportIndex];
			ImportDesc.GlobalImportIndex = ImportMap[ImportIndex];
		}

		const FExportMapEntry* ExportMap = reinterpret_cast<const FExportMapEntry*>(PackageSummaryData + PackageSummary->ExportMapOffset);
		for (int32 ExportIndex = 0; ExportIndex < Job.PackageDesc->Exports.Num(); ++ExportIndex)
		{
			const FExportMapEntry& ExportMapEntry = ExportMap[ExportIndex];
			FExportDesc& ExportDesc = Job.PackageDesc->Exports[ExportIndex];
			ExportDesc.Package = Job.PackageDesc;
			ExportDesc.Name = FName::CreateFromDisplayId(PackageNameMap[ExportMapEntry.ObjectName.GetIndex()], ExportMapEntry.ObjectName.GetNumber());
			ExportDesc.OuterIndex = ExportMapEntry.OuterIndex;
			ExportDesc.ClassIndex = ExportMapEntry.ClassIndex;
			ExportDesc.SuperIndex = ExportMapEntry.SuperIndex;
			ExportDesc.TemplateIndex = ExportMapEntry.TemplateIndex;
			ExportDesc.GlobalImportIndex = ExportMapEntry.GlobalImportIndex;
			ExportDesc.SerialSize = ExportMapEntry.CookedSerialSize;
		}

		const FExportBundleHeader* ExportBundleHeaders = reinterpret_cast<const FExportBundleHeader*>(PackageSummaryData + PackageSummary->ExportBundlesOffset);
		const FExportBundleEntry* ExportBundleEntries = reinterpret_cast<const FExportBundleEntry*>(ExportBundleHeaders + Job.PackageDesc->ExportBundleCount);
		uint64 CurrentExportOffset = PackageSummarySize;
		for (int32 ExportBundleIndex = 0; ExportBundleIndex < Job.PackageDesc->ExportBundleCount; ++ExportBundleIndex)
		{
			TArray<FExportBundleEntryDesc>& ExportBundleDesc = Job.PackageDesc->ExportBundles.AddDefaulted_GetRef();
			const FExportBundleHeader* ExportBundle = ExportBundleHeaders + ExportBundleIndex;
			const FExportBundleEntry* BundleEntry = ExportBundleEntries + ExportBundle->FirstEntryIndex;
			const FExportBundleEntry* BundleEntryEnd = BundleEntry + ExportBundle->EntryCount;
			check(BundleEntry <= BundleEntryEnd);
			while (BundleEntry < BundleEntryEnd)
			{
				FExportBundleEntryDesc& EntryDesc = ExportBundleDesc.AddDefaulted_GetRef();
				EntryDesc.CommandType = FExportBundleEntry::EExportCommandType(BundleEntry->CommandType);
				EntryDesc.LocalExportIndex = BundleEntry->LocalExportIndex;
				EntryDesc.Export = &Job.PackageDesc->Exports[BundleEntry->LocalExportIndex];
				if (BundleEntry->CommandType == FExportBundleEntry::ExportCommandType_Serialize)
				{
					EntryDesc.Export->SerialOffset = CurrentExportOffset;
					CurrentExportOffset += EntryDesc.Export->SerialSize;

					if (bIncludeExportHashes)
					{
						check(EntryDesc.Export->SerialOffset + EntryDesc.Export->SerialSize <= IoBuffer.ValueOrDie().DataSize());
						FSHA1::HashBuffer(IoBuffer.ValueOrDie().Data() + EntryDesc.Export->SerialOffset, EntryDesc.Export->SerialSize, EntryDesc.Export->Hash.Hash);
					}
				}
				++BundleEntry;
			}
		}
	}, EParallelForFlags::Unbalanced);

	UE_LOG(LogIoStore, Display, TEXT("Connecting imports and exports..."));
	TMap<FPackageObjectIndex, FExportDesc*> ExportByGlobalIdMap;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(ConnectImportsAndExports);

		for (FPackageDesc* PackageDesc : Packages)
		{
			for (FExportDesc& ExportDesc : PackageDesc->Exports)
			{
				if (!ExportDesc.GlobalImportIndex.IsNull())
				{
					ExportByGlobalIdMap.Add(ExportDesc.GlobalImportIndex, &ExportDesc);
				}
			}
		}

		ParallelFor(Packages.Num(), [&Packages](int32 Index)
		{
			FPackageDesc* PackageDesc = Packages[Index];
			for (FExportDesc& ExportDesc : PackageDesc->Exports)
			{
				if (ExportDesc.FullName.IsNone())
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(GenerateExportFullName);

					TArray<FExportDesc*> ExportStack;
					
					FExportDesc* Current = &ExportDesc;
					TStringBuilder<2048> FullNameBuilder;
					TCHAR NameBuffer[FName::StringBufferSize];
					for (;;)
					{
						if (!Current->FullName.IsNone())
						{
							Current->FullName.ToString(NameBuffer);
							FullNameBuilder.Append(NameBuffer);
							break;
						}
						ExportStack.Push(Current);
						if (Current->OuterIndex.IsNull())
						{
							PackageDesc->PackageName.ToString(NameBuffer);
							FullNameBuilder.Append(NameBuffer);
							break;
						}
						Current = &PackageDesc->Exports[Current->OuterIndex.Value()];
					}
					while (ExportStack.Num() > 0)
					{
						Current = ExportStack.Pop(false);
						FullNameBuilder.Append(TEXT("/"));
						Current->Name.ToString(NameBuffer);
						FullNameBuilder.Append(NameBuffer);
						Current->FullName = FName(FullNameBuilder);
					}
				}
			}
		}, EParallelForFlags::Unbalanced);

		for (FPackageDesc* PackageDesc : Packages)
		{
			for (FImportDesc& Import : PackageDesc->Imports)
			{
				if (!Import.GlobalImportIndex.IsNull())
				{
					if (Import.GlobalImportIndex.IsPackageImport())
					{
						Import.Export = ExportByGlobalIdMap.FindRef(Import.GlobalImportIndex);
						if (!Import.Export)
						{
							UE_LOG(LogIoStore, Warning, TEXT("Missing import: 0x%llX in package 0x%llX '%s'"), Import.GlobalImportIndex.Value(), PackageDesc->PackageId.ValueForDebugging(), *PackageDesc->PackageName.ToString());
						}
						else
						{
							Import.Name = Import.Export->FullName;
						}
					}
					else
					{
						FScriptObjectDesc* ScriptObjectDesc = ScriptObjectByGlobalIdMap.Find(Import.GlobalImportIndex);
						check(ScriptObjectDesc);
						Import.Name = ScriptObjectDesc->FullName;
					}
				}
			}
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Collecting output packages..."));
	TArray<const FPackageDesc*> OutputPackages;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(CollectOutputPackages);

		if (PackageFilter.IsEmpty())
		{
			OutputPackages.Append(Packages);
		}
		else
		{
			TArray<FString> SplitPackageFilters;
			const TCHAR* Delimiters[] = { TEXT(","), TEXT(" ") };
			PackageFilter.ParseIntoArray(SplitPackageFilters, Delimiters, UE_ARRAY_COUNT(Delimiters), true);

			TArray<FString> PackageNameFilter;
			TSet<FPackageId> PackageIdFilter;
			for (const FString& PackageNameOrId : SplitPackageFilters)
			{
				if (PackageNameOrId.Len() > 0 && FChar::IsDigit(PackageNameOrId[0]))
				{
					uint64 Value;
					LexFromString(Value, *PackageNameOrId);
					PackageIdFilter.Add(*(FPackageId*)(&Value));
				}
				else
				{
					PackageNameFilter.Add(PackageNameOrId);
				}
			}

			TArray<const FPackageDesc*> PackageStack;
			for (const FPackageDesc* PackageDesc : Packages)
			{
				bool bInclude = false;
				if (PackageIdFilter.Contains(PackageDesc->PackageId))
				{
					bInclude = true;
				}
				else
				{
					FString PackageName = PackageDesc->PackageName.ToString();
					for (const FString& Wildcard : PackageNameFilter)
					{
						if (PackageName.MatchesWildcard(Wildcard))
						{
							bInclude = true;
							break;
						}
					}
				}
				if (bInclude)
				{
					PackageStack.Push(PackageDesc);
				}
			}
			TSet<const FPackageDesc*> Visited;
			while (PackageStack.Num() > 0)
			{
				const FPackageDesc* PackageDesc = PackageStack.Pop();
				if (!Visited.Contains(PackageDesc))
				{
					Visited.Add(PackageDesc);
					OutputPackages.Add(PackageDesc);
					for (const FImportDesc& Import : PackageDesc->Imports)
					{
						if (Import.Export && Import.Export->Package)
						{
							PackageStack.Push(Import.Export->Package);
						}
					}
				}
			}
		}
		Algo::Sort(OutputPackages, [](const FPackageDesc* A, const FPackageDesc* B)
			{
				return A->LoadOrder < B->LoadOrder;
			});
	}

	UE_LOG(LogIoStore, Display, TEXT("Generating report..."));

	FOutputDevice* OutputOverride = GWarn;
	FString OutputFilename;
	TUniquePtr<FOutputDeviceFile> OutputBuffer;
	if (!OutPath.IsEmpty())
	{
		OutputBuffer = MakeUnique<FOutputDeviceFile>(*OutPath, true);
		OutputBuffer->SetSuppressEventTag(true);
		OutputOverride = OutputBuffer.Get();
	}
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(GenerateReport);
		TGuardValue<ELogTimes::Type> GuardPrintLogTimes(GPrintLogTimes, ELogTimes::None);
		TGuardValue<bool> GuardPrintLogCategory(GPrintLogCategory, false);
		TGuardValue<bool> GuardPrintLogVerbosity(GPrintLogVerbosity, false);

		auto PackageObjectIndexToString = [&ScriptObjectByGlobalIdMap, &ExportByGlobalIdMap](const FPackageObjectIndex& PackageObjectIndex, bool bIncludeName) -> FString
		{
			if (PackageObjectIndex.IsNull())
			{
				return TEXT("<null>");
			}
			else if (PackageObjectIndex.IsPackageImport())
			{
				FExportDesc* ExportDesc = ExportByGlobalIdMap.FindRef(PackageObjectIndex);
				if (ExportDesc && bIncludeName)
				{
					return FString::Printf(TEXT("0x%llX '%s'"), PackageObjectIndex.Value(), *ExportDesc->FullName.ToString());
				}
				else
				{
					return FString::Printf(TEXT("0x%llX"), PackageObjectIndex.Value());
				}
			}
			else if (PackageObjectIndex.IsScriptImport())
			{
				FScriptObjectDesc* ScriptObjectDesc = ScriptObjectByGlobalIdMap.Find(PackageObjectIndex);
				if (ScriptObjectDesc && bIncludeName)
				{
					return FString::Printf(TEXT("0x%llX '%s'"), PackageObjectIndex.Value(), *ScriptObjectDesc->FullName.ToString());
				}
				else
				{
					return FString::Printf(TEXT("0x%llX"), PackageObjectIndex.Value());
				}
			}
			else if (PackageObjectIndex.IsExport())
			{
				return FString::Printf(TEXT("%d"), PackageObjectIndex.Value());
			}
			else
			{
				return FString::Printf(TEXT("0x%llX"), PackageObjectIndex.Value());
			}
		};

		for (const FContainerDesc* ContainerDesc : Containers)
		{
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("********************************************"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("Container '%s' Summary"), *ContainerDesc->Name.ToString());
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("--------------------------------------------"));

			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t      ContainerId: 0x%llX"), ContainerDesc->Id.Value());
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t       Compressed: %s"), ContainerDesc->bCompressed ? TEXT("Yes") : TEXT("No"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t           Signed: %s"), ContainerDesc->bSigned ? TEXT("Yes") : TEXT("No"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t          Indexed: %s"), ContainerDesc->bIndexed ? TEXT("Yes") : TEXT("No"));
			if (ContainerDesc->bEncrypted)
			{
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\tEncryptionKeyGuid: %s"), *ContainerDesc->EncryptionKeyGuid.ToString());
			}

			if (ContainerDesc->PackageRedirects.Num())
			{
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("--------------------------------------------"));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("Package Redirects"));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("=========="));
				for (const FPackageRedirect& Redirect : ContainerDesc->PackageRedirects)
				{
					OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t*************************"));
					if (!Redirect.Culture.IsNone())
					{
						OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t          Culture: %s"), *Redirect.Culture.ToString());
					}
					OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t           Source: 0x%llX '%s'"), Redirect.Source->PackageId.ValueForDebugging(), *Redirect.Source->PackageName.ToString());
					OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t           Target: 0x%llX '%s'"), Redirect.Target->PackageId.ValueForDebugging(), *Redirect.Target->PackageName.ToString());
				}
			}
		}

		for (const FPackageDesc* PackageDesc : OutputPackages)
		{
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("********************************************"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("Package '%s' Summary"), *PackageDesc->PackageName.ToString());
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("--------------------------------------------"));

			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t        PackageId: 0x%llX"), PackageDesc->PackageId.ValueForDebugging());
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t             Size: %lld"), PackageDesc->Size);
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t        LoadOrder: %d"), PackageDesc->LoadOrder);
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t     PackageFlags: %X"), PackageDesc->PackageFlags);
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t        NameCount: %d"), PackageDesc->NameCount);
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t      ImportCount: %d"), PackageDesc->Imports.Num());
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t      ExportCount: %d"), PackageDesc->Exports.Num());
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\tExportBundleCount: %d"), PackageDesc->ExportBundleCount);

			OutputOverride->Logf(ELogVerbosity::Display, TEXT("--------------------------------------------"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("Locations"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("=========="));
			int32 Index = 0;
			for (const FPackageLocation& Location : PackageDesc->Locations)
			{
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t*************************"));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\tLocation %d: '%s'"), Index++, *Location.Container->Name.ToString());
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t           Offset: %lld"), Location.Offset);
			}

			OutputOverride->Logf(ELogVerbosity::Display, TEXT("--------------------------------------------"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("Imports"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("=========="));
			Index = 0;
			for (const FImportDesc& Import : PackageDesc->Imports)
			{
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t*************************"));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\tImport %d: '%s'"), Index++, *Import.Name.ToString());
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\tGlobalImportIndex: %s"), *PackageObjectIndexToString(Import.GlobalImportIndex, false));
			}

			OutputOverride->Logf(ELogVerbosity::Display, TEXT("--------------------------------------------"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("Exports"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("=========="));
			Index = 0;
			for (const FExportDesc& Export : PackageDesc->Exports)
			{
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t*************************"));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\tExport %d: '%s'"), Index++, *Export.Name.ToString());
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t       OuterIndex: %s"), *PackageObjectIndexToString(Export.OuterIndex, true));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t       ClassIndex: %s"), *PackageObjectIndexToString(Export.ClassIndex, true));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t       SuperIndex: %s"), *PackageObjectIndexToString(Export.SuperIndex, true));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t    TemplateIndex: %s"), *PackageObjectIndexToString(Export.TemplateIndex, true));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\tGlobalImportIndex: %s"), *PackageObjectIndexToString(Export.GlobalImportIndex, false));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t           Offset: %lld"), Export.SerialOffset);
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t             Size: %lld"), Export.SerialSize);
				if (bIncludeExportHashes)
				{
					OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t             Hash: %s"), *Export.Hash.ToString());
				}
			}

			OutputOverride->Logf(ELogVerbosity::Display, TEXT("--------------------------------------------"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("Export Bundles"));
			OutputOverride->Logf(ELogVerbosity::Display, TEXT("=========="));
			Index = 0;
			for (const TArray<FExportBundleEntryDesc>& ExportBundle : PackageDesc->ExportBundles)
			{
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t*************************"));
				OutputOverride->Logf(ELogVerbosity::Display, TEXT("\tExport Bundle %d"), Index++);
				for (const FExportBundleEntryDesc& ExportBundleEntry : ExportBundle)
				{
					if (ExportBundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Create)
					{
						OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t           Create: %d '%s'"), ExportBundleEntry.LocalExportIndex, *ExportBundleEntry.Export->Name.ToString());
					}
					else
					{
						OutputOverride->Logf(ELogVerbosity::Display, TEXT("\t\t        Serialize: %d '%s'"), ExportBundleEntry.LocalExportIndex, *ExportBundleEntry.Export->Name.ToString());
					}
				}
			}
		}
	}

	for (FPackageDesc* PackageDesc : Packages)
	{
		delete PackageDesc;
	}
	for (FContainerDesc* ContainerDesc : Containers)
	{
		delete ContainerDesc;
	}

	return 0;
}

static int32 Diff(
	const FString& SourcePath,
	const FKeyChain& SourceKeyChain,
	const FString& TargetPath,
	const FKeyChain& TargetKeyChain,
	const FString& OutPath)
{
	struct FContainerChunkInfo
	{
		FString ContainerName;
		TMap<FIoChunkId, FIoStoreTocChunkInfo> ChunkInfoById;
		int64 UncompressedContainerSize = 0;
		int64 CompressedContainerSize = 0;
	};

	struct FContainerDiff
	{
		TSet<FIoChunkId> Unmodified;
		TSet<FIoChunkId> Modified;
		TSet<FIoChunkId> Added;
		TSet<FIoChunkId> Removed;
		int64 UnmodifiedCompressedSize = 0;
		int64 ModifiedCompressedSize = 0;
		int64 AddedCompressedSize = 0;
		int64 RemovedCompressedSize = 0;
	};

	using FContainers = TMap<FString, FContainerChunkInfo>;

	auto ReadContainers = [](const FString& Directory, const FKeyChain& KeyChain, FContainers& OutContainers)
	{
		TArray<FString> ContainerFileNames;
		IFileManager::Get().FindFiles(ContainerFileNames, *(Directory / TEXT("*.utoc")), true, false);

		for (const FString& ContainerFileName : ContainerFileNames)
		{
			FString ContainerFilePath = Directory / ContainerFileName;
			UE_LOG(LogIoStore, Display, TEXT("Reading container '%s'"), *ContainerFilePath);

			TUniquePtr<FIoStoreReader> Reader = CreateIoStoreReader(*ContainerFilePath, KeyChain);
			if (!Reader.IsValid())
			{
				UE_LOG(LogIoStore, Warning, TEXT("Failed to read container '%s'"), *ContainerFilePath);
				continue;
			}

			FString ContainerName = FPaths::GetBaseFilename(ContainerFileName);
			FContainerChunkInfo& ContainerChunkInfo = OutContainers.FindOrAdd(ContainerName);
			ContainerChunkInfo.ContainerName = MoveTemp(ContainerName);

			Reader->EnumerateChunks([&ContainerChunkInfo](const FIoStoreTocChunkInfo& ChunkInfo)
			{
				ContainerChunkInfo.ChunkInfoById.Add(ChunkInfo.Id, ChunkInfo);
				ContainerChunkInfo.UncompressedContainerSize += ChunkInfo.Size;
				ContainerChunkInfo.CompressedContainerSize += ChunkInfo.CompressedSize;
				return true;
			});
		}
	};

	auto ComputeDiff = [](const FContainerChunkInfo& SourceContainer, const FContainerChunkInfo& TargetContainer) -> FContainerDiff 
	{
		check(SourceContainer.ContainerName == TargetContainer.ContainerName);

		FContainerDiff ContainerDiff;

		for (const auto& TargetChunkInfo : TargetContainer.ChunkInfoById)
		{
			if (const FIoStoreTocChunkInfo* SourceChunkInfo = SourceContainer.ChunkInfoById.Find(TargetChunkInfo.Key))
			{
				if (SourceChunkInfo->Hash != TargetChunkInfo.Value.Hash)
				{
					ContainerDiff.Modified.Add(TargetChunkInfo.Key);
					ContainerDiff.ModifiedCompressedSize += TargetChunkInfo.Value.CompressedSize;
				}
				else
				{
					ContainerDiff.Unmodified.Add(TargetChunkInfo.Key);
					ContainerDiff.UnmodifiedCompressedSize += TargetChunkInfo.Value.CompressedSize;
				}
			}
			else
			{
				ContainerDiff.Added.Add(TargetChunkInfo.Key);
				ContainerDiff.AddedCompressedSize += TargetChunkInfo.Value.CompressedSize;
			}
		}

		for (const auto& SourceChunkInfo : SourceContainer.ChunkInfoById)
		{
			if (!TargetContainer.ChunkInfoById.Contains(SourceChunkInfo.Key))
			{
				ContainerDiff.Removed.Add(SourceChunkInfo.Key);
				ContainerDiff.RemovedCompressedSize += SourceChunkInfo.Value.CompressedSize;
			}
		}

		return MoveTemp(ContainerDiff);
	};

	FOutputDevice* OutputDevice = GWarn;
	TUniquePtr<FOutputDeviceFile> FileOutputDevice;

	if (!OutPath.IsEmpty())
	{
		UE_LOG(LogIoStore, Error, TEXT("Redirecting output to: '%s'"), *OutPath);

		FileOutputDevice = MakeUnique<FOutputDeviceFile>(*OutPath, true);
		FileOutputDevice->SetSuppressEventTag(true);
		OutputDevice = FileOutputDevice.Get();
	}

	FContainers SourceContainers, TargetContainers;
	TArray<FString> AddedContainers, ModifiedContainers, RemovedContainers;
	TArray<FContainerDiff> ContainerDiffs;

	UE_LOG(LogIoStore, Display, TEXT("Reading source container(s) from '%s':"), *SourcePath);
	ReadContainers(SourcePath, SourceKeyChain, SourceContainers);

	if (!SourceContainers.Num())
	{
		UE_LOG(LogIoStore, Error, TEXT("Failed to read source container(s) from '%s':"), *SourcePath);
		return -1;
	}

	UE_LOG(LogIoStore, Display, TEXT("Reading target container(s) from '%s':"), *TargetPath);
	ReadContainers(TargetPath, TargetKeyChain, TargetContainers);

	if (!TargetContainers.Num())
	{
		UE_LOG(LogIoStore, Error, TEXT("Failed to read target container(s) from '%s':"), *SourcePath);
		return -1;
	}

	for (const auto& TargetContainer : TargetContainers)
	{
		if (SourceContainers.Contains(TargetContainer.Key))
		{
			ModifiedContainers.Add(TargetContainer.Key);
		}
		else
		{
			AddedContainers.Add(TargetContainer.Key);
		}
	}

	for (const auto& SourceContainer : SourceContainers)
	{
		if (!TargetContainers.Contains(SourceContainer.Key))
		{
			RemovedContainers.Add(SourceContainer.Key);
		}
	}

	for (const FString& ModifiedContainer : ModifiedContainers)
	{
		ContainerDiffs.Emplace(ComputeDiff(*SourceContainers.Find(ModifiedContainer), *TargetContainers.Find(ModifiedContainer)));
	}

	OutputDevice->Logf(ELogVerbosity::Display, TEXT(""));
	OutputDevice->Logf(ELogVerbosity::Display, TEXT("------------------------------ Container Diff Summary ------------------------------"));
	OutputDevice->Logf(ELogVerbosity::Display, TEXT("Source path '%s'"), *SourcePath);
	OutputDevice->Logf(ELogVerbosity::Display, TEXT("Target path '%s'"), *TargetPath);

	OutputDevice->Logf(ELogVerbosity::Display, TEXT(""));
	OutputDevice->Logf(ELogVerbosity::Display, TEXT("Source container file(s):"));
	OutputDevice->Logf(ELogVerbosity::Display, TEXT(""));
	OutputDevice->Logf(ELogVerbosity::Display, TEXT("%-40s %15s %15s"), TEXT("Container"), TEXT("Size (MB)"), TEXT("Chunks"));
	OutputDevice->Logf(ELogVerbosity::Display, TEXT("-------------------------------------------------------------------------"));

	{
		uint64 TotalSourceBytes = 0;
		uint64 TotalSourceChunks = 0;

		for (const auto& NameContainerPair : SourceContainers)
		{
			const FContainerChunkInfo& SourceContainer = NameContainerPair.Value;
			OutputDevice->Logf(ELogVerbosity::Display, TEXT("%-40s %15.2lf %15d"), *SourceContainer.ContainerName, double(SourceContainer.CompressedContainerSize) / 1024.0 / 1024.0, SourceContainer.ChunkInfoById.Num());

			TotalSourceBytes += SourceContainer.CompressedContainerSize;
			TotalSourceChunks += SourceContainer.ChunkInfoById.Num();
		}

		OutputDevice->Logf(ELogVerbosity::Display, TEXT("-------------------------------------------------------------------------"));
		OutputDevice->Logf(ELogVerbosity::Display, TEXT("%-40s %15.2lf %15d"), *FString::Printf(TEXT("Total of %d container file(s)"), SourceContainers.Num()), double(TotalSourceBytes) / 1024.0 / 1024.0, TotalSourceChunks);
	}

	{
		uint64 TotalTargetBytes = 0;
		uint64 TotalTargetChunks = 0;
		uint64 TotalUnmodifiedChunks = 0;
		uint64 TotalUnmodifiedCompressedBytes = 0;
		uint64 TotalModifiedChunks = 0;
		uint64 TotalModifiedCompressedBytes = 0;
		uint64 TotalAddedChunks = 0;
		uint64 TotalAddedCompressedBytes = 0;
		uint64 TotalRemovedChunks = 0;
		uint64 TotalRemovedCompressedBytes = 0;

		if (ModifiedContainers.Num())
		{
			OutputDevice->Logf(ELogVerbosity::Display, TEXT(""));
			OutputDevice->Logf(ELogVerbosity::Display, TEXT("Target container file(s):"));
			OutputDevice->Logf(ELogVerbosity::Display, TEXT(""));
			OutputDevice->Logf(ELogVerbosity::Display, TEXT("%-40s %15s %15s %25s %25s %25s %25s %25s %25s %25s %25s"), TEXT("Container"), TEXT("Size (MB)"), TEXT("Chunks"), TEXT("Unmodified"), TEXT("Unmodified (MB)"), TEXT("Modified"), TEXT("Modified (MB)"), TEXT("Added"), TEXT("Added (MB)"), TEXT("Removed"), TEXT("Removed (MB)"));
			OutputDevice->Logf(ELogVerbosity::Display, TEXT("----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------"));

			for (int32 Idx = 0; Idx < ModifiedContainers.Num(); Idx++)
			{
				const FContainerChunkInfo& SourceContainer = *SourceContainers.Find(ModifiedContainers[Idx]);
				const FContainerChunkInfo& TargetContainer = *TargetContainers.Find(ModifiedContainers[Idx]);
				const FContainerDiff& Diff = ContainerDiffs[Idx];

				const int32 NumChunks = TargetContainer.ChunkInfoById.Num();
				const int32 NumSourceChunks = SourceContainer.ChunkInfoById.Num();

				OutputDevice->Logf(ELogVerbosity::Display, TEXT("%-40s %15s %15d %25s %25s %25s %25s %25s %25s %25s %25s"),
					*TargetContainer.ContainerName,
					*FString::Printf(TEXT("%.2lf"),
						double(TargetContainer.CompressedContainerSize) / 1024.0 / 1024.0),
					NumChunks,
					*FString::Printf(TEXT("%d (%.2lf%%)"),
						Diff.Unmodified.Num(),
						100.0 * (double(Diff.Unmodified.Num()) / double(NumChunks))),
					*FString::Printf(TEXT("%.2lf (%.2lf%%)"),
						double(Diff.UnmodifiedCompressedSize) / 1024.0 / 1024.0,
						100.0 * (Diff.UnmodifiedCompressedSize) / double(TargetContainer.CompressedContainerSize)),
					*FString::Printf(TEXT("%d (%.2lf%%)"),
						Diff.Modified.Num(),
						100.0 * (double(Diff.Modified.Num()) / double(NumChunks))),
					*FString::Printf(TEXT("%.2lf (%.2lf%%)"),
						double(Diff.ModifiedCompressedSize) / 1024.0 / 1024.0,
						100.0 * (Diff.ModifiedCompressedSize) / double(TargetContainer.CompressedContainerSize)),
					*FString::Printf(TEXT("%d (%.2lf%%)"),
						Diff.Added.Num(),
						100.0 * (double(Diff.Added.Num()) / double(NumChunks))),
					*FString::Printf(TEXT("%.2lf (%.2lf%%)"),
						double(Diff.AddedCompressedSize) / 1024.0 / 1024.0,
						100.0 * (Diff.AddedCompressedSize) / double(TargetContainer.CompressedContainerSize)),
					*FString::Printf(TEXT("%d/%d (%.2lf%%)"),
						Diff.Removed.Num(),
						NumSourceChunks,
						100.0 * (double(Diff.Removed.Num()) / double(NumSourceChunks))),
					*FString::Printf(TEXT("%.2lf (%.2lf%%)"),
						double(Diff.RemovedCompressedSize) / 1024.0 / 1024.0,
						100.0 * (Diff.RemovedCompressedSize) / double(SourceContainer.CompressedContainerSize)));

				TotalTargetBytes += TargetContainer.CompressedContainerSize;
				TotalTargetChunks += NumChunks;
				TotalUnmodifiedChunks += Diff.Unmodified.Num();
				TotalUnmodifiedCompressedBytes += Diff.UnmodifiedCompressedSize;
				TotalModifiedChunks += Diff.Modified.Num();
				TotalModifiedCompressedBytes += Diff.ModifiedCompressedSize;
				TotalAddedChunks += Diff.Added.Num();
				TotalAddedCompressedBytes += Diff.AddedCompressedSize;
				TotalRemovedChunks += Diff.Removed.Num();
				TotalRemovedCompressedBytes += Diff.RemovedCompressedSize;
			}
		}

		if (AddedContainers.Num())
		{
			for (const FString& AddedContainer : AddedContainers)
			{
				const FContainerChunkInfo& TargetContainer = *TargetContainers.Find(AddedContainer);
				OutputDevice->Logf(ELogVerbosity::Display, TEXT("+%-39s %15.2lf %15d %25s %25s %25s %25s %25s %25s %25s %25s"),
					*TargetContainer.ContainerName,
					double(TargetContainer.CompressedContainerSize) / 1024.0 / 1024.0,
					TargetContainer.ChunkInfoById.Num(),
					TEXT("-"), TEXT("-"), TEXT("-"), TEXT("-"), TEXT("-"), TEXT("-"), TEXT("-"), TEXT("-"));

				TotalTargetBytes += TargetContainer.CompressedContainerSize;
				TotalTargetChunks += TargetContainer.ChunkInfoById.Num();
			}
		}

		OutputDevice->Logf(ELogVerbosity::Display, TEXT("----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------"));
		OutputDevice->Logf(ELogVerbosity::Display, TEXT("%-40s %15.2lf %15d %25d %25.2f %25d %25.2f %25d %25.2f %25d %25.2f"),
			*FString::Printf(TEXT("Total of %d container file(s)"), TargetContainers.Num()),
			double(TotalTargetBytes) / 1024.0 / 1024.0,
			TotalTargetChunks,
			TotalUnmodifiedChunks,
			double(TotalUnmodifiedCompressedBytes) / 1024.0 / 1024.0,
			TotalModifiedChunks,
			double(TotalModifiedCompressedBytes) / 1024.0 / 1024.0,
			TotalAddedChunks,
			double(TotalAddedCompressedBytes) / 1024.0 / 1024.0,
			TotalRemovedChunks,
			double(TotalRemovedCompressedBytes) / 1024.0 / 1024.0);
	}

	return 0;
}

static bool ParsePakResponseFile(const TCHAR* FilePath, TArray<FContainerSourceFile>& OutFiles)
{
	TArray<FString> ResponseFileContents;
	if (!FFileHelper::LoadFileToStringArray(ResponseFileContents, FilePath))
	{
		UE_LOG(LogIoStore, Error, TEXT("Failed to read response file '%s'."), *FilePath);
		return false;
	}

	for (const FString& ResponseLine : ResponseFileContents)
	{
		TArray<FString> SourceAndDest;
		TArray<FString> Switches;

		FString NextToken;
		const TCHAR* ResponseLinePtr = *ResponseLine;
		while (FParse::Token(ResponseLinePtr, NextToken, false))
		{
			if ((**NextToken == TCHAR('-')))
			{
				new(Switches) FString(NextToken.Mid(1));
			}
			else
			{
				new(SourceAndDest) FString(NextToken);
			}
		}

		if (SourceAndDest.Num() == 0)
		{
			continue;
		}

		if (SourceAndDest.Num() != 2)
		{
			UE_LOG(LogIoStore, Error, TEXT("Invalid line in response file '%s'."), *ResponseLine);
			return false;
		}

		FPaths::NormalizeFilename(SourceAndDest[0]);

		FContainerSourceFile& FileEntry = OutFiles.AddDefaulted_GetRef();
		FileEntry.NormalizedPath = MoveTemp(SourceAndDest[0]);
		FileEntry.DestinationPath = MoveTemp(SourceAndDest[1]);

		for (int32 Index = 0; Index < Switches.Num(); ++Index)
		{
			if (Switches[Index] == TEXT("compress"))
			{
				FileEntry.bNeedsCompression = true;
			}
			if (Switches[Index] == TEXT("encrypt"))
			{
				FileEntry.bNeedsEncryption = true;
			}
		}
	}
	return true;
}

static bool ParsePakOrderFile(const TCHAR* FilePath, FFileOrderMap& Map, uint64& InOutNumEntries)
{
	TArray<FString> OrderFileContents;
	if (!FFileHelper::LoadFileToStringArray(OrderFileContents, FilePath))
	{
		UE_LOG(LogIoStore, Error, TEXT("Failed to read order file '%s'."), *FilePath);
		return false;
	}

	Map.Name = FPaths::GetCleanFilename(FilePath);
	uint64 LocalNumEntries = InOutNumEntries;
	UE_LOG(LogIoStore, Display, TEXT("Order file %s (short name %s) starting at global index %llu"), FilePath, *Map.Name, LocalNumEntries);
	for (const FString& OrderLine : OrderFileContents)
	{
		const TCHAR* OrderLinePtr = *OrderLine;
		FString Path;
		if (!FParse::Token(OrderLinePtr, Path, false))
		{
			UE_LOG(LogIoStore, Error, TEXT("Invalid line in order file '%s'."), *OrderLine);
			return false;
		}
		FString PackageName;
		if (!FPackageName::TryConvertFilenameToLongPackageName(Path, PackageName, nullptr))
		{
			continue;;
		}

		FName PackageFName(MoveTemp(PackageName));
		if (!Map.PackageNameToOrder.Contains(PackageFName))
		{
			Map.PackageNameToOrder.Emplace(PackageFName, LocalNumEntries++);
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Order file %s (short name %s) contained %llu valid entries"), FilePath, *Map.Name, LocalNumEntries - InOutNumEntries);
	InOutNumEntries = LocalNumEntries;
	return true;
}

class FCookedFileVisitor : public IPlatformFile::FDirectoryStatVisitor
{
	FCookedFileStatMap& CookedFileStatMap;
	FContainerSourceSpec* ContainerSpec = nullptr;
	bool bFileRegions;

public:
	FCookedFileVisitor(FCookedFileStatMap& InCookedFileSizes, FContainerSourceSpec* InContainerSpec, bool bInFileRegions)
		: CookedFileStatMap(InCookedFileSizes)
		, ContainerSpec(InContainerSpec)
		, bFileRegions(bInFileRegions)
	{}

	FCookedFileVisitor(FCookedFileStatMap& InFileSizes, bool bInFileRegions)
		: CookedFileStatMap(InFileSizes)
		, bFileRegions(bInFileRegions)
	{}

	virtual bool Visit(const TCHAR* FilenameOrDirectory, const FFileStatData& StatData)
	{
		// Should match FCookedFileStatData::EFileExt
		static const TCHAR* Extensions[] = { TEXT("umap"), TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("uptnl"), TEXT("m.ubulk") };
		static const int32 NumPackageExtensions = 2;
		static const int32 UExpExtensionIndex = 2;

		if (StatData.bIsDirectory)
		{
			return true;
		}

		const TCHAR* Extension = FCString::Strrchr(FilenameOrDirectory, '.');
		if (!Extension || *(++Extension) == TEXT('\0'))
		{
			return true;
		}

		int32 ExtIndex = 0;
		if (0 == FCString::Stricmp(Extension, Extensions[3]))
		{
			ExtIndex = 3;
			if (0 == FCString::Stricmp(Extension - 3, TEXT(".m.ubulk")))
			{
				ExtIndex = 5;
			}
		}
		else
		{
			for (ExtIndex = 0; ExtIndex < UE_ARRAY_COUNT(Extensions); ++ExtIndex)
			{
				if (0 == FCString::Stricmp(Extension, Extensions[ExtIndex]))
					break;
			}
		}

		if (ExtIndex >= UE_ARRAY_COUNT(Extensions))
		{
			return true;
		}

		FString Path = FilenameOrDirectory;
		FPaths::NormalizeFilename(Path);

		if (ContainerSpec && ExtIndex != UExpExtensionIndex)
		{
			FContainerSourceFile& FileEntry = ContainerSpec->SourceFiles.AddDefaulted_GetRef();
			FileEntry.NormalizedPath = Path;
		}

		// Read the matching regions file, if it exists.
		TUniquePtr<FArchive> RegionsFile;
		if (bFileRegions)
		{
			RegionsFile.Reset(IFileManager::Get().CreateFileReader(*(Path + FFileRegion::RegionsFileExtension)));
		}

		FCookedFileStatData& CookedFileStatData = CookedFileStatMap.Add(MoveTemp(Path));
		CookedFileStatData.FileSize = StatData.FileSize;
		CookedFileStatData.FileExt = FCookedFileStatData::EFileExt(ExtIndex);
		if (ExtIndex < NumPackageExtensions)
		{
			CookedFileStatData.FileType = FCookedFileStatData::PackageHeader;
		}
		else if (ExtIndex == UExpExtensionIndex)
		{
			CookedFileStatData.FileType = FCookedFileStatData::PackageData;
		}
		else
		{
			CookedFileStatData.FileType = FCookedFileStatData::BulkData;
		}

		if (RegionsFile.IsValid())
		{
			FFileRegion::SerializeFileRegions(*RegionsFile.Get(), CookedFileStatData.FileRegions);
		}

		return true;
	}
};

static bool ParseSizeArgument(const TCHAR* CmdLine, const TCHAR* Argument, uint64& OutSize, uint64 DefaultSize = 0)
{
	FString SizeString;
	if (FParse::Value(CmdLine, Argument, SizeString) && FParse::Value(CmdLine, Argument, OutSize))
	{
		if (SizeString.EndsWith(TEXT("MB")))
		{
			OutSize *= 1024*1024;
		}
		else if (SizeString.EndsWith(TEXT("KB")))
		{
			OutSize *= 1024;
		}
		return true;
	}
	else
	{
		OutSize = DefaultSize;
		return false;
	}
}

int32 CreateIoStoreContainerFiles(const TCHAR* CmdLine)
{
	IOSTORE_CPU_SCOPE(CreateIoStoreContainerFiles);

	UE_LOG(LogIoStore, Display, TEXT("==================== IoStore Utils ===================="));

	FIoStoreArguments Arguments;

	LoadKeyChain(FCommandLine::Get(), Arguments.KeyChain);

	if (FParse::Param(FCommandLine::Get(), TEXT("sign")))
	{
		Arguments.bSign = true;
	}

	UE_LOG(LogIoStore, Display, TEXT("Container signing - %s"), Arguments.bSign ? TEXT("ENABLED") : TEXT("DISABLED"));

	Arguments.bCreateDirectoryIndex = !FParse::Param(FCommandLine::Get(), TEXT("NoDirectoryIndex"));
	UE_LOG(LogIoStore, Display, TEXT("Directory index - %s"), Arguments.bCreateDirectoryIndex  ? TEXT("ENABLED") : TEXT("DISABLED"));

	FString PatchReferenceCryptoKeysFilename;
	FKeyChain PatchKeyChain;
	if (FParse::Value(FCommandLine::Get(), TEXT("PatchCryptoKeys="), PatchReferenceCryptoKeysFilename))
	{
		KeyChainUtilities::LoadKeyChainFromFile(PatchReferenceCryptoKeysFilename, Arguments.PatchKeyChain);
	}

	uint64 OrderMapStartIndex = 0;
	FString GameOrderFileStr;
	if (FParse::Value(FCommandLine::Get(), TEXT("GameOrder="), GameOrderFileStr, false))
	{
		TArray<FString> GameOrderFilePaths;
		GameOrderFileStr.ParseIntoArray(GameOrderFilePaths, TEXT(","), true);
		bool bMerge = false;
		for (FString& PrimaryOrderFile : GameOrderFilePaths)
		{
			FFileOrderMap OrderMap;
			if (!ParsePakOrderFile(*PrimaryOrderFile, OrderMap, OrderMapStartIndex))
			{
				return -1;
			}
			Arguments.OrderMaps.Add(OrderMap);
		}
	}

	FString CookerOrderFileStr;
	if (FParse::Value(FCommandLine::Get(), TEXT("CookerOrder="), CookerOrderFileStr, false))
	{
		TArray<FString> CookerOrderFilePaths;
		CookerOrderFileStr.ParseIntoArray(CookerOrderFilePaths, TEXT(","), true);
		bool bMerge = false;
		for (FString& SecondOrderFile : CookerOrderFilePaths)
		{
			FFileOrderMap OrderMap;
			if (!ParsePakOrderFile(*SecondOrderFile, OrderMap, OrderMapStartIndex))
			{
				return -1;
			}
			Arguments.OrderMaps.Add(OrderMap);
		}
	}

	FIoStoreWriterSettings GeneralIoWriterSettings { DefaultCompressionMethod, DefaultCompressionBlockSize, false };
	GeneralIoWriterSettings.bEnableCsvOutput = FParse::Param(CmdLine, TEXT("-csvoutput"));

	TArray<FName> CompressionFormats;
	FString DesiredCompressionFormats;
	if (FParse::Value(CmdLine, TEXT("-compressionformats="), DesiredCompressionFormats) ||
		FParse::Value(CmdLine, TEXT("-compressionformat="), DesiredCompressionFormats))
	{
		TArray<FString> Formats;
		DesiredCompressionFormats.ParseIntoArray(Formats, TEXT(","));
		for (FString& Format : Formats)
		{
			// look until we have a valid format
			FName FormatName = *Format;

			if (FCompression::IsFormatValid(FormatName))
			{
				GeneralIoWriterSettings.CompressionMethod = FormatName;
				break;
			}
		}

		if (GeneralIoWriterSettings.CompressionMethod == NAME_None)
		{
			UE_LOG(LogIoStore, Warning, TEXT("Failed to find desired compression format(s) '%s'. Using falling back to '%s'"),
				*DesiredCompressionFormats, *DefaultCompressionMethod.ToString());
		}
		else
		{
			UE_LOG(LogIoStore, Display, TEXT("Using compression format '%s'"), *GeneralIoWriterSettings.CompressionMethod.ToString());
		}
	}

	ParseSizeArgument(CmdLine, TEXT("-alignformemorymapping="), GeneralIoWriterSettings.MemoryMappingAlignment, DefaultMemoryMappingAlignment);
	ParseSizeArgument(CmdLine, TEXT("-compressionblocksize="), GeneralIoWriterSettings.CompressionBlockSize, DefaultCompressionBlockSize);
		
	GeneralIoWriterSettings.CompressionBlockAlignment = DefaultCompressionBlockAlignment;
	
	uint64 BlockAlignment = 0;
	if (ParseSizeArgument(CmdLine, TEXT("-blocksize="), BlockAlignment))
	{
		GeneralIoWriterSettings.CompressionBlockAlignment = BlockAlignment;
	}
	
	uint64 PatchPaddingAlignment = 0;
	if (ParseSizeArgument(CmdLine, TEXT("-patchpaddingalign="), PatchPaddingAlignment))
	{
		if (PatchPaddingAlignment < GeneralIoWriterSettings.CompressionBlockAlignment)
		{
			GeneralIoWriterSettings.CompressionBlockAlignment = PatchPaddingAlignment;
		}
	}
	
	// Temporary, this command-line allows us to explicitly override the value otherwise shared between pak building and iostore
	uint64 IOStorePatchPaddingAlignment = 0;
	if (ParseSizeArgument(CmdLine, TEXT("-iostorepatchpaddingalign="), IOStorePatchPaddingAlignment))
	{
		GeneralIoWriterSettings.CompressionBlockAlignment = IOStorePatchPaddingAlignment;
	}

	uint64 MaxPartitionSize = 0;
	if (ParseSizeArgument(CmdLine, TEXT("-maxPartitionSize="), MaxPartitionSize))
	{
		GeneralIoWriterSettings.MaxPartitionSize = MaxPartitionSize;
	}

	UE_LOG(LogIoStore, Display, TEXT("Using memory mapping alignment '%ld'"), GeneralIoWriterSettings.MemoryMappingAlignment);
	UE_LOG(LogIoStore, Display, TEXT("Using compression block size '%ld'"), GeneralIoWriterSettings.CompressionBlockSize);
	UE_LOG(LogIoStore, Display, TEXT("Using compression block alignment '%ld'"), GeneralIoWriterSettings.CompressionBlockAlignment);
	UE_LOG(LogIoStore, Display, TEXT("Using max partition size '%lld'"), GeneralIoWriterSettings.MaxPartitionSize);

	FParse::Value(CmdLine, TEXT("-MetaOutputDirectory="), Arguments.MetaOutputDir);
	FParse::Value(CmdLine, TEXT("-MetaInputDirectory="), Arguments.MetaInputDir);

	FString CommandListFile;
	if (FParse::Value(FCommandLine::Get(), TEXT("Commands="), CommandListFile))
	{
		UE_LOG(LogIoStore, Display, TEXT("Using command list file: '%s'"), *CommandListFile);
		TArray<FString> Commands;
		if (!FFileHelper::LoadFileToStringArray(Commands, *CommandListFile))
		{
			UE_LOG(LogIoStore, Error, TEXT("Failed to read command list file '%s'."), *CommandListFile);
			return -1;
		}

		Arguments.Containers.Reserve(Commands.Num());
		for (const FString& Command : Commands)
		{
			FContainerSourceSpec& ContainerSpec = Arguments.Containers.AddDefaulted_GetRef();

			if (!FParse::Value(*Command, TEXT("Output="), ContainerSpec.OutputPath))
			{
				UE_LOG(LogIoStore, Error, TEXT("Output argument missing from command '%s'"), *Command);
				return -1;
			}
			ContainerSpec.OutputPath = FPaths::ChangeExtension(ContainerSpec.OutputPath, TEXT(""));

			FString ContainerName;
			if (FParse::Value(*Command, TEXT("ContainerName="), ContainerName))
			{
				ContainerSpec.Name = FName(ContainerName);
			}

			FString PatchSourceWildcard;
			if (FParse::Value(*Command, TEXT("PatchSource="), PatchSourceWildcard))
			{
				IFileManager::Get().FindFiles(ContainerSpec.PatchSourceContainerFiles, *PatchSourceWildcard, true, false);
				FString PatchSourceContainersDirectory = FPaths::GetPath(*PatchSourceWildcard);
				for (FString& PatchSourceContainerFile : ContainerSpec.PatchSourceContainerFiles)
				{
					PatchSourceContainerFile = PatchSourceContainersDirectory / PatchSourceContainerFile;
					FPaths::NormalizeFilename(PatchSourceContainerFile);
				}
			}

			ContainerSpec.bGenerateDiffPatch = FParse::Param(*Command, TEXT("GenerateDiffPatch"));

			FParse::Value(*Command, TEXT("PatchTarget="), ContainerSpec.PatchTargetFile);

			FString ResponseFilePath;
			if (FParse::Value(*Command, TEXT("ResponseFile="), ResponseFilePath))
			{
				if (!ParsePakResponseFile(*ResponseFilePath, ContainerSpec.SourceFiles))
				{
					UE_LOG(LogIoStore, Error, TEXT("Failed to parse Pak response file '%s'"), *ResponseFilePath);
					return -1;
				}

				FString EncryptionKeyOverrideGuidString;
				if (FParse::Value(*Command, TEXT("EncryptionKeyOverrideGuid="), EncryptionKeyOverrideGuidString))
				{
					FGuid::Parse(EncryptionKeyOverrideGuidString, ContainerSpec.EncryptionKeyOverrideGuid);
				}
			}
		}
	}

	if (FParse::Value(FCommandLine::Get(), TEXT("BasedOnReleaseVersionPath="), Arguments.BasedOnReleaseVersionPath))
	{
		UE_LOG(LogIoStore, Display, TEXT("Based on release version path: '%s'"), *Arguments.BasedOnReleaseVersionPath);
	}

	if (FParse::Value(FCommandLine::Get(), TEXT("DLCFile="), Arguments.DLCPluginPath))
	{
		Arguments.DLCName = FPaths::GetBaseFilename(*Arguments.DLCPluginPath);
		Arguments.bRemapPluginContentToGame = FParse::Param(FCommandLine::Get(), TEXT("RemapPluginContentToGame"));

		UE_LOG(LogIoStore, Display, TEXT("DLC: '%s'"), *Arguments.DLCPluginPath);
		UE_LOG(LogIoStore, Display, TEXT("Remapping plugin content to game: '%s'"), Arguments.bRemapPluginContentToGame ? TEXT("True") : TEXT("False"));

		if (Arguments.BasedOnReleaseVersionPath.IsEmpty())
		{
			UE_LOG(LogIoStore, Error, TEXT("Based on release version path is needed for DLC"));
			return -1;
		}

		FString DevelopmentAssetRegistryPath = FPaths::Combine(Arguments.BasedOnReleaseVersionPath, TEXT("Metadata/DevelopmentAssetRegistry.bin"));

		bool bAssetRegistryLoaded = false;
		FArrayReader SerializedAssetData;
		if (FFileHelper::LoadFileToArray(SerializedAssetData, *DevelopmentAssetRegistryPath))
		{
			FAssetRegistrySerializationOptions Options;
			if (Arguments.ReleaseAssetRegistry.Serialize(SerializedAssetData, Options))
			{
				UE_LOG(LogIoStore, Display, TEXT("Loaded asset registry '%s'"), *DevelopmentAssetRegistryPath);
				bAssetRegistryLoaded = true;

				TArray<FName> PackageNames;
				Arguments.ReleaseAssetRegistry.GetPackageNames(PackageNames);
				Arguments.ReleasedPackages.PackageNames.Reserve(PackageNames.Num());
				Arguments.ReleasedPackages.PackageIdToName.Reserve(PackageNames.Num());

				for (FName PackageName : PackageNames)
				{
					Arguments.ReleasedPackages.PackageNames.Add(PackageName);
					Arguments.ReleasedPackages.PackageIdToName.Add(FPackageId::FromName(PackageName), PackageName);
				}
			}
		}

		if (!bAssetRegistryLoaded)
		{
			UE_LOG(LogIoStore, Warning, TEXT("Failed to load Asset registry '%s'. Needed to verify DLC package names"), *DevelopmentAssetRegistryPath);
		}
	}

	if (FParse::Value(FCommandLine::Get(), TEXT("CreateGlobalContainer="), Arguments.GlobalContainerPath))
	{
		Arguments.GlobalContainerPath = FPaths::ChangeExtension(Arguments.GlobalContainerPath, TEXT(""));
	}

	if (Arguments.ShouldCreateContainers())
	{
		FString TargetPlatform;
		if (FParse::Value(FCommandLine::Get(), TEXT("TargetPlatform="), TargetPlatform))
		{
			UE_LOG(LogIoStore, Display, TEXT("Using target platform '%s'"), *TargetPlatform);
			ITargetPlatformManagerModule& TPM = GetTargetPlatformManagerRef();
			Arguments.TargetPlatform = TPM.FindTargetPlatform(TargetPlatform);
			if (!Arguments.TargetPlatform)
			{
				UE_LOG(LogIoStore, Error, TEXT("Invalid TargetPlatform: '%s'"), *TargetPlatform);
				return 1;
			}
		}
		else
		{
			UE_LOG(LogIoStore, Error, TEXT("TargetPlatform must be specified"));
			return 1;
		}

		// Now that we have the target platform we need to error out if LegacyBulkDataOffsets is enabled for it
		{
			FConfigFile PlatformEngineIni;
			FConfigCacheIni::LoadLocalIniFile(PlatformEngineIni, TEXT("Engine"), true, *Arguments.TargetPlatform->IniPlatformName());

			bool bLegacyBulkDataOffsets = false;
			PlatformEngineIni.GetBool(TEXT("Core.System"), TEXT("LegacyBulkDataOffsets"), bLegacyBulkDataOffsets);

			if (bLegacyBulkDataOffsets)
			{
				UE_LOG(LogIoStore, Error, TEXT("'LegacyBulkDataOffsets' is enabled for the target platform '%s', this needs to be disabled and the data recooked in order for the IoStore to work"), *Arguments.TargetPlatform->IniPlatformName());
				return 1;
			}
		}

		if (!FParse::Value(FCommandLine::Get(), TEXT("CookedDirectory="), Arguments.CookedDir))
		{
			UE_LOG(LogIoStore, Error, TEXT("CookedDirectory must be specified"));
			return 1;
		}

		for (const FContainerSourceSpec& Container : Arguments.Containers)
		{
			if (Container.Name.IsNone())
			{
				UE_LOG(LogIoStore, Error, TEXT("ContainerName argument missing for container '%s'"), *Container.OutputPath);
				return -1;
			}
		}

		// Enable file region metadata if required by the target platform.
		bool bFileRegions = Arguments.TargetPlatform->SupportsFeature(ETargetPlatformFeatures::CookFileRegionMetadata);
		GeneralIoWriterSettings.bEnableFileRegions = bFileRegions;

		UE_LOG(LogIoStore, Display, TEXT("Searching for cooked assets in folder '%s'"), *Arguments.CookedDir);
		FCookedFileVisitor CookedFileVistor(Arguments.CookedFileStatMap, nullptr, bFileRegions);
		IFileManager::Get().IterateDirectoryStatRecursively(*Arguments.CookedDir, CookedFileVistor);
		UE_LOG(LogIoStore, Display, TEXT("Found '%d' files"), Arguments.CookedFileStatMap.Num());

		int32 ReturnValue = CreateTarget(Arguments, GeneralIoWriterSettings);
		if (ReturnValue != 0)
		{
			return ReturnValue;
		}
	}
	else if (FParse::Param(FCommandLine::Get(), TEXT("CreateContentPatch")))
	{
		for (const FContainerSourceSpec& Container : Arguments.Containers)
		{
			if (Container.PatchTargetFile.IsEmpty())
			{
				UE_LOG(LogIoStore, Error, TEXT("PatchTarget argument missing for container '%s'"), *Container.OutputPath);
				return -1;
			}
		}

		int32 ReturnValue = CreateContentPatch(Arguments, GeneralIoWriterSettings);
		if (ReturnValue != 0)
		{
			return ReturnValue;
		}
	}
	else
	{
		FString ContainerPathOrWildcard;
		if (FParse::Value(FCommandLine::Get(), TEXT("List="), ContainerPathOrWildcard))
		{
			FString CsvPath;
			if (!FParse::Value(FCommandLine::Get(), TEXT("csv="), CsvPath))
			{
				UE_LOG(LogIoStore, Error, TEXT("Incorrect arguments. Expected: -list=<ContainerFile> -csv=<path>"));
			}

			return ListContainer(Arguments, ContainerPathOrWildcard, CsvPath);
		}
		else if (FParse::Value(FCommandLine::Get(), TEXT("Describe="), ContainerPathOrWildcard))
		{
			FString PackageFilter;
			FParse::Value(FCommandLine::Get(), TEXT("PackageFilter="), PackageFilter);
			FString OutPath;
			FParse::Value(FCommandLine::Get(), TEXT("DumpToFile="), OutPath);
			bool bIncludeExportHashes = FParse::Param(FCommandLine::Get(), TEXT("IncludeExportHashes"));
			return Describe(ContainerPathOrWildcard, Arguments.KeyChain, PackageFilter, OutPath, bIncludeExportHashes);
		}
		else if (FParse::Param(FCommandLine::Get(), TEXT("Diff")))
		{
			FString SourcePath, TargetPath, OutPath;
			FKeyChain SourceKeyChain, TargetKeyChain;

			if (!FParse::Value(FCommandLine::Get(), TEXT("Source="), SourcePath))
			{
				UE_LOG(LogIoStore, Error, TEXT("Incorrect arguments. Expected: -Diff -Source=<Path> -Target=<path>"));
				return -1;
			}

			if (!IFileManager::Get().DirectoryExists(*SourcePath))
			{
				UE_LOG(LogIoStore, Error, TEXT("Source directory '%s' doesn't exist"), *SourcePath);
				return -1;
			}

			if (!FParse::Value(FCommandLine::Get(), TEXT("Target="), TargetPath))
			{
				UE_LOG(LogIoStore, Error, TEXT("Incorrect arguments. Expected: -Diff -Source=<Path> -Target=<path>"));
			}

			if (!IFileManager::Get().DirectoryExists(*TargetPath))
			{
				UE_LOG(LogIoStore, Error, TEXT("Target directory '%s' doesn't exist"), *TargetPath);
				return -1;
			}

			FParse::Value(FCommandLine::Get(), TEXT("DumpToFile="), OutPath);

			FString CryptoKeysCacheFilename;
			if (FParse::Value(CmdLine, TEXT("SourceCryptoKeys="), CryptoKeysCacheFilename))
			{
				UE_LOG(LogIoStore, Display, TEXT("Parsing source crypto keys from '%s'"), *CryptoKeysCacheFilename);
				KeyChainUtilities::LoadKeyChainFromFile(CryptoKeysCacheFilename, SourceKeyChain);
			}

			if (FParse::Value(CmdLine, TEXT("TargetCryptoKeys="), CryptoKeysCacheFilename))
			{
				UE_LOG(LogIoStore, Display, TEXT("Parsing target crypto keys from '%s'"), *CryptoKeysCacheFilename);
				KeyChainUtilities::LoadKeyChainFromFile(CryptoKeysCacheFilename, TargetKeyChain);
			}
			
			return Diff(SourcePath, SourceKeyChain, TargetPath, TargetKeyChain, OutPath);
		}
		else
		{
			UE_LOG(LogIoStore, Display, TEXT("Usage:"));
			UE_LOG(LogIoStore, Display, TEXT(" -List=</path/to/[container.utoc|*.utoc]> -CSV=<list.csv> [-CryptoKeys=</path/to/crypto.json>]"));
			UE_LOG(LogIoStore, Display, TEXT(" -Describe=</path/to/global.utoc> [-PackageFilter=<PackageName>] [-DumpToFile=<describe.txt>] [-CryptoKeys=</path/to/crypto.json>]"));
			return -1;
		}
	}

	return 0;
}

#endif