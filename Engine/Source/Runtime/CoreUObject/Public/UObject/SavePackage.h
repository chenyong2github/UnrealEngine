// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Containers/Map.h"
#include "UObject/NameTypes.h"

class FArchive;
class FIoBuffer;
class FLinkerLoad;
class FLinkerSave;
class FPackageStoreBulkDataManifest;

class FPackageStoreWriter
{
public:
	COREUOBJECT_API			FPackageStoreWriter();
	COREUOBJECT_API virtual ~FPackageStoreWriter();

	struct HeaderInfo
	{
		FName	PackageName;
		FString	LooseFilePath;
	};

	/** Write 'uasset' data
	  */
	virtual void WriteHeader(const HeaderInfo& Info, const FIoBuffer& HeaderData) = 0;

	struct ExportsInfo
	{
		FName	PackageName;
		FString	LooseFilePath;

		TArray<FIoBuffer> Exports;
	};

	/** Write 'uexp' data
	  */
	virtual void WriteExports(const ExportsInfo& Info, const FIoBuffer& ExportsData) = 0;

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

	/** Write 'ubulk' data
	  */
	virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData) = 0;
};

class FLooseFileWriter : public FPackageStoreWriter
{
public:
	COREUOBJECT_API FLooseFileWriter();
	COREUOBJECT_API ~FLooseFileWriter();

	COREUOBJECT_API virtual void WriteHeader(const HeaderInfo& Info, const FIoBuffer& HeaderData) override;
	COREUOBJECT_API virtual void WriteExports(const ExportsInfo& Info, const FIoBuffer& ExportsData) override;
	COREUOBJECT_API virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData) override;

private:
};

class FSavePackageContext
{
public:
	FSavePackageContext(FPackageStoreWriter* InPackageStoreWriter, FPackageStoreBulkDataManifest* InBulkDataManifest)
	: PackageStoreWriter(InPackageStoreWriter) 
	, BulkDataManifest(InBulkDataManifest)
	{
	}

	COREUOBJECT_API ~FSavePackageContext();

	FPackageStoreWriter* PackageStoreWriter;
	FPackageStoreBulkDataManifest* BulkDataManifest;
};
