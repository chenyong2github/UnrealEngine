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

class FPackageStoreBulkDataManifest
{
public:
	COREUOBJECT_API FPackageStoreBulkDataManifest(const FString& ProjectPath);
	COREUOBJECT_API ~FPackageStoreBulkDataManifest();

	COREUOBJECT_API bool Load();
	COREUOBJECT_API void Save() ;

	void AddFileAccess(const FString& PackageFilename, EIoChunkType InType, uint64 InChunkId, uint64 InOffset, uint64 InSize);
	
	const FString& GetFilename() const { return Filename; }

	class PackageDesc
	{
	public:
		struct BulkDataDesc
		{
			uint64 ChunkId;	// Note this is the Offset before the linker BulkDataStartOffset is
							// applied, to make it easier to compute at runtime.
			uint64 Offset;
			uint64 Size;
			EIoChunkType Type;
		};

		void AddData(EIoChunkType InType, uint64 InChunkId, uint64 InOffset, uint64 InSize, const FString& DebugFilename);
		void AddZeroByteData(EIoChunkType InType);

		const TArray<BulkDataDesc>& GetDataArray() const { return Data; }
	private:
		friend FArchive& operator<<(FArchive& Ar, PackageDesc& Entry);
		TArray<BulkDataDesc> Data;
	};

	COREUOBJECT_API const PackageDesc* Find(const FString& PackageName) const;

private:
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
	FSavePackageContext(FPackageStoreWriter* InPackageStoreWriter, FPackageStoreBulkDataManifest* InBulkDataManifest)
	: PackageStoreWriter(InPackageStoreWriter) 
	, BulkDataManifest(InBulkDataManifest)
	{
	}

	~FSavePackageContext()
	{
		delete PackageStoreWriter;
		delete BulkDataManifest;
	}

	FPackageStoreWriter* PackageStoreWriter;
	FPackageStoreBulkDataManifest* BulkDataManifest;
};
