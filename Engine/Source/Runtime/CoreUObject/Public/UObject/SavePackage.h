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

class INameMapSaver
{
public:
	virtual ~INameMapSaver() = default;

	virtual void Begin() = 0;
	virtual void End() = 0;
	virtual void BeginPackage() = 0;
	virtual void EndPackage(FLinkerSave& Linker, FLinkerLoad* Conform, FArchive* BinarySaver) = 0;

	virtual void MarkNameAsReferenced(FName Name) = 0;
	virtual int32 MapName(FName Name) const = 0;
	virtual bool NameExistsInCurrentPackage(FNameEntryId ComparisonId) const = 0;
};

class FSinglePackageNameMapSaver : public INameMapSaver
{
public:
	virtual void Begin() override {};
	virtual void End() override {};
	virtual void BeginPackage() override {};
	virtual void EndPackage(FLinkerSave& Linker, FLinkerLoad* Conform, FArchive* BinarySaver) override;

	virtual void MarkNameAsReferenced(FName Name) override;
	virtual int32 MapName(FName Name) const override;
	virtual bool NameExistsInCurrentPackage(FNameEntryId ComparisonId) const override;

	void MarkNameAsReferenced(FNameEntryId Name);

private:
	TSet<FNameEntryId> ReferencedNames;
	TMap<FNameEntryId, int32> NameIndices;
};

class FPackageStoreNameMapSaver : public INameMapSaver
{
public:
	COREUOBJECT_API FPackageStoreNameMapSaver(const TCHAR* InFilename);

	COREUOBJECT_API virtual void Begin() override {};
	COREUOBJECT_API virtual void End() override;
	virtual void BeginPackage();
	virtual void EndPackage(FLinkerSave& Linker, FLinkerLoad* Conform, FArchive* BinarySaver) override {}

	virtual void MarkNameAsReferenced(FName Name) override;
	virtual int32 MapName(FName Name) const override;
	virtual bool NameExistsInCurrentPackage(FNameEntryId ComparisonId) const override;

private:
	TMap<FNameEntryId, int32> NameIndices;
	TArray<FNameEntryId> NameMap;
	mutable TMap<FNameEntryId, TTuple<int32,int32,int32>> DebugNameCounts; // <Number0Count,OtherNumberCount,MaxNumber>
	TSet<FNameEntryId> PackageReferencedNames;
	const FString Filename;
};

class FPackageHeaderSaver
{
public:
	FPackageHeaderSaver(INameMapSaver& InNameMapSaver) : NameMapSaver(InNameMapSaver) {}
	INameMapSaver& NameMapSaver;
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
	FSavePackageContext(FPackageHeaderSaver& InHeaderSaver, FPackageStoreWriter& InPackageStoreWriter) 
	: HeaderSaver(InHeaderSaver), PackageStoreWriter(InPackageStoreWriter) 
	{
	}

	FPackageHeaderSaver& HeaderSaver;
	FPackageStoreWriter& PackageStoreWriter;
};
