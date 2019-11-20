// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Array.h"
#include "Containers/Set.h"
#include "Containers/Map.h"
#include "IO/IoDispatcher.h"
#include "UObject/NameTypes.h"

class FArchive;
class FLinkerLoad;
class FLinkerSave;

class IBulkDataManifest 
{
public:
	virtual ~IBulkDataManifest() = default;

	virtual void Save() = 0;
	virtual void AddFileAccess(const FString& PackageFilename, uint16 InIndex, uint64 InOffset, uint64 InSize) = 0;
};

class FPackageStoreBulkDataManifest : public IBulkDataManifest
{
public:
	COREUOBJECT_API FPackageStoreBulkDataManifest(const FString& InRootPath);
	COREUOBJECT_API virtual ~FPackageStoreBulkDataManifest();

	COREUOBJECT_API bool Load();
	virtual void Save() override;
	
	const FString& GetFilename() const { return Filename; }

	class PackageDesc
	{
	public:
		struct BulkDataDesc
		{
			uint16 Index;
			uint64 Offset;
			uint64 Size;
		};

		void AddData(uint16 InIndex, uint64 InOffset, uint64 InSize);
		const TArray<BulkDataDesc>& GetDataArray() const { return Data; }
	private:
		friend FArchive& operator<<(FArchive& Ar, PackageDesc& Entry);
		TArray<BulkDataDesc> Data;
	};

	COREUOBJECT_API const PackageDesc* Find(const FString& PackageName) const;

private:
	virtual void AddFileAccess(const FString& PackageFilename, uint16 InIndex, uint64 InOffset, uint64 InSize) override;

	PackageDesc& GetOrCreateFileAccess(const FString& PackageFilename);

	FString FixFilename(const FString& InFileName) const;

	FString RootPath;
	FString Filename;
	TMap<FString, PackageDesc> Data;
};

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
	FSavePackageContext(FPackageStoreWriter& InPackageStoreWriter, IBulkDataManifest& InBulkDataManifest)
	: PackageStoreWriter(InPackageStoreWriter) 
	, BulkDataManifest(InBulkDataManifest)
	{
	}

	FPackageStoreWriter& PackageStoreWriter;
	IBulkDataManifest& BulkDataManifest;
};
