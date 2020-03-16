// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "Algo/Find.h"
#include "Containers/StringView.h"
#include "IAssetRegistry.h"
#include "Misc/PackageName.h"

/* This class has marked all of its functions const and its variables mutable so that CookOnTheFlyServer can use its functions from const functions */
struct FPackageNameCache
{
	bool			HasCacheForPackageName(const FName& PackageName) const;

	FString			GetCachedStandardFileNameString(const UPackage* Package) const;

	FName			GetCachedStandardFileName(const FName& PackageName) const;
	FName			GetCachedStandardFileName(const UPackage* Package) const;

	const FName*	GetCachedPackageNameFromStandardFileName(const FName& NormalizedFileName, bool bExactMatchRequired=true, FName* FoundFileName = nullptr) const;

	void			ClearPackageFileNameCache(IAssetRegistry* InAssetRegistry) const;
	bool			ClearPackageFileNameCacheForPackage(const UPackage* Package) const;
	bool			ClearPackageFileNameCacheForPackage(const FName& PackageName) const;

	void			SetAssetRegistry(IAssetRegistry* InAssetRegistry) const;

	/** Normalize the given FileName for use in looking up the cached data associated with the FileName. This normalization is equivalent to FPaths::MakeStandardFilename */
	static FName	GetStandardFileName(const FName& FileName);
	static FName	GetStandardFileName(const FStringView& FileName);

private:
	struct FCachedPackageFilename
	{
		FCachedPackageFilename(FString&& InStandardFilename, FName InStandardFileFName)
			: StandardFileNameString(MoveTemp(InStandardFilename))
			, StandardFileName(InStandardFileFName)
		{
		}

		FCachedPackageFilename(const FCachedPackageFilename& In) = default;

		FCachedPackageFilename(FCachedPackageFilename&& In)
			: StandardFileNameString(MoveTemp(In.StandardFileNameString))
			, StandardFileName(In.StandardFileName)
		{
		}

		FString		StandardFileNameString;
		FName		StandardFileName;
	};

	bool DoesPackageExist(const FName& PackageName, FString* OutFilename) const;
	const FCachedPackageFilename& Cache(const FName& PackageName) const;

	mutable IAssetRegistry* AssetRegistry = nullptr;

	mutable TMap<FName, FCachedPackageFilename> PackageFilenameCache; // filename cache (only process the string operations once)
	mutable TMap<FName, FName>					PackageFilenameToPackageFNameCache;
};

inline FName FPackageNameCache::GetCachedStandardFileName(const FName& PackageName) const
{
	return Cache(PackageName).StandardFileName;
}

inline bool FPackageNameCache::HasCacheForPackageName(const FName& PackageName) const
{
	return PackageFilenameCache.Find(PackageName) != nullptr;
}

inline FString FPackageNameCache::GetCachedStandardFileNameString(const UPackage* Package) const
{
	// check( Package->GetName() == Package->GetFName().ToString() );
	return Cache(Package->GetFName()).StandardFileNameString;
}

inline FName FPackageNameCache::GetCachedStandardFileName(const UPackage* Package) const
{
	// check( Package->GetName() == Package->GetFName().ToString() );
	return Cache(Package->GetFName()).StandardFileName;
}

inline bool FPackageNameCache::ClearPackageFileNameCacheForPackage(const UPackage* Package) const
{
	return ClearPackageFileNameCacheForPackage(Package->GetFName());
}

inline bool FPackageNameCache::ClearPackageFileNameCacheForPackage(const FName& PackageName) const
{
	check(IsInGameThread());

	return PackageFilenameCache.Remove(PackageName) >= 1;
}

inline bool FPackageNameCache::DoesPackageExist(const FName& PackageName, FString* OutFilename) const
{
	if (!AssetRegistry)
	{
		return FPackageName::DoesPackageExist(PackageName.ToString(), NULL, OutFilename, false);
	}

	TArray<FAssetData> Assets;
	bool bIncludeOnlyDiskAssets = !FPackageName::IsExtraPackage(PackageName.ToString());
	AssetRegistry->GetAssetsByPackageName(PackageName, Assets, bIncludeOnlyDiskAssets);

	if (Assets.Num() <= 0)
	{
		return false;
	}

	if (OutFilename)
	{
		const bool ContainsMap = Algo::FindByPredicate(Assets, [](const FAssetData& Asset) { return Asset.PackageFlags & PKG_ContainsMap; }) != nullptr;
		const FString& PackageExtension = ContainsMap ? FPackageName::GetMapPackageExtension() : FPackageName::GetAssetPackageExtension();
		*OutFilename = FPackageName::LongPackageNameToFilename(PackageName.ToString(), PackageExtension);
	}

	return true;
}

inline const FPackageNameCache::FCachedPackageFilename& FPackageNameCache::Cache(const FName& PackageName) const
{
	check(IsInGameThread());

	FCachedPackageFilename *Cached = PackageFilenameCache.Find(PackageName);

	if (Cached != NULL)
	{
		return *Cached;
	}

	// cache all the things, like it's your birthday!

	FString FilenameOnDisk;
	FString FileNameString;
	FName FileName = NAME_None;

	if (DoesPackageExist(PackageName, &FilenameOnDisk))
	{
		FileNameString = FPaths::ConvertRelativePathToFull(FilenameOnDisk);

		FPaths::MakeStandardFilename(FileNameString);
		FileName = FName(*FileNameString);
	}

	PackageFilenameToPackageFNameCache.Add(FileName, PackageName);

	return PackageFilenameCache.Emplace(PackageName, FCachedPackageFilename(MoveTemp(FileNameString), FileName));
}

inline const FName* FPackageNameCache::GetCachedPackageNameFromStandardFileName(const FName& NormalizedFileName, bool bExactMatchRequired, FName* FoundFileName) const
{
	check(IsInGameThread());
	const FName* Result = PackageFilenameToPackageFNameCache.Find(NormalizedFileName);
	if (Result)
	{
		if (FoundFileName)
		{
			*FoundFileName = NormalizedFileName;
		}
		return Result;
	}

	FName PackageName = NormalizedFileName;
	FString PotentialLongPackageName = NormalizedFileName.ToString();
	if (FPackageName::IsValidLongPackageName(PotentialLongPackageName) == false)
	{
		PotentialLongPackageName = FPackageName::FilenameToLongPackageName(PotentialLongPackageName);
		PackageName = FName(*PotentialLongPackageName);
	}

	const FCachedPackageFilename& CachedFilename = Cache(PackageName);

	if (bExactMatchRequired)
	{
		if (FoundFileName)
		{
			*FoundFileName = NormalizedFileName;
		}
		return PackageFilenameToPackageFNameCache.Find(NormalizedFileName);
	}
	else
	{
		check(FoundFileName != nullptr);
		*FoundFileName = CachedFilename.StandardFileName;
		return PackageFilenameToPackageFNameCache.Find(CachedFilename.StandardFileName);
	}
}

inline void FPackageNameCache::ClearPackageFileNameCache(IAssetRegistry* InAssetRegistry) const
{
	check(IsInGameThread());
	PackageFilenameCache.Empty();
	PackageFilenameToPackageFNameCache.Empty();
	AssetRegistry = InAssetRegistry;
}

inline void FPackageNameCache::SetAssetRegistry(IAssetRegistry* InAssetRegistry) const
{
	AssetRegistry = InAssetRegistry;
}

inline FName FPackageNameCache::GetStandardFileName(const FName& FileName)
{
	return GetStandardFileName(FileName.ToString());
}

inline FName FPackageNameCache::GetStandardFileName(const FStringView& InFileName)
{
	FString FileName(InFileName);
	FPaths::MakeStandardFilename(FileName);
	return FName(FileName);
}

