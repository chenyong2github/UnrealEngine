// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "PackageId.h"
#include "Misc/SecureHash.h"

class FArchive;
class FStructuredArchiveSlot;
class FCbObject;
class FCbWriter;

/**
 * Package export information.
 */
struct FPackageStoreExportInfo
{
	int32 ExportCount = 0;
	int32 ExportBundleCount = 0;
	
	CORE_API friend FArchive& operator<<(FArchive& Ar, FPackageStoreExportInfo& ExportInfo);
	
	CORE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FPackageStoreExportInfo& ExportInfo);
	
	CORE_API static FPackageStoreExportInfo FromCbObject(const FCbObject& Obj);
};

/**
 * Package store entry.
 */ 
struct FPackageStoreEntry
{
	FPackageStoreExportInfo ExportInfo;
	TArrayView<const FPackageId> ImportedPackageIds;
	TArrayView<const FSHAHash> ShaderMapHashes;
};

/**
 * Package store entry flags.
 */
enum class EPackageStoreEntryFlags : uint32
{
	None		= 0,
	Redirected	= 0x01,
};
ENUM_CLASS_FLAGS(EPackageStoreEntryFlags);

/**
 * Package store entry resource.
 *
 * This is a non-optimized serializable version
 * of a package store entry. Used when cooking
 * and when running cook-on-the-fly.
 */
struct FPackageStoreEntryResource
{
	/** The package store entry flags. */
	EPackageStoreEntryFlags Flags;
	/** The package name. */
	FName PackageName;
	/** Used for localized and redirected packages. */
	FName SourcePackageName;
	/** Region name for localized packages. */
	FName Region;
	/** The package export information. */
	FPackageStoreExportInfo ExportInfo;
	/** Imported package IDs. */
	TArray<FPackageId> ImportedPackageIds;
	/** Referenced shader map hashes. */
	TArray<FSHAHash> ShaderMapHashes;

	/** Returns the package ID. */
	FPackageId GetPackageId() const
	{
		return FPackageId::FromName(PackageName);
	}

	/** Returns the source package ID. */
	FPackageId GetSourcePackageId() const
	{
		return SourcePackageName.IsNone() ? FPackageId() : FPackageId::FromName(SourcePackageName);
	}

	FName GetSourcePackageName() const
	{
		return SourcePackageName;
	}

	/** Returns whether this package is redirected. */
	bool IsRedirected() const
	{
		return EnumHasAnyFlags(Flags, EPackageStoreEntryFlags::Redirected); 
	}

	CORE_API friend FArchive& operator<<(FArchive& Ar, FPackageStoreEntryResource& PackageStoreEntry);
	
	CORE_API friend FCbWriter& operator<<(FCbWriter& Writer, const FPackageStoreEntryResource& PackageStoreEntry);
	
	CORE_API static FPackageStoreEntryResource FromCbObject(const FCbObject& Obj);
};

/**
 * Package store entry status.
 */
enum class EPackageStoreEntryStatus
{
	None,
	Ok,
	Pending,
	Missing,
};

/**
 * Represents a handle to package information.
 */
class FPackageStoreEntryHandle
{
	static constexpr uint64 HandleBits = 62ull;
	static constexpr uint64 HandleMask = (1ull << HandleBits) - 1ull;
	static constexpr uint64 StatusMask = ~HandleMask;
	static constexpr uint64 StatusShift = HandleBits;
	static constexpr uint64 InvalidHandle = 0;

	uint64 Handle = InvalidHandle;

	inline explicit FPackageStoreEntryHandle(const uint64 InHandle)
		: Handle(InHandle) { }

public:
	FPackageStoreEntryHandle() = default;

	inline uint64 Value() const
	{
		return (Handle & HandleMask);
	}

	inline bool IsValid() const
	{
		return Value() != InvalidHandle;
	}

	inline EPackageStoreEntryStatus Status() const
	{
		return static_cast<EPackageStoreEntryStatus>((Handle & StatusMask) >> StatusShift);
	}

	inline bool operator<(FPackageStoreEntryHandle Other) const
	{
		return Handle < Other.Handle;
	}

	inline bool operator==(FPackageStoreEntryHandle Other) const
	{
		return Handle == Other.Handle;
	}
	
	inline bool operator!=(FPackageStoreEntryHandle Other) const
	{
		return Handle != Other.Handle;
	}

	inline friend uint32 GetTypeHash(const FPackageStoreEntryHandle& Entry)
	{
		return uint32(Entry.Handle);
	}

	operator bool() const
	{
		return IsValid();
	}

	static FPackageStoreEntryHandle Create(uint64 InHandle, EPackageStoreEntryStatus EntryStatus = EPackageStoreEntryStatus::Ok)
	{
		check(!(InHandle & StatusMask));
		return FPackageStoreEntryHandle((static_cast<uint64>(EntryStatus) << StatusShift) | (InHandle & HandleMask));
	}
};

/**
 * Stores information about available packages that can be loaded.
 */
class IPackageStore
{
public:
	/* Destructor. */
	virtual ~IPackageStore() { }

	/* Initialize the package store. */
	virtual void Initialize() = 0;

	/* Returns whether the package exists. */
	virtual bool DoesPackageExist(FPackageId PackageId) = 0;

	/* Returns the package store entry for the specified package ID. */
	virtual FPackageStoreEntryHandle GetPackageEntryHandle(FPackageId PackageId, const FName& PackageName = NAME_None) = 0;

	/* Returns the package store entry data with export info and imported packages for the specified package ID. */
	virtual FPackageStoreEntry GetPackageEntry(FPackageStoreEntryHandle Handle) = 0;

	/* Returns the redirected package ID and source package name for the specified package ID if it's being redirected. */
	virtual bool GetPackageRedirectInfo(FPackageId PackageId, FName& OutSourcePackageName, FPackageId& OutRedirectedToPackageId) = 0;
};
