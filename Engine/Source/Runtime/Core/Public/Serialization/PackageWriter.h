// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/StringView.h"
#include "IO/IoDispatcher.h"
#include "Misc/DateTime.h"
#include "Misc/SecureHash.h"
#include "Serialization/CompactBinary.h"

class FAssetRegistryState;
class IPackageStoreWriter;
struct FPackageStoreEntryResource;

/** Interface for SavePackage to write packages to storage. */
class IPackageWriter
{
public:
	virtual ~IPackageWriter() = default;


	struct FCapabilities
	{
		/** Whether this writer needs BulkDatas to be written after the Linker's archive has finalized its size.

			Some PackageWriters need that behavior because they put the BulkDatas in a segment following
			the exports in a Composite archive.
		 */
		bool bAdditionalFilesNeedLinkerSize = false;
		/** Whether data stored in Linker.AdditionalDataToAppend should be serialized to a separate archive.

			If false, the data will be serialized to the end of the LinkerSave archive instead.
		*/
		bool bLinkerAdditionalDataInSeparateArchive = false;
	};

	/** Return capabilities/settings this PackageWriter has/requires 
	  */
	virtual FCapabilities GetCapabilities() const
	{
		return FCapabilities();
	}

	// Events the PackageWriter receives
	struct FBeginPackageInfo
	{
		FName	PackageName;
	};

	/** Mark the beginning of a package store transaction for the specified package

		This must be called before any data is produced for a given package
	  */
	virtual void BeginPackage(const FBeginPackageInfo& Info) = 0;

	struct FCommitPackageInfo
	{
		FName PackageName;
		FGuid PackageGuid;
		FCbObject TargetDomainDependencies;
		bool bSucceeded = false;
	};

	/** Finalize a package started with BeginPackage()
	  */
	virtual void CommitPackage(const FCommitPackageInfo& Info) = 0;

	struct FPackageInfo
	{
		FName		PackageName;
		FString		LooseFilePath;
		uint64		HeaderSize;
		FIoChunkId	ChunkId = FIoChunkId::InvalidChunkId;
	};

	/** Write package data (exports and serialized header)

		This may only be called after a BeginPackage() call has been made
		to signal the start of a package store transaction
	  */
	virtual void WritePackageData(const FPackageInfo& Info, const FIoBuffer& PackageData, const TArray<FFileRegion>& FileRegions) = 0;

	struct FBulkDataInfo
	{
		enum EType
		{
			Standard,
			Mmap,
			Optional
		};

		FName		PackageName;
		EType		BulkdataType = Standard;
		FString		LooseFilePath;
		FIoChunkId	ChunkId = FIoChunkId::InvalidChunkId;
	};

	/** Write bulk data for the current package
	  */
	virtual void WriteBulkdata(const FBulkDataInfo& Info, const FIoBuffer& BulkData, const TArray<FFileRegion>& FileRegions) = 0;

	struct FAdditionalFileInfo
	{
		FName	PackageName;
		FString	Filename;
		FIoChunkId ChunkId;
	};

	/** Write separate files written by UObjects during cooking via UObject::CookAdditionalFiles. */
	virtual bool WriteAdditionalFile(const FAdditionalFileInfo& Info, const FIoBuffer& FileData) = 0;

	struct FLinkerAdditionalDataInfo
	{
		FName PackageName;
	};
	/** Write separate data written by UObjects via FLinkerSave::AdditionalDataToAppend.

		This function will not be called unless GetCapabilities().bLinkerAdditionalDataInSeparateArchive is true.
		If that function is false, the data was inlined into the PackageData instead.
	*/
	virtual void WriteLinkerAdditionalData(const FLinkerAdditionalDataInfo& Info, const FIoBuffer& Data, const TArray<FFileRegion>& FileRegions) = 0;
};

/** Interface for cooking that writes cooked packages to storage usable by the runtime game. */
class ICookedPackageWriter : public IPackageWriter
{
public:
	virtual ~ICookedPackageWriter() = default;

	struct FCookCapabilities
	{
		/** Whether this writer implements -diffonly and -linkerdiff. */
		bool bDiffModeSupported = false;
		/** Whether this writer implements the IPackageWriter interface and can be passed to SavePackage. */
		bool bSavePackageSupported = true;
	};

	/** Return cook capabilities/settings this PackageWriter has/requires
	  */
	virtual FCookCapabilities GetCookCapabilities() const
	{
		return FCookCapabilities();
	}

	/** Return the timestamp of the previous cook, or FDateTime::MaxValue to indicate previous cook should be assumed newer than any other cook data. */
	virtual FDateTime GetPreviousCookTime() const
	{
		return FDateTime::MaxValue();
	}

	struct FCookInfo
	{
		enum ECookMode
		{
			CookByTheBookMode,
			CookOnTheFlyMode
		};

		ECookMode CookMode = CookByTheBookMode;
		bool bCleanBuild = true;
		bool bIterateSharedBuild = false;
	};

	/** Signal the start of a cooking pass

		Package data may only be produced after BeginCook() has been called and
		before EndCook() is called
	  */
	virtual void BeginCook(const FCookInfo& Info) = 0;

	/** Signal the end of a cooking pass.
	  */
	virtual void EndCook() = 0;

	/**
	 * Flush any outstanding writes.
	 */
	virtual void Flush() = 0;

	struct FCookedPackageInfo
	{
		FName PackageName;
		FMD5Hash Hash;
		FGuid PackageGuid;
		int64 DiskSize = -1;
		FIoHash TargetDomainDependencies;
	};

	/**
	 * Appends list of cooked package(s) to the given output array.
	 */
	virtual void GetCookedPackages(TArray<FCookedPackageInfo>& OutCookedPackages) = 0;

	/**
	 * Returns the TargetDomainDependencies that were previously commited for the given PackageName.
	 * Returns an empty object if not found.
	 */
	virtual FCbObject GetTargetDomainDependencies(FName PackageName) = 0;

	/**
	 * Remove the given cooked package(s) from storage; they have been modified since the last cook.
	 */
	virtual void RemoveCookedPackages(TArrayView<const FName> PackageNamesToRemove) = 0;

	/**
	 * Remove all cooked packages from storage.
	 */
	virtual void RemoveCookedPackages() = 0;

	/**
	 * If a previous-version AssetRegistryState was allocated during GetCookedPackages, drop the reference to it
	 * and return it to the caller who takes responsibility for deleting it.
	 */
	virtual FAssetRegistryState* ReleasePreviousAssetRegistry()
	{
		return nullptr;
	}

	/** Downcast function for ICookedPackageWriters that implement the IPackageStoreWriter inherited interface. */
	virtual IPackageStoreWriter* AsPackageStoreWriter()
	{
		return nullptr;
	}
};

static inline const ANSICHAR* LexToString(IPackageWriter::FBulkDataInfo::EType Value)
{
	switch (Value)
	{
	case IPackageWriter::FBulkDataInfo::Standard:
		return "Standard";
	case IPackageWriter::FBulkDataInfo::Mmap:
		return "Mmap";
	case IPackageWriter::FBulkDataInfo::Optional:
		return "Optional";
	}

	return "Unknown";
}
