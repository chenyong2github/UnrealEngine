// Copyright Epic Games, Inc. All Rights Reserved.

#include "IoStoreUtilities.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
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
#include "Serialization/BulkDataManifest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/BufferWriter.h"
#include "Serialization/LargeMemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/AsyncLoading2.h"
#include "Serialization/ArrayReader.h"
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

//PRAGMA_DISABLE_OPTIMIZATION

IMPLEMENT_MODULE(FDefaultModuleImpl, IoStoreUtilities);

DEFINE_LOG_CATEGORY_STATIC(LogIoStore, Log, All);

#define IOSTORE_CPU_SCOPE(NAME) TRACE_CPUPROFILER_EVENT_SCOPE(IoStore##NAME);
#define IOSTORE_CPU_SCOPE_DATA(NAME, DATA) TRACE_CPUPROFILER_EVENT_SCOPE(IoStore##NAME);

#define OUTPUT_CHUNKID_DIRECTORY 0
#define OUTPUT_NAMEMAP_CSV 0
#define OUTPUT_DEBUG_PACKAGE_EXPORT_BUNDLES 0

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

class FNameMapBuilder
{
public:
	void SetNameMapType(FMappedName::EType InNameMapType)
	{
		NameMapType = InNameMapType;
	}

	void AddName(const FName& Name)
	{
		const FNameEntryId ComparisonIndex = Name.GetComparisonIndex();
		const FNameEntryId DisplayIndex = Name.GetDisplayIndex();
		NameMap.Add(DisplayIndex);
		int32 Index = NameMap.Num();
		NameIndices.Add(ComparisonIndex, Index);
	}

	void MarkNamesAsReferenced(const TArray<FName>& Names, TArray<int32>& OutNameIndices)
	{
		for (const FName& Name : Names)
		{
			const FNameEntryId ComparisonIndex = Name.GetComparisonIndex();
			const FNameEntryId DisplayIndex = Name.GetDisplayIndex();
			int32& Index = NameIndices.FindOrAdd(ComparisonIndex);
			if (Index == 0)
			{
				NameMap.Add(DisplayIndex);
				Index = NameMap.Num();
			}

			OutNameIndices.Add(Index - 1);
		}
	}

	void MarkNameAsReferenced(const FName& Name)
	{
		const FNameEntryId ComparisonIndex = Name.GetComparisonIndex();
		const FNameEntryId DisplayIndex = Name.GetDisplayIndex();
		int32& Index = NameIndices.FindOrAdd(ComparisonIndex);
		if (Index == 0)
		{
			NameMap.Add(DisplayIndex);
			Index = NameMap.Num();
		}
#if OUTPUT_NAMEMAP_CSV
		// debug counts
		{
			const int32 Number = Name.GetNumber();
			TTuple<int32, int32, int32>& Counts = DebugNameCounts.FindOrAdd(ComparisonIndex);

			if (Number == 0)
			{
				++Counts.Get<0>();
			}
			else
			{
				++Counts.Get<1>();
				if (Number > Counts.Get<2>())
				{
					Counts.Get<2>() = Number;
				}
			}
		}
#endif
	}

	FMappedName MapName(const FName& Name) const
	{
		const FNameEntryId Id = Name.GetComparisonIndex();
		const int32* Index = NameIndices.Find(Id);
		check(Index);
		return FMappedName::Create(*Index - 1, Name.GetNumber(), NameMapType);
	}

	const TArray<FNameEntryId>& GetNameMap() const
	{
		return NameMap;
	}

	friend FArchive& operator<<(FArchive& Ar, FNameMapBuilder& NameMapBuilder)
	{
		if (Ar.IsSaving())
		{
			int32 NameCount = NameMapBuilder.NameMap.Num();
			Ar << NameCount;
			for (FNameEntryId NameEntryId : NameMapBuilder.NameMap)
			{
				const FNameEntry* NameEntry = FName::GetEntry(NameEntryId);
				NameEntry->Write(Ar);
			}
		}
		else
		{
			int32 NameCount;
			Ar << NameCount;
			for (int32 NameIndex = 0; NameIndex < NameCount; ++NameIndex)
			{
				FNameEntrySerialized NameEntrySerialized(ENAME_LinkerConstructor);
				Ar << NameEntrySerialized;
				FName Name(NameEntrySerialized);
				NameMapBuilder.NameMap.Add(Name.GetDisplayIndex());
				NameMapBuilder.NameIndices.Add(Name.GetComparisonIndex(), NameIndex + 1);
			}
		}
		return Ar;
	}

#if OUTPUT_NAMEMAP_CSV
	void SaveCsv(const FString& CsvFilePath)
	{
		{
			TUniquePtr<FArchive> CsvArchive(IFileManager::Get().CreateFileWriter(*CsvFilePath));
			if (CsvArchive)
			{
				TCHAR Name[FName::StringBufferSize];
				ANSICHAR Line[MAX_SPRINTF + FName::StringBufferSize];
				ANSICHAR Header[] = "Length\tMaxNumber\tNumberCount\tBaseCount\tTotalCount\tFName\n";
				CsvArchive->Serialize(Header, sizeof(Header) - 1);
				for (auto& Counts : DebugNameCounts)
				{
					const int32 NameLen = FName::CreateFromDisplayId(Counts.Key, 0).ToString(Name);
					FCStringAnsi::Sprintf(Line, "%d\t%d\t%d\t%d\t%d\t",
						NameLen, Counts.Value.Get<2>(), Counts.Value.Get<1>(), Counts.Value.Get<0>(), Counts.Value.Get<0>() + Counts.Value.Get<1>());
					ANSICHAR* L = Line + FCStringAnsi::Strlen(Line);
					const TCHAR* N = Name;
					while (*N)
					{
						*L++ = CharCast<ANSICHAR,TCHAR>(*N++);
					}
					*L++ = '\n';
					CsvArchive.Get()->Serialize(Line, L - Line);
				}
			}
		}
	}
#endif

	void Empty()
	{
		NameIndices.Empty();
		NameMap.Empty();
#if OUTPUT_NAMEMAP_CSV
		DebugNameCounts.Empty();
#endif
	}
private:
	TMap<FNameEntryId, int32> NameIndices;
	TArray<FNameEntryId> NameMap;
#if OUTPUT_NAMEMAP_CSV
	TMap<FNameEntryId, TTuple<int32,int32,int32>> DebugNameCounts; // <Number0Count,OtherNumberCount,MaxNumber>
#endif
	FMappedName::EType NameMapType = FMappedName::EType::Package;
};

class FNameReaderProxyArchive
	: public FArchiveProxy
{
	const TArray<FNameEntryId>& NameMap;

public:
	using FArchiveProxy::FArchiveProxy;

	FNameReaderProxyArchive(FArchive& InAr, const TArray<FNameEntryId>& InNameMap)
		: FArchiveProxy(InAr)
		, NameMap(InNameMap)
	{ 
		// Replicate the filter editor only state of the InnerArchive as FArchiveProxy will
		// not intercept it.
		FArchive::SetFilterEditorOnly(InAr.IsFilterEditorOnly());
	}

	FArchive& operator<<(FName& Name)
	{
		int32 NameIndex, Number;
		InnerArchive << NameIndex << Number;

		if (!NameMap.IsValidIndex(NameIndex))
		{
			UE_LOG(LogStreaming, Fatal, TEXT("Bad name index %i/%i"), NameIndex, NameMap.Num());
		}

		const FNameEntryId MappedName = NameMap[NameIndex];
		Name = FName::CreateFromDisplayId(MappedName, Number);

		return *this;
	}
};

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

struct FPackage;
struct FContainerTargetSpec;

struct FContainerTargetFile
{
	FContainerTargetSpec* ContainerTarget = nullptr;
	FPackage* Package = nullptr;
	FString NormalizedSourcePath;
	FString TargetPath;
	FString DestinationPath;
	uint64 SourceSize = 0;
	uint64 IdealOrder = 0;
	FIoChunkId ChunkId;
	TArray<uint8> PackageHeaderData;
	TArray<int32> NameIndices;
	FNameMapBuilder* NameMapBuilder = nullptr;
	bool bIsBulkData = false;
	bool bIsOptionalBulkData = false;
	bool bIsMemoryMappedBulkData = false;
	bool bForceUncompressed = false;

	int64 UGraphSize = 0;
	int64 NameMapSize = 0;
	int64 ImportMapSize = 0;
	int64 ExportMapSize = 0;
	int64 ExportBundlesHeaderSize = 0;

	uint64 HeaderSerialSize = 0;

	TArray<FFileRegion> FileRegions;
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
	TMap<FName, uint64> GameOrderMap;
	TMap<FName, uint64> CookerOrderMap;
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
	FContainerHeader Header;
	FName Name;
	FGuid EncryptionKeyGuid;
	FString OutputPath;
	FIoStoreWriter* IoStoreWriter;
	TArray<FContainerTargetFile> TargetFiles;
	TUniquePtr<FIoStoreEnvironment> IoStoreEnv;
	TArray<TUniquePtr<FIoStoreReader>> PatchSourceReaders;
	FNameMapBuilder LocalNameMapBuilder;
	FNameMapBuilder* NameMapBuilder = &LocalNameMapBuilder;
	EIoContainerFlags ContainerFlags = EIoContainerFlags::None;
	uint32 PackageCount = 0;
	bool bUseLocalNameMap = false;
	bool bGenerateDiffPatch = false;
};

struct FPackageAssetData
{
	TArray<FObjectImport> ObjectImports;
	TArray<FObjectExport> ObjectExports;
	TArray<FPackageIndex> PreloadDependencies;
};

struct FPackage;
using FPackageNameMap = TMap<FName, FPackage*>;
using FPackageIdMap = TMap<FPackageId, FPackage*>;
using FSourceToLocalizedPackageMultimap = TMultiMap<FPackage*, FPackage*>;
using FLocalizedToSourceImportIndexMap = TMap<FPackageObjectIndex, FPackageObjectIndex>;

static constexpr TCHAR L10NString[] = TEXT("/L10N/");
static constexpr TCHAR ScriptPrefix[] = TEXT("/Script/");

// modified copy from PakFileUtilities
static FString RemapLocalizationPathIfNeeded(const FString& Path, FString* OutRegion)
{
	static constexpr int32 L10NPrefixLength = sizeof(L10NString) / sizeof(TCHAR) - 1;

	int32 BeginL10NOffset = Path.Find(L10NString, ESearchCase::IgnoreCase);
	if (BeginL10NOffset >= 0)
	{
		int32 EndL10NOffset = BeginL10NOffset + L10NPrefixLength;
		int32 NextSlashIndex = Path.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, EndL10NOffset);
		int32 RegionLength = NextSlashIndex - EndL10NOffset;
		if (RegionLength >= 2)
		{
			FString NonLocalizedPath = Path.Mid(0, BeginL10NOffset) + Path.Mid(NextSlashIndex);
			if (OutRegion)
			{
				*OutRegion = Path.Mid(EndL10NOffset, RegionLength);
				OutRegion->ToLowerInline();
			}
			return NonLocalizedPath;
		}
	}
	return Path;
}

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

enum EPreloadDependencyType
{
	PreloadDependencyType_Create,
	PreloadDependencyType_Serialize,
};

struct FArc
{
	uint32 FromNodeIndex;
	uint32 ToNodeIndex;

	bool operator==(const FArc& Other) const
	{
		return ToNodeIndex == Other.ToNodeIndex && FromNodeIndex == Other.FromNodeIndex;
	}
};

struct FExportGraphNode;

struct FExportBundle
{
	TArray<FExportGraphNode*> Nodes;
	uint32 LoadOrder;
};

struct FPackageGraphNode
{
	FPackage* Package = nullptr;
	mutable bool bTemporaryMark = false;
	mutable bool bPermanentMark = false;
};

class FPackageGraph
{
public:
	FPackageGraph()
	{

	}

	~FPackageGraph()
	{
		for (FPackageGraphNode* Node : Nodes)
		{
			delete Node;
		}
	}

	FPackageGraphNode* AddNode(FPackage* Package)
	{
		FPackageGraphNode* Node = new FPackageGraphNode();
		Node->Package = Package;
		Nodes.Add(Node);
		return Node;
	}

	void AddImportDependency(FPackageGraphNode* FromNode, FPackageGraphNode* ToNode)
	{
		Edges.Add(FromNode, ToNode);
	}

	TArray<FPackage*> TopologicalSort() const;

private:
	TArray<FPackageGraphNode*> Nodes;
	TMultiMap<FPackageGraphNode*, FPackageGraphNode*> Edges;
};

struct FExportGraphNode
{
	FPackage* Package;
	FExportBundleEntry BundleEntry;
	TSet<FExportGraphNode*> ExternalDependencies;
	TSet<FPackageId> BaseGamePackageDependencies;
	uint64 NodeIndex;
};

class FExportGraph
{
public:
	FExportGraph(int32 NumExports, int32 NumPreloadDependencies)
	{
		Nodes.Reserve(NumExports * 2);
		Edges.Reserve(NumExports + NumPreloadDependencies);
	}

	~FExportGraph()
	{
		for (FExportGraphNode* Node : Nodes)
		{
			delete Node;
		}
	}

	FExportGraphNode* AddNode(FPackage* Package, const FExportBundleEntry& BundleEntry)
	{
		FExportGraphNode* Node = new FExportGraphNode();
		Node->Package = Package;
		Node->BundleEntry = BundleEntry;
		Node->NodeIndex = Nodes.Num();
		Nodes.Add(Node);
		return Node;
	}

	void AddInternalDependency(FExportGraphNode* FromNode, FExportGraphNode* ToNode)
	{
		AddEdge(FromNode, ToNode);
	}

	void AddExternalDependency(FExportGraphNode* FromNode, FExportGraphNode* ToNode)
	{
		AddEdge(FromNode, ToNode);
		ToNode->ExternalDependencies.Add(FromNode);
	}

	TArray<FExportGraphNode*> ComputeLoadOrder(const TArray<FPackage*>& Packages) const;

private:
	void AddEdge(FExportGraphNode* FromNode, FExportGraphNode* ToNode)
	{
		Edges.Add(FromNode, ToNode);
	}

	TArray<FExportGraphNode*> TopologicalSort() const;

	TArray<FExportGraphNode*> Nodes;
	TMultiMap<FExportGraphNode*, FExportGraphNode*> Edges;
};

struct FPackage
{
	FName Name;
	FName SourcePackageName; // for localized packages
	FString FileName;
	FPackageId GlobalPackageId;
	FString Region; // for localized packages
	FPackageId SourceGlobalPackageId; // for localized packages
	FPackageId RedirectedPackageId;
	uint32 PackageFlags = 0;
	uint32 CookedHeaderSize = 0;
	int32 NameCount = 0;
	int32 ImportCount = 0;
	int32 PreloadDependencyCount = 0;
	int32 ExportCount = 0;
	int32 ImportIndexOffset = -1;
	int32 ExportIndexOffset = -1;
	int32 PreloadIndexOffset = -1;
	int64 UExpSize = 0;
	int64 UAssetSize = 0;
	int64 SummarySize = 0;
	uint64 ExportsSerialSize = 0;
	bool bIsLocalizedAndConformed = false;

	TArray<FPackage*> ImportedPackages;
	TArray<FPackageId> ImportedPackageIds;

	TArray<FName> SummaryNames;
	FNameMapBuilder LocalNameMapBuilder;
	
	TArray<FPackageObjectIndex> Imports;
	TArray<int32> Exports;
	TMap<FPackageId, TArray<FArc>> ExternalArcs;
	
	TArray<FExportBundle> ExportBundles;
	TMap<FExportGraphNode*, uint32> ExportBundleMap;

	TArray<FExportGraphNode*> CreateExportNodes;
	TArray<FExportGraphNode*> SerializeExportNodes;

	TArray<FExportGraphNode*> NodesWithNoIncomingEdges;
	FPackageGraphNode* Node = nullptr;

	uint64 DiskLayoutOrder = MAX_uint64;
};

struct FCircularImportChain
{
	TArray<FName> SortedNames;
	TArray<FPackage*> Packages;
	uint32 Hash = 0;

	FCircularImportChain()
	{
		Packages.Reserve(128);
	}

	void Add(FPackage* Package)
	{
		Packages.Add(Package);
	}

	void Pop()
	{
		Packages.Pop();
	}

	int32 Num()
	{
		return Packages.Num();
	}

	void SortAndGenerateHash()
	{
		SortedNames.Empty(Packages.Num());
		for (FPackage* Package : Packages)
		{
			SortedNames.Emplace(Package->Name);
		}
		SortedNames.Sort(FNameLexicalLess());
		Hash = CityHash32((char*)SortedNames.GetData(), SortedNames.Num() * SortedNames.GetTypeSize());
	}

	FString ToString()
	{
		FString Result = FString::Printf(TEXT("%d:%u: "), SortedNames.Num(), Hash);
		for (const FName& Name : SortedNames)
		{
			Result.Append(Name.ToString());
			Result.Append(TEXT(" -> "));
		}
		Result.Append(SortedNames[0].ToString());
		return Result;
	}

	bool operator==(const FCircularImportChain& Other) const
	{
		return Hash == Other.Hash && SortedNames == Other.SortedNames;
	}

	friend FORCEINLINE uint32 GetTypeHash(const FCircularImportChain& In)
	{
		return In.Hash;
	}
};

TArray<FPackage*> FPackageGraph::TopologicalSort() const
{
	TMap<const FPackageGraphNode*, TArray<const FPackageGraphNode*>> SortedEdges;
	for (const auto& KV : Edges)
	{
		const FPackageGraphNode* Source = KV.Key;
		const FPackageGraphNode* Target = KV.Value;
		TArray<const FPackageGraphNode*>& SourceArray = SortedEdges.FindOrAdd(Source);
		SourceArray.Add(Target);
	}
	for (auto& KV : SortedEdges)
	{
		TArray<const FPackageGraphNode*>& SourceArray = KV.Value;
		Algo::Sort(SourceArray, [](const FPackageGraphNode* A, const FPackageGraphNode* B)
		{
			return A->Package->GlobalPackageId < B->Package->GlobalPackageId;
		});
	}

	TArray<FPackage*> Result;
	Result.Reserve(Nodes.Num());
	
	struct
	{
		void Visit(const FPackageGraphNode* Node)
		{
			if (Node->bPermanentMark || Node->bTemporaryMark)
			{
				return;
			}
			Node->bTemporaryMark = true;
			TArray<const FPackageGraphNode*>* TargetNodes = Edges.Find(Node);
			if(TargetNodes)
			{
				for (const FPackageGraphNode* ToNode : *TargetNodes)
				{
					Visit(ToNode);
				}
			}
			Node->bTemporaryMark = false;
			Node->bPermanentMark = true;
			Result.Add(Node->Package);
		}

		TMap<const FPackageGraphNode*, TArray<const FPackageGraphNode*>>& Edges;
		TArray<FPackage*>& Result;

	} Visitor{ SortedEdges, Result };

	for (FPackageGraphNode* Node : Nodes)
	{
		Visitor.Visit(Node);
	}
	check(Result.Num() == Nodes.Num());

	Algo::Reverse(Result);
	return Result;
}

TArray<FExportGraphNode*> FExportGraph::ComputeLoadOrder(const TArray<FPackage*>& Packages) const
{
	IOSTORE_CPU_SCOPE(ComputeLoadOrder);
	FPackageGraph PackageGraph;
	{
		for (FPackage* Package : Packages)
		{
			Package->Node = PackageGraph.AddNode(Package);
		}
		for (FPackage* Package : Packages)
		{
			for (FPackage* ImportedPackage : Package->ImportedPackages)
			{
				PackageGraph.AddImportDependency(ImportedPackage->Node, Package->Node);
			}
		}
	}

	TArray<FPackage*> SortedPackages = PackageGraph.TopologicalSort();
	
	int32 NodeCount = Nodes.Num();
	TArray<uint32> NodesIncomingEdgeCount;
	NodesIncomingEdgeCount.AddZeroed(NodeCount);

	TMultiMap<FExportGraphNode*, FExportGraphNode*> EdgesCopy = Edges;
	for (auto& KV : Edges)
	{
		FExportGraphNode* ToNode = KV.Value;
		++NodesIncomingEdgeCount[ToNode->NodeIndex];
	}

	TArray<FExportGraphNode*> LoadOrder;
	LoadOrder.Reserve(NodeCount);
	
	auto NodeSorter = [](const FExportGraphNode& A, const FExportGraphNode& B)
	{
		if (A.BundleEntry.LocalExportIndex == B.BundleEntry.LocalExportIndex)
		{
			return A.BundleEntry.CommandType < B.BundleEntry.CommandType;
		}
		return A.BundleEntry.LocalExportIndex < B.BundleEntry.LocalExportIndex;
	};

	for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
	{
		if (NodesIncomingEdgeCount[NodeIndex] == 0)
		{
			FExportGraphNode* Node = Nodes[NodeIndex];
			Node->Package->NodesWithNoIncomingEdges.HeapPush(Node, NodeSorter);
		}
	}
	while (LoadOrder.Num() < NodeCount)
	{
		for (FPackage* Package : SortedPackages)
		{
			while (Package->NodesWithNoIncomingEdges.Num() > 0)
			{
				FExportGraphNode* RemovedNode;
				Package->NodesWithNoIncomingEdges.HeapPop(RemovedNode, NodeSorter, false);
				LoadOrder.Add(RemovedNode);
				for (auto EdgeIt = EdgesCopy.CreateKeyIterator(RemovedNode); EdgeIt; ++EdgeIt)
				{
					FExportGraphNode* ToNode = EdgeIt.Value();
					if (--NodesIncomingEdgeCount[ToNode->NodeIndex] == 0)
					{
						ToNode->Package->NodesWithNoIncomingEdges.HeapPush(ToNode, NodeSorter);
					}
					EdgeIt.RemoveCurrent();
				}
			}
		}
	}

	return LoadOrder;
}

static void AddInternalExportArc(FExportGraph& ExportGraph, FPackage& Package, uint32 FromExportIndex, EPreloadDependencyType FromPhase, uint32 ToExportIndex, EPreloadDependencyType ToPhase)
{
	FExportGraphNode* FromNode = FromPhase == PreloadDependencyType_Create ? Package.CreateExportNodes[FromExportIndex] : Package.SerializeExportNodes[FromExportIndex];
	FExportGraphNode* ToNode = ToPhase == PreloadDependencyType_Create ? Package.CreateExportNodes[ToExportIndex] : Package.SerializeExportNodes[ToExportIndex];
	ExportGraph.AddInternalDependency(FromNode, ToNode);
}

static void AddExternalExportArc(FExportGraph& ExportGraph, FPackage& FromPackage, uint32 FromExportIndex, EPreloadDependencyType FromPhase, FPackage& ToPackage, uint32 ToExportIndex, EPreloadDependencyType ToPhase)
{
	FExportGraphNode* FromNode = FromPhase == PreloadDependencyType_Create ? FromPackage.CreateExportNodes[FromExportIndex] : FromPackage.SerializeExportNodes[FromExportIndex];
	FExportGraphNode* ToNode = ToPhase == PreloadDependencyType_Create ? ToPackage.CreateExportNodes[ToExportIndex] : ToPackage.SerializeExportNodes[ToExportIndex];
	ExportGraph.AddExternalDependency(FromNode, ToNode);
}

static void AddBaseGamePackageArc(FExportGraph& ExportGraph, FPackageId FromPackageId, FPackage& ToPackage, uint32 ToExportIndex, EPreloadDependencyType ToPhase)
{
	FExportGraphNode* ToNode = ToPhase == PreloadDependencyType_Create ? ToPackage.CreateExportNodes[ToExportIndex] : ToPackage.SerializeExportNodes[ToExportIndex];
	ToNode->BaseGamePackageDependencies.Add(FromPackageId);
}

static void AddUniqueExternalBundleArc(FPackageId FromPackageId, uint32 FromBundleIndex, FPackage& ToPackage, uint32 ToBundleIndex)
{
	TArray<FArc>& ExternalArcs = ToPackage.ExternalArcs.FindOrAdd(FromPackageId);
	ExternalArcs.AddUnique({ FromBundleIndex, ToBundleIndex });
}

static void BuildBundles(FExportGraph& ExportGraph, const TArray<FPackage*>& Packages)
{
	IOSTORE_CPU_SCOPE(BuildBundles)
	UE_LOG(LogIoStore, Display, TEXT("Building bundles..."));

	TArray<FExportGraphNode*> ExportLoadOrder = ExportGraph.ComputeLoadOrder(Packages);
	FPackage* LastPackage = nullptr;
	uint32 BundleLoadOrder = 0;
	for (FExportGraphNode* Node : ExportLoadOrder)
	{
		FPackage* Package = Node->Package;
		check(Package);
		if (!Package)
		{
			continue;
		}

		uint32 BundleIndex;
		FExportBundle* Bundle;
		if (Package != LastPackage)
		{
			BundleIndex = Package->ExportBundles.Num();
			Bundle = &Package->ExportBundles.AddDefaulted_GetRef();
			Bundle->LoadOrder = BundleLoadOrder++;
			LastPackage = Package;
		}
		else
		{
			BundleIndex = Package->ExportBundles.Num() - 1;
			Bundle = &Package->ExportBundles[BundleIndex];
		}
		for (FExportGraphNode* ExternalDependency : Node->ExternalDependencies)
		{
			uint32* FindDependentBundleIndex = ExternalDependency->Package->ExportBundleMap.Find(ExternalDependency);
			check(FindDependentBundleIndex);
			check(*FindDependentBundleIndex < uint32(ExternalDependency->Package->ExportBundles.Num()));
			AddUniqueExternalBundleArc(ExternalDependency->Package->GlobalPackageId, *FindDependentBundleIndex, *Package, BundleIndex);
		}
		for (FPackageId FromPackageId : Node->BaseGamePackageDependencies)
		{
			AddUniqueExternalBundleArc(FromPackageId, MAX_uint32, *Package, BundleIndex);
		}
		Bundle->Nodes.Add(Node);
		Package->ExportBundleMap.Add(Node, BundleIndex);
	}
}

static void AssignPackagesDiskOrder(
	const TArray<FPackage*>& Packages,
	const TMap<FName, uint64> GameOrderMap,
	const TMap<FName, uint64>& CookerOrderMap)
{
	struct FCluster
	{
		TArray<FPackage*> Packages;
	};

	TArray<FCluster*> Clusters;
	TSet<FPackage*> AssignedPackages;
	TArray<FPackage*> ProcessStack;

	struct FPackageAndOrder
	{
		FPackage* Package;
		uint64 GameOpenOrder;
		uint64 CookerOpenOrder;

		bool operator<(const FPackageAndOrder& Other) const
		{
			if (GameOpenOrder != Other.GameOpenOrder)
			{
				return GameOpenOrder < Other.GameOpenOrder;
			}
			if (CookerOpenOrder != Other.CookerOpenOrder)
			{
				return CookerOpenOrder < Other.CookerOpenOrder;
			}
			// Fallback to reverse bundle order (so that packages are considered before their imports)
			return Package->ExportBundles[0].LoadOrder > Other.Package->ExportBundles[0].LoadOrder;
		}
	};

	TArray<FPackageAndOrder> SortedPackages;
	SortedPackages.Reserve(Packages.Num());
	for (FPackage* Package : Packages)
	{
		if (!Package->ExportBundles.Num())
		{
			continue;
		}
		FPackageAndOrder& Entry = SortedPackages.AddDefaulted_GetRef();
		Entry.Package = Package;
		const uint64* FindGameOpenOrder = GameOrderMap.Find(Package->Name);
		Entry.GameOpenOrder = FindGameOpenOrder ? *FindGameOpenOrder : MAX_uint64;
		const uint64* FindCookerOpenOrder = CookerOrderMap.Find(Package->Name);
		/*if (!FindCookerOpenOrder)
		{
			UE_LOG(LogIoStore, Warning, TEXT("Missing cooker order for package: %s"), *Package->Name.ToString());
		}*/
		Entry.CookerOpenOrder = FindCookerOpenOrder ? *FindCookerOpenOrder : MAX_uint64;
	}
	bool bHasGameOrder = true;
	bool bHasCookerOrder = true;
	int32 LastAssignedCount = 0;
	Algo::Sort(SortedPackages);
	for (FPackageAndOrder& Entry : SortedPackages)
	{
		if (bHasGameOrder && Entry.GameOpenOrder == MAX_uint64)
		{
			UE_LOG(LogIoStore, Display, TEXT("Ordered %d/%d packages using game open order"), AssignedPackages.Num(), Packages.Num());
			LastAssignedCount = AssignedPackages.Num();
			bHasGameOrder = false;
		}
		if (!bHasGameOrder && bHasCookerOrder && Entry.CookerOpenOrder == MAX_uint64)
		{
			UE_LOG(LogIoStore, Display, TEXT("Ordered %d/%d packages using cooker open order"), AssignedPackages.Num() - LastAssignedCount, Packages.Num() - LastAssignedCount);
			LastAssignedCount = AssignedPackages.Num();
			bHasCookerOrder = false;
		}
		if (!AssignedPackages.Contains(Entry.Package))
		{
			FCluster* Cluster = new FCluster();
			Clusters.Add(Cluster);
			ProcessStack.Push(Entry.Package);

			while (ProcessStack.Num())
			{
				FPackage* PackageToProcess = ProcessStack.Pop(false);
				if (!AssignedPackages.Contains(PackageToProcess))
				{
					AssignedPackages.Add(PackageToProcess);
					if (PackageToProcess->ExportBundles.Num())
					{
						Cluster->Packages.Add(PackageToProcess);
					}
					for (FPackage* ImportedPackage : PackageToProcess->ImportedPackages)
					{
						ProcessStack.Push(ImportedPackage);
					}
				}
			}
		}
	}
	UE_LOG(LogIoStore, Display, TEXT("Ordered %d packages using fallback bundle order"), AssignedPackages.Num() - LastAssignedCount);

	check(AssignedPackages.Num() == Packages.Num());
	
	for (FCluster* Cluster : Clusters)
	{
		Algo::Sort(Cluster->Packages, [](const FPackage* A, const FPackage* B)
		{
			return A->ExportBundles[0].LoadOrder < B->ExportBundles[0].LoadOrder;
		});
	}

	uint64 LayoutIndex = 0;
	for (FCluster* Cluster : Clusters)
	{
		for (FPackage* Package : Cluster->Packages)
		{
			Package->DiskLayoutOrder = LayoutIndex++;
		}
		delete Cluster;
	}
}

static void CreateDiskLayout(
	const TArray<FContainerTargetSpec*>& ContainerTargets,
	const TArray<FPackage*>& Packages,
	const TMap<FName, uint64> PackageOrderMap,
	const TMap<FName, uint64>& CookerOrderMap)
{
	IOSTORE_CPU_SCOPE(CreateDiskLayout);

	AssignPackagesDiskOrder(Packages, PackageOrderMap, CookerOrderMap);

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
				return B->bIsMemoryMappedBulkData;
			}
			if (A->bIsBulkData != B->bIsBulkData)
			{
				return B->bIsBulkData;
			}

			return A->Package->DiskLayoutOrder < B->Package->DiskLayoutOrder;
		});
		uint64 IdealOrder = 0;
		for (FContainerTargetFile* TargetFile : SortedTargetFiles)
		{
			TargetFile->IdealOrder = IdealOrder++;
		}
	}
}

static EIoChunkType BulkdataTypeToChunkIdType(FPackageStoreBulkDataManifest::EBulkdataType Type)
{
	switch (Type)
	{
	case FPackageStoreBulkDataManifest::EBulkdataType::Normal:
		return EIoChunkType::BulkData;
	case FPackageStoreBulkDataManifest::EBulkdataType::Optional:
		return EIoChunkType::OptionalBulkData;
	case FPackageStoreBulkDataManifest::EBulkdataType::MemoryMapped:
		return EIoChunkType::MemoryMappedBulkData;
	default:
		UE_LOG(LogIoStore, Error, TEXT("Invalid EBulkdataType (%d) found!"), Type);
		return EIoChunkType::Invalid;
	}
}

struct FScriptObjectData
{
	FName ObjectName;
	FString FullName;
	FPackageObjectIndex GlobalIndex;
	FPackageObjectIndex OuterIndex;
	FPackageObjectIndex CDOClassIndex;

	friend FArchive& operator<<(FArchive& Ar, FScriptObjectData& Data)
	{
		FString ObjectNameStr;
		if (Ar.IsSaving())
		{
			ObjectNameStr = Data.ObjectName.ToString();
			Ar << ObjectNameStr;
		}
		else
		{
			Ar << ObjectNameStr;
			Data.ObjectName = FName(*ObjectNameStr);
		}
		Ar << Data.FullName;
		Ar << Data.GlobalIndex;
		Ar << Data.OuterIndex;
		Ar << Data.CDOClassIndex;
		return Ar;
	}
};

struct FExportObjectData
{
	FName ObjectName;
	FString FullName;
	int32 GlobalIndex = -1;
	int32 SourceIndex = -1;
	FPackageObjectIndex GlobalImportIndex;
	FPackageObjectIndex OuterIndex;
	FPackageObjectIndex ClassIndex;
	FPackageObjectIndex SuperIndex;
	FPackageObjectIndex TemplateIndex;
	EObjectFlags ObjectFlags = RF_NoFlags;

	FPackage* Package = nullptr;
	FExportGraphNode* CreateNode = nullptr;
	FExportGraphNode* SerializeNode = nullptr;

	bool IsPublicExport() const
	{
		return !!(ObjectFlags & RF_Public);
	}

	friend FArchive& operator<<(FArchive& Ar, FExportObjectData& Data)
	{
		FString ObjectNameStr;
		if (Ar.IsSaving())
		{
			ObjectNameStr = Data.ObjectName.ToString();
			Ar << ObjectNameStr;
		}
		else
		{
			Ar << ObjectNameStr;
			Data.ObjectName = FName(*ObjectNameStr);
		}
		Ar << Data.FullName;
		Ar << Data.GlobalIndex;
		Ar << Data.SourceIndex;
		Ar << Data.SourceIndex;
		Ar << Data.GlobalImportIndex;
		Ar << Data.OuterIndex;
		Ar << Data.ClassIndex;
		Ar << Data.TemplateIndex;
		Ar << (uint32&)Data.ObjectFlags;
		return Ar;
	}
};

using FImportObjectsByFullName = TMap<FString, FPackageObjectIndex>;
using FImportObjectsById = TMap<FPackageObjectIndex, FPackageObjectIndex>;
using FExportObjectsByFullName = TMap<FString, int32>;

using FGlobalScriptObjects = TMap<FPackageObjectIndex, FScriptObjectData>;
using FGlobalExportObjects = TArray<FExportObjectData>;

struct FGlobalPackageData
{
	FGlobalScriptObjects ScriptObjects;
	FGlobalExportObjects ExportObjects;
	TMap<FPackageObjectIndex, int32> PublicExportIndices;
	FImportObjectsByFullName ImportsByFullName;
	FExportObjectsByFullName ExportsByFullName;

	void Reserve(int32 TotalExportCount)
	{
		const int32 EstimatedPublicExportObjectCount = TotalExportCount / 10;
		const int32 EstimatedScriptObjectCount = 64000;
		ExportObjects.Reserve(TotalExportCount);
		ExportsByFullName.Reserve(TotalExportCount);
		PublicExportIndices.Reserve(EstimatedPublicExportObjectCount);
		ScriptObjects.Reserve(EstimatedScriptObjectCount);
		ImportsByFullName.Reserve(EstimatedScriptObjectCount + EstimatedPublicExportObjectCount);
	}

	const FExportObjectData* FindPublicExport(FPackageObjectIndex Index) const
	{
		check(Index.IsPackageImport());
		if (const int32* GlobalExportIndex = PublicExportIndices.Find(Index))
		{
			return &ExportObjects[*GlobalExportIndex];
		}
		return nullptr;
	}

	FName GetObjectName(FPackageObjectIndex Index, const TArray<int32>* PackageExportIndices) const
	{
		if (Index.IsScriptImport())
		{
			const FScriptObjectData* ScriptObjectData = ScriptObjects.Find(Index);
			check(ScriptObjectData);
			return ScriptObjectData->ObjectName;
		}
		if (Index.IsPackageImport())
		{
			const int32* GlobalExportIndex = PublicExportIndices.Find(Index);
			check(GlobalExportIndex);
			return ExportObjects[*GlobalExportIndex].ObjectName;
		}
		if (Index.IsExport() && PackageExportIndices)
		{
			int32 GlobalExportIndex = (*PackageExportIndices)[Index.ToExport()];
			return ExportObjects[GlobalExportIndex].ObjectName;
		}
		return FName();
	}
};

static void FindImportFullName(
	const TMap<FName, FName>& Redirects,
	TArray<FString>& ImportFullNames,
	FObjectImport* ImportMap,
	const int32 LocalImportIndex)
{
	FString& FullName = ImportFullNames[LocalImportIndex];

	if (FullName.Len() == 0)
	{
		FullName.Reserve(256);

		FObjectImport* Import = &ImportMap[LocalImportIndex];
		if (Import->OuterIndex.IsNull())
		{
			FName PackageName = Import->ObjectName;
			if (const FName* RedirectedName = Redirects.Find(PackageName))
			{
				PackageName = *RedirectedName;
			}
			PackageName.AppendString(FullName);
			FullName.ToLowerInline();
		}
		else
		{
			const int32 OuterIndex = Import->OuterIndex.ToImport();
			FindImportFullName(Redirects, ImportFullNames, ImportMap, OuterIndex);
			const FString& OuterName = ImportFullNames[OuterIndex];
			check(OuterName.Len() > 0);

			FullName.Append(OuterName);
			FullName.AppendChar(TEXT('/'));
			Import->ObjectName.AppendString(FullName);
			FullName.ToLowerInline();
		}
	}
}

static FPackageObjectIndex FindAndVerifyGlobalImport(
	const FPackage* Package,
	FGlobalPackageData& GlobalPackageData,
	FObjectImport& Import,
	const FString& FullName,
	const FString& DLCPrefix)
{
	FPackageObjectIndex GlobalImportIndex = GlobalPackageData.ImportsByFullName.FindRef(FullName);
	if (GlobalImportIndex.IsNull())
	{
		bool bIsPackage = Import.OuterIndex.IsNull();
		bool bIsScript = FullName.StartsWith(ScriptPrefix);
		if (bIsPackage)
		{
			if (bIsScript)
			{
				UE_LOG(LogIoStore, Display, TEXT("For package '%s' (0x%llX): Missing import script package '%s'. Editor only?"),
					*Package->Name.ToString(),
					Package->GlobalPackageId.ValueForDebugging(),
					*FullName);
			}
			else
			{
				// UPackages are never serialized, hence they are never imports
			}
		}
		else
		{
			if (bIsScript)
			{
				UE_LOG(LogIoStore, Display, TEXT("For package '%s' (0x%llX): Missing import script object '%s'. Editor only?"),
					*Package->Name.ToString(),
					Package->GlobalPackageId.ValueForDebugging(),
					*FullName);
			}
			else if (!DLCPrefix.Len() || FullName.StartsWith(DLCPrefix))
			{
				UE_LOG(LogIoStore, Display, TEXT("For package '%s' (0x%llX): Missing import object '%s' due to missing public export. Editor only?"),
					*Package->Name.ToString(),
					Package->GlobalPackageId.ValueForDebugging(),
					*FullName);
			}
		}
	}
	return GlobalImportIndex;
}

static int32 FindExport(
	FGlobalPackageData& GlobalPackageData,
	TArray<FString>& TempFullNames,
	const FObjectExport* ExportMap,
	const int32 LocalExportIndex,
	FPackage* Package)
{
	FString& FullName = TempFullNames[LocalExportIndex];

	if (FullName.Len() == 0)
	{
		FullName.Reserve(256);

		const FObjectExport* Export = ExportMap + LocalExportIndex;
		if (Export->OuterIndex.IsNull())
		{
			if (Package->RedirectedPackageId.IsValid())
			{
				Package->SourcePackageName.AppendString(FullName);
			}
			else
			{
				Package->Name.AppendString(FullName);
			}
			FullName.AppendChar(TEXT('/'));
			Export->ObjectName.AppendString(FullName);
			FullName.ToLowerInline();
		}
		else
		{
			check(Export->OuterIndex.IsExport());

			FindExport(GlobalPackageData, TempFullNames, ExportMap, Export->OuterIndex.ToExport(), Package);
			FString& OuterName = TempFullNames[Export->OuterIndex.ToExport()];
			check(OuterName.Len() > 0);
			FullName.Append(OuterName);
			FullName.AppendChar(TEXT('/'));
			Export->ObjectName.AppendString(FullName);
			FullName.ToLowerInline();
		}

		int32 GlobalExportIndex = -1;
		int32* FindGlobalExportIndex = GlobalPackageData.ExportsByFullName.Find(FullName);
		if (!FindGlobalExportIndex)
		{
			GlobalExportIndex = GlobalPackageData.ExportObjects.Num();
			GlobalPackageData.ExportsByFullName.Add(FullName, GlobalExportIndex);
			GlobalPackageData.ExportObjects.AddDefaulted();
		}
		else
		{
			GlobalExportIndex = *FindGlobalExportIndex;
		}
		FExportObjectData& ExportData = GlobalPackageData.ExportObjects[GlobalExportIndex];
		
		ExportData.GlobalIndex = GlobalExportIndex;
		ExportData.Package = Package;
		ExportData.ObjectName = Export->ObjectName;
		ExportData.SourceIndex = LocalExportIndex;
		ExportData.FullName = FullName;
		ExportData.ObjectFlags = Export->ObjectFlags;

		return GlobalExportIndex;
	}
	else
	{
		int32* GlobalExportIndex = GlobalPackageData.ExportsByFullName.Find(FullName);
		check(GlobalExportIndex);
		return *GlobalExportIndex;
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
		if (ExistingContainer->Header.ContainerId == ContainerId)
		{
			UE_LOG(LogIoStore, Fatal, TEXT("Hash collision for container names: '%s' and '%s'"), *Name.ToString(), *ExistingContainer->Name.ToString());
			return nullptr;
		}
	}
	
	FContainerTargetSpec* ContainerTargetSpec = new FContainerTargetSpec();
	ContainerTargetSpec->Name = Name;
	ContainerTargetSpec->Header.ContainerId = ContainerId;
	Containers.Add(ContainerTargetSpec);
	return ContainerTargetSpec;
}

FPackage* FindOrAddPackage(
	const FIoStoreArguments& Arguments,
	const TCHAR* RelativeFileName,
	TArray<FPackage*>& Packages,
	FPackageNameMap& PackageNameMap,
	FPackageIdMap& PackageIdMap)
{
	FString PackageName;
	FString ErrorMessage;
	if (!FPackageName::TryConvertFilenameToLongPackageName(RelativeFileName, PackageName, &ErrorMessage))
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed to obtain package name from file name '%s'"), *ErrorMessage);
		return nullptr;
	}

	FName PackageFName = *PackageName;

	FPackage* Package = PackageNameMap.FindRef(PackageFName);
	if (!Package)
	{
		FPackageId PackageId = FPackageId::FromName(PackageFName);
		if (FPackage* FindById = PackageIdMap.FindRef(PackageId))
		{
			UE_LOG(LogIoStore, Fatal, TEXT("Package name hash collision \"%s\" and \"%s"), *FindById->Name.ToString(), *PackageFName.ToString());
		}

		if (const FName* ReleasedPackageName = Arguments.ReleasedPackages.PackageIdToName.Find(PackageId))
		{
			UE_LOG(LogIoStore, Fatal, TEXT("Package name hash collision \"%s\" and \"%s"), *ReleasedPackageName->ToString(), *PackageFName.ToString());
		}

		Package = new FPackage();
		Package->Name = PackageFName;
		Package->GlobalPackageId = PackageId;

		if (Arguments.IsDLC() && Arguments.bRemapPluginContentToGame)
		{
			const int32 DLCNameLen = Arguments.DLCName.Len() + 1;
			FString RedirectedPackageNameStr = TEXT("/Game");
			RedirectedPackageNameStr.AppendChars(*PackageName + DLCNameLen, PackageName.Len() - DLCNameLen);
			FName RedirectedPackageName = FName(*RedirectedPackageNameStr);

			if (Arguments.ReleasedPackages.PackageNames.Contains(RedirectedPackageName))
			{
				Package->SourcePackageName = RedirectedPackageName;
				Package->RedirectedPackageId = FPackageId::FromName(RedirectedPackageName);
			}
		}
		else
		{
			Package->SourcePackageName = *RemapLocalizationPathIfNeeded(PackageName, &Package->Region);
		}
	
		Packages.Add(Package);
		PackageNameMap.Add(PackageFName, Package);
		PackageIdMap.Add(PackageId, Package);
	}

	return Package;
}

static bool ConformLocalizedPackage(
	const FPackageNameMap& PackageMap,
	FGlobalPackageData& GlobalPackageData,
	const FPackage& SourcePackage,
	FPackage& LocalizedPackage,
	FLocalizedToSourceImportIndexMap& LocalizedToSourceImportIndexMap)
{
	const int32 ExportCount =
		SourcePackage.ExportCount < LocalizedPackage.ExportCount ?
		SourcePackage.ExportCount :
		LocalizedPackage.ExportCount;

	UE_CLOG(SourcePackage.ExportCount != LocalizedPackage.ExportCount, LogIoStore, Verbose,
		TEXT("For culture '%s': Localized package '%s' (0x%llX) for source package '%s' (0x%llX)  - Has ExportCount %d vs. %d"),
			*LocalizedPackage.Region,
			*LocalizedPackage.Name.ToString(),
			LocalizedPackage.GlobalPackageId.ValueForDebugging(),
			*LocalizedPackage.SourcePackageName.ToString(),
			SourcePackage.GlobalPackageId.ValueForDebugging(),
			LocalizedPackage.ExportCount,
			SourcePackage.ExportCount);

	auto GetExportNameSafe = [](
		const FString& ExportFullName,
		const FName& PackageName,
		int32 PackageNameLen) -> const TCHAR*
	{
		const bool bValidNameLen = ExportFullName.Len() > PackageNameLen + 1;
		if (bValidNameLen)
		{
			const TCHAR* ExportNameStr = *ExportFullName + PackageNameLen;
			const bool bValidNameFormat = *ExportNameStr == '/';
			if (bValidNameFormat)
			{
				return ExportNameStr + 1; // skip verified '/'
			}
			else
			{
				UE_CLOG(!bValidNameFormat, LogIoStore, Warning,
					TEXT("Export name '%s' should start with '/' at position %d, i.e. right after package prefix '%s'"),
					*ExportFullName,
					PackageNameLen,
					*PackageName.ToString());
			}
		}
		else
		{
			UE_CLOG(!bValidNameLen, LogIoStore, Warning,
				TEXT("Export name '%s' with length %d should be longer than package name '%s' with length %d"),
				*ExportFullName,
				PackageNameLen,
				*PackageName.ToString());
		}

		return nullptr;
	};

	auto AppendMismatchMessage = [&GlobalPackageData, &LocalizedPackage, &SourcePackage](
		const TCHAR* Text, FName ExportName, FPackageObjectIndex LocIndex, FPackageObjectIndex SrcIndex, FString& FailReason)
	{
		FString LocString = GlobalPackageData.GetObjectName(LocIndex, &LocalizedPackage.Exports).ToString();
		FString SrcString = GlobalPackageData.GetObjectName(SrcIndex, &SourcePackage.Exports).ToString();

		FailReason.Appendf(TEXT("Public export '%s' has %s %s vs. %s"),
			*ExportName.ToString(),
			Text,
			*LocString,
			*SrcString);
	};

	const int32 LocalizedPackageNameLen = LocalizedPackage.Name.GetStringLength();
	const int32 SourcePackageNameLen = SourcePackage.Name.GetStringLength();

	TArray <TPair<int32, int32>, TInlineAllocator<64>> NewPublicExports;
	NewPublicExports.Reserve(ExportCount);

	bool bSuccess = true;
	int32 LocalizedIndex = 0;
	int32 SourceIndex = 0;
	while (LocalizedIndex < ExportCount && SourceIndex < ExportCount)
	{
		FString FailReason;
		const FExportObjectData& LocalizedExportData = GlobalPackageData.ExportObjects[LocalizedPackage.Exports[LocalizedIndex]];
		const FExportObjectData& SourceExportData = GlobalPackageData.ExportObjects[SourcePackage.Exports[SourceIndex]];

		const TCHAR* LocalizedExportStr = GetExportNameSafe(
			LocalizedExportData.FullName, LocalizedPackage.Name, LocalizedPackageNameLen);
		const TCHAR* SourceExportStr = GetExportNameSafe(
			SourceExportData.FullName, SourcePackage.Name, SourcePackageNameLen);

		if (!LocalizedExportStr || !SourceExportStr)
		{
			UE_LOG(LogIoStore, Error,
				TEXT("Culture '%s': Localized package '%s' (0x%llX) for source package '%s' (0x%llX) - Has some bad data from an earlier phase."),
				*LocalizedPackage.Region,
				*LocalizedPackage.Name.ToString(),
				LocalizedPackage.GlobalPackageId.ValueForDebugging(),
				*LocalizedPackage.SourcePackageName.ToString(),
				SourcePackage.GlobalPackageId.ValueForDebugging())
			return false;
		}

		int32 CompareResult = FCString::Stricmp(LocalizedExportStr, SourceExportStr);
		if (CompareResult < 0)
		{
			++LocalizedIndex;

			if (LocalizedExportData.IsPublicExport())
			{
				// public localized export is missing in the source package, so just keep it as it is
				NewPublicExports.Emplace(LocalizedIndex - 1, 1);
			}
		}
		else if (CompareResult > 0)
		{
			++SourceIndex;

			if (SourceExportData.IsPublicExport())
			{
				FailReason.Appendf(TEXT("Public source export '%s' is missing in the localized package"),
					*SourceExportData.ObjectName.ToString());
			}
		}
		else
		{
			++LocalizedIndex;
			++SourceIndex;

			if (SourceExportData.IsPublicExport())
			{
				if (!LocalizedExportData.IsPublicExport())
				{
					FailReason.Appendf(TEXT("Public source export '%s' exists in the localized package")
						TEXT(", but is not a public localized export."),
						*SourceExportData.ObjectName.ToString());
				}
				else if (LocalizedExportData.ClassIndex != SourceExportData.ClassIndex)
				{
					AppendMismatchMessage(TEXT("class"), LocalizedExportData.ObjectName,
						LocalizedExportData.ClassIndex, SourceExportData.ClassIndex, FailReason);
				}
				else if (LocalizedExportData.TemplateIndex != SourceExportData.TemplateIndex)
				{
					AppendMismatchMessage(TEXT("template"), LocalizedExportData.ObjectName,
						LocalizedExportData.TemplateIndex, SourceExportData.TemplateIndex, FailReason);
				}
				else if (LocalizedExportData.SuperIndex != SourceExportData.SuperIndex)
				{
					AppendMismatchMessage(TEXT("super"), LocalizedExportData.ObjectName,
						LocalizedExportData.SuperIndex, SourceExportData.SuperIndex, FailReason);
				}
				else
				{
					NewPublicExports.Emplace(LocalizedIndex - 1, SourceIndex - 1);
				}
			}
			else if (LocalizedExportData.IsPublicExport())
			{
				FailReason.Appendf(TEXT("Public localized export '%s' exists in the source package")
					TEXT(", but is not a public source export."),
					*LocalizedExportData.ObjectName.ToString());
			}
		}

		if (FailReason.Len() > 0)
		{
			UE_LOG(LogIoStore, Warning,
				TEXT("Culture '%s': Localized package '%s' (0x%llX) for '%s' (0x%llX) - %s"),
				*LocalizedPackage.Region,
				*LocalizedPackage.Name.ToString(),
				LocalizedPackage.GlobalPackageId.ValueForDebugging(),
				*LocalizedPackage.SourcePackageName.ToString(),
				SourcePackage.GlobalPackageId.ValueForDebugging(),
				*FailReason);
			bSuccess = false;
		}
	}

	if (bSuccess)
	{
		for (TPair<int32, int32>& Pair : NewPublicExports)
		{
			FExportObjectData& LocalizedExportData = GlobalPackageData.ExportObjects[LocalizedPackage.Exports[Pair.Key]];
			if (Pair.Value != -1)
			{
				const FExportObjectData& SourceExportData = GlobalPackageData.ExportObjects[SourcePackage.Exports[Pair.Value]];

				LocalizedToSourceImportIndexMap.Add(LocalizedExportData.GlobalImportIndex, SourceExportData.GlobalImportIndex);

				LocalizedExportData.GlobalImportIndex = SourceExportData.GlobalImportIndex;
			}
		}
	}

	return bSuccess;
}

static void AddPreloadDependencies(
	const FPackageAssetData& PackageAssetData,
	const FGlobalPackageData& GlobalPackageData,
	const FSourceToLocalizedPackageMultimap& SourceToLocalizedPackageMap,
	FExportGraph& ExportGraph,
	TArray<FPackage*>& Packages)
{
	IOSTORE_CPU_SCOPE(PreLoadDependencies);
	UE_LOG(LogIoStore, Display, TEXT("Adding preload dependencies..."));

	TSet<FPackageId> ExternalPackageDependencies;
	TArray<FPackage*> LocalizedPackages;
	for (FPackage* Package : Packages)
	{
		ExternalPackageDependencies.Reset();

		// Convert PreloadDependencies to arcs
		for (int32 I = 0; I < Package->ExportCount; ++I)
		{
			const FObjectExport& ObjectExport = PackageAssetData.ObjectExports[Package->ExportIndexOffset + I];
			int32 PreloadDependenciesBaseIndex = Package->PreloadIndexOffset;

			FPackageIndex ExportPackageIndex = FPackageIndex::FromExport(I);

			auto AddPreloadArc = [&](FPackageIndex Dep, EPreloadDependencyType PhaseFrom, EPreloadDependencyType PhaseTo)
			{
				if (Dep.IsExport())
				{
					AddInternalExportArc(ExportGraph, *Package, Dep.ToExport(), PhaseFrom, I, PhaseTo);
				}
				else
				{
					FPackageObjectIndex ImportIndex = Package->Imports[Dep.ToImport()];
					if (ImportIndex.IsPackageImport())
					{
						// When building DLC's exports can be missing
						if (const FExportObjectData* Export = GlobalPackageData.FindPublicExport(ImportIndex))
						{
							check(Export->GlobalImportIndex == ImportIndex);

							AddExternalExportArc(ExportGraph, *Export->Package, Export->SourceIndex, PhaseFrom, *Package, I, PhaseTo);

							LocalizedPackages.Reset();
							SourceToLocalizedPackageMap.MultiFind(Export->Package, LocalizedPackages);
							for (FPackage* LocalizedPackage : LocalizedPackages)
							{
								UE_LOG(LogIoStore, Verbose, TEXT("For package '%s' (0x%llX): Adding localized preload dependency '%s' in '%s'"),
									*Package->Name.ToString(),
									Package->GlobalPackageId.ValueForDebugging(),
									*Export->ObjectName.ToString(),
									*LocalizedPackage->Name.ToString());

								AddExternalExportArc(ExportGraph, *LocalizedPackage, Export->SourceIndex, PhaseFrom, *Package, I, PhaseTo);
							}
						}
						else
						{
							const FObjectImport* ImportMap = PackageAssetData.ObjectImports.GetData() + Package->ImportIndexOffset;
							const FObjectImport* Import = ImportMap + Dep.ToImport();
							while (!Import->OuterIndex.IsNull())
							{
								Import = ImportMap + Import->OuterIndex.ToImport();
							}

							FPackageId PackageId = FPackageId::FromName(Import->ObjectName);
							bool bIsAlreadyInSet = false;
							ExternalPackageDependencies.Add(PackageId, &bIsAlreadyInSet);
							if (!bIsAlreadyInSet)
							{
								AddBaseGamePackageArc(ExportGraph, PackageId, *Package, I, PhaseTo);
							}
						}
					}
				}
			};

			if (PreloadDependenciesBaseIndex >= 0 && ObjectExport.FirstExportDependency >= 0)
			{
				int32 RunningIndex = PreloadDependenciesBaseIndex + ObjectExport.FirstExportDependency;
				for (int32 Index = ObjectExport.SerializationBeforeSerializationDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PackageAssetData.PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					AddPreloadArc(Dep, PreloadDependencyType_Serialize, PreloadDependencyType_Serialize);
				}

				for (int32 Index = ObjectExport.CreateBeforeSerializationDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PackageAssetData.PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					AddPreloadArc(Dep, PreloadDependencyType_Create, PreloadDependencyType_Serialize);
				}

				for (int32 Index = ObjectExport.SerializationBeforeCreateDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PackageAssetData.PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					AddPreloadArc(Dep, PreloadDependencyType_Serialize, PreloadDependencyType_Create);
				}

				for (int32 Index = ObjectExport.CreateBeforeCreateDependencies; Index > 0; Index--)
				{
					FPackageIndex Dep = PackageAssetData.PreloadDependencies[RunningIndex++];
					check(!Dep.IsNull());
					// can't create this export until these things are created
					AddPreloadArc(Dep, PreloadDependencyType_Create, PreloadDependencyType_Create);
				}
			}
		}
	}
};

void FinalizeNameMaps(FContainerTargetSpec& ContainerTarget)
{
	for (FContainerTargetFile& TargetFile : ContainerTarget.TargetFiles)
	{
		if (TargetFile.bIsBulkData)
		{
			continue;
		}
		FPackage* Package = TargetFile.Package;
		TargetFile.NameMapBuilder->MarkNameAsReferenced(Package->Name);
		TargetFile.NameMapBuilder->MarkNameAsReferenced(Package->SourcePackageName);
		TargetFile.NameMapBuilder->MarkNamesAsReferenced(Package->SummaryNames, TargetFile.NameIndices);
	}
}

void FinalizePackageHeaders(
	FContainerTargetSpec& ContainerTarget,
	const TArray<FObjectExport>& ObjectExports,
	const TArray<FExportObjectData>& GlobalExports,
	const FImportObjectsByFullName& GlobalImportsByFullName)
{
	for (FContainerTargetFile& TargetFile : ContainerTarget.TargetFiles)
	{
		if (TargetFile.bIsBulkData)
		{
			continue;
		}

		FPackage* Package = TargetFile.Package;

		// Temporary Archive for serializing ImportMap
		FBufferWriter ImportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
		for (FPackageObjectIndex& GlobalImportIndex : Package->Imports)
		{
			ImportMapArchive << GlobalImportIndex;
		}
		TargetFile.ImportMapSize = ImportMapArchive.Tell();

		// Temporary Archive for serializing EDL graph data
		FBufferWriter GraphArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);

		int32 ReferencedPackagesCount = Package->ExternalArcs.Num();
		GraphArchive << ReferencedPackagesCount;
		TArray<TTuple<FPackageId, TArray<FArc>>> SortedExternalArcs;
		SortedExternalArcs.Reserve(Package->ExternalArcs.Num());
		for (auto& KV : Package->ExternalArcs)
		{
			FPackageId ImportedPackageId = KV.Key;
			TArray<FArc> SortedArcs = KV.Value;
			Algo::Sort(SortedArcs, [](const FArc& A, const FArc& B)
			{
				if (A.FromNodeIndex == B.FromNodeIndex)
				{
					return A.ToNodeIndex < B.ToNodeIndex;
				}
				return A.FromNodeIndex < B.ToNodeIndex;
			});
			SortedExternalArcs.Emplace(ImportedPackageId, MoveTemp(SortedArcs));
		}
		Algo::Sort(SortedExternalArcs, [](const TTuple<FPackageId, TArray<FArc>>& A, const TTuple<FPackageId, TArray<FArc>>& B)
		{
			return A.Key < B.Key;
		});
		for (auto& KV : SortedExternalArcs)
		{
			FPackageId ImportedPackageId = KV.Key;
			TArray<FArc>& Arcs = KV.Value;
			int32 ExternalArcCount = Arcs.Num();

			GraphArchive << ImportedPackageId;
			GraphArchive << ExternalArcCount;
			GraphArchive.Serialize(Arcs.GetData(), ExternalArcCount * sizeof(FArc));
		}
		TargetFile.UGraphSize = GraphArchive.Tell();

		// Temporary Archive for serializing export map data
		FBufferWriter ExportMapArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
		for (int32 I = 0; I < Package->ExportCount; ++I)
		{
			const FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + I];
			const FExportObjectData& ExportData = GlobalExports[Package->Exports[I]];

			FExportMapEntry ExportMapEntry;
			ExportMapEntry.CookedSerialOffset = ObjectExport.SerialOffset;
			ExportMapEntry.CookedSerialSize = ObjectExport.SerialSize;
			ExportMapEntry.ObjectName = TargetFile.NameMapBuilder->MapName(ObjectExport.ObjectName);
			ExportMapEntry.OuterIndex = ExportData.OuterIndex;
			ExportMapEntry.ClassIndex = ExportData.ClassIndex;
			ExportMapEntry.SuperIndex = ExportData.SuperIndex;
			ExportMapEntry.TemplateIndex = ExportData.TemplateIndex;
			ExportMapEntry.GlobalImportIndex = ExportData.GlobalImportIndex;
			ExportMapEntry.ObjectFlags = ObjectExport.ObjectFlags;
			ExportMapEntry.FilterFlags = EExportFilterFlags::None;
			if (ObjectExport.bNotForClient)
			{
				ExportMapEntry.FilterFlags = EExportFilterFlags::NotForClient;
			}
			else if (ObjectExport.bNotForServer)
			{
				ExportMapEntry.FilterFlags = EExportFilterFlags::NotForServer;
			}

			ExportMapArchive << ExportMapEntry;
		}
		TargetFile.ExportMapSize = ExportMapArchive.Tell();

		// Temporary archive for serializing export bundle data
		FBufferWriter ExportBundlesArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership);
		uint32 ExportBundleEntryIndex = 0;
		for (FExportBundle& ExportBundle : Package->ExportBundles)
		{
			const uint32 EntryCount = ExportBundle.Nodes.Num();
			FExportBundleHeader ExportBundleHeader { ExportBundleEntryIndex, EntryCount };
			ExportBundlesArchive << ExportBundleHeader ;

			ExportBundleEntryIndex += EntryCount; 
		}
		for (FExportBundle& ExportBundle : Package->ExportBundles)
		{
			for (FExportGraphNode* ExportNode : ExportBundle.Nodes)
			{
				ExportBundlesArchive << ExportNode->BundleEntry;
			}
		}
		TargetFile.ExportBundlesHeaderSize = ExportBundlesArchive.Tell();

		FMappedName MappedPackageName = TargetFile.NameMapBuilder->MapName(Package->Name);
		FMappedName MappedPackageSourceName = TargetFile.NameMapBuilder->MapName(Package->SourcePackageName);

		TArray<uint8> NamesBuffer;
		TArray<uint8> NameHashesBuffer;
		SaveNameBatch(Package->LocalNameMapBuilder.GetNameMap(), NamesBuffer, NameHashesBuffer);
		TargetFile.NameMapSize = Align(NamesBuffer.Num(), 8) + NameHashesBuffer.Num();

		TargetFile.HeaderSerialSize =
			sizeof(FPackageSummary)
			+ TargetFile.NameMapSize
			+ TargetFile.ImportMapSize
			+ TargetFile.ExportMapSize
			+ TargetFile.ExportBundlesHeaderSize
			+ TargetFile.UGraphSize;

		TargetFile.PackageHeaderData.AddZeroed(TargetFile.HeaderSerialSize);
		uint8* PackageHeaderBuffer = TargetFile.PackageHeaderData.GetData();
		FPackageSummary* PackageSummary = reinterpret_cast<FPackageSummary*>(PackageHeaderBuffer);

		PackageSummary->Name = MappedPackageName;
		PackageSummary->SourceName = MappedPackageSourceName;
		PackageSummary->PackageFlags = Package->PackageFlags;
		PackageSummary->CookedHeaderSize = Package->CookedHeaderSize;
		FBufferWriter SummaryArchive(PackageHeaderBuffer, TargetFile.HeaderSerialSize);
		SummaryArchive.Seek(sizeof(FPackageSummary));

		// NameMap data
		{
			PackageSummary->NameMapNamesOffset = SummaryArchive.Tell();
			check(PackageSummary->NameMapNamesOffset % 8 == 0);
			PackageSummary->NameMapNamesSize = NamesBuffer.Num();
			SummaryArchive.Serialize(NamesBuffer.GetData(), NamesBuffer.Num());
			PackageSummary->NameMapHashesOffset = Align(SummaryArchive.Tell(), 8);
			int32 PaddingByteCount = PackageSummary->NameMapHashesOffset - SummaryArchive.Tell();
			if (PaddingByteCount)
			{
				check(PaddingByteCount < 8);
				uint8 PaddingBytes[8]{ 0 };
				SummaryArchive.Serialize(PaddingBytes, PaddingByteCount);
			}
			PackageSummary->NameMapHashesSize = NameHashesBuffer.Num();
			SummaryArchive.Serialize(NameHashesBuffer.GetData(), NameHashesBuffer.Num());
		}

		// ImportMap data
		{
			check(ImportMapArchive.Tell() == TargetFile.ImportMapSize);
			PackageSummary->ImportMapOffset = SummaryArchive.Tell();
			SummaryArchive.Serialize(ImportMapArchive.GetWriterData(), ImportMapArchive.Tell());
		}

		// ExportMap data
		{
			check(ExportMapArchive.Tell() == TargetFile.ExportMapSize);
			PackageSummary->ExportMapOffset = SummaryArchive.Tell();
			SummaryArchive.Serialize(ExportMapArchive.GetWriterData(), ExportMapArchive.Tell());
		}

		// ExportBundle data
		{
			check(ExportBundlesArchive.Tell() == TargetFile.ExportBundlesHeaderSize);
			PackageSummary->ExportBundlesOffset = SummaryArchive.Tell();
			SummaryArchive.Serialize(ExportBundlesArchive.GetWriterData(), ExportBundlesArchive.Tell());
		}

		// Graph data
		{
			check(GraphArchive.Tell() == TargetFile.UGraphSize);
			PackageSummary->GraphDataOffset = SummaryArchive.Tell();
			PackageSummary->GraphDataSize = TargetFile.UGraphSize;
			SummaryArchive.Serialize(GraphArchive.GetWriterData(), GraphArchive.Tell());
		}
	}
}

void FinalizePackageStoreContainerHeader(FContainerTargetSpec& ContainerTarget)
{
	IOSTORE_CPU_SCOPE(FinalizePackageStoreContainerHeader);
	check(ContainerTarget.NameMapBuilder);
	FNameMapBuilder& NameMapBuilder = *ContainerTarget.NameMapBuilder;
	FCulturePackageMap& CulturePackageMap = ContainerTarget.Header.CulturePackageMap;
	TArray<FPackageId>& PackageIds = ContainerTarget.Header.PackageIds;
	TArray<TTuple<FPackageId, FPackageId>>& PackageRedirects = ContainerTarget.Header.PackageRedirects;
	
	int32 StoreTocSize = ContainerTarget.PackageCount * sizeof(FPackageStoreEntry);
	FLargeMemoryWriter StoreTocArchive(0, true);
	FLargeMemoryWriter StoreDataArchive(0, true);

	auto SerializePackageEntryCArrayHeader = [&StoreTocSize,&StoreTocArchive,&StoreDataArchive](int32 Count)
	{
		const int32 RemainingTocSize = StoreTocSize - StoreTocArchive.Tell();
		const int32 OffsetFromThis = RemainingTocSize + StoreDataArchive.Tell();
		uint32 ArrayNum = Count > 0 ? Count : 0; 
		uint32 OffsetToDataFromThis = ArrayNum > 0 ? OffsetFromThis : 0;

		StoreTocArchive << ArrayNum;
		StoreTocArchive << OffsetToDataFromThis;
	};

	PackageIds.Reserve(ContainerTarget.PackageCount);
	TArray<FContainerTargetFile*> SortedTargetFiles;
	SortedTargetFiles.Reserve(ContainerTarget.PackageCount);
	for (FContainerTargetFile& TargetFile : ContainerTarget.TargetFiles)
	{
		if (!TargetFile.bIsBulkData)
		{
			SortedTargetFiles.Add(&TargetFile);
		}
	}
	Algo::Sort(SortedTargetFiles, [](const FContainerTargetFile* A, const FContainerTargetFile* B)
	{
		return A->Package->GlobalPackageId < B->Package->GlobalPackageId;
	});

	for (FContainerTargetFile* TargetFile : SortedTargetFiles)
	{
		FPackage* Package = TargetFile->Package;
		
		// PackageIds
		{
			//check(!PackageIds.Contains(Package->GlobalPackageId));
			PackageIds.Add(Package->GlobalPackageId);
		}

		// CulturePackageMap
		if (Package->bIsLocalizedAndConformed)
		{
			CulturePackageMap.FindOrAdd(Package->Region).Emplace(Package->SourceGlobalPackageId, Package->GlobalPackageId);
		}

		// Redirects
		if (Package->RedirectedPackageId.IsValid())
		{
			PackageRedirects.Add(MakeTuple(Package->RedirectedPackageId, Package->GlobalPackageId));
		}

		// StoreEntries
		{
			uint64 ExportBundlesSize = TargetFile->HeaderSerialSize + Package->ExportsSerialSize;
			int32 ExportBundleCount = Package->ExportBundles.Num();
			uint32 LoadOrder = Package->ExportBundles.Num() > 0 ? Package->ExportBundles[0].LoadOrder : 0;
			uint32 Pad = 0;

			StoreTocArchive << ExportBundlesSize;
			StoreTocArchive << Package->ExportCount;
			StoreTocArchive << ExportBundleCount;
			StoreTocArchive << LoadOrder;
			StoreTocArchive << Pad;

			// ImportedPackages
			{
				SerializePackageEntryCArrayHeader(Package->ImportedPackageIds.Num());
				for (FPackageId PackageId : Package->ImportedPackageIds)
				{
					check(PackageId.IsValid());
					StoreDataArchive << PackageId;
				}
			}
		}
	}

	const int32 StoreByteCount = StoreTocArchive.TotalSize() + StoreDataArchive.TotalSize();
	ContainerTarget.Header.PackageCount = ContainerTarget.PackageCount;
	ContainerTarget.Header.StoreEntries.AddUninitialized(StoreByteCount);
	FBufferWriter PackageStoreArchive(ContainerTarget.Header.StoreEntries.GetData(), StoreByteCount);
	PackageStoreArchive.Serialize(StoreTocArchive.GetData(), StoreTocArchive.TotalSize());
	PackageStoreArchive.Serialize(StoreDataArchive.GetData(), StoreDataArchive.TotalSize());
}

static void FinalizeInitialLoadMeta(
	FNameMapBuilder& GlobalNameMapBuilder,
	const FGlobalScriptObjects& GlobalScriptImports,
	FArchive& InitialLoadArchive)
{
	IOSTORE_CPU_SCOPE(FinalizeInitialLoad);
	UE_LOG(LogIoStore, Display, TEXT("Finalizing initial load..."));

	int32 NumScriptObjects = GlobalScriptImports.Num();
	InitialLoadArchive << NumScriptObjects; 

	TArray<FScriptObjectData> ScriptObjects;
	GlobalScriptImports.GenerateValueArray(ScriptObjects );
	Algo::Sort(ScriptObjects, [](const FScriptObjectData& A, const FScriptObjectData& B)
	{
		return A.FullName < B.FullName;
	});

	for (const FScriptObjectData& ImportData : ScriptObjects)
	{
		GlobalNameMapBuilder.MarkNameAsReferenced(ImportData.ObjectName);
		FScriptObjectEntry Entry;
		Entry.ObjectName = GlobalNameMapBuilder.MapName(ImportData.ObjectName).ToUnresolvedMinimalName();
		Entry.GlobalIndex = ImportData.GlobalIndex;
		Entry.OuterIndex = ImportData.OuterIndex;
		Entry.CDOClassIndex = ImportData.CDOClassIndex;

		InitialLoadArchive << Entry; 
	}
};

static FIoBuffer CreateExportBundleBuffer(const FContainerTargetFile& TargetFile, const TArray<FObjectExport>& ObjectExports, const FIoBuffer UExpBuffer, TArray<FFileRegion>* InOutFileRegions = nullptr)
{
	const FPackage* Package = TargetFile.Package;
	check(TargetFile.PackageHeaderData.Num() > 0);
	const uint64 BundleBufferSize = TargetFile.PackageHeaderData.Num() + TargetFile.Package->ExportsSerialSize;
	FIoBuffer BundleBuffer(BundleBufferSize);
	FMemory::Memcpy(BundleBuffer.Data(), TargetFile.PackageHeaderData.GetData(), TargetFile.PackageHeaderData.Num());
	uint64 BundleBufferOffset = TargetFile.PackageHeaderData.Num();

	TArray<FFileRegion> OutputRegions;

	for (const FExportBundle& ExportBundle : TargetFile.Package->ExportBundles)
	{
		for (const FExportGraphNode* Node : ExportBundle.Nodes)
		{
			if (Node->BundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Serialize)
			{
				const FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + Node->BundleEntry.LocalExportIndex];
				const uint64 Offset = uint64(ObjectExport.SerialOffset - Package->UAssetSize);
				const uint64 End = uint64(Offset + ObjectExport.SerialSize);
				check(End <= UExpBuffer.DataSize());
				FMemory::Memcpy(BundleBuffer.Data() + BundleBufferOffset, UExpBuffer.Data() + Offset, ObjectExport.SerialSize);

				if (InOutFileRegions)
				{
					// Find overlapping regions and adjust them to match the new offset of the export data
					for (const FFileRegion& Region : *InOutFileRegions)
					{
						uint64 RegionStart = Region.Offset;
						uint64 RegionEnd = RegionStart + Region.Length;

						if (Offset <= RegionStart && RegionEnd <= End)
						{
							FFileRegion NewRegion = Region;
							NewRegion.Offset -= Offset;
							NewRegion.Offset += BundleBufferOffset;
							OutputRegions.Add(NewRegion);
						}
					}
				}

				BundleBufferOffset += ObjectExport.SerialSize;
			}
		}
	}
	check(BundleBufferOffset == BundleBuffer.DataSize());

	if (InOutFileRegions)
	{
		*InOutFileRegions = OutputRegions;
	}

	return BundleBuffer;
}

static void ParsePackageAssets(
	TArray<FPackage*>& Packages,
	FPackageAssetData& PackageAssetData)
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
		for (const FPackage* Package : Packages)
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
			FPackage* Package = Packages[Index];
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
				&Packages](int32 Index)
		{
			uint8* PackageBuffer = PackageAssetBuffers[Index];
			FPackageFileSummary& Summary = PackageFileSummaries[Index];
			FPackage& Package = *Packages[Index];

			TArrayView<const uint8> MemView(PackageBuffer, Package.UAssetSize);
			if (!Package.UAssetSize)
			{
				return;
			}

			FMemoryReaderView Ar(MemView);
			Ar << Summary;

			Package.SummarySize = Ar.Tell();
			Package.NameCount = Summary.NameCount;
			Package.ImportCount = Summary.ImportCount;
			Package.PreloadDependencyCount = Summary.PreloadDependencyCount;
			Package.ExportCount = Summary.ExportCount;
			Package.PackageFlags = Summary.PackageFlags;
			Package.CookedHeaderSize = Summary.TotalHeaderSize;

		}, EParallelForFlags::Unbalanced);
	}

	int32 TotalImportCount = 0;
	int32 TotalPreloadDependencyCount = 0;
	int32 TotalExportCount = 0;
	for (FPackage* Package : Packages)
	{
		if (Package->ImportCount > 0)
		{
			Package->ImportIndexOffset = TotalImportCount;
			TotalImportCount += Package->ImportCount;
		}

		if (Package->PreloadDependencyCount > 0)
		{
			Package->PreloadIndexOffset = TotalPreloadDependencyCount;
			TotalPreloadDependencyCount += Package->PreloadDependencyCount;
		}

		if (Package->ExportCount > 0)
		{
			Package->ExportIndexOffset = TotalExportCount;
			TotalExportCount += Package->ExportCount;
		}
	}
	PackageAssetData.ObjectImports.AddUninitialized(TotalImportCount);
	PackageAssetData.PreloadDependencies.AddUninitialized(TotalPreloadDependencyCount);
	PackageAssetData.ObjectExports.AddUninitialized(TotalExportCount);

	UE_LOG(LogIoStore, Display, TEXT("Parsing package assets..."));
	{
		IOSTORE_CPU_SCOPE(SerializeAssets);

		for (int32 PackageIndex = 0; PackageIndex < TotalPackageCount; ++PackageIndex)
		{
			uint8* PackageBuffer = PackageAssetBuffers[PackageIndex];
			const FPackageFileSummary& Summary = PackageFileSummaries[PackageIndex];
			FPackage& Package = *Packages[PackageIndex];
			TArrayView<const uint8> MemView(PackageBuffer, Package.UAssetSize);
			FMemoryReaderView Ar(MemView);

			if (Summary.NameCount > 0)
			{
				Ar.Seek(Summary.NameOffset);

				FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);

				Package.SummaryNames.Reserve(Summary.NameCount);
				for (int32 I = 0; I < Summary.NameCount; ++I)
				{
					Ar << NameEntry;
					FName Name(NameEntry);
					Package.SummaryNames.Add(Name);
					Package.LocalNameMapBuilder.AddName(Name);
				}
			}
		}

		ParallelFor(TotalPackageCount,[
				&ParseCount,
				&PackageAssetBuffers,
				&PackageFileSummaries,
				&Packages,
				&PackageAssetData](int32 Index)
		{
			uint8* PackageBuffer = PackageAssetBuffers[Index];
			const FPackageFileSummary& Summary = PackageFileSummaries[Index];
			FPackage& Package = *Packages[Index];
			TArrayView<const uint8> MemView(PackageBuffer, Package.UAssetSize);
			FMemoryReaderView Ar(MemView);
			Ar.SetFilterEditorOnly((Package.PackageFlags & EPackageFlags::PKG_FilterEditorOnly) != 0);

			IOSTORE_CPU_SCOPE_DATA(ParsePackage, TCHAR_TO_ANSI(*Package.FileName));

			const int32 Count = ParseCount.IncrementExchange();
			UE_CLOG(Count % 1000 == 0, LogIoStore, Display, TEXT("Parsing %d/%d: '%s'"), Count, Packages.Num(), *Package.FileName);

			if (Summary.ImportCount > 0)
			{
				FNameReaderProxyArchive ProxyAr(Ar, Package.LocalNameMapBuilder.GetNameMap());
				ProxyAr.Seek(Summary.ImportOffset);

				for (int32 I = 0; I < Summary.ImportCount; ++I)
				{
					FObjectImport& ObjectImport = PackageAssetData.ObjectImports[Package.ImportIndexOffset + I];
					ProxyAr << ObjectImport;
				}
			}

			if (Summary.PreloadDependencyCount > 0)
			{
				Ar.Seek(Summary.PreloadDependencyOffset);
				Ar.Serialize(PackageAssetData.PreloadDependencies.GetData() + Package.PreloadIndexOffset, Summary.PreloadDependencyCount * sizeof(FPackageIndex));
			}

			if (Summary.ExportCount > 0)
			{
				FNameReaderProxyArchive ProxyAr(Ar, Package.LocalNameMapBuilder.GetNameMap());
				ProxyAr.Seek(Summary.ExportOffset);

				for (int32 I = 0; I < Summary.ExportCount; ++I)
				{
					FObjectExport& ObjectExport = PackageAssetData.ObjectExports[Package.ExportIndexOffset + I];
					ProxyAr << ObjectExport;
					Package.ExportsSerialSize += ObjectExport.SerialSize;
				}
			}
		}, EParallelForFlags::Unbalanced);
	}

	FMemory::Free(UAssetMemory);
}

// modified copy from SavePackage
EObjectMark GetExcludedObjectMarksForTargetPlatform(const ITargetPlatform* TargetPlatform)
{
	EObjectMark Marks = OBJECTMARK_NOMARKS;
	if (!TargetPlatform->HasEditorOnlyData())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_EditorOnly);
	}
	if (TargetPlatform->IsServerOnly())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForServer);
	}
	if (TargetPlatform->IsClientOnly())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForClient);
	}
	return Marks;
}

// modified copy from SavePackage
EObjectMark GetExcludedObjectMarksForObject(const UObject* Object, const ITargetPlatform* TargetPlatform)
{
	EObjectMark Marks = OBJECTMARK_NOMARKS;
	if (!Object->NeedsLoadForClient())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForClient);
	}
	if (!Object->NeedsLoadForServer())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForServer);
	}
	if (!Object->NeedsLoadForTargetPlatform(TargetPlatform))
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_NotForClient | OBJECTMARK_NotForServer);
	}
	if (Object->IsEditorOnly())
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_EditorOnly);
	}
	if ((Marks & OBJECTMARK_NotForClient) && (Marks & OBJECTMARK_NotForServer))
	{
		Marks = (EObjectMark)(Marks | OBJECTMARK_EditorOnly);
	}
	return Marks;
}

static void FindScriptObjectsRecursive(
	FGlobalPackageData& GlobalPackageData,
	FPackageObjectIndex OuterIndex,
	UObject* Object,
	const ITargetPlatform* TargetPlatform,
	const EObjectMark ExcludedObjectMarks)
{
	if (!Object->HasAllFlags(RF_Public))
	{
		UE_LOG(LogIoStore, Log, TEXT("Skipping script object: %s (!RF_Public)"), *Object->GetFullName());
		return;
	}

	const UObject* ObjectForExclusion = Object->HasAnyFlags(RF_ClassDefaultObject) ? (const UObject*)Object->GetClass() : Object;
	const EObjectMark ObjectMarks = GetExcludedObjectMarksForObject(ObjectForExclusion, TargetPlatform);

	if (ObjectMarks & ExcludedObjectMarks)
	{
		UE_LOG(LogIoStore, Log, TEXT("Skipping script object: %s (Excluded for target platform)"), *Object->GetFullName());
		return;
	}

	FGlobalScriptObjects& ScriptObjects = GlobalPackageData.ScriptObjects;
	const FScriptObjectData* Outer = ScriptObjects.Find(OuterIndex);
	check(Outer);

	FName ObjectName = Object->GetFName();

	FString TempFullName = ScriptObjects.FindRef(OuterIndex).FullName;
	TempFullName.AppendChar(TEXT('/'));
	ObjectName.AppendString(TempFullName);

	TempFullName.ToLowerInline();
	FPackageObjectIndex GlobalImportIndex = FPackageObjectIndex::FromScriptPath(TempFullName);

	check(!GlobalPackageData.ImportsByFullName.Contains(TempFullName));
	FScriptObjectData* ScriptImport = ScriptObjects.Find(GlobalImportIndex);
	if (ScriptImport)
	{
		UE_LOG(LogIoStore, Fatal, TEXT("Import name hash collision \"%s\" and \"%s"), *TempFullName, *ScriptImport->FullName);
	}

	FPackageObjectIndex CDOClassIndex = Outer->CDOClassIndex;
	if (CDOClassIndex.IsNull())
	{
		TCHAR NameBuffer[FName::StringBufferSize];
		uint32 Len = ObjectName.ToString(NameBuffer);
		if (FCString::Strncmp(NameBuffer, TEXT("Default__"), 9) == 0)
		{
			FString CDOClassFullName = Outer->FullName;
			CDOClassFullName.AppendChar(TEXT('/'));
			CDOClassFullName.AppendChars(NameBuffer + 9, Len - 9);
			CDOClassFullName.ToLowerInline();

			CDOClassIndex = GlobalPackageData.ImportsByFullName.FindRef(CDOClassFullName);
			check(CDOClassIndex.IsScriptImport());
		}
	}

	GlobalPackageData.ImportsByFullName.Add(TempFullName, GlobalImportIndex);
	ScriptImport = &ScriptObjects.Add(GlobalImportIndex);
	ScriptImport->GlobalIndex = GlobalImportIndex;
	ScriptImport->FullName = MoveTemp(TempFullName);
	ScriptImport->OuterIndex = Outer->GlobalIndex;
	ScriptImport->ObjectName = ObjectName;
	ScriptImport->CDOClassIndex = CDOClassIndex;

	TArray<UObject*> InnerObjects;
	GetObjectsWithOuter(Object, InnerObjects, /*bIncludeNestedObjects*/false);
	for (UObject* InnerObject : InnerObjects)
	{
		FindScriptObjectsRecursive(GlobalPackageData, GlobalImportIndex, InnerObject, TargetPlatform, ExcludedObjectMarks);
	}
};

static void CreateGlobalScriptObjects(
	FGlobalPackageData& GlobalPackageData,
	const ITargetPlatform* TargetPlatform)
{
	IOSTORE_CPU_SCOPE(CreateGlobalScriptObjects);
	UE_LOG(LogIoStore, Display, TEXT("Creating global script objects..."));

	const EObjectMark ExcludedObjectMarks = GetExcludedObjectMarksForTargetPlatform(TargetPlatform);

	TArray<UPackage*> ScriptPackages;
	FindAllRuntimeScriptPackages(ScriptPackages);

	TArray<UObject*> InnerObjects;
	for (UPackage* Package : ScriptPackages)
	{
		FGlobalScriptObjects& ScriptObjects = GlobalPackageData.ScriptObjects;

		FName ObjectName = Package->GetFName();
		FString FullName = Package->GetName();

		FullName.ToLowerInline();
		FPackageObjectIndex GlobalImportIndex = FPackageObjectIndex::FromScriptPath(FullName);

		check(!GlobalPackageData.ImportsByFullName.Contains(FullName));
		FScriptObjectData* ScriptImport = ScriptObjects.Find(GlobalImportIndex);
		if (ScriptImport)
		{
			UE_LOG(LogIoStore, Fatal, TEXT("Import name hash collision \"%s\" and \"%s"), *FullName, *ScriptImport->FullName);
		}

		GlobalPackageData.ImportsByFullName.Add(FullName, GlobalImportIndex);
		ScriptImport = &ScriptObjects.Add(GlobalImportIndex);
		ScriptImport->GlobalIndex = GlobalImportIndex;
		ScriptImport->FullName = FullName;
		ScriptImport->OuterIndex = FPackageObjectIndex();
		ScriptImport->ObjectName = ObjectName;

		InnerObjects.Reset();
		GetObjectsWithOuter(Package, InnerObjects, /*bIncludeNestedObjects*/false);
		for (UObject* InnerObject : InnerObjects)
		{
			FindScriptObjectsRecursive(GlobalPackageData, GlobalImportIndex, InnerObject, TargetPlatform, ExcludedObjectMarks);
		}
	}
}

static void CreateGlobalImportsAndExports(
	const FIoStoreArguments& Arguments,
	TArray<FPackage*>& Packages,
	const FPackageIdMap& PackageIdMap,
	FPackageAssetData& PackageAssetData,
	FGlobalPackageData& GlobalPackageData,
	FExportGraph& ExportGraph)
{
	IOSTORE_CPU_SCOPE(CreateGlobalImportsAndExports);
	UE_LOG(LogIoStore, Display, TEXT("Creating global imports and exports..."));

	TArray<FString> TempFullNames;
	TMap<FName, FName> Redirects;
	const FString DLCPrefix = Arguments.IsDLC() ? FString::Printf(TEXT("/%s/"), *FPaths::GetBaseFilename(Arguments.DLCPluginPath)) : FString();

	TSet<FPackage*> TempImportedPackages;
	for (FPackage* Package : Packages)
	{
		if (Package->ExportCount == 0)
		{
			continue;
		}

		if (Package->RedirectedPackageId.IsValid())
		{
			Redirects.Add(Package->Name, Package->SourcePackageName);
		}

		TempFullNames.Reset();
		TempFullNames.SetNum(Package->ExportCount, false);
		FObjectExport* ExportMap = PackageAssetData.ObjectExports.GetData() + Package->ExportIndexOffset;
		for (int32 ExportIndex = 0; ExportIndex < Package->ExportCount; ExportIndex++)
		{
			int32 GlobalExportIndex = FindExport(
				GlobalPackageData,
				TempFullNames,
				PackageAssetData.ObjectExports.GetData() + Package->ExportIndexOffset,
				ExportIndex,
				Package);

			FExportObjectData& ExportData = GlobalPackageData.ExportObjects[GlobalExportIndex];
			ExportData.CreateNode = ExportGraph.AddNode(Package, { uint32(ExportIndex), FExportBundleEntry::ExportCommandType_Create });
			ExportData.SerializeNode = ExportGraph.AddNode(Package, { uint32(ExportIndex), FExportBundleEntry::ExportCommandType_Serialize });

			Package->Exports.Add(GlobalExportIndex);
			Package->CreateExportNodes.Add(ExportData.CreateNode);
			Package->SerializeExportNodes.Add(ExportData.SerializeNode);
			ExportGraph.AddInternalDependency(ExportData.CreateNode, ExportData.SerializeNode);
		}
	}

	for (FExportObjectData& Export : GlobalPackageData.ExportObjects)
	{
		if (Export.IsPublicExport())
		{
			TMap<FPackageObjectIndex, int32>& PublicExports = GlobalPackageData.PublicExportIndices;
			FPackageObjectIndex GlobalImportIndex = FPackageObjectIndex::FromPackagePath(Export.FullName);

			check(!GlobalPackageData.ImportsByFullName.Contains(Export.FullName));
			int32* ExportIndex = PublicExports.Find(GlobalImportIndex);
			if (ExportIndex)
			{
				UE_LOG(LogIoStore, Fatal, TEXT("Import name hash collision \"%s\" and \"%s"),
					*Export.FullName,
					*GlobalPackageData.ExportObjects[*ExportIndex].FullName);
			}
			GlobalPackageData.ImportsByFullName.Add(Export.FullName, GlobalImportIndex);
			PublicExports.Add(GlobalImportIndex, Export.GlobalIndex);
			Export.GlobalImportIndex = GlobalImportIndex;
		}
	}

	for (FPackage* Package : Packages)
	{
		if (Package->ImportCount == 0)
		{
			continue;
		}

		FObjectImport* ImportMap = PackageAssetData.ObjectImports.GetData() + Package->ImportIndexOffset;
		TempFullNames.Reset();
		TempFullNames.SetNum(Package->ImportCount, false);
		Package->Imports.Reserve(Package->ImportCount);
		Package->ImportedPackages.Reserve(Package->ImportCount / 2);
		TempImportedPackages.Reset();

		for (int32 ImportIndex = 0; ImportIndex < Package->ImportCount; ++ImportIndex)
		{
			FindImportFullName(Redirects, TempFullNames, ImportMap, ImportIndex);
			FString& FullName = TempFullNames[ImportIndex];
			FObjectImport& Import = ImportMap[ImportIndex];
			const bool bIsPackage = Import.OuterIndex.IsNull();

			FPackageObjectIndex GlobalImportIndex = FindAndVerifyGlobalImport(
				Package,
				GlobalPackageData,
				Import,
				FullName,
				DLCPrefix);

			// When building DLC:s and we don't have all packages available,
			// then a global package import object can be missing and still be valid
			if (GlobalImportIndex.IsNull() && !bIsPackage && !FullName.StartsWith(ScriptPrefix))
			{
				GlobalImportIndex = FPackageObjectIndex::FromPackagePath(FullName);
			}

			Package->Imports.Add(GlobalImportIndex);

			if (bIsPackage && GlobalImportIndex.IsNull())
			{
				FPackageId PackageId = FPackageId::FromName(Import.ObjectName);
				Package->ImportedPackageIds.Add(PackageId);
				if (FPackage* ImportedPackage = PackageIdMap.FindRef(PackageId))
				{
					Package->ImportedPackages.Add(ImportedPackage);
				}
			}
		}
	}
}

static void MapExportEntryIndices(
	TArray<FObjectExport>& ObjectExports,
	TArray<FExportObjectData>& GlobalExports,
	TArray<FPackage*>& Packages)
{
	IOSTORE_CPU_SCOPE(ExportData);
	UE_LOG(LogIoStore, Display, TEXT("Converting export map import indices..."));

	auto PackageObjectIdFromPackageIndex =
		[](const TArray<FPackageObjectIndex>& Imports, const FPackageIndex& PackageIndex) -> FPackageObjectIndex 
		{ 
			if (PackageIndex.IsImport())
			{
				return Imports[PackageIndex.ToImport()];
			}
			if (PackageIndex.IsExport())
			{
				return FPackageObjectIndex::FromExportIndex(PackageIndex.ToExport());
			}
			return FPackageObjectIndex();
		};

	for (FPackage* Package : Packages)
	{
		for (int32 I = 0; I < Package->ExportCount; ++I)
		{
			const FObjectExport& ObjectExport = ObjectExports[Package->ExportIndexOffset + I];
			FExportObjectData& ExportData = GlobalExports[Package->Exports[I]];
			ExportData.OuterIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.OuterIndex);
			ExportData.ClassIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.ClassIndex);
			ExportData.SuperIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.SuperIndex);
			ExportData.TemplateIndex = PackageObjectIdFromPackageIndex(Package->Imports, ObjectExport.TemplateIndex);
		}
	}
};

static void ProcessLocalizedPackages(
	const TArray<FPackage*>& Packages,
	const FPackageNameMap& PackageMap,
	FGlobalPackageData& GlobalPackageData,
	FSourceToLocalizedPackageMultimap& OutSourceToLocalizedPackageMap)
{
	IOSTORE_CPU_SCOPE(ProcessLocalizedPackages);

	FLocalizedToSourceImportIndexMap LocalizedToSourceImportIndexMap;

	UE_LOG(LogIoStore, Display, TEXT("Conforming localized packages..."));
	for (FPackage* Package : Packages)
	{
		if (Package->Region.Len() == 0)
		{
			continue;
		}

		check(!Package->RedirectedPackageId.IsValid());
		if (Package->Name == Package->SourcePackageName)
		{
			UE_LOG(LogIoStore, Error,
				TEXT("For culture '%s': Localized package '%s' (0x%llX) should have a package name different from source name."),
				*Package->Region,
				*Package->Name.ToString(),
				Package->GlobalPackageId.ValueForDebugging())
			continue;
		}

		FPackage* SourcePackage = PackageMap.FindRef(Package->SourcePackageName);
		if (!SourcePackage)
		{
			// no update or verification required
			UE_LOG(LogIoStore, Verbose,
				TEXT("For culture '%s': Localized package '%s' (0x%llX) is unique and does not override a source package."),
				*Package->Region,
				*Package->Name.ToString(),
				Package->GlobalPackageId.ValueForDebugging());
			continue;
		}

		Package->SourceGlobalPackageId = SourcePackage->GlobalPackageId;

		Package->bIsLocalizedAndConformed = ConformLocalizedPackage(
			PackageMap, GlobalPackageData, *SourcePackage,
			*Package, LocalizedToSourceImportIndexMap);

		if (Package->bIsLocalizedAndConformed)
		{
			UE_LOG(LogIoStore, Verbose, TEXT("For culture '%s': Adding conformed localized package '%s' (0x%llX) for '%s' (0x%llX). ")
				TEXT("When loading the source package, it will be remapped to this localized package."),
				*Package->Region,
				*Package->Name.ToString(),
				Package->GlobalPackageId.ValueForDebugging(),
				*Package->SourcePackageName.ToString(),
				SourcePackage->GlobalPackageId.ValueForDebugging());

			OutSourceToLocalizedPackageMap.Add(SourcePackage, Package);
		}
		else
		{
			UE_LOG(LogIoStore, Display,
				TEXT("For culture '%s': Localized package '%s' (0x%llX) does not conform to source package '%s' (0x%llX) due to mismatching public exports. ")
				TEXT("When loading the source package, it will never be remapped to this localized package."),
				*Package->Region,
				*Package->Name.ToString(),
				Package->GlobalPackageId.ValueForDebugging(),
				*Package->SourcePackageName.ToString(),
				SourcePackage->GlobalPackageId.ValueForDebugging());
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Adding localized import packages..."));
	TArray<FPackage*> LocalizedPackages;

	for (FPackage* Package : Packages)
	{
		LocalizedPackages.Reset();
		for (FPackage* ImportedPackage : Package->ImportedPackages)
		{
			LocalizedPackages.Reset();
			OutSourceToLocalizedPackageMap.MultiFind(ImportedPackage, LocalizedPackages);
			for (FPackage* LocalizedPackage : LocalizedPackages)
			{
				UE_LOG(LogIoStore, Verbose, TEXT("For package '%s' (0x%llX): Adding localized imported package '%s' (0x%llX)"),
					*Package->Name.ToString(),
					Package->GlobalPackageId.ValueForDebugging(),
					*LocalizedPackage->Name.ToString(),
					LocalizedPackage->GlobalPackageId.ValueForDebugging());
			}
		}
		Package->ImportedPackages.Append(LocalizedPackages);
	}

	UE_LOG(LogIoStore, Display, TEXT("Conforming localized imports..."));
	for (FPackage* Package : Packages)
	{
		for (FPackageObjectIndex& GlobalImportIndex : Package->Imports)
		{
			if (GlobalImportIndex.IsPackageImport())
			{
				// When building DLC's the export can be missing
				if (const FExportObjectData* Export = GlobalPackageData.FindPublicExport(GlobalImportIndex))
				{
					if (Export->Package->SourcePackageName != Export->Package->Name)
					{
						const FPackageObjectIndex* SourceGlobalImportIndex = LocalizedToSourceImportIndexMap.Find(GlobalImportIndex);
						if (SourceGlobalImportIndex)
						{
							GlobalImportIndex = *SourceGlobalImportIndex;

							const FExportObjectData& SourceExportData = *GlobalPackageData.FindPublicExport(*SourceGlobalImportIndex);
							UE_LOG(LogIoStore, Verbose,
								TEXT("For package '%s' (0x%llX): Remap localized import %s to source import %s (in a conformed localized package)"),
								*Package->Name.ToString(),
								Package->GlobalPackageId.ValueForDebugging(),
								*Export->FullName,
								*SourceExportData.FullName);
						}
						else
						{
							UE_LOG(LogIoStore, Verbose,
								TEXT("For package '%s' (0x%llX): Skip remap for localized import %s")
								TEXT(", either there is no source package or the localized package did not conform to it."),
								*Package->Name.ToString(),
								Package->GlobalPackageId.ValueForDebugging(),
								*Export->FullName);
						}
					}
				}
			}
		}
	}
}

TUniquePtr<FIoStoreReader> CreateIoStoreReader(const TCHAR* Path, const FKeyChain& KeyChain)
{
	FIoStoreEnvironment IoEnvironment;
	IoEnvironment.InitializeFileEnvironment(FPaths::ChangeExtension(Path, TEXT("")));
	TUniquePtr<FIoStoreReader> IoStoreReader(new FIoStoreReader());

	TMap<FGuid, FAES::FAESKey> DecryptionKeys;
	for (const auto& KV : KeyChain.EncryptionKeys)
	{
		DecryptionKeys.Add(KV.Key, KV.Value.Key);
	}
	FIoStatus Status = IoStoreReader->Initialize(IoEnvironment, DecryptionKeys);
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
	TArray<FPackage*>& Packages,
	FPackageNameMap& PackageNameMap,
	FPackageIdMap& PackageIdMap,
	TArray<FContainerTargetSpec*>& ContainerTargets,
	FNameMapBuilder& GlobalNameMapBuilder)
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

		ContainerTarget->LocalNameMapBuilder.SetNameMapType(FMappedName::EType::Container);
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
				FPackage* Package = nullptr;
				const bool bIsMemoryMappedBulkData = CookedFileStatData->FileExt == FCookedFileStatData::EFileExt::UMappedBulk;

				if (bIsMemoryMappedBulkData)
				{
					FString TmpFileName = FString(RelativeFileName.Len() - 8, GetData(RelativeFileName)) + TEXT(".ubulk");
					Package = FindOrAddPackage(Arguments, *TmpFileName, Packages, PackageNameMap, PackageIdMap);
				}
				else
				{
					Package = FindOrAddPackage(Arguments, *RelativeFileName, Packages, PackageNameMap, PackageIdMap);
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
						ContainerTarget->bUseLocalNameMap = true;
					}

					if (CookedFileStatData->FileType == FCookedFileStatData::BulkData)
					{
						TargetFile.bIsBulkData = true;
						if (CookedFileStatData->FileExt == FCookedFileStatData::UPtnl)
						{
							TargetFile.bIsOptionalBulkData = true;
							TargetFile.ChunkId = CreateChunkId(Package->GlobalPackageId, 0, BulkdataTypeToChunkIdType(FPackageStoreBulkDataManifest::EBulkdataType::Optional), *TargetFile.TargetPath);
						}
						else if (CookedFileStatData->FileExt == FCookedFileStatData::UMappedBulk)
						{
							TargetFile.bIsMemoryMappedBulkData = true;
							TargetFile.bForceUncompressed = true;
							TargetFile.ChunkId = CreateChunkId(Package->GlobalPackageId, 0, BulkdataTypeToChunkIdType(FPackageStoreBulkDataManifest::EBulkdataType::MemoryMapped), *TargetFile.TargetPath);
						}
						else
						{
							TargetFile.ChunkId = CreateChunkId(Package->GlobalPackageId, 0, BulkdataTypeToChunkIdType(FPackageStoreBulkDataManifest::EBulkdataType::Normal), *TargetFile.TargetPath);
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
						TargetFile.NameMapBuilder = &Package->LocalNameMapBuilder;
					}

					if (TargetFile.bForceUncompressed && !SourceFile.bNeedsEncryption)
					{
						// Only keep the regions for the file if neither compression nor encryption are enabled, otherwise the regions will be meaningless.
						TargetFile.FileRegions = CookedFileStatData->FileRegions;
					}
				}
			}

			if (!ContainerTarget->bUseLocalNameMap)
			{
				ContainerTarget->NameMapBuilder = &GlobalNameMapBuilder;
			}
		}
	}

	Algo::Sort(Packages, [](const FPackage* A, const FPackage* B)
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

			UGraphSize += TargetFile.UGraphSize;
			ImportMapSize += TargetFile.ImportMapSize;
			ExportMapSize += TargetFile.ExportMapSize;
			NameMapSize += TargetFile.NameMapSize;
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
	FIoStoreWriteRequestManager()
		: MemoryAvailableEvent(FPlatformProcess::GetSynchEventFromPool(false))
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

	IIoStoreWriteRequest* Read(const FContainerTargetFile& InTargetFile, const TArray<FObjectExport>* InObjectExports)
	{
		return new FWriteContainerTargetFileRequest(*this, InTargetFile, InObjectExports);
	}

private:
	class FWriteContainerTargetFileRequest
		: public IIoStoreWriteRequest
	{
	public:
		FWriteContainerTargetFileRequest(FIoStoreWriteRequestManager& InManager, const FContainerTargetFile& InTargetFile, const TArray<FObjectExport>* InObjectExports)
			: Manager(InManager)
			, TargetFile(InTargetFile)
			, ObjectExports(InObjectExports)
			, FileRegions(TargetFile.FileRegions)
		{
			check(InTargetFile.bIsBulkData || InObjectExports);
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

		FIoBuffer ConsumeSourceBuffer() override
		{
			Manager.OnBufferMemoryFreed(TargetFile.SourceSize);
			FIoBuffer Result = MoveTemp(SourceBuffer);
			return Result;
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
			SourceBuffer = FIoBuffer(TargetFile.SourceSize);
			return SourceBuffer;
		}

		void AsyncReadCallback()
		{
			//TRACE_CPUPROFILER_EVENT_SCOPE(ReadCallback);
			if (!TargetFile.bIsBulkData)
			{
				check(ObjectExports);
				SourceBuffer = CreateExportBundleBuffer(TargetFile, *ObjectExports, SourceBuffer, bHasUpdatedExportBundleRegions ? nullptr : &FileRegions);
				bHasUpdatedExportBundleRegions = true;
			}
			TArray<FBaseGraphTask*> NewTasks;
			CompletionEvent->DispatchSubsequents(NewTasks);
		}

	private:
		FIoStoreWriteRequestManager& Manager;
		const FContainerTargetFile& TargetFile;
		const TArray<FObjectExport>* ObjectExports;
		TArray<FFileRegion> FileRegions;
		FGraphEventRef CompletionEvent;
		FIoBuffer SourceBuffer;
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

	TFuture<void> InitiatorThread;
	TFuture<void> RetirerThread;
	FQueue InitiatorQueue;
	FQueue RetirerQueue;
	TAtomic<uint64> UsedBufferMemory { 0 };
	FEvent* MemoryAvailableEvent;

	static constexpr uint64 BufferMemoryLimit = 2ull << 30;
};

class FIoStoreProgressReporter
{
public:
	FIoStoreProgressReporter(const FIoStoreWriterContext& InWriterContext)
		: WriterContext(InWriterContext)
		, StopEvent(FPlatformProcess::GetSynchEventFromPool(false))
	{
		ReporterThread = Async(EAsyncExecution::Thread, [this]() { ReporterThreadFunc(); });
	}

	~FIoStoreProgressReporter()
	{
		bStop.Store(true);
		StopEvent->Trigger();
		ReporterThread.Wait();
		FPlatformProcess::ReturnSynchEventToPool(StopEvent);
	}

private:
	void ReporterThreadFunc()
	{
		while (!bStop.Load())
		{
			StopEvent->Wait(FTimespan::FromSeconds(2.0));
			FIoStoreWriterContext::FProgress Progress = WriterContext.GetProgress();
			UE_LOG(LogIoStore, Display, TEXT("Hashed, Compressed, Serialized: %lld, %lld, %lld / %lld"), Progress.HashedChunksCount, Progress.CompressedChunksCount, Progress.SerializedChunksCount, Progress.TotalChunksCount);
		}
	}

	const FIoStoreWriterContext& WriterContext;
	TFuture<void> ReporterThread;
	FEvent* StopEvent;
	TAtomic<bool> bStop{ false };
};

int32 CreateTarget(const FIoStoreArguments& Arguments, const FIoStoreWriterSettings& GeneralIoWriterSettings)
{
	TGuardValue<int32> GuardAllowUnversionedContentInEditor(GAllowUnversionedContentInEditor, 1);

	FPackageStoreBulkDataManifest BulkDataManifest(FString(Arguments.CookedDir) / FApp::GetProjectName());
	if (!BulkDataManifest.Load())
	{
		UE_LOG(LogIoStore, Warning, TEXT("Failed to load Bulk Data manifest %s"), *BulkDataManifest.GetFilename());
	}
	else
	{
		UE_LOG(LogIoStore, Display, TEXT("Loaded Bulk Data manifest '%s'"), *BulkDataManifest.GetFilename());
	}
	
#if OUTPUT_CHUNKID_DIRECTORY
	ChunkIdCsv.CreateOutputFile(CookedDir);
#endif

	FNameMapBuilder GlobalNameMapBuilder;
	GlobalNameMapBuilder.SetNameMapType(FMappedName::EType::Global);
	FPackageAssetData PackageAssetData;
	FGlobalPackageData GlobalPackageData;

	TArray<FPackage*> Packages;
	FPackageNameMap PackageNameMap;
	FPackageIdMap PackageIdMap;

	FIoStoreWriteRequestManager WriteRequestManager;

	TArray<FContainerTargetSpec*> ContainerTargets;
	UE_LOG(LogIoStore, Display, TEXT("Creating container targets..."));
	{
		IOSTORE_CPU_SCOPE(CreateContainerTargets);
		InitializeContainerTargetsAndPackages(Arguments, Packages, PackageNameMap, PackageIdMap, ContainerTargets, GlobalNameMapBuilder);
	}

	TUniquePtr<FIoStoreWriterContext> IoStoreWriterContext(new FIoStoreWriterContext());
	TArray<FIoStoreWriter*> IoStoreWriters;
	FIoStoreEnvironment GlobalIoStoreEnv;
	FIoStoreWriter* GlobalIoStoreWriter = nullptr;
	{
		IOSTORE_CPU_SCOPE(InitializeIoStoreWriters);
		if (!Arguments.IsDLC())
		{
			GlobalIoStoreEnv.InitializeFileEnvironment(*Arguments.GlobalContainerPath);
			GlobalIoStoreWriter = new FIoStoreWriter(GlobalIoStoreEnv);
			IoStoreWriters.Add(GlobalIoStoreWriter);
		}
		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			check(ContainerTarget->Header.ContainerId.IsValid());
			if (!ContainerTarget->OutputPath.IsEmpty())
			{
				ContainerTarget->IoStoreEnv.Reset(new FIoStoreEnvironment());
				ContainerTarget->IoStoreEnv->InitializeFileEnvironment(ContainerTarget->OutputPath);
				ContainerTarget->IoStoreWriter = new FIoStoreWriter(*ContainerTarget->IoStoreEnv);
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
				ContainerSettings.ContainerId = ContainerTarget->Header.ContainerId;
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
				IoStatus = ContainerTarget->IoStoreWriter->Initialize(*IoStoreWriterContext, ContainerSettings, ContainerTarget->PatchSourceReaders);
				check(IoStatus.IsOk());
			}
		}
	}

	ParsePackageAssets(Packages, PackageAssetData);

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
					ContainerTarget->IoStoreWriter->Append(TargetFile.ChunkId, WriteRequestManager.Read(TargetFile, nullptr), WriteOptions);
				}
			}
		}
	}

	FExportGraph ExportGraph(PackageAssetData.ObjectExports.Num(), PackageAssetData.PreloadDependencies.Num());
	GlobalPackageData.Reserve(PackageAssetData.ObjectExports.Num());

	CreateGlobalScriptObjects(GlobalPackageData, Arguments.TargetPlatform);
	CreateGlobalImportsAndExports(Arguments, Packages, PackageIdMap, PackageAssetData, GlobalPackageData, ExportGraph);

	// Mapped import and exports are required before processing localization, and preload/postload arcs
	MapExportEntryIndices(PackageAssetData.ObjectExports, GlobalPackageData.ExportObjects, Packages);
	// Intermediate map for adding extra ImportedPackages and ExternalArcs to the build graph
	// for every dependency on a source package with localizations, add dependencies to all language localizations
	FSourceToLocalizedPackageMultimap SourceToLocalizedPackageMap;

	ProcessLocalizedPackages(Packages, PackageNameMap, GlobalPackageData, SourceToLocalizedPackageMap);

	for (FPackage* Package : Packages)
	{
		if (Package->RedirectedPackageId.IsValid())
		{
			for (int32 ExportIndex : Package->Exports)
			{
				const FExportObjectData& ExportData = GlobalPackageData.ExportObjects[ExportIndex];
				if (!ExportData.SuperIndex.IsNull() && ExportData.OuterIndex.IsNull())
				{
					UE_LOG(LogIoStore, Warning, TEXT("Skipping redirect to package '%s' due to presence of UStruct '%s'"), *Package->Name.ToString(), *ExportData.ObjectName.ToString());
					Package->RedirectedPackageId = FPackageId();
					break;
				}
			}
		}
	}

	AddPreloadDependencies(
		PackageAssetData,
		GlobalPackageData,
		SourceToLocalizedPackageMap,
		ExportGraph,
		Packages);

	BuildBundles(ExportGraph, Packages);

#if OUTPUT_DEBUG_PACKAGE_EXPORT_BUNDLES
	if (!Arguments.MetaOutputDir.IsEmpty())
	{
		TUniquePtr<IFileHandle> ExportBundleMetaFile(FPlatformFileManager::Get().GetPlatformFile().OpenWrite(*(Arguments.MetaOutputDir / TEXT("iodispatcher.uexportbundles"))));
		for (const FPackage* Package : Packages)
		{
			int32 ExportBundleIndex = 0;
			for (const FExportBundle& ExportBundle : Package->ExportBundles)
			{
				FString Text = FString::Printf(TEXT("%s[%d]\n"), *Package->Name.ToString(), ExportBundleIndex);
				ExportBundleMetaFile->Write((uint8*)StringCast<ANSICHAR>(*Text).Get(), Text.Len());
				for (const FExportGraphNode* Node : ExportBundle.Nodes)
				{
					Text = FString::Printf(TEXT("- %s %d\n"), Node->BundleEntry.CommandType == FExportBundleEntry::ExportCommandType_Serialize ? "S" : "C", Node->BundleEntry.LocalExportIndex);
					ExportBundleMetaFile->Write((uint8*)StringCast<ANSICHAR>(*Text).Get(), Text.Len());
				}
				++ExportBundleIndex;
			}
		}
		ExportBundleMetaFile->Flush();
	}
#endif

	{
		IOSTORE_CPU_SCOPE(FinalizeNameMaps);
		UE_LOG(LogIoStore, Display, TEXT("Finalizing name maps..."));

		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			FinalizeNameMaps(*ContainerTarget);
		}
	}

	{
		IOSTORE_CPU_SCOPE(FinalizePackageHeaders);
		UE_LOG(LogIoStore, Display, TEXT("Finalizing package headers..."));

		for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
		{
			FinalizePackageHeaders(
				*ContainerTarget,
				PackageAssetData.ObjectExports,
				GlobalPackageData.ExportObjects,
				GlobalPackageData.ImportsByFullName);

			FinalizePackageStoreContainerHeader(*ContainerTarget);

			const FNameMapBuilder& NameMapBuilder = *ContainerTarget->NameMapBuilder;
			SaveNameBatch(ContainerTarget->LocalNameMapBuilder.GetNameMap(), ContainerTarget->Header.Names, ContainerTarget->Header.NameHashes);
		}
	}

	UE_LOG(LogIoStore, Display, TEXT("Creating disk layout..."));
	CreateDiskLayout(ContainerTargets, Packages, Arguments.GameOrderMap, Arguments.CookerOrderMap);

	for (FContainerTargetSpec* ContainerTarget : ContainerTargets)
	{
		if (ContainerTarget->IoStoreWriter)
		{
			for (FContainerTargetFile& TargetFile : ContainerTarget->TargetFiles)
			{
				if (!TargetFile.bIsBulkData)
				{
					FIoWriteOptions WriteOptions;
					WriteOptions.DebugName = *TargetFile.TargetPath;
					WriteOptions.bForceUncompressed = TargetFile.bForceUncompressed;
					WriteOptions.FileName = TargetFile.DestinationPath;
					ContainerTarget->IoStoreWriter->Append(TargetFile.ChunkId, WriteRequestManager.Read(TargetFile, &PackageAssetData.ObjectExports), WriteOptions);
				}
			}

			FLargeMemoryWriter Ar(0, true);
			Ar << ContainerTarget->Header;

			FIoWriteOptions WriteOptions;
			WriteOptions.DebugName = TEXT("ContainerHeader");
			ContainerTarget->IoStoreWriter->Append(
				CreateIoChunkId(ContainerTarget->Header.ContainerId.Value(), 0, EIoChunkType::ContainerHeader),
				FIoBuffer(FIoBuffer::Wrap, Ar.GetData(), Ar.TotalSize()), WriteOptions);
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
		FLargeMemoryWriter InitialLoadArchive(0, true);
		FinalizeInitialLoadMeta(
			GlobalNameMapBuilder,
			GlobalPackageData.ScriptObjects,
			InitialLoadArchive);

		InitialLoadSize = InitialLoadArchive.Tell();

		UE_LOG(LogIoStore, Display, TEXT("Serializing global meta data"));
		IOSTORE_CPU_SCOPE(SerializeInitialLoadMeta);
		FIoWriteOptions WriteOptions;
		WriteOptions.DebugName = TEXT("LoaderInitialLoadMeta");
		GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderInitialLoadMeta), FIoBuffer(FIoBuffer::Wrap, InitialLoadArchive.GetData(), InitialLoadArchive.TotalSize()), WriteOptions);
	}

	uint64 GlobalNamesMB = 0;
	uint64 GlobalNameHashesMB = 0;
	if (GlobalIoStoreWriter)
	{
		IOSTORE_CPU_SCOPE(SerializeGlobalNameMap);

		UE_LOG(LogIoStore, Display, TEXT("Saving global name map to container file"));

		TArray<uint8> Names;
		TArray<uint8> Hashes;
		SaveNameBatch(GlobalNameMapBuilder.GetNameMap(), /* out */ Names, /* out */ Hashes);

		InitialLoadSize += Names.Num() + Hashes.Num();
		GlobalNamesMB = Names.Num() >> 20;
		GlobalNameHashesMB = Hashes.Num() >> 20;

		FIoWriteOptions WriteOptions;
		WriteOptions.DebugName = TEXT("LoaderGlobalNames");
		GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNames), 
													 FIoBuffer(FIoBuffer::Wrap, Names.GetData(), Names.Num()), WriteOptions);
		WriteOptions.DebugName = TEXT("LoaderGlobalNameHashes");
		GlobalIoStoreWriter->Append(CreateIoChunkId(0, 0, EIoChunkType::LoaderGlobalNameHashes),
													 FIoBuffer(FIoBuffer::Wrap, Hashes.GetData(), Hashes.Num()), WriteOptions);
		
#if OUTPUT_NAMEMAP_CSV
		NameMapBuilder.SaveCsv(OutputDir / TEXT("Container.namemap.csv"));
#endif
	}

	UE_LOG(LogIoStore, Display, TEXT("Serializing container(s)..."));

	FIoStoreProgressReporter* IoStoreProgressReporter = new FIoStoreProgressReporter(*IoStoreWriterContext);

	TArray<FIoStoreWriterResult> IoStoreWriterResults;
	IoStoreWriterResults.Reserve(IoStoreWriters.Num());
	for (FIoStoreWriter* IoStoreWriter : IoStoreWriters)
	{
		IoStoreWriterResults.Emplace(IoStoreWriter->Flush().ConsumeValueOrDie());
		delete IoStoreWriter;
	}
	IoStoreWriters.Empty();

	delete IoStoreProgressReporter;

	IOSTORE_CPU_SCOPE(CalculateStats);

	UE_LOG(LogIoStore, Display, TEXT("Calculating stats..."));
	uint64 UExpSize = 0;
	uint64 UAssetSize = 0;
	uint64 SummarySize = 0;
	uint64 PackageSummarySize = Packages.Num() * sizeof(FPackageSummary);
	uint64 ImportedPackagesCount = 0;
	uint64 NoImportedPackagesCount = 0;
	uint64 PublicExportsCount = 0;
	uint64 TotalExternalArcCount = 0;
	uint64 NameMapCount = 0;

	uint64 BundleCount = 0;
	uint64 BundleEntryCount = 0;

	for (const FPackage* Package : Packages)
	{
		UExpSize += Package->UExpSize;
		UAssetSize += Package->UAssetSize;
		SummarySize += Package->SummarySize;
		NameMapCount += Package->SummaryNames.Num();
		ImportedPackagesCount += Package->ImportedPackages.Num();
		NoImportedPackagesCount += Package->ImportedPackages.Num() == 0;

		for (auto& KV : Package->ExternalArcs)
		{
			const TArray<FArc>& Arcs = KV.Value;
			TotalExternalArcCount += Arcs.Num();
		}

		for (const FExportBundle& Bundle : Package->ExportBundles)
		{
			++BundleCount;
			BundleEntryCount += Bundle.Nodes.Num();
		}
	}

	for (FExportObjectData& Export : GlobalPackageData.ExportObjects)
	{
		if (Export.IsPublicExport())
		{
			++PublicExportsCount;
		}
	}

	LogWriterResults(IoStoreWriterResults);
	LogContainerPackageInfo(ContainerTargets);
	
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8.2lf MB UExp"), (double)UExpSize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8.2lf MB UAsset"), (double)UAssetSize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8.2lf MB FPackageFileSummary"), (double)SummarySize / 1024.0 / 1024.0);
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8d Packages"), Packages.Num());
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8llu Imported package entries"), ImportedPackagesCount);
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8llu Packages without imports"), NoImportedPackagesCount);
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8llu Name map entries"), NameMapCount);
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8d PreloadDependencies entries"), PackageAssetData.PreloadDependencies.Num());
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8d ImportMap entries"), PackageAssetData.ObjectImports.Num());
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8d ExportMap entries"), PackageAssetData.ObjectExports.Num());
	UE_LOG(LogIoStore, Display, TEXT("Input:  %8llu Public exports"), PublicExportsCount);
	UE_LOG(LogIoStore, Display, TEXT(""));
	UE_LOG(LogIoStore, Display, TEXT("Output: %8llu Export bundles"), BundleCount);
	UE_LOG(LogIoStore, Display, TEXT("Output: %8llu Export bundle entries"), BundleEntryCount);
	UE_LOG(LogIoStore, Display, TEXT("Output: %8llu Export bundle arcs"), TotalExternalArcCount);
	UE_LOG(LogIoStore, Display, TEXT("Output: %8d Public runtime script objects"), GlobalPackageData.ScriptObjects.Num());
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

		FIoStoreEnvironment IoStoreEnv;
		FIoStoreWriter IoStoreWriter(IoStoreEnv);
		IoStoreEnv.InitializeFileEnvironment(*Container.OutputPath);
		
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

static bool ParsePakOrderFile(const TCHAR* FilePath, TMap<FName, uint64>& OutMap, bool bMerge)
{
	TArray<FString> OrderFileContents;
	if (!FFileHelper::LoadFileToStringArray(OrderFileContents, FilePath))
	{
		UE_LOG(LogIoStore, Error, TEXT("Failed to read order file '%s'."), *FilePath);
		return false;
	}

	uint64 LineNumber = 1;

	if (bMerge)
	{
		const auto* maxValue = Algo::MaxElementBy(OutMap, [](const auto& data) { return data.Value; });
		if(maxValue != nullptr)
		{ 
			LineNumber += maxValue->Value;
		}
	}

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

		uint64 Order = LineNumber;
		
		FName PackageFName(MoveTemp(PackageName));
		if (!OutMap.Contains(PackageFName))
		{
			OutMap.Emplace(PackageFName, Order);
		}

		++LineNumber;
	}
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

	FString GameOrderFileStr;
	if (FParse::Value(FCommandLine::Get(), TEXT("GameOrder="), GameOrderFileStr, false))
	{
		TArray<FString> GameOrderFilePaths;
		GameOrderFileStr.ParseIntoArray(GameOrderFilePaths, TEXT(","), true);
		bool bMerge = false;
		for (FString& PrimaryOrderFile : GameOrderFilePaths)
		{
			if (!ParsePakOrderFile(*PrimaryOrderFile, Arguments.GameOrderMap, bMerge))
			{
				return -1;
			}
			bMerge = true;
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
			// Pass the GameOpenOrder as excusion map, since new entries from the CookerOpenOrder could potentially resolve to packages already in the GameOpenOrder.
			if (!ParsePakOrderFile(*SecondOrderFile, Arguments.CookerOrderMap, bMerge))
			{
				return -1;
			}
			bMerge = true;
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
