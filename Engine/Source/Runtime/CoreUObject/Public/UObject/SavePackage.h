// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"
#include "Serialization/FileRegions.h"
#include "Misc/DateTime.h"
#include "ObjectMacros.h"

#if !defined(UE_WITH_SAVEPACKAGE)
#	define UE_WITH_SAVEPACKAGE 1
#endif

class FArchive;
class FIoBuffer;
class FPackageStoreBulkDataManifest;
class FSavePackageContext;
class FArchiveDiffMap;
class FOutputDevice;

/**
 * Struct to encapsulate arguments specific to saving one package
 */
struct FPackageSaveInfo
{
	class UPackage* Package = nullptr;
	class UObject* Asset = nullptr;
	FString Filename;
};

/**
 * Struct to encapsulate UPackage::Save arguments. 
 * These arguments are shared between packages when saving multiple packages concurrently.
 */
struct FSavePackageArgs
{
	class ITargetPlatform* TargetPlatform = nullptr;
	EObjectFlags TopLevelFlags = RF_NoFlags;
	uint32 SaveFlags = 0;
	bool bForceByteSwapping = false; // for FLinkerSave
	bool bWarnOfLongFilename = false;
	bool bSlowTask = true;
	FDateTime FinalTimeStamp;
	FOutputDevice* Error = nullptr;
	FArchiveDiffMap* DiffMap = nullptr;
	FSavePackageContext* SavePackageContext = nullptr;
};

class IPackageStoreWriter
{
public:
	COREUOBJECT_API virtual ~IPackageStoreWriter();

	struct FPackageInfo
	{
		FName	PackageName;
		FString	LooseFilePath;
		uint64  HeaderSize;
	};

	virtual void WritePackage(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions) = 0;

	struct FBulkDataInfo
	{
		enum EType 
		{
			Standard,
			Mmap,
			Optional
		};

		FName	PackageName;
		EType	BulkdataType = Standard;
		FString	LooseFilePath;
	};

	virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) = 0;

	virtual void Finalize() = 0;
};

class FSavePackageContext
{
public:
	FSavePackageContext(IPackageStoreWriter* InPackageStoreWriter, FPackageStoreBulkDataManifest* InBulkDataManifest, bool InbForceLegacyOffsets)
	: PackageStoreWriter(InPackageStoreWriter) 
	, BulkDataManifest(InBulkDataManifest)
	, bForceLegacyOffsets(InbForceLegacyOffsets)
	{
	}

	COREUOBJECT_API ~FSavePackageContext();

	IPackageStoreWriter* PackageStoreWriter;
	FPackageStoreBulkDataManifest* BulkDataManifest;
	bool bForceLegacyOffsets;
};
