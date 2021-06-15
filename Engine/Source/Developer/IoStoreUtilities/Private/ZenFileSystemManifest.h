// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ITargetPlatform;
class FArchive;

class FZenFileSystemManifest
{
public:

	struct FManifestEntry
	{
		FString ServerPath;
		FString ClientPath;
		uint32 FileId = ~uint32(0);
	};
	
	FZenFileSystemManifest(const ITargetPlatform& InTargetPlatform, FString InCookDirectory);
	
	void Generate();

	const FManifestEntry& AddFile(const FString& Filename);

	TArrayView<const FManifestEntry> ManifestEntries() const
	{
		return Entries;
	}

	int32 ManifestVersion() const
	{
		return Entries.Num();
	}

	void Save(const TCHAR* Filename);

private:
	const FManifestEntry& AddManifestEntry(FString ServerPath, FString ClientPath);

	const ITargetPlatform& TargetPlatform;
	FString CookDirectory;
	FString ServerRoot;
	TMap<FString, int32> ServerPathToEntry;
	TArray<FManifestEntry> Entries;
	
	static const FManifestEntry InvalidEntry;
};
