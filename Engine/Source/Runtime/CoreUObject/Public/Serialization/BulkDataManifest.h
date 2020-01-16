// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Containers/Map.h"
#include "IO/IoDispatcher.h"

class FPackageStoreBulkDataManifest
{
public:
	COREUOBJECT_API FPackageStoreBulkDataManifest(const FString& ProjectPath);
	COREUOBJECT_API ~FPackageStoreBulkDataManifest();

	COREUOBJECT_API bool Load();
	COREUOBJECT_API void Save();

	void AddFileAccess(const FString& PackageFilename, EIoChunkType InType, uint64 InChunkId, uint64 InOffset, uint64 InSize);

	const FString& GetFilename() const { return Filename; }

	class FPackageDesc
	{
	public:
		struct FBulkDataDesc
		{
			uint64 ChunkId;	// Note this is the Offset before the linker BulkDataStartOffset is
							// applied, to make it easier to compute at runtime.
			uint64 Offset;
			uint64 Size;
			EIoChunkType Type;
		};

		void AddData(EIoChunkType InType, uint64 InChunkId, uint64 InOffset, uint64 InSize, const FString& DebugFilename);
		void AddZeroByteData(EIoChunkType InType);

		const TArray<FBulkDataDesc>& GetDataArray() const { return Data; }
	private:
		friend FArchive& operator<<(FArchive& Ar, FPackageDesc& Entry);
		TArray<FBulkDataDesc> Data;
	};

	COREUOBJECT_API const FPackageDesc* Find(const FString& PackageName) const;

private:
	FPackageDesc& GetOrCreateFileAccess(const FString& PackageFilename);

	FString FixFilename(const FString& InFileName) const;

	FString RootPath;
	FString Filename;
	TMap<FString, FPackageDesc> Data;
};
