// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/PackageId.h"

class FIoDispatcher;
struct FPackageStoreEntry;

/**
 * Package store entry array view.
 */
template<typename T>
class TPackageStoreEntryCArrayView
{
	const uint32 ArrayNum = 0;
	const uint32 OffsetToDataFromThis = 0;

public:
	inline uint32 Num() const						{ return ArrayNum; }

	inline const T* Data() const					{ return (T*)((char*)this + OffsetToDataFromThis); }
	inline T* Data()								{ return (T*)((char*)this + OffsetToDataFromThis); }

	inline const T* begin() const					{ return Data(); }
	inline T* begin()								{ return Data(); }

	inline const T* end() const						{ return Data() + ArrayNum; }
	inline T* end()									{ return Data() + ArrayNum; }

	inline const T& operator[](uint32 Index) const	{ return Data()[Index]; }
	inline T& operator[](uint32 Index)				{ return Data()[Index]; }
};

/**
 * Package store entry.
 */
struct FPackageStoreEntry
{
	uint64 ExportBundlesSize;
	int32 ExportCount;
	int32 ExportBundleCount;
	uint32 LoadOrder;
	uint32 Pad;
	TPackageStoreEntryCArrayView<FPackageId> ImportedPackages;
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

	/* Get the package information for the specified package ID. */
	virtual const FPackageStoreEntry* GetPackageEntry(FPackageId PackageId) = 0;

	/* Returns the redirected package ID for the specified package ID. */
	virtual FPackageId GetRedirectedPackageId(FPackageId PackageId) = 0;

	/* Returns whether the package ID is a redirect. */
	virtual bool IsRedirect(FPackageId PackageId) = 0;
};

TUniquePtr<IPackageStore> MakeFilePackageStore(FIoDispatcher& IoDispatcher);
