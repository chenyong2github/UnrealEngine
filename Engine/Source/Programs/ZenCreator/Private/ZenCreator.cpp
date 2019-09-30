// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.


#include "ZenCreator.h"

#include "RequiredProgramMainCPPInclude.h"
#include "UObject/PackageFileSummary.h"
#include "UObject/ObjectResource.h"
#include "UObject/Package.h"
#include "Misc/PackageName.h"
#include "Serialization/Archive.h"
#include "IPlatformFilePak.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/BufferWriter.h"
#include "Misc/Base64.h"
#include "IO/IoDispatcher.h"

#include "Algo/Find.h"
//PRAGMA_DISABLE_OPTIMIZATION

DEFINE_LOG_CATEGORY_STATIC(LogZenCreator, Log, All);

IMPLEMENT_APPLICATION(ZenCreator, "ZenCreator");

class FGlobalNameMap
{
public:
	void Load(const FString& FilePath)
	{
		UE_LOG(LogZenCreator, Display, TEXT("Loading global name map from '%s' for container files..."), *FilePath);

		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*FilePath));
		check(Ar);

		int32 NameCount;
		*Ar << NameCount;

		DisplayEntries.Reserve(NameCount);
		ComparisonEntries.Reserve(NameCount);

		FNameEntrySerialized SerializedNameEntry(ENAME_LinkerConstructor);

		for (int32 I = 0; I < NameCount; ++I)
		{
			*Ar << SerializedNameEntry;
			FName Name(SerializedNameEntry);

			DisplayEntries.Emplace(Name.GetDisplayIndex());
			DisplayEntryToIndex.Emplace(DisplayEntries[I], I);

			ComparisonEntries.Emplace(Name.GetComparisonIndex());
			ComparisonEntryToIndex.Emplace(ComparisonEntries[I], I);
		}
	}

	void Save(const FString& FilePath)
	{
		UE_LOG(LogZenCreator, Display, TEXT("Saving Container name map to '%s' with '%d' additional names"),
			*FilePath,
			ComparisonEntries.Num() - DisplayEntries.Num());

		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileWriter(*FilePath));
		check(Ar);

		int32 NameCount = ComparisonEntries.Num();
		*Ar << NameCount;
		
		for (int32 I = 0; I < NameCount; ++I)
		{
			FName::GetEntry(ComparisonEntries[I])->Write(*Ar);
		}
	}

	FName GetNameFromDisplayIndex(const uint32 DisplayIndex, const uint32 NameNumber) const
	{
		FNameEntryId DisplayEntry = DisplayEntries[DisplayIndex];
		return FName::CreateFromDisplayId(DisplayEntry, NameNumber);
	}

	const int32* GetComparisonIndex(const FName& Name) const
	{
		return ComparisonEntryToIndex.Find(Name.GetComparisonIndex());
	}

	int32 GetOrCreateComparisonIndex(const FName& Name)
	{
		if (const int32* ExistingIndex = ComparisonEntryToIndex.Find(Name.GetComparisonIndex()))
		{
			return *ExistingIndex;
		}
		else
		{
			int32 NewIndex = ComparisonEntries.Add(Name.GetComparisonIndex());
			ComparisonEntryToIndex.Add(ComparisonEntries[NewIndex], NewIndex);
			return NewIndex;
		}
	}
private:
	TArray<FNameEntryId> DisplayEntries;
	TArray<FNameEntryId> ComparisonEntries;
	TMap<FNameEntryId, int32> DisplayEntryToIndex;
	TMap<FNameEntryId, int32> ComparisonEntryToIndex;
};

enum class EZenChunkType : uint8
{
	None,
	PackageSummary,
	ExportData,
	BulkData
};

FIoChunkId CreateZenChunkId(uint32 NameIndex, uint32 NameNumber, uint16 ChunkIndex, EZenChunkType ChunkType)
{
	uint8 Data[12] = {0};

	*reinterpret_cast<uint32*>(&Data[0]) = NameIndex;
	*reinterpret_cast<int32*>(&Data[4]) = NameNumber;
	*reinterpret_cast<uint16*>(&Data[8]) = ChunkIndex;
	*reinterpret_cast<uint8*>(&Data[10]) = static_cast<uint8>(ChunkType);

	FIoChunkId ChunkId;
	ChunkId.Set(Data, 12);

	return ChunkId;
}

struct FZenPackageSummary
{
	FGuid Guid;
	uint32 PackageFlags;
	int32 ImportCount;
	int32 ExportCount;
	int32 PreloadDependencyCount;
	int32 ExportOffset;
	int32 GraphDataOffset;
	int32 GraphDataSize;
	int32 BulkDataStartOffset;
};

struct FNamedAESKey
{
	FString Name;
	FGuid Guid;
	FAES::FAESKey Key;

	bool IsValid() const
	{
		return Key.IsValid();
	}
};

struct FKeyChain
{
	FRSAKeyHandle SigningKey = InvalidRSAKeyHandle;
	TMap<FGuid, FNamedAESKey> EncryptionKeys;
	const FNamedAESKey* MasterEncryptionKey = nullptr;
};

FRSAKeyHandle ParseRSAKeyFromJson(TSharedPtr<FJsonObject> InObj)
{
	TSharedPtr<FJsonObject> PublicKey = InObj->GetObjectField(TEXT("PublicKey"));
	TSharedPtr<FJsonObject> PrivateKey = InObj->GetObjectField(TEXT("PrivateKey"));

	FString PublicExponentBase64, PrivateExponentBase64, PublicModulusBase64, PrivateModulusBase64;

	if (PublicKey->TryGetStringField("Exponent", PublicExponentBase64)
		&& PublicKey->TryGetStringField("Modulus", PublicModulusBase64)
		&& PrivateKey->TryGetStringField("Exponent", PrivateExponentBase64)
		&& PrivateKey->TryGetStringField("Modulus", PrivateModulusBase64))
	{
		check(PublicModulusBase64 == PrivateModulusBase64);

		TArray<uint8> PublicExponent, PrivateExponent, Modulus;
		FBase64::Decode(PublicExponentBase64, PublicExponent);
		FBase64::Decode(PrivateExponentBase64, PrivateExponent);
		FBase64::Decode(PublicModulusBase64, Modulus);

		return FRSA::CreateKey(PublicExponent, PrivateExponent, Modulus);
	}
	else
	{
		return nullptr;
	}
}

void LoadKeyChainFromFile(const FString& InFilename, FKeyChain& OutCryptoSettings)
{
	FArchive* File = IFileManager::Get().CreateFileReader(*InFilename);
	UE_CLOG(File == nullptr, LogPakFile, Fatal, TEXT("Specified crypto keys cache '%s' does not exist!"), *InFilename);
	TSharedPtr<FJsonObject> RootObject;
	TSharedRef<TJsonReader<char>> Reader = TJsonReaderFactory<char>::Create(File);
	if (FJsonSerializer::Deserialize(Reader, RootObject))
	{
		const TSharedPtr<FJsonObject>* EncryptionKeyObject;
		if (RootObject->TryGetObjectField(TEXT("EncryptionKey"), EncryptionKeyObject))
		{
			FString EncryptionKeyBase64;
			if ((*EncryptionKeyObject)->TryGetStringField(TEXT("Key"), EncryptionKeyBase64))
			{
				if (EncryptionKeyBase64.Len() > 0)
				{
					TArray<uint8> Key;
					FBase64::Decode(EncryptionKeyBase64, Key);
					check(Key.Num() == sizeof(FAES::FAESKey::Key));
					FNamedAESKey NewKey;
					NewKey.Name = TEXT("Default");
					NewKey.Guid = FGuid();
					FMemory::Memcpy(NewKey.Key.Key, &Key[0], sizeof(FAES::FAESKey::Key));
					OutCryptoSettings.EncryptionKeys.Add(NewKey.Guid, NewKey);
				}
			}
		}

		const TSharedPtr<FJsonObject>* SigningKey = nullptr;
		if (RootObject->TryGetObjectField(TEXT("SigningKey"), SigningKey))
		{
			OutCryptoSettings.SigningKey = ParseRSAKeyFromJson(*SigningKey);
		}

		const TArray<TSharedPtr<FJsonValue>>* SecondaryEncryptionKeyArray = nullptr;
		if (RootObject->TryGetArrayField(TEXT("SecondaryEncryptionKeys"), SecondaryEncryptionKeyArray))
		{
			for (TSharedPtr<FJsonValue> EncryptionKeyValue : *SecondaryEncryptionKeyArray)
			{
				FNamedAESKey NewKey;
				TSharedPtr<FJsonObject> SecondaryEncryptionKeyObject = EncryptionKeyValue->AsObject();
				FGuid::Parse(SecondaryEncryptionKeyObject->GetStringField(TEXT("Guid")), NewKey.Guid);
				NewKey.Name = SecondaryEncryptionKeyObject->GetStringField(TEXT("Name"));
				FString KeyBase64 = SecondaryEncryptionKeyObject->GetStringField(TEXT("Key"));

				TArray<uint8> Key;
				FBase64::Decode(KeyBase64, Key);
				check(Key.Num() == sizeof(FAES::FAESKey::Key));
				FMemory::Memcpy(NewKey.Key.Key, &Key[0], sizeof(FAES::FAESKey::Key));

				check(!OutCryptoSettings.EncryptionKeys.Contains(NewKey.Guid) || OutCryptoSettings.EncryptionKeys[NewKey.Guid].Key == NewKey.Key);
				OutCryptoSettings.EncryptionKeys.Add(NewKey.Guid, NewKey);
			}
		}
	}
	delete File;
	FGuid EncryptionKeyOverrideGuid;
	OutCryptoSettings.MasterEncryptionKey = OutCryptoSettings.EncryptionKeys.Find(EncryptionKeyOverrideGuid);
}

void ApplyEncryptionKeys(const FKeyChain& KeyChain)
{
	if (KeyChain.EncryptionKeys.Contains(FGuid()))
	{
		FAES::FAESKey DefaultKey = KeyChain.EncryptionKeys[FGuid()].Key;
		FCoreDelegates::GetPakEncryptionKeyDelegate().BindLambda([DefaultKey](uint8 OutKey[32]) { FMemory::Memcpy(OutKey, DefaultKey.Key, sizeof(DefaultKey.Key)); });
	}

	for (const TMap<FGuid, FNamedAESKey>::ElementType& Key : KeyChain.EncryptionKeys)
	{
		if (Key.Key.IsValid())
		{
			FCoreDelegates::GetRegisterEncryptionKeyDelegate().ExecuteIfBound(Key.Key, Key.Value.Key);
		}
	}
}

uint64 AppendToMegaFile(FArchive* PayloadArchive, const TCHAR* FileName, int64& OutOffset, int64& OutSize)
{
	if (IFileManager::Get().FileExists(FileName))
	{
		TUniquePtr<FArchive> SourceArchive(IFileManager::Get().CreateFileReader(FileName));
		OutOffset = PayloadArchive->Tell();
		OutSize = SourceArchive->TotalSize();

		int64 BytesLeft = OutSize;
		while (BytesLeft > 0)
		{
			constexpr int64 BufferSize = 256 << 10;
			uint8 Buffer[BufferSize];
			uint64 BytesToSerialize = FMath::Min(BytesLeft, BufferSize);
			SourceArchive->Serialize(Buffer, BytesToSerialize);
			PayloadArchive->Serialize(Buffer, BytesToSerialize);
			BytesLeft -= BytesToSerialize;
		}
		check(BytesLeft == 0);
	}
	else
	{
		OutOffset = -1;
		OutSize = 0;
	}
	return OutSize;
}

enum EEventLoadNode2 : uint8
{
	Package_CreateLinker,
	Package_LoadSummary,
	Package_ImportPackages,
	Package_SetupImports,
	Package_SetupExports,
	Package_ExportsSerialized,
	Package_PostLoad,
	Package_Tick,
	Package_Delete,
	Package_NumPhases,

	ImportOrExport_Create = 0,
	ImportOrExport_Serialize,
	Import_NumPhases,

	Export_StartIO = Import_NumPhases,
	Export_NumPhases,
};

struct FPackageArc
{
	uint32 FromNodeIndex;
	uint32 ToNodeIndex;

	bool operator<(const FPackageArc& Other) const
	{
		if (ToNodeIndex == Other.ToNodeIndex)
		{
			return FromNodeIndex < Other.ToNodeIndex;
		}
		return ToNodeIndex < Other.ToNodeIndex;
	}
};

struct FPackage
{
	FName Name;
	FString FileName;
	FString RelativeFileName;
	FGuid Guid;
	uint32 PackageFlags = 0;
	int32 ImportCount = 0;
	int32 ImportOffset = -1;
	int32 SlimportCount = 0;
	int32 SlimportOffset = -1;
	int32 ExportCount = 0;
	int32 ExportOffset = -1;
	int32 ExportIndexOffset = -1;
	int32 PreloadDependencyCount = 0;
	int32 PreloadDependencyOffset = -1;
	int64 BulkDataStartOffset = -1;
	int64 UAssetOffset = -1;
	int64 UAssetSize = 0;
	int64 UExpOffset = -1;
	int64 UExpSize = 0;
	int64 UBulkOffset = -1;
	int64 UBulkSize = 0;
	int64 UGraphOffset = -1;
	int64 UGraphSize = 0;

	TSet<FName> ImportedPackages;
	TArray<int32> Imports;
	TArray<int32> Exports;
	TArray<FPackageArc> InternalArcs;
	TMap<FName, TArray<FPackageArc>> ExternalArcs;
};

static uint32 GetNodeIndex(const FPackage& Package, FPackageIndex PackageIndex, EEventLoadNode2 Phase)
{
	if (PackageIndex.IsNull())
	{
		return Phase;
	}
	else if (PackageIndex.IsImport())
	{
		uint32 BaseIndex = EEventLoadNode2::Package_NumPhases;
		return BaseIndex + PackageIndex.ToImport() * EEventLoadNode2::Import_NumPhases + Phase;
	}
	else
	{
		uint32 BaseIndex = EEventLoadNode2::Package_NumPhases + Package.ImportCount * EEventLoadNode2::Import_NumPhases;
		return BaseIndex + PackageIndex.ToExport() * EEventLoadNode2::Export_NumPhases + Phase;
	}
}

static void AddArc(FPackage& FromPackage, FPackageIndex FromPackageIndex, EEventLoadNode2 FromPhase, FPackage& ToPackage, FPackageIndex ToPackageIndex, EEventLoadNode2 ToPhase)
{
	uint32 FromNodeIndex = GetNodeIndex(FromPackage, FromPackageIndex, FromPhase);
	uint32 ToNodeIndex = GetNodeIndex(ToPackage, ToPackageIndex, ToPhase);
	if (&FromPackage == &ToPackage)
	{
		check(FromNodeIndex != ToNodeIndex);
		FromPackage.InternalArcs.Add({ FromNodeIndex, ToNodeIndex });
	}
	else
	{
		TArray<FPackageArc>& ExternalArcs = ToPackage.ExternalArcs.FindOrAdd(FromPackage.Name);
		ExternalArcs.Add({ FromNodeIndex, ToNodeIndex });
	}
}

static void AddPostLoadDependenciesRecursive(FPackage& Package, FPackage& ImportedPackage, TSet<FName>& Visited, TMap<FName, FPackage>& PackageMap)
{
	if (&ImportedPackage == &Package || Visited.Contains(ImportedPackage.Name))
	{
		return;
	}
	Visited.Add(ImportedPackage.Name);

	AddArc(ImportedPackage, FPackageIndex(), EEventLoadNode2::Package_ExportsSerialized,
		Package, FPackageIndex(), EEventLoadNode2::Package_PostLoad);

	for (const FName& DependentPackageName : ImportedPackage.ImportedPackages)
	{
		FPackage* FindDependentPackage = PackageMap.Find(DependentPackageName);
		if (FindDependentPackage)
		{
			AddPostLoadDependenciesRecursive(Package, *FindDependentPackage, Visited, PackageMap);
		}
	}
}

struct FImportData
{
	int32 GlobalIndex = -1;
	int32 OuterIndex = -1;
	int32 OutermostIndex = -1;
	int32 RefCount = 0;
	FName ObjectName;
	bool bIsPackage = false;
	FString FullName;
};

struct FExportData
{
	int32 GlobalIndex = -1;
	FName SourcePackageName;
	FName ObjectName;
	int32 SourceIndex = -1;
	int32 GlobalImportIndex = -1;
	FString FullName;
};

void FindImport(TArray<FImportData>& GlobalImports, TMap<FString, int32>& GlobalImportsByFullName, TArray<FString>& TempFullNames, FObjectImport* ImportMap, int32 LocalImportIndex)
{
	FObjectImport* Import = &ImportMap[LocalImportIndex];
	FString& FullName = TempFullNames[LocalImportIndex];
	if (FullName.Len() == 0)
	{
		if (Import->OuterIndex.IsNull())
		{
			Import->ObjectName.AppendString(FullName);
			int32* FindGlobalImport = GlobalImportsByFullName.Find(FullName);
			if (!FindGlobalImport)
			{
				// first time, assign global index for this root package
				int32 GlobalImportIndex = GlobalImports.Num();
				GlobalImportsByFullName.Add(FullName, GlobalImportIndex);
				FImportData& GlobalImport = GlobalImports.AddDefaulted_GetRef();
				GlobalImport.GlobalIndex = GlobalImportIndex;
				GlobalImport.OutermostIndex = GlobalImportIndex;
				GlobalImport.OuterIndex = -1;
				GlobalImport.ObjectName = Import->ObjectName;
				GlobalImport.bIsPackage = true;
				GlobalImport.FullName = FullName;
				GlobalImport.RefCount = 1;
			}
			else
			{
				++GlobalImports[*FindGlobalImport].RefCount;
			}
		}
		else
		{
			int32 LocalOuterIndex = Import->OuterIndex.ToImport();
			FindImport(GlobalImports, GlobalImportsByFullName, TempFullNames, ImportMap, LocalOuterIndex);
			FString& OuterName = TempFullNames[LocalOuterIndex];
			ensure(OuterName.Len() > 0);

			FullName.Append(OuterName);
			FullName.AppendChar(TEXT('/'));
			Import->ObjectName.AppendString(FullName);

			int32* FindGlobalImport = GlobalImportsByFullName.Find(FullName);
			if (!FindGlobalImport)
			{
				// first time, assign global index for this intermediate import
				int32 GlobalImportIndex = GlobalImports.Num();
				GlobalImportsByFullName.Add(FullName, GlobalImportIndex);
				FImportData& GlobalImport = GlobalImports.AddDefaulted_GetRef();
				int32* FindOuterGlobalImport = GlobalImportsByFullName.Find(OuterName);
				check(FindOuterGlobalImport);
				const FImportData& OuterGlobalImport = GlobalImports[*FindOuterGlobalImport];
				GlobalImport.GlobalIndex = GlobalImportIndex;
				GlobalImport.OutermostIndex = OuterGlobalImport.OutermostIndex;
				GlobalImport.OuterIndex = OuterGlobalImport.GlobalIndex;
				GlobalImport.ObjectName = Import->ObjectName;
				GlobalImport.FullName = FullName;
				GlobalImport.RefCount = 1;
			}
			else
			{
				++GlobalImports[*FindGlobalImport].RefCount;
			}
		}
	}
};

void FindExport(TArray<FExportData>& GlobalExports, TMap<FString, int32>& GlobalExportsByFullName, TArray<FString>& TempFullNames, const FObjectExport* ExportMap, int32 LocalExportIndex, const FName& PackageName)
{
	const FObjectExport* Export = ExportMap + LocalExportIndex;
	FString& FullName = TempFullNames[LocalExportIndex];
	if (FullName.Len() == 0)
	{
		if (Export->OuterIndex.IsNull())
		{
			PackageName.AppendString(FullName);
			FullName.AppendChar(TEXT('/'));
			Export->ObjectName.AppendString(FullName);
		}
		else
		{
			check(Export->OuterIndex.IsExport());

			FindExport(GlobalExports, GlobalExportsByFullName, TempFullNames, ExportMap, Export->OuterIndex.ToExport(), PackageName);
			FString& OuterName = TempFullNames[Export->OuterIndex.ToExport()];
			check(OuterName.Len() > 0);

			FullName.Append(OuterName);
			FullName.AppendChar(TEXT('/'));
			Export->ObjectName.AppendString(FullName);
		}
		check(!GlobalExportsByFullName.Contains(FullName));
		int32 GlobalExportIndex = GlobalExports.Num();
		GlobalExportsByFullName.Add(FullName, GlobalExportIndex);
		FExportData& ExportData = GlobalExports.AddDefaulted_GetRef();
		ExportData.GlobalIndex = GlobalExportIndex;
		ExportData.SourcePackageName = PackageName;
		ExportData.ObjectName = Export->ObjectName;
		ExportData.SourceIndex = LocalExportIndex;
		ExportData.FullName = FullName;
	}
};

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	GEngineLoop.PreInit(ArgC, ArgV);

	bool bPakMode = false;
	FString CookedDir = ArgC > 1 ? ArgV[1] : TEXT("D:\\zen-proto\\FortniteGame\\Saved\\Cooked\\WindowsClient");
	FString RelativePrefixForLegacyFilename = ArgC > 2 ? ArgV[2] : TEXT("../../../");
	FString CryptoFilePath = TEXT("D:\\zen-proto\\FortniteGame\\Saved\\Cooked\\WindowsClient\\FortniteGame\\Metadata\\Crypto.json");
	FString PakDir = TEXT("D:\\zen-proto\\FortniteGame\\Saved\\StagedBuilds\\WindowsClient\\FortniteGame\\Content\\Paks");
	FString OutputDir = CookedDir;// / TEXT("..");
	FGlobalNameMap GlobalNameMap;
	GlobalNameMap.Load(CookedDir / TEXT("megafile.unamemap"));

	TArray<FString> FileNames;
	FString RemappingPrefix;
	if (bPakMode)
	{
		FKeyChain KeyChain;
		LoadKeyChainFromFile(CryptoFilePath, KeyChain);
		ApplyEncryptionKeys(KeyChain);

		UE_LOG(LogZenCreator, Display, TEXT("Searching for .pak files in %s..."), *PakDir);

		TArray<FString> PakFileNames;
		IFileManager::Get().FindFilesRecursive(PakFileNames, *PakDir, TEXT("*.pak"), true, false, false);

		IPlatformFile* CurrentPlatformFile = &FPlatformFileManager::Get().GetPlatformFile();
		FPakPlatformFile* PakPlatformFile = static_cast<FPakPlatformFile*>(FPlatformFileManager::Get().GetPlatformFile(TEXT("PakFile")));
		bool bSuccess = PakPlatformFile->Initialize(CurrentPlatformFile, TEXT(""));
		check(bSuccess);
		for (const FString& PakFileName : PakFileNames)
		{
			UE_LOG(LogZenCreator, Display, TEXT("Mounting %s..."), *PakFileName);
			bSuccess = PakPlatformFile->Mount(*PakFileName, 0);
			check(bSuccess);
		}

		UE_LOG(LogZenCreator, Display, TEXT("Searching for .uasset and .umap files..."));
		PakPlatformFile->FindFilesInternal(FileNames, TEXT("../../../"), TEXT("uasset"), true);
		PakPlatformFile->FindFilesInternal(FileNames, TEXT("../../../"), TEXT("umap"), true);
		UE_LOG(LogZenCreator, Display, TEXT("Found '%d' files"), FileNames.Num());

		FPlatformFileManager::Get().SetPlatformFile(*PakPlatformFile);
		RemappingPrefix = TEXT("../../..");
	}
	else
	{
		UE_LOG(LogZenCreator, Display, TEXT("Searching for .uasset and .umap files..."));
		IFileManager::Get().FindFilesRecursive(FileNames, *CookedDir, TEXT("*.uasset"), true, false, false);
		IFileManager::Get().FindFilesRecursive(FileNames, *CookedDir, TEXT("*.umap"), true, false, false);
		UE_LOG(LogZenCreator, Display, TEXT("Found '%d' files"), FileNames.Num());

		RemappingPrefix = CookedDir;
	}

	TArray<FNameEntryId> NameMap;
	TMap<FNameEntryId, int32> NameIndices;
	{
		FString FilePath = CookedDir / TEXT("megafile.unamemap");

		UE_LOG(LogZenCreator, Display, TEXT("Loading global namemap %s..."), *FilePath);
		
		TUniquePtr<FArchive> Archive(IFileManager::Get().CreateFileReader(*FilePath));
		if (Archive)
		{
			int32 NameCount;
			*Archive << NameCount;
			NameMap.Reserve(NameCount);
			FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);
			for (int32 I = 0; I < NameCount; ++I)
			{
				*Archive << NameEntry;
				NameMap.Emplace(FName(NameEntry).GetDisplayIndex());
				NameIndices.Add(NameMap[I], I);
			}
		}
	}
	check(NameMap.Num() > 0);

	auto ConvertSerializedFNameToRuntimeFName = [&NameMap](int32* InName, FName& OutName)
	{
		int32 NameIndex = InName[0];
		int32 NameNumber = InName[1];
		check(NameMap.IsValidIndex(NameIndex));
		FNameEntryId MappedName = NameMap[NameIndex];
		OutName = FName::CreateFromDisplayId(MappedName, NameNumber);
	};

	TArray<TPair<FString, FString>> PathRemappings;
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Content/"), TEXT("/Engine/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/2D/Paper2D/Content/"), TEXT("/Paper2D/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/CommonUI/Content/"), TEXT("/CommonUI/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/NotForLicensees/CommonUI/Content/"), TEXT("/CommonUI/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/Experimental/ControlRig/Content/"), TEXT("/ControlRig/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/Experimental/ImagePlate/Content/"), TEXT("/ImagePlate/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/FX/Niagara/Content/"), TEXT("/Niagara/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/Landmass/Content/"), TEXT("/Landmass/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/NotForLicensees/Landmass/Content/"), TEXT("/Landmass/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/Water/Content/"), TEXT("/Water/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/Enterprise/DatasmithContent/Content/"), TEXT("/DatasmithContent/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/Runtime/Oculus/OculusVR/Content/"), TEXT("/OculusVR/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("Engine/Plugins/NotForLicensees/Water/Content/"), TEXT("/Water/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("FortniteGame/Content/"), TEXT("/Game/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("ShooterGame/Content/"), TEXT("/Game/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("FortniteGame/Plugins/KairosSceneCapture/Content/"), TEXT("/KairosSceneCapture/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("FortniteGame/Plugins/LauncherSocial/Content/"), TEXT("/LauncherSocial/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("FortniteGame/Plugins/NiagaraFN/Content/"), TEXT("/NiagaraFN/"));
	PathRemappings.Emplace(RemappingPrefix / TEXT("FortniteGame/Plugins/Runtime/FortInstallBundleManager/Content/"), TEXT("/FortInstallBundleManager/"));

	TArray<FName> Names;
	TSet<FName> UniqueNames;
	TSet<FName> UniqueImports;
	uint64 NameSize = 0;
	uint64 UniqueNameSize = 0;
	TArray<FObjectImport> Imports;
	TArray<FObjectExport> Exports;
	TArray<FImportData> GlobalImports;
	TArray<FExportData> GlobalExports;
	TMap<FString, int32> GlobalImportsByFullName;
	TMap<FString, int32> GlobalExportsByFullName;
	TArray<FString> TempFullNames;
	TArray<FPackageIndex> PreloadDependencies;
	uint64 SummarySize = 0;
	uint64 UAssetSize = 0;
	uint64 UExpSize = 0;
	uint64 UBulkSize = 0;
	uint64 UniqueImportPackages = 0;
	uint64 UniqueImportPackageReferences = 0;
	TArray<FPackageFileSummary> Summaries;
	TArray<int32> ImportPreloadCounts;
	TArray<int32> ExportPreloadCounts;
	TArray<FCustomVersionArray> AllCustomVersions;
	Summaries.AddUninitialized(FileNames.Num());
	ImportPreloadCounts.AddUninitialized(FileNames.Num());
	ExportPreloadCounts.AddUninitialized(FileNames.Num());
	uint64 ImportPreloadCount = 0;
	uint64 ExportPreloadCount = 0;

	TUniquePtr<FArchive> StoreTocArchive(IFileManager::Get().CreateFileWriter(*(OutputDir / TEXT("megafile.ustoretoc"))));
	TUniquePtr<FArchive> ImportArchive(IFileManager::Get().CreateFileWriter(*(OutputDir / TEXT("megafile.uimport"))));
	TUniquePtr<FArchive> GlimportArchive(IFileManager::Get().CreateFileWriter(*(OutputDir / TEXT("megafile.uglimport"))));
	TUniquePtr<FArchive> SlimportArchive(IFileManager::Get().CreateFileWriter(*(OutputDir / TEXT("megafile.uslimport"))));
	TUniquePtr<FArchive> ExportArchive(IFileManager::Get().CreateFileWriter(*(OutputDir / TEXT("megafile.uexport"))));
	TUniquePtr<FArchive> PreloadArchive(IFileManager::Get().CreateFileWriter(*(OutputDir / TEXT("megafile.upreload"))));

	TUniquePtr<FArchive> TocArchive;
	TUniquePtr<FArchive> PayloadArchive;
#if 1
	TocArchive.Reset(IFileManager::Get().CreateFileWriter(*(OutputDir / TEXT("megafile.utoc"))));
	PayloadArchive.Reset(IFileManager::Get().CreateFileWriter(*(OutputDir / TEXT("megafile.udata"))));
#endif

	TUniquePtr<FArchive> GraphArchive(IFileManager::Get().CreateFileWriter(*(OutputDir / TEXT("megafile.ugraph"))));

	const bool bWriteToIoStore = true;
	FIoStoreEnvironment IoStoreEnv;
	TUniquePtr<FIoStoreWriter> IoStoreWriter;

	if (bWriteToIoStore)
	{
		IoStoreEnv.InitializeFileEnvironment(OutputDir);
		IoStoreWriter = MakeUnique<FIoStoreWriter>(IoStoreEnv);
		FIoStatus IoStatus = IoStoreWriter->Initialize();
		check(IoStatus.IsOk());
	}

	TMap<FName, FPackage> PackageMap;

	for (int FileIndex = 0; FileIndex < FileNames.Num(); ++FileIndex)
	{
		const FString& FileName = FileNames[FileIndex];
		FPackageFileSummary& Summary = Summaries[FileIndex];
		TUniquePtr<FArchive> Ar(IFileManager::Get().CreateFileReader(*FileName));
		check(Ar);

		UE_CLOG(FileIndex % 1000 == 0, LogZenCreator, Display, TEXT("Parsing %d: '%s'"), FileIndex, *FileName);

		FString PackageName;
		FName PackageFName;
		bool bConverted = false;
		for (const TPair<FString, FString>& RemappingPair : PathRemappings)
		{
			if (FileName.StartsWith(RemappingPair.Get<0>()))
			{
				PackageName = FPaths::ChangeExtension(RemappingPair.Get<1>() / (*FileName + RemappingPair.Get<0>().Len()), TEXT(""));
				PackageFName = *PackageName;
				bConverted = true;
			}
		}
		check(bConverted);

		uint64 SummaryStartPos = Ar->Tell();
		*Ar << Summary;
		SummarySize += Ar->Tell() - SummaryStartPos;

		FPackage& Package = PackageMap.Add(PackageFName);
		Package.Name = PackageFName;
		Package.FileName = FileName;
		Package.Guid = Summary.Guid;
		Package.ImportCount = Summary.ImportCount;
		Package.ExportCount = Summary.ExportCount;
		Package.PackageFlags = Summary.PackageFlags;
		Package.PreloadDependencyCount = Summary.PreloadDependencyCount;
		Package.BulkDataStartOffset = Summary.BulkDataStartOffset;

		Package.RelativeFileName = RelativePrefixForLegacyFilename;
		Package.RelativeFileName.Append(*FileName + CookedDir.Len());

		const FCustomVersionArray& CustomVersions = Summary.GetCustomVersionContainer().GetAllVersions();
		if (CustomVersions.Num() > 0)
		{
			auto FoundVersion = [&AllCustomVersions, &CustomVersions]() -> bool
			{
				for (int J = 0; J < AllCustomVersions.Num(); ++J)
				{
					const FCustomVersionArray& B = AllCustomVersions[J];
					if (CustomVersions.Num() != B.Num())
						continue;

					int I = 0;
					for (; I < CustomVersions.Num(); ++I)
					{
						const FCustomVersion& AA = CustomVersions[I];
						const FCustomVersion& BB = B[I];
						if (AA.Key != BB.Key || AA.Version != BB.Version)
							break;
					}
					if (I == CustomVersions.Num())
						return true;

				}
				return false;
			};

			if (!FoundVersion())
			{
				UE_LOG(LogZenCreator, Display, TEXT("Adding custom version %d with size %d "), AllCustomVersions.Num(), CustomVersions.Num());
				AllCustomVersions.Add(CustomVersions);
			}
		}

		if (Summary.NameCount > 0)
		{
			Ar->Seek(Summary.NameOffset);
			uint64 LastOffset = Summary.NameOffset;

			for (int32 I = 0; I < Summary.NameCount; ++I)
			{
				FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);
				*Ar << NameEntry;
				FName Name = FName(NameEntry);
				Names.Add(Name);
				if (!UniqueNames.Contains(Name))
				{
					UniqueNames.Add(Name);
					UniqueNameSize += Ar->Tell() - LastOffset;
				}
				LastOffset = Ar->Tell();
			}

			NameSize += Ar->Tell() - Summary.NameOffset;
		}

		if (Summary.ImportCount > 0)
		{
			Package.ImportOffset = ImportArchive->Tell();
			Ar->Seek(Summary.ImportOffset);

			int32 NumPackages = 0;
			int32 BaseIndex = Imports.Num();
			Imports.AddUninitialized(Summary.ImportCount);
			TArray<FString> ImportNames;
			ImportNames.Reserve(Summary.ImportCount);
			for (int32 I = 0; I < Summary.ImportCount; ++I)
			{
				FObjectImport& ObjectImport = Imports[BaseIndex + I];
				int32 ClassPackage[2];
				int32 ClassName[2];
				int32 ObjectName[2];
				//*Ar << ObjectImport.ClassPackage;
				*Ar << ClassPackage[0] << ClassPackage[1];
				//*Ar << ObjectImport.ClassName;
				*Ar << ClassName[0] << ClassName[1];
				*Ar << ObjectImport.OuterIndex;
				//*Ar << ObjectImport.ObjectName;
				*Ar << ObjectName[0] << ObjectName[1];

				if (ObjectImport.OuterIndex.IsNull())
				{
					++NumPackages;
				}

				// Serialize for in-place loading of FObjectImport
				ConvertSerializedFNameToRuntimeFName(ClassPackage, ObjectImport.ClassPackage);
				ConvertSerializedFNameToRuntimeFName(ClassName, ObjectImport.ClassName);
				ConvertSerializedFNameToRuntimeFName(ObjectName, ObjectImport.ObjectName);

				ImportNames.Emplace(ObjectImport.ObjectName.ToString());

				int32 Pad = 1;
				int64 XObjectPtr = 0;
				int64 SourceLinkerPtr = 0;
				int32 SourceIndex = INDEX_NONE;
				int32 BoolsAndPad = 0;
				*ImportArchive << ObjectName[0] << ObjectName[1];
				*ImportArchive << ObjectImport.OuterIndex;
				*ImportArchive << ClassPackage[0] << ClassPackage[1];
				*ImportArchive << ClassName[0] << ClassName[1];
				*ImportArchive << Pad;
				*ImportArchive << XObjectPtr;
				*ImportArchive << SourceLinkerPtr;
				*ImportArchive << SourceIndex;
				*ImportArchive << BoolsAndPad;
			}

			UniqueImportPackageReferences += NumPackages;

			Package.SlimportCount = Summary.ImportCount;
			Package.SlimportOffset = SlimportArchive->Tell();
			TempFullNames.Reset();
			TempFullNames.SetNum(Summary.ImportCount);
			for (int32 I = 0; I < Summary.ImportCount; ++I)
			{
				FindImport(GlobalImports, GlobalImportsByFullName, TempFullNames, Imports.GetData() + BaseIndex, I);

				FImportData& ImportData = GlobalImports[*GlobalImportsByFullName.Find(TempFullNames[I])];
				*SlimportArchive << ImportData.GlobalIndex;

				if (ImportData.bIsPackage)
				{
					Package.ImportedPackages.Add(ImportData.ObjectName);
				}
				Package.Imports.Add(ImportData.GlobalIndex);
			}
		}

		int32 PreloadDependenciesBaseIndex = -1;
		if (Summary.PreloadDependencyCount > 0)
		{
			Ar->Seek(Summary.PreloadDependencyOffset);
			Package.PreloadDependencyOffset = PreloadArchive->Tell();
			PreloadDependenciesBaseIndex = PreloadDependencies.Num();
			PreloadDependencies.AddUninitialized(Summary.PreloadDependencyCount);
			for (int32 I = 0; I < Summary.PreloadDependencyCount; ++I)
			{
				FPackageIndex& Index = PreloadDependencies[PreloadDependenciesBaseIndex + I];
				*Ar << Index;
				*PreloadArchive << Index;
				if (Index.IsImport())
				{
					++ImportPreloadCounts[FileIndex];
					++ImportPreloadCount;
				}
				else
				{
					++ExportPreloadCounts[FileIndex];
					++ExportPreloadCount;
				}
			}
		}

		Package.ExportIndexOffset = Exports.Num();
		if (Summary.ExportCount > 0)
		{
			Package.ExportOffset = ExportArchive->Tell();
			Ar->Seek(Summary.ExportOffset);

			int32 BaseIndex = Exports.Num();
			Exports.AddUninitialized(Summary.ExportCount);
			for (int32 I = 0; I < Summary.ExportCount; ++I)
			{
				FObjectExport& ObjectExport = Exports[BaseIndex + I];
				*Ar << ObjectExport.ClassIndex;
				*Ar << ObjectExport.SuperIndex;
				*Ar << ObjectExport.TemplateIndex;
				*Ar << ObjectExport.OuterIndex;
				//*Ar << ObjectExport.ObjectName;
				int32 ObjectName[2];
				*Ar << ObjectName[0] << ObjectName[1];
				uint32 ObjectFlags;
				*Ar << ObjectFlags;
				ObjectExport.ObjectFlags = (EObjectFlags)ObjectFlags;
				*Ar << ObjectExport.SerialSize;
				*Ar << ObjectExport.SerialOffset;
				*Ar << ObjectExport.bForcedExport;
				*Ar << ObjectExport.bNotForClient;
				*Ar << ObjectExport.bNotForServer;
				*Ar << ObjectExport.PackageGuid;
				*Ar << ObjectExport.PackageFlags;
				*Ar << ObjectExport.bNotAlwaysLoadedForEditorGame;
				*Ar << ObjectExport.bIsAsset;
				*Ar << ObjectExport.FirstExportDependency;
				*Ar << ObjectExport.SerializationBeforeSerializationDependencies;
				*Ar << ObjectExport.CreateBeforeSerializationDependencies;
				*Ar << ObjectExport.SerializationBeforeCreateDependencies;
				*Ar << ObjectExport.CreateBeforeCreateDependencies;

				FPackageIndex ThisIndex = FPackageIndex::FromExport(I);
				int64 ScriptSerializationStartOffset = 0;
				int64 ScriptSerializationEndOffset = 0;
				int64 ObjectPtr = 0;
				int32 HashNext = INDEX_NONE;
				bool bExportLoadFailed = false;
				uint8 DynamicType = 0;
				bool bWasFiltered = false;
				int32 Pad = 0;
				*ExportArchive << ObjectName[0] << ObjectName[1];
				*ExportArchive << ObjectExport.OuterIndex;
				*ExportArchive << ObjectExport.ClassIndex;
				*ExportArchive << ThisIndex;
				*ExportArchive << ObjectExport.SuperIndex;
				*ExportArchive << ObjectExport.TemplateIndex;
				*ExportArchive << ObjectFlags;
				*ExportArchive << ObjectExport.SerialSize;
				*ExportArchive << ObjectExport.SerialOffset;
				*ExportArchive << ScriptSerializationStartOffset;
				*ExportArchive << ScriptSerializationEndOffset;
				*ExportArchive << ObjectPtr;
				*ExportArchive << HashNext;
				*ExportArchive << (uint8&)ObjectExport.bForcedExport;
				*ExportArchive << (uint8&)ObjectExport.bNotForClient;
				*ExportArchive << (uint8&)ObjectExport.bNotForServer;
				*ExportArchive << (uint8&)ObjectExport.bNotAlwaysLoadedForEditorGame;
				*ExportArchive << (uint8&)ObjectExport.bIsAsset;
				*ExportArchive << (uint8&)bExportLoadFailed;
				*ExportArchive << DynamicType;
				*ExportArchive << (uint8&)bWasFiltered;
				*ExportArchive << ObjectExport.PackageGuid;
				*ExportArchive << ObjectExport.PackageFlags;
				*ExportArchive << ObjectExport.FirstExportDependency;
				*ExportArchive << ObjectExport.SerializationBeforeSerializationDependencies;
				*ExportArchive << ObjectExport.CreateBeforeSerializationDependencies;
				*ExportArchive << ObjectExport.SerializationBeforeCreateDependencies;
				*ExportArchive << ObjectExport.CreateBeforeCreateDependencies;
				*ExportArchive << Pad;

				ConvertSerializedFNameToRuntimeFName(ObjectName, ObjectExport.ObjectName);

				FPackageIndex ExportPackageIndex = FPackageIndex::FromExport(I);

				if (PreloadDependenciesBaseIndex >= 0 && ObjectExport.FirstExportDependency >= 0)
				{
					int32 RunningIndex = PreloadDependenciesBaseIndex + ObjectExport.FirstExportDependency;
					for (int32 Index = ObjectExport.SerializationBeforeSerializationDependencies; Index > 0; Index--)
					{
						FPackageIndex Dep = PreloadDependencies[RunningIndex++];
						check(!Dep.IsNull());
						// don't request IO for this export until these are serialized
						AddArc(Package, Dep, EEventLoadNode2::ImportOrExport_Serialize,
							Package, ExportPackageIndex, EEventLoadNode2::Export_StartIO);
					}

					for (int32 Index = ObjectExport.CreateBeforeSerializationDependencies; Index > 0; Index--)
					{
						FPackageIndex Dep = PreloadDependencies[RunningIndex++];
						check(!Dep.IsNull());
						// don't request IO for this export until these are created
						AddArc(Package, Dep, EEventLoadNode2::ImportOrExport_Create,
							Package, ExportPackageIndex, EEventLoadNode2::Export_StartIO);
					}

					for (int32 Index = ObjectExport.SerializationBeforeCreateDependencies; Index > 0; Index--)
					{
						FPackageIndex Dep = PreloadDependencies[RunningIndex++];
						check(!Dep.IsNull());
						// can't create this export until these things are serialized
						AddArc(Package, Dep, EEventLoadNode2::ImportOrExport_Serialize,
							Package, ExportPackageIndex, EEventLoadNode2::ImportOrExport_Create);
					}

					for (int32 Index = ObjectExport.CreateBeforeCreateDependencies; Index > 0; Index--)
					{
						FPackageIndex Dep = PreloadDependencies[RunningIndex++];
						check(!Dep.IsNull());
						// can't create this export until these things are created
						AddArc(Package, Dep, EEventLoadNode2::ImportOrExport_Create,
							Package, ExportPackageIndex, EEventLoadNode2::ImportOrExport_Create);
					}
				}
			}

			TempFullNames.Reset();
			TempFullNames.SetNum(Summary.ExportCount);
			for (int32 I = 0; I < Summary.ExportCount; ++I)
			{
				FindExport(GlobalExports, GlobalExportsByFullName, TempFullNames, Exports.GetData() + BaseIndex, I, PackageFName);

				FExportData& ExportData = GlobalExports[*GlobalExportsByFullName.Find(TempFullNames[I])];
				Package.Exports.Add(ExportData.GlobalIndex);
			}
		}
	
		if (PayloadArchive.IsValid())
		{
			UAssetSize += AppendToMegaFile(PayloadArchive.Get(), *FileName, Package.UAssetOffset, Package.UAssetSize);
			FString UExpFileName = FPaths::ChangeExtension(FileName, TEXT(".uexp"));
			UExpSize += AppendToMegaFile(PayloadArchive.Get(), *UExpFileName, Package.UExpOffset, Package.UExpSize);
			FString UBulkFileName = FPaths::ChangeExtension(FileName, TEXT(".ubulk_SKIP_THIS_FILE"));
			UBulkSize += AppendToMegaFile(PayloadArchive.Get(), *UBulkFileName, Package.UBulkOffset, Package.UBulkSize);
		}

		Ar->Close();
	}

	if (PayloadArchive.IsValid())
	{
		PayloadArchive->Close();
	}

	for (FExportData& GlobalExport : GlobalExports)
	{
		int32* FindGlobalImport = GlobalImportsByFullName.Find(GlobalExport.FullName);
		if (FindGlobalImport)
		{
			GlobalExport.GlobalImportIndex = *FindGlobalImport;
		}
	}

	uint64 ImportSize = Imports.Num() * 28;
	uint64 ExportSize = Exports.Num() * 104;
	uint64 PreloadDependenciesSize = PreloadDependencies.Num() * 4;

	FString CsvFilePath = SlimportArchive->GetArchiveName();
	CsvFilePath.Append(TEXT(".csv"));
	TUniquePtr<FArchive> CsvArchive(IFileManager::Get().CreateFileWriter(*CsvFilePath));
	if (CsvArchive)
	{
		ANSICHAR Line[MAX_SPRINTF + FName::StringBufferSize];
		ANSICHAR Header[] = "Count\tOuter\tOutermost\tImportName\n";
		CsvArchive->Serialize(Header, sizeof(Header) - 1);
		for (const FImportData& ImportData : GlobalImports)
		{
			FCStringAnsi::Sprintf(Line, "%d\t%d\t%d\t",
				ImportData.RefCount, ImportData.OuterIndex, ImportData.OutermostIndex);
			ANSICHAR* L = Line + FCStringAnsi::Strlen(Line);
			const TCHAR* N = *ImportData.FullName;
			while (*N)
			{
				*L++ = CharCast<ANSICHAR,TCHAR>(*N++);
			}
			*L++ = '\n';
			CsvArchive.Get()->Serialize(Line, L - Line);
		}
	}
	if (GlimportArchive)
	{
		int32 Pad = 0;
		for (const FImportData& ImportData : GlobalImports)
		{
			UniqueImportPackages += (ImportData.OuterIndex == 0);
			int32 NameIndex = NameIndices[ImportData.ObjectName.GetComparisonIndex()];
			int32 NameNumber = ImportData.ObjectName.GetNumber();
			*GlimportArchive << NameIndex << NameNumber;//ImportData.Value.ObjectName;
			FPackageIndex Index;
			Index = FPackageIndex::FromImport(ImportData.GlobalIndex);
			*GlimportArchive << Index;
			Index = ImportData.OuterIndex >= 0 ? FPackageIndex::FromImport(ImportData.OuterIndex) :
				FPackageIndex();
			*GlimportArchive << Index;
			Index = FPackageIndex::FromImport(ImportData.OutermostIndex);
			*GlimportArchive << Index;
			*GlimportArchive << Pad;
		}
	}

	int32 PackageCount = PackageMap.Num();
	if (TocArchive.IsValid())
	{
		*TocArchive << PackageCount;
	}

	TSet<FString> MissingExports;
	for (auto& PackageKV : PackageMap)
	{
		FPackage& Package = PackageKV.Value;

		for (int32 ImportIndex = 0; ImportIndex < Package.Imports.Num(); ++ImportIndex)
		{
			FImportData& Import = GlobalImports[Package.Imports[ImportIndex]];

			if (Import.bIsPackage)
			{
				continue;
			}

			int32* FindGlobalExport = GlobalExportsByFullName.Find(Import.FullName);
			if (FindGlobalExport)
			{
				FExportData& Export = GlobalExports[*FindGlobalExport];
				FPackage* FindImportPackage = PackageMap.Find(Export.SourcePackageName);
				check(FindImportPackage);
				AddArc(*FindImportPackage, FPackageIndex::FromExport(Export.SourceIndex), EEventLoadNode2::ImportOrExport_Create,
					Package, FPackageIndex::FromImport(ImportIndex), EEventLoadNode2::ImportOrExport_Create);
				AddArc(*FindImportPackage, FPackageIndex::FromExport(Export.SourceIndex), EEventLoadNode2::ImportOrExport_Serialize,
					Package, FPackageIndex::FromImport(ImportIndex), EEventLoadNode2::ImportOrExport_Serialize);
			}
			else if (!Import.FullName.StartsWith(TEXT("/Script/")))
			{
				MissingExports.Add(Import.FullName);
			}
		}

		TSet<FName> Visited;
		for (const FName& ImportedPackageName : Package.ImportedPackages)
		{
			FPackage* FindImportedPackage = PackageMap.Find(ImportedPackageName);
			if (FindImportedPackage)
			{
				AddPostLoadDependenciesRecursive(Package, *FindImportedPackage, Visited, PackageMap);
			}
		}
		
		// Temporary Archive for serializing EDL graph data
		FBufferWriter ZenGraphArchive(nullptr, 0, EBufferWriterFlags::AllowResize | EBufferWriterFlags::TakeOwnership );

		Package.UGraphOffset = GraphArchive->Tell();
		Algo::Sort(Package.InternalArcs);
		int32 InternalArcCount = Package.InternalArcs.Num();
		*GraphArchive << InternalArcCount;
		ZenGraphArchive << InternalArcCount;
		for (FPackageArc& InternalArc : Package.InternalArcs)
		{
			*GraphArchive << InternalArc.FromNodeIndex;
			*GraphArchive << InternalArc.ToNodeIndex;
			ZenGraphArchive << InternalArc.FromNodeIndex;
			ZenGraphArchive << InternalArc.ToNodeIndex;
		}
		int32 ImportedPackagesCount = Package.ExternalArcs.Num();
		*GraphArchive << ImportedPackagesCount;
		ZenGraphArchive << ImportedPackagesCount;
		for (auto& KV : Package.ExternalArcs)
		{
			const FName& ImportedPackageName = KV.Key;
			int32 ImportedPackageNameIndex = NameIndices[ImportedPackageName.GetComparisonIndex()];
			int32 ImportedPackageNameNumber = ImportedPackageName.GetNumber();

			*GraphArchive << ImportedPackageNameIndex << ImportedPackageNameNumber;
			ZenGraphArchive << ImportedPackageNameIndex << ImportedPackageNameNumber;

			TArray<FPackageArc>& Arcs = KV.Value;
			Algo::Sort(Arcs);

			int32 ExternalArcCount = Arcs.Num();
			*GraphArchive << ExternalArcCount;
			ZenGraphArchive << ExternalArcCount;
			for (FPackageArc& ExternalArc : Arcs)
			{
				*GraphArchive << ExternalArc.FromNodeIndex;
				*GraphArchive << ExternalArc.ToNodeIndex;
				ZenGraphArchive << ExternalArc.FromNodeIndex;
				ZenGraphArchive << ExternalArc.ToNodeIndex;
			}
		}
		Package.UGraphSize = GraphArchive->Tell() - Package.UGraphOffset;

		// Aligned FPackageStore entry 92 bytes, no padding
		int32 PackageNameIndex = GlobalNameMap.GetOrCreateComparisonIndex(Package.Name);
		int32 PackageNameNumber = Package.Name.GetNumber();

		FName RelativeFileName(*Package.RelativeFileName);
		int32 FileNameIndex = GlobalNameMap.GetOrCreateComparisonIndex(RelativeFileName);
		int32 FileNameNumber = Package.Name.GetNumber();

		int32 Pad = 0;
		*StoreTocArchive << Package.Guid;
		*StoreTocArchive << PackageNameIndex << PackageNameNumber;
		*StoreTocArchive << FileNameIndex << FileNameNumber;
		*StoreTocArchive << Package.PackageFlags;
		*StoreTocArchive << Package.ImportCount;
		*StoreTocArchive << Package.ImportOffset;
		*StoreTocArchive << Package.SlimportCount;
		*StoreTocArchive << Package.SlimportOffset;
		*StoreTocArchive << Package.ExportCount;
		*StoreTocArchive << Package.ExportOffset;
		*StoreTocArchive << Package.PreloadDependencyCount;
		*StoreTocArchive << Package.PreloadDependencyOffset;
		*StoreTocArchive << Pad;
		*StoreTocArchive << Package.BulkDataStartOffset;

		if (TocArchive.IsValid())
		{
			FString PackageNameString = Package.Name.ToString();
			*TocArchive << PackageNameString;
			*TocArchive << Package.RelativeFileName;
			*TocArchive << Package.UAssetOffset;
			*TocArchive << Package.UAssetSize;
			*TocArchive << Package.UExpOffset;
			*TocArchive << Package.UExpSize;
			*TocArchive << Package.UGraphOffset;
			*TocArchive << Package.UGraphSize;
			*TocArchive << Package.UBulkOffset;
			*TocArchive << Package.UBulkSize;
		}

		if (bWriteToIoStore)
		{
			auto SerializeName = [&](FArchive& A, const FName& N)
			{
				if (const int32* NameIndex = GlobalNameMap.GetComparisonIndex(N))
				{
					uint32 NameIndexNumber[] { *NameIndex, N.GetNumber() };
					A << NameIndexNumber[0] << NameIndexNumber[1];
				}
				else
				{
					UE_LOG(LogZenCreator, Display, TEXT("FName '%s' in package '%s' has no valid name index in global name map"), *N.ToString(), *Package.Name.ToString());
					check(false);
				}
			};

			const uint64 ZenSummarySize = 
						sizeof FZenPackageSummary
						+ (sizeof FObjectExport * Package.ExportCount)
						+ Package.UGraphSize;

			uint8* ZenSummaryBuffer = static_cast<uint8*>(FMemory::Malloc(ZenSummarySize));
			FZenPackageSummary* ZenSummary = reinterpret_cast<FZenPackageSummary*>(ZenSummaryBuffer);

			// TODO: Remove redundant data
			ZenSummary->Guid = Package.Guid;
			ZenSummary->PackageFlags = Package.PackageFlags;
			ZenSummary->ImportCount = Package.ImportCount;
			ZenSummary->ExportCount = Package.ExportCount;
			ZenSummary->PreloadDependencyCount = Package.PreloadDependencyCount;
			ZenSummary->GraphDataSize = Package.UGraphSize;
			ZenSummary->BulkDataStartOffset = Package.BulkDataStartOffset;

			FBufferWriter ZenAr(ZenSummaryBuffer, ZenSummarySize); 
			ZenAr.Seek(sizeof FZenPackageSummary);

			// Export table
			ZenSummary->ExportOffset = ZenAr.Tell();
			for (int32 I = 0; I < Package.ExportCount; ++I)
			{
				FObjectExport& ObjectExport = Exports[Package.ExportIndexOffset + I];

				// TODO: Serialize slim exports
				FPackageIndex ThisIndex = FPackageIndex::FromExport(I);
				int64 ScriptSerializationStartOffset = 0;
				int64 ScriptSerializationEndOffset = 0;
				int64 ObjectPtr = 0;
				int32 HashNext = INDEX_NONE;
				bool bExportLoadFailed = false;
				uint8 DynamicType = 0;
				bool bWasFiltered = false;
				int32 ExportPad = 0;
				SerializeName(ZenAr, ObjectExport.ObjectName);
				ZenAr << ObjectExport.OuterIndex;
				ZenAr << ObjectExport.ClassIndex;
				ZenAr << ThisIndex;
				ZenAr << ObjectExport.SuperIndex;
				ZenAr << ObjectExport.TemplateIndex;
				ZenAr << (uint32&)ObjectExport.ObjectFlags;
				ZenAr << ObjectExport.SerialSize;
				ZenAr << ObjectExport.SerialOffset;
				ZenAr << ScriptSerializationStartOffset;
				ZenAr << ScriptSerializationEndOffset;
				ZenAr << ObjectPtr;
				ZenAr << HashNext;
				ZenAr << (uint8&)ObjectExport.bForcedExport;
				ZenAr << (uint8&)ObjectExport.bNotForClient;
				ZenAr << (uint8&)ObjectExport.bNotForServer;
				ZenAr << (uint8&)ObjectExport.bNotAlwaysLoadedForEditorGame;
				ZenAr << (uint8&)ObjectExport.bIsAsset;
				ZenAr << (uint8&)bExportLoadFailed;
				ZenAr << DynamicType;
				ZenAr << (uint8&)bWasFiltered;
				ZenAr << ObjectExport.PackageGuid;
				ZenAr << ObjectExport.PackageFlags;
				ZenAr << ObjectExport.FirstExportDependency;
				ZenAr << ObjectExport.SerializationBeforeSerializationDependencies;
				ZenAr << ObjectExport.CreateBeforeSerializationDependencies;
				ZenAr << ObjectExport.SerializationBeforeCreateDependencies;
				ZenAr << ObjectExport.CreateBeforeCreateDependencies;
				ZenAr << ExportPad;
			}

			// Graph data
			{
				check(ZenGraphArchive.Tell() == Package.UGraphSize);	
				ZenSummary->GraphDataOffset = ZenAr.Tell();
				ZenAr.Serialize(ZenGraphArchive.GetWriterData(), ZenGraphArchive.Tell());
			}

			// Package summary chunk
			{
				FIoBuffer IoBuffer(FIoBuffer::AssumeOwnership, ZenSummaryBuffer, ZenSummarySize);
				IoStoreWriter->Append(CreateZenChunkId(PackageNameIndex, PackageNameNumber, 0, EZenChunkType::PackageSummary), IoBuffer);
			}

			// Export chunks
			if (Package.ExportCount)
			{
				FString UExpFileName = FPaths::ChangeExtension(Package.FileName, TEXT(".uexp"));
				TUniquePtr<FArchive> ExpAr(IFileManager::Get().CreateFileReader(*UExpFileName));
				const int64 TotalExportsSize = ExpAr->TotalSize();
				uint8* ExportsBuffer = static_cast<uint8*>(FMemory::Malloc(TotalExportsSize));
				ExpAr->Serialize(ExportsBuffer, TotalExportsSize);

				for (int32 I = 0; I < Package.ExportCount; ++I)
				{
					check(I < UINT16_MAX); 
					FObjectExport& ObjectExport = Exports[Package.ExportIndexOffset + I];
					const int64 Offset = ObjectExport.SerialOffset - Package.UAssetSize;
					FIoBuffer IoBuffer(FIoBuffer::Wrap, ExportsBuffer + Offset, ObjectExport.SerialSize);
					IoStoreWriter->Append(CreateZenChunkId(PackageNameIndex, PackageNameNumber, I, EZenChunkType::ExportData), IoBuffer);
				}

				FMemory::Free(ExportsBuffer);
				ExpAr->Close();
			}

			// Bulk chunks
			{
				FString UBulkFileName = FPaths::ChangeExtension(Package.FileName, TEXT(".ubulk_SKIP_THIS_FILE"));
				TUniquePtr<FArchive> BulkAr(IFileManager::Get().CreateFileReader(*UBulkFileName));
				if (BulkAr)
				{
					uint8* BulkBuffer = static_cast<uint8*>(FMemory::Malloc(BulkAr->TotalSize()));
					BulkAr->Serialize(BulkBuffer, BulkAr->TotalSize());
					FIoBuffer IoBuffer(FIoBuffer::AssumeOwnership, BulkBuffer, BulkAr->TotalSize());
					IoStoreWriter->Append(CreateZenChunkId(PackageNameIndex, PackageNameNumber, 0, EZenChunkType::BulkData), IoBuffer);
					BulkAr->Close();
				}
			}
		}
	}

	GlobalNameMap.Save(CookedDir / TEXT("Container.namemap"));

	GraphArchive->Close();
	StoreTocArchive->Close();
	if (TocArchive.IsValid())
	{
		TocArchive->Close();
	}

	// Exports per package, bucketed...
	// Imports per package, bucketed...
	// Unique imports...
	UE_LOG(LogZenCreator, Display, TEXT("%f MB package file summary"), (double)SummarySize / 1024.0 / 1024.0);
	UE_LOG(LogZenCreator, Display, TEXT("%d unique custom versions"), AllCustomVersions.Num());
	UE_LOG(LogZenCreator, Display, TEXT("%d names (%fMB)"), Names.Num(), (double)NameSize / 1024.0 / 1024.0);
	UE_LOG(LogZenCreator, Display, TEXT("%d unique names (%fMB)"), UniqueNames.Num(), (double)UniqueNameSize / 1024.0 / 1024.0);
	UE_LOG(LogZenCreator, Display, TEXT("%d unique imports, %d unique packages"), GlobalImportsByFullName.Num(), UniqueImportPackages);
	UE_LOG(LogZenCreator, Display, TEXT("%d imports (%fMB), %d unique import package references"), Imports.Num(), (double)ImportSize / 1024.0 / 1024.0, UniqueImportPackageReferences);
	UE_LOG(LogZenCreator, Display, TEXT("%d exports (%fMB)"), Exports.Num(), (double)ExportSize / 1024.0 / 1024.0);
	UE_LOG(LogZenCreator, Display, TEXT("%d import preloads, %d export preloads"), ImportPreloadCount, ExportPreloadCount);
	UE_LOG(LogZenCreator, Display, TEXT("%d preload dependencies (%fMB)"), PreloadDependencies.Num(), (double)PreloadDependenciesSize / 1024.0 / 1024.0);
	UE_LOG(LogZenCreator, Display, TEXT("%f MB uasset/umap, %d files"), (double)UAssetSize / 1024.0 / 1024.0, FileNames.Num());
	UE_LOG(LogZenCreator, Display, TEXT("%f MB uexp files, %d files"), (double)UExpSize / 1024.0 / 1024.0, FileNames.Num());
	UE_LOG(LogZenCreator, Display, TEXT("%f MB ubulk files"), (double)UBulkSize / 1024.0 / 1024.0);

	RequestEngineExit(TEXT("ZenCreator finished"));

	return 0;
}
