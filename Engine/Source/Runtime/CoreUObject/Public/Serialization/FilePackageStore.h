// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "IO/PackageId.h"

class IPackageStore;
class FIoDispatcher;

/**
 * Package store entry array view.
 */
template<typename T>
class TFilePackageStoreEntryCArrayView
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
 * File based package store entry
 */
struct FFilePackageStoreEntry
{
	int32 ExportCount;
	int32 ExportBundleCount;
	TFilePackageStoreEntryCArrayView<FPackageId> ImportedPackages;
	TFilePackageStoreEntryCArrayView<FSHAHash> ShaderMapHashes;
};

TUniquePtr<IPackageStore> MakeFilePackageStore(FIoDispatcher& IoDispatcher);
