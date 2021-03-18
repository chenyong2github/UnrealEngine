// Copyright Epic Games, Inc. All Rights Reserved.

#include "PackageReader.h"
#include "HAL/FileManager.h"
#include "Internationalization/Internationalization.h"
#include "Logging/MessageLog.h"
#include "Misc/PackageName.h"
#include "UObject/Class.h"
#include "AssetRegistryPrivate.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry.h"

FPackageReader::FPackageReader()
	: Loader(nullptr)
	, PackageFileSize(0)
	, AssetRegistryDependencyDataOffset(INDEX_NONE)
{
	this->SetIsLoading(true);
	this->SetIsPersistent(true);
}

FPackageReader::~FPackageReader()
{
	if (Loader)
	{
		delete Loader;
	}
}

bool FPackageReader::OpenPackageFile(const FString& InPackageFilename, EOpenPackageResult* OutErrorCode)
{
	PackageFilename = InPackageFilename;
	Loader = IFileManager::Get().CreateFileReader(*PackageFilename);
	return OpenPackageFile(OutErrorCode);
}

bool FPackageReader::OpenPackageFile(FArchive* InLoader, EOpenPackageResult* OutErrorCode)
{
	Loader = InLoader;
	PackageFilename = Loader->GetArchiveName();
	return OpenPackageFile(OutErrorCode);
}

bool FPackageReader::OpenPackageFile(EOpenPackageResult* OutErrorCode)
{
	auto SetPackageErrorCode = [&](const EOpenPackageResult InErrorCode)
	{
		if (OutErrorCode)
		{
			*OutErrorCode = InErrorCode;
		}
	};

	if( Loader == nullptr )
	{
		// Couldn't open the file
		SetPackageErrorCode(EOpenPackageResult::NoLoader);
		return false;
	}

	// Read package file summary from the file
	*this << PackageFileSummary;

	// Validate the summary.

	// Make sure this is indeed a package
	if( PackageFileSummary.Tag != PACKAGE_FILE_TAG || IsError())
	{
		// Unrecognized or malformed package file
		UE_LOG(LogAssetRegistry, Error, TEXT("Package %s has malformed tag"), *PackageFilename);
		SetPackageErrorCode(EOpenPackageResult::MalformedTag);
		return false;
	}

	// Don't read packages that are too old
	if( PackageFileSummary.GetFileVersionUE4() < VER_UE4_OLDEST_LOADABLE_PACKAGE )
	{
		UE_LOG(LogAssetRegistry, Error, TEXT("Package %s is too old"), *PackageFilename);
		SetPackageErrorCode(EOpenPackageResult::VersionTooOld);
		return false;
	}

	// Don't read packages that were saved with an package version newer than the current one.
	if( (PackageFileSummary.GetFileVersionUE4() > GPackageFileUE4Version) || (PackageFileSummary.GetFileVersionLicenseeUE4() > GPackageFileLicenseeUE4Version) )
	{
		UE_LOG(LogAssetRegistry, Error, TEXT("Package %s is too new"), *PackageFilename);
		SetPackageErrorCode(EOpenPackageResult::VersionTooNew);
		return false;
	}

	// Check serialized custom versions against latest custom versions.
	TArray<FCustomVersionDifference> Diffs = FCurrentCustomVersions::Compare(PackageFileSummary.GetCustomVersionContainer().GetAllVersions(), *PackageFilename);
	for (FCustomVersionDifference Diff : Diffs)
	{
		if (Diff.Type == ECustomVersionDifference::Missing)
		{
			SetPackageErrorCode(EOpenPackageResult::CustomVersionMissing);
			return false;
		}
		else if (Diff.Type == ECustomVersionDifference::Invalid)
		{
			SetPackageErrorCode(EOpenPackageResult::CustomVersionInvalid);
			return false;
		}
		else if (Diff.Type == ECustomVersionDifference::Newer)
		{
			UE_LOG(LogAssetRegistry, Error, TEXT("Package %s has newer custom version of %s"), *PackageFilename, *Diff.Version->GetFriendlyName().ToString());

			SetPackageErrorCode(EOpenPackageResult::VersionTooNew);
			return false;
		}
	}

	//make sure the filereader gets the correct version number (it defaults to latest version)
	SetUE4Ver(PackageFileSummary.GetFileVersionUE4());
	SetLicenseeUE4Ver(PackageFileSummary.GetFileVersionLicenseeUE4());
	SetEngineVer(PackageFileSummary.SavedByEngineVersion);

	const FCustomVersionContainer& PackageFileSummaryVersions = PackageFileSummary.GetCustomVersionContainer();
	SetCustomVersions(PackageFileSummaryVersions);

	PackageFileSize = Loader->TotalSize();

	SetPackageErrorCode(EOpenPackageResult::Success);
	return true;
}
bool FPackageReader::StartSerializeSection(int64 Offset)
{
	check(Loader);
	if (Offset <= 0 || Offset > PackageFileSize)
	{
		return false;
	}
	ClearError();
	Loader->ClearError();
	Seek(Offset);
	return !IsError();
}

#define UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING(MessageKey, PackageFileName) \
	do \
	{\
		FFormatNamedArguments CorruptPackageWarningArguments; \
		CorruptPackageWarningArguments.Add(TEXT("FileName"), FText::FromString(PackageFileName)); \
		FMessageLog("AssetRegistry").Warning(FText::Format(NSLOCTEXT("AssetRegistry", MessageKey, "Cannot read AssetRegistry Data in {FileName}, skipping it. Error: " MessageKey "."), CorruptPackageWarningArguments)); \
	} while (false)

bool FPackageReader::ReadAssetRegistryData(TArray<FAssetData*>& AssetDataList)
{
	if (!StartSerializeSection(PackageFileSummary.AssetRegistryDataOffset))
	{
		return false;
	}

	// Determine the package name and path
	FString PackageName;
	if (!FPackageName::TryConvertFilenameToLongPackageName(PackageFilename, PackageName))
	{
		// Path was possibly unmounted
		return false;
	}

	using namespace UE::AssetRegistry;

	EReadPackageDataMainErrorCode ErrorCode;
	if (!ReadPackageDataMain(*this, PackageName, PackageFileSummary, AssetRegistryDependencyDataOffset, AssetDataList, ErrorCode))
	{
		switch (ErrorCode)
		{
		case EReadPackageDataMainErrorCode::InvalidObjectCount:
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("EReadPackageDataMainErrorCode::InvalidObjectCount", PackageFilename);
			break;
		case EReadPackageDataMainErrorCode::InvalidTagCount:
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("EReadPackageDataMainErrorCode::InvalidTagCount", PackageFilename);
			break;
		case EReadPackageDataMainErrorCode::InvalidTag:
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("EReadPackageDataMainErrorCode::InvalidTag", PackageFilename);
			break;
		default:
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("EReadPackageDataMainErrorCode::Unknown", PackageFilename);
			break;
		}
		return false;
	}

	return true;
}

bool FPackageReader::SerializeAssetRegistryDependencyData(FPackageDependencyData& DependencyData)
{
	if (AssetRegistryDependencyDataOffset == INDEX_NONE)
	{
		// For old package versions that did not write out the dependency flags, set default values of the flags
		DependencyData.ImportUsedInGame.Init(true, DependencyData.ImportMap.Num());
		DependencyData.SoftPackageUsedInGame.Init(true, DependencyData.SoftPackageReferenceList.Num());
		return true;
	}

	if (!StartSerializeSection(AssetRegistryDependencyDataOffset))
	{
		return false;
	}

	if (!UE::AssetRegistry::ReadPackageDataDependencies(*this, DependencyData.ImportUsedInGame, DependencyData.SoftPackageUsedInGame)
		|| !DependencyData.IsValid())
	{
		UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeAssetRegistryDependencyData", PackageFilename);
		return false;
	}
	return true;
}

bool FPackageReader::ReadAssetDataFromThumbnailCache(TArray<FAssetData*>& AssetDataList)
{
	if (!StartSerializeSection(PackageFileSummary.ThumbnailTableOffset))
	{
		return false;
	}

	// Determine the package name and path
	FString PackageName = FPackageName::FilenameToLongPackageName(PackageFilename);
	FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

	// Load the thumbnail count
	int32 ObjectCount = 0;
	*this << ObjectCount;
	const int32 MinBytesPerObject = 1;
	if (IsError() || ObjectCount < 0 || PackageFileSize < Tell() + ObjectCount * MinBytesPerObject)
	{
		UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("ReadAssetDataFromThumbnailCacheInvalidObjectCount", PackageFilename);
		return false;
	}

	// Iterate over every thumbnail entry and harvest the objects classnames
	for(int32 ObjectIdx = 0; ObjectIdx < ObjectCount; ++ObjectIdx)
	{
		// Serialize the classname
		FString AssetClassName;
		*this << AssetClassName;

		// Serialize the object path.
		FString ObjectPathWithoutPackageName;
		*this << ObjectPathWithoutPackageName;

		// Serialize the rest of the data to get at the next object
		int32 FileOffset = 0;
		*this << FileOffset;

		if (IsError())
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("ReadAssetDataFromThumbnailCacheInvalidObject", PackageFilename);
			return false;
		}

		FString GroupNames;
		FString AssetName;

		if (!ensureMsgf(!ObjectPathWithoutPackageName.Contains(TEXT("."), ESearchCase::CaseSensitive), TEXT("Cannot make FAssetData for sub object %s!"), *ObjectPathWithoutPackageName))
		{
			continue;
		}

		// Create a new FAssetData for this asset and update it with the gathered data
		AssetDataList.Add(new FAssetData(FName(*PackageName), FName(*PackagePath), FName(*ObjectPathWithoutPackageName), FName(*AssetClassName), FAssetDataTagMap(), PackageFileSummary.ChunkIDs, PackageFileSummary.PackageFlags));
	}

	return true;
}

bool FPackageReader::ReadAssetRegistryDataIfCookedPackage(TArray<FAssetData*>& AssetDataList, TArray<FString>& CookedPackageNamesWithoutAssetData)
{
	if (!!(GetPackageFlags() & PKG_FilterEditorOnly))
	{
		const FString PackageName = FPackageName::FilenameToLongPackageName(PackageFilename);
		
		bool bFoundAtLeastOneAsset = false;

		// If the packaged is saved with the right version we have the information
		// which of the objects in the export map as the asset.
		// Otherwise we need to store a temp minimal data and then force load the asset
		// to re-generate its registry data
		if (UE4Ver() >= VER_UE4_COOKED_ASSETS_IN_EDITOR_SUPPORT)
		{
			const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);

			TArray<FObjectImport> ImportMap;
			TArray<FObjectExport> ExportMap;
			if (!SerializeNameMap())
			{
				return false;
			}
			if (!SerializeImportMap(ImportMap))
			{
				return false;
			}
			if (!SerializeExportMap(ExportMap))
			{
				return false;
			}
			for (FObjectExport& Export : ExportMap)
			{
				if (Export.bIsAsset)
				{
					// We need to get the class name from the import/export maps
					FName ObjectClassName;
					if (Export.ClassIndex.IsNull())
					{
						ObjectClassName = UClass::StaticClass()->GetFName();
					}
					else if (Export.ClassIndex.IsExport())
					{
						const FObjectExport& ClassExport = ExportMap[Export.ClassIndex.ToExport()];
						ObjectClassName = ClassExport.ObjectName;
					}
					else if (Export.ClassIndex.IsImport())
					{
						const FObjectImport& ClassImport = ImportMap[Export.ClassIndex.ToImport()];
						ObjectClassName = ClassImport.ObjectName;
					}

					AssetDataList.Add(new FAssetData(FName(*PackageName), FName(*PackagePath), Export.ObjectName, ObjectClassName, FAssetDataTagMap(), TArray<int32>(), GetPackageFlags()));
					bFoundAtLeastOneAsset = true;
				}
			}
		}
		if (!bFoundAtLeastOneAsset)
		{
			CookedPackageNamesWithoutAssetData.Add(PackageName);
		}
		return true;
	}

	return false;
}

bool FPackageReader::ReadDependencyData(FPackageDependencyData& OutDependencyData)
{
	FString PackageNameString;
	if (!FPackageName::TryConvertFilenameToLongPackageName(PackageFilename, PackageNameString))
	{
		// Path was possibly unmounted
		return false;
	}

	OutDependencyData.PackageName = FName(*PackageNameString);
	OutDependencyData.PackageData.DiskSize = PackageFileSize;
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	OutDependencyData.PackageData.PackageGuid = PackageFileSummary.Guid;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	if (!SerializeNameMap())
	{
		return false;
	}
	if (!SerializeImportMap(OutDependencyData.ImportMap))
	{
		return false;
	}
	if (!SerializeSoftPackageReferenceList(OutDependencyData.SoftPackageReferenceList))
	{
		return false;
	}
	if (!SerializeSearchableNamesMap(OutDependencyData))
	{
		return false;
	}
	if (!SerializeAssetRegistryDependencyData(OutDependencyData))
	{
		return false;
	}

	checkf(OutDependencyData.IsValid(), TEXT("We should have early exited above rather than creating invalid dependency data"));
	return true;
}

bool FPackageReader::SerializeNameMap()
{
	if( PackageFileSummary.NameCount > 0 )
	{
		if (!StartSerializeSection(PackageFileSummary.NameOffset))
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeNameMapInvalidNameOffset", PackageFilename);
			return false;
		}

		const int MinSizePerNameEntry = 1;
		if (PackageFileSize < Tell() + PackageFileSummary.NameCount * MinSizePerNameEntry)
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeNameMapInvalidNameCount", PackageFilename);
			return false;
		}

		for ( int32 NameMapIdx = 0; NameMapIdx < PackageFileSummary.NameCount; ++NameMapIdx )
		{
			// Read the name entry from the file.
			FNameEntrySerialized NameEntry(ENAME_LinkerConstructor);
			*this << NameEntry;
			if (IsError())
			{
				UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeNameMapInvalidName", PackageFilename);
				return false;
			}
			NameMap.Add(FName(NameEntry));
		}
	}

	return true;
}

bool FPackageReader::SerializeImportMap(TArray<FObjectImport>& OutImportMap)
{
	if( PackageFileSummary.ImportCount > 0 )
	{
		if (!StartSerializeSection(PackageFileSummary.ImportOffset))
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeImportMapInvalidImportOffset", PackageFilename);
			return false;
		}

		const int MinSizePerImport = 1;
		if (PackageFileSize < Tell() + PackageFileSummary.ImportCount * MinSizePerImport)
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeImportMapInvalidImportCount", PackageFilename);
			return false;
		}
		for ( int32 ImportMapIdx = 0; ImportMapIdx < PackageFileSummary.ImportCount; ++ImportMapIdx )
		{
			FObjectImport* Import = new(OutImportMap)FObjectImport;
			*this << *Import;
			if (IsError())
			{
				UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeImportMapInvalidImport", PackageFilename);
				return false;
			}
		}
	}

	return true;
}

bool FPackageReader::SerializeExportMap(TArray<FObjectExport>& OutExportMap)
{
	if (PackageFileSummary.ExportCount > 0)
	{
		if (!StartSerializeSection(PackageFileSummary.ExportOffset))
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeExportMapInvalidExportOffset", PackageFilename);
			return false;
		}

		const int MinSizePerExport = 1;
		if (PackageFileSize < Tell() + PackageFileSummary.ExportCount * MinSizePerExport)
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeExportMapInvalidExportCount", PackageFilename);
			return false;
		}
		for (int32 ExportMapIdx = 0; ExportMapIdx < PackageFileSummary.ExportCount; ++ExportMapIdx)
		{
			FObjectExport* Export = new(OutExportMap)FObjectExport;
			*this << *Export;
			if (IsError())
			{
				UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeExportMapInvalidExport", PackageFilename);
				return false;
			}
		}
	}

	return true;
}

bool FPackageReader::SerializeSoftPackageReferenceList(TArray<FName>& OutSoftPackageReferenceList)
{
	if (UE4Ver() >= VER_UE4_ADD_STRING_ASSET_REFERENCES_MAP && PackageFileSummary.SoftPackageReferencesOffset > 0 && PackageFileSummary.SoftPackageReferencesCount > 0)
	{
		if (!StartSerializeSection(PackageFileSummary.SoftPackageReferencesOffset))
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeSoftPackageReferenceListInvalidReferencesOffset", PackageFilename);
			return false;
		}
		
		const int MinSizePerSoftPackageReference = 1;
		if (PackageFileSize < Tell() + PackageFileSummary.SoftPackageReferencesCount * MinSizePerSoftPackageReference)
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeSoftPackageReferenceListInvalidReferencesCount", PackageFilename);
			return false;
		}
		if (UE4Ver() < VER_UE4_ADDED_SOFT_OBJECT_PATH)
		{
			for (int32 ReferenceIdx = 0; ReferenceIdx < PackageFileSummary.SoftPackageReferencesCount; ++ReferenceIdx)
			{
				FString PackageName;
				*this << PackageName;
				if (IsError())
				{
					UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeSoftPackageReferenceListInvalidReferencePreSoftObjectPath", PackageFilename);
					return false;
				}

				if (UE4Ver() < VER_UE4_KEEP_ONLY_PACKAGE_NAMES_IN_STRING_ASSET_REFERENCES_MAP)
				{
					PackageName = FPackageName::GetNormalizedObjectPath(PackageName);
					if (!PackageName.IsEmpty())
					{
						PackageName = FPackageName::ObjectPathToPackageName(PackageName);
					}
				}

				OutSoftPackageReferenceList.Add(FName(*PackageName));
			}
		}
		else
		{
			for (int32 ReferenceIdx = 0; ReferenceIdx < PackageFileSummary.SoftPackageReferencesCount; ++ReferenceIdx)
			{
				FName PackageName;
				*this << PackageName;
				if (IsError())
				{
					UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeSoftPackageReferenceListInvalidReference", PackageFilename);
					return false;
				}

				OutSoftPackageReferenceList.Add(PackageName);
			}
		}
	}

	return true;
}

bool FPackageReader::SerializeSearchableNamesMap(FPackageDependencyData& OutDependencyData)
{
	if (UE4Ver() >= VER_UE4_ADDED_SEARCHABLE_NAMES && PackageFileSummary.SearchableNamesOffset > 0)
	{
		if (!StartSerializeSection(PackageFileSummary.SearchableNamesOffset))
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeSearchableNamesMapInvalidOffset", PackageFilename);
			return false;
		}

		OutDependencyData.SerializeSearchableNamesMap(*this);
		if (IsError())
		{
			UE_PACKAGEREADER_CORRUPTPACKAGE_WARNING("SerializeSearchableNamesMapInvalidSearchableNamesMap", PackageFilename);
			return false;
		}
	}

	return true;
}

void FPackageReader::Serialize( void* V, int64 Length )
{
	check(Loader);
	Loader->Serialize( V, Length );
	if (Loader->IsError())
	{
		SetError();
	}
}

bool FPackageReader::Precache( int64 PrecacheOffset, int64 PrecacheSize )
{
	check(Loader);
	return Loader->Precache( PrecacheOffset, PrecacheSize );
}

void FPackageReader::Seek( int64 InPos )
{
	check(Loader);
	Loader->Seek( InPos );
	if (Loader->IsError())
	{
		SetError();
	}
}

int64 FPackageReader::Tell()
{
	check(Loader);
	return Loader->Tell();
}

int64 FPackageReader::TotalSize()
{
	check(Loader);
	return Loader->TotalSize();
}

uint32 FPackageReader::GetPackageFlags() const
{
	return PackageFileSummary.PackageFlags;
}

FArchive& FPackageReader::operator<<( FName& Name )
{
	int32 NameIndex;
	FArchive& Ar = *this;
	Ar << NameIndex;

	if( !NameMap.IsValidIndex(NameIndex) )
	{
		UE_LOG(LogAssetRegistry, Warning, TEXT("Bad name index %i/%i when reading package %s"), NameIndex, NameMap.Num(), *PackageFilename );
		SetError();
		return *this;
	}

	// if the name wasn't loaded (because it wasn't valid in this context)
	if (NameMap[NameIndex] == NAME_None)
	{
		int32 TempNumber;
		Ar << TempNumber;
		Name = NAME_None;
	}
	else
	{
		int32 Number;
		Ar << Number;
		// simply create the name from the NameMap's name and the serialized instance number
		Name = FName(NameMap[NameIndex], Number);
	}

	return *this;
}

namespace UE
{
namespace AssetRegistry
{
	// See the corresponding WritePackageData defined in SavePackageUtilities.cpp in CoreUObject module
	bool ReadPackageDataMain(FArchive& BinaryArchive, const FString& PackageName, const FPackageFileSummary& PackageFileSummary, int64& OutDependencyDataOffset, TArray<FAssetData*>& OutAssetDataList, EReadPackageDataMainErrorCode& OutError)
	{
		OutError = EReadPackageDataMainErrorCode::Unknown;

		const FString PackagePath = FPackageName::GetLongPackagePath(PackageName);
		const int64 PackageFileSize = BinaryArchive.TotalSize();
		const bool bIsMapPackage = (PackageFileSummary.PackageFlags & PKG_ContainsMap) != 0;

		// To avoid large patch sizes, we have frozen cooked package format at the format before VER_UE4_ASSETREGISTRY_DEPENDENCYFLAGS
		bool bPreDependencyFormat = PackageFileSummary.GetFileVersionUE4() < VER_UE4_ASSETREGISTRY_DEPENDENCYFLAGS || !!(PackageFileSummary.PackageFlags & PKG_FilterEditorOnly);

		// Load offsets to optionally-read data
		if (bPreDependencyFormat)
		{
			OutDependencyDataOffset = INDEX_NONE;
		}
		else
		{
			BinaryArchive << OutDependencyDataOffset;
		}

		// Load the object count
		int32 ObjectCount = 0;
		BinaryArchive << ObjectCount;
		const int32 MinBytesPerObject = 1;
		if (BinaryArchive.IsError() || ObjectCount < 0 || PackageFileSize < BinaryArchive.Tell() + ObjectCount * MinBytesPerObject)
		{
			OutError = EReadPackageDataMainErrorCode::InvalidObjectCount;
			return false;
		}

		// Worlds that were saved before they were marked public do not have asset data so we will synthesize it here to make sure we see all legacy umaps
		// We will also do this for maps saved after they were marked public but no asset data was saved for some reason. A bug caused this to happen for some maps.
		if (bIsMapPackage)
		{
			const bool bLegacyPackage = PackageFileSummary.GetFileVersionUE4() < VER_UE4_PUBLIC_WORLDS;
			const bool bNoMapAsset = (ObjectCount == 0);
			if (bLegacyPackage || bNoMapAsset)
			{
				FString AssetName = FPackageName::GetLongPackageAssetName(PackageName);
				OutAssetDataList.Add(new FAssetData(FName(*PackageName), FName(*PackagePath), FName(*AssetName), FName(TEXT("World")), FAssetDataTagMap(), PackageFileSummary.ChunkIDs, PackageFileSummary.PackageFlags));
			}
		}

		const int32 MinBytesPerTag = 1;
		// UAsset files usually only have one asset, maps and redirectors have multiple
		for (int32 ObjectIdx = 0; ObjectIdx < ObjectCount; ++ObjectIdx)
		{
			FString ObjectPath;
			FString ObjectClassName;
			int32 TagCount = 0;
			BinaryArchive << ObjectPath;
			BinaryArchive << ObjectClassName;
			BinaryArchive << TagCount;
			if (BinaryArchive.IsError() || TagCount < 0 || PackageFileSize < BinaryArchive.Tell() + TagCount * MinBytesPerTag)
			{
				OutError = EReadPackageDataMainErrorCode::InvalidTagCount;
				return false;
			}

			FAssetDataTagMap TagsAndValues;
			TagsAndValues.Reserve(TagCount);

			for (int32 TagIdx = 0; TagIdx < TagCount; ++TagIdx)
			{
				FString Key;
				FString Value;
				BinaryArchive << Key;
				BinaryArchive << Value;
				if (BinaryArchive.IsError())
				{
					OutError = EReadPackageDataMainErrorCode::InvalidTag;
					return false;
				}

				if (!Key.IsEmpty() && !Value.IsEmpty())
				{
					TagsAndValues.Add(FName(*Key), Value);
				}
			}

			// Before worlds were RF_Public, other non-public assets were added to the asset data table in map packages.
			// Here we simply skip over them
			if (bIsMapPackage && PackageFileSummary.GetFileVersionUE4() < VER_UE4_PUBLIC_WORLDS)
			{
				if (ObjectPath != FPackageName::GetLongPackageAssetName(PackageName))
				{
					continue;
				}
			}

			// if we have an object path that starts with the package then this asset is outer-ed to another package
			const bool bFullObjectPath = ObjectPath.StartsWith(TEXT("/"), ESearchCase::CaseSensitive);

			// if we do not have a full object path already, build it
			if (!bFullObjectPath)
			{
				// if we do not have a full object path, ensure that we have a top level object for the package and not a sub object
				if (!ensureMsgf(!ObjectPath.Contains(TEXT("."), ESearchCase::CaseSensitive), TEXT("Cannot make FAssetData for sub object %s in package %s!"), *ObjectPath, *PackageName))
				{
					UE_ASSET_LOG(LogAssetRegistry, Warning, *PackageName, TEXT("Cannot make FAssetData for sub object %s!"), *ObjectPath);
					continue;
				}
				ObjectPath = PackageName + TEXT(".") + ObjectPath;
			}
			// Previously export couldn't have its outer as an import
			else if (PackageFileSummary.GetFileVersionUE4() < VER_UE4_NON_OUTER_PACKAGE_IMPORT)
			{
				UE_ASSET_LOG(LogAssetRegistry, Warning, *PackageName, TEXT("Package has invalid export %s, resave source package!"), *ObjectPath);
				continue;
			}

			// Create a new FAssetData for this asset and update it with the gathered data
			OutAssetDataList.Add(new FAssetData(PackageName, ObjectPath, FName(*ObjectClassName), MoveTemp(TagsAndValues), PackageFileSummary.ChunkIDs, PackageFileSummary.PackageFlags));
		}

		return true;
	}

	// See the corresponding WriteAssetRegistryPackageData defined in SavePackageUtilities.cpp in CoreUObject module
	bool ReadPackageDataDependencies(FArchive& BinaryArchive, TBitArray<>& OutImportUsedInGame, TBitArray<>& OutSoftPackageUsedInGame)
	{
		BinaryArchive << OutImportUsedInGame;
		BinaryArchive << OutSoftPackageUsedInGame;
		return !BinaryArchive.IsError();
	}
}
}
