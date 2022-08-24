// Copyright Epic Games, Inc. All Rights Reserved.
#pragma once

#if WITH_EDITOR

#include "CoreMinimal.h"
#include "Hash/CityHash.h"
#include "Misc/Paths.h"
#include "WorldPartition/WorldPartitionLog.h"

struct FWorldPartitionCookPackage
{
	enum class EType
	{
		Unknown,
		Level,
		Generic
	};

	using IDType = uint64;
	
	static IDType MakeCookPackageID(const FString& InRoot, const FString& InRelativeFilename)
	{
		check(!InRoot.IsEmpty() && !InRelativeFilename.IsEmpty());
		check(InRoot[0] == '/' && InRoot[InRoot.Len() - 1] != '/'); // Root is assumed to be in the format "/Root"
		check(InRelativeFilename[0] == '/' && InRelativeFilename[InRelativeFilename.Len() - 1] != '/'); // RelativeFileName is assumed to be in the format "/Relativefilename

		// Avoid doing string copies as this function is often called during cook when bridging between Cook code & WorldPartition code.
		// Compute a hash for both InRoot & InRelativeFilename. Then combine them instead of creating a new fullpath string and computing the hash on it.
		uint64 RootHash = CityHash64(reinterpret_cast<const char*>(*InRoot), InRoot.Len() * sizeof(TCHAR));
		uint64 RelativePathHash = CityHash64(reinterpret_cast<const char*>(*InRelativeFilename), InRelativeFilename.Len() * sizeof(TCHAR));
		return RootHash ^ RelativePathHash;
	}

	static FString MakeFullPath(const FString& InRoot, const FString& InRelativeFilename)
	{
		TStringBuilderWithBuffer<TCHAR, NAME_SIZE> FullPath;
		FullPath += TEXT("/");
		FullPath += InRoot;
		FullPath += TEXT("/");
		FullPath += InRelativeFilename;

		return FPaths::RemoveDuplicateSlashes(*FullPath);
	}

	FWorldPartitionCookPackage(const FString& InRoot, const FString& InRelativePath, EType InType)
		: Root(SanitizePathComponent(InRoot)),
		RelativePath(SanitizePathComponent(InRelativePath)),
		GeneratedPackage(nullptr),
		PackageId(MakeCookPackageID(Root, RelativePath)),
		Type(InType)
	{
	}

	FString GetFullPath() const { return MakeFullPath(Root, RelativePath); }

	const FString Root;
	const FString RelativePath;
	UPackage* GeneratedPackage;
	const IDType PackageId;
	const EType Type;

private:
	// PathComponents (Root & RelativePath members) need to follow the "/<PathComponent>" format for the PackageId computation to work.
	FString SanitizePathComponent(const FString& Path) const
	{
		FString SanitizedPath = TEXT("/") + Path;
		FPaths::RemoveDuplicateSlashes(SanitizedPath);
		
		if (SanitizedPath[SanitizedPath.Len() - 1] == '/')
		{
			SanitizedPath.RemoveAt(SanitizedPath.Len() - 1, 1);
		}

		return SanitizedPath;
	}
};

#endif
