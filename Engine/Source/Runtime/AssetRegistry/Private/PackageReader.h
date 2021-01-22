// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Serialization/ArchiveUObject.h"
#include "UObject/ObjectResource.h"
#include "UObject/PackageFileSummary.h"

struct FAssetData;
class FPackageDependencyData;

class FPackageReader : public FArchiveUObject
{
public:
	FPackageReader();
	~FPackageReader();

	enum class EOpenPackageResult : uint8
	{
		Success,
		NoLoader,
		MalformedTag,
		VersionTooOld,
		VersionTooNew,
		CustomVersionMissing,
		CustomVersionInvalid,
	};

	/** Creates a loader for the filename */
	bool OpenPackageFile(const FString& PackageFilename, EOpenPackageResult* OutErrorCode = nullptr);
	bool OpenPackageFile(FArchive* Loader, EOpenPackageResult* OutErrorCode = nullptr);
	bool OpenPackageFile(EOpenPackageResult* OutErrorCode = nullptr);

	/** Reads information from the asset registry data table and converts it to FAssetData */
	bool ReadAssetRegistryData(TArray<FAssetData*>& AssetDataList);

	/** Attempts to get the class name of an object from the thumbnail cache for packages older than VER_UE4_ASSET_REGISTRY_TAGS */
	bool ReadAssetDataFromThumbnailCache(TArray<FAssetData*>& AssetDataList);

	/** Creates asset data reconstructing all the required data from cooked package info */
	bool ReadAssetRegistryDataIfCookedPackage(TArray<FAssetData*>& AssetDataList, TArray<FString>& CookedPackageNamesWithoutAssetData);

	/** Reads information used by the dependency graph */
	bool ReadDependencyData(FPackageDependencyData& OutDependencyData);

	/** Serializers for different package maps */
	bool SerializeNameMap();
	bool SerializeImportMap(TArray<FObjectImport>& OutImportMap);
	bool SerializeExportMap(TArray<FObjectExport>& OutExportMap);
	bool SerializeSoftPackageReferenceList(TArray<FName>& OutSoftPackageReferenceList);
	bool SerializeSearchableNamesMap(FPackageDependencyData& OutDependencyData);
	bool SerializeAssetRegistryDependencyData(FPackageDependencyData& DependencyData);

	/** Returns flags the asset package was saved with */
	uint32 GetPackageFlags() const;

	// Farchive implementation to redirect requests to the Loader
	virtual void Serialize( void* V, int64 Length ) override;
	virtual bool Precache( int64 PrecacheOffset, int64 PrecacheSize ) override;
	virtual void Seek( int64 InPos ) override;
	virtual int64 Tell() override;
	virtual int64 TotalSize() override;
	virtual FArchive& operator<<( FName& Name ) override;
	virtual FString GetArchiveName() const override
	{
		return PackageFilename;
	}

private:
	bool StartSerializeSection(int64 Offset);

	FString PackageFilename;
	/* Loader is the interface used to read the bytes from the package's repository. All interpretation of the bytes is done by serializing into *this, which is also an FArchive. */
	FArchive* Loader;
	FPackageFileSummary PackageFileSummary;
	TArray<FName> NameMap;
	int64 PackageFileSize;
	int64 AssetRegistryDependencyDataOffset;
	bool bLoaderOwner;
};
