// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/ContentBundle/ContentBundlePaths.h"

#if WITH_EDITOR
#include "String/Find.h"
#endif

#if WITH_EDITOR

FStringView ContentBundlePaths::GetRelativeExternalActorPackagePath(FStringView InContentBundleExternalActorPackagePath)
{
	uint32 ContentBundleStartIdx = InContentBundleExternalActorPackagePath.Find(ContentBundlePaths::ContentBundleFolder, ESearchCase::IgnoreCase);
	if (ContentBundleStartIdx != INDEX_NONE)
	{
		uint32 ContentBundleUIDIdx = ContentBundleStartIdx + FCString::Strlen(ContentBundlePaths::ContentBundleFolder) + 1; // + 1 for trailing slash
		FStringView RelativeActorPackge = InContentBundleExternalActorPackagePath.RightChop(ContentBundleUIDIdx);
		
		uint32 ContentBundleEndIdx = RelativeActorPackge.Find(TEXT("/"), ESearchCase::IgnoreCase); // Search for the end of the content bundle guids
		check(ContentBundleEndIdx != INDEX_NONE);

		return RelativeActorPackge.RightChop(ContentBundleEndIdx);
	}

	return FStringView();
}

#endif