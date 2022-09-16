// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/ContentBundle/ContentBundlePaths.h"

#if WITH_EDITOR
#include "String/Find.h"
#endif

#if WITH_EDITOR

FStringView ContentBundlePaths::GetRelativeExternalActorPackagePath(FStringView InContentBundleExternalActorPackagePath)
{
	uint32 ContentBundleStartIdx = UE::String::FindFirst(InContentBundleExternalActorPackagePath, ContentBundleFolder, ESearchCase::IgnoreCase);
	if (ContentBundleStartIdx != INDEX_NONE)
	{
		uint32 ContentBundleUIDIdx = ContentBundleStartIdx + FCString::Strlen(ContentBundleFolder) + 1; // + 1 for trailing slash
		FStringView RelativeActorPackge = InContentBundleExternalActorPackagePath.RightChop(ContentBundleUIDIdx);
		
		uint32 ContentBundleEndIdx = RelativeActorPackge.Find(TEXT("/"), ESearchCase::IgnoreCase); // Search for the end of the content bundle guids
		check(ContentBundleEndIdx != INDEX_NONE);

		return RelativeActorPackge.RightChop(ContentBundleEndIdx);
	}

	return FStringView();
}

FGuid ContentBundlePaths::GetGuidFromPath(FStringView InPath)
{
	FGuid Result;

	static const TCHAR* ContentBundlerFolderPath = TEXT("/ContentBundle/");
	if (int FoundIdx = UE::String::FindFirst(InPath, ContentBundlerFolderPath, ESearchCase::IgnoreCase); FoundIdx != INDEX_NONE)
	{
		FString GuidPath(InPath);
		const uint32 StartUIDIdx = FoundIdx + FCString::Strlen(ContentBundlerFolderPath);
		const uint32 EndUIDIdx = GuidPath.Find(TEXT("/"), ESearchCase::IgnoreCase, ESearchDir::FromStart, StartUIDIdx);
		GuidPath.MidInline(StartUIDIdx, EndUIDIdx - StartUIDIdx);		
		verify(FGuid::Parse(GuidPath, Result));
	}

	return Result;
}

#endif