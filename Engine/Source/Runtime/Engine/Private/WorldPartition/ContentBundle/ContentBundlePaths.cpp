// Copyright Epic Games, Inc. All Rights Reserved.
#include "WorldPartition/ContentBundle/ContentBundlePaths.h"

#if WITH_EDITOR
#include "String/Find.h"
#include "Engine/Level.h"
#endif

#if WITH_EDITOR

FString ContentBundlePaths::MakeExternalActorPackagePath(const FString& ContentBundleExternalActorFolder, const FString& ActorName)
{
	const FString ContentBundleExternalActor = ULevel::GetActorPackageName(ContentBundleExternalActorFolder, EActorPackagingScheme::Reduced, *ActorName);
	check(IsAContentBundlePackagePath(ContentBundleExternalActor));
	return ContentBundleExternalActor;
}

bool ContentBundlePaths::IsAContentBundlePackagePath(FStringView InPackagePath)
{
	return GetContentBundleGuidFromExternalActorPackagePath(InPackagePath).IsValid();
}

FStringView ContentBundlePaths::GetRelativeExternalActorPackagePath(FStringView InContentBundleExternalActorPackagePath)
{
	FStringView RelativeContentBundlePath = GetActorPathRelativeToExternalActors(InContentBundleExternalActorPackagePath);
	if (!RelativeContentBundlePath.IsEmpty())
	{
		check(RelativeContentBundlePath.Left(GetContentBundleFolder().Len()).Equals(GetContentBundleFolder()));
		RelativeContentBundlePath = RelativeContentBundlePath.RightChop(GetContentBundleFolder().Len());
		if (!RelativeContentBundlePath.IsEmpty())
		{
			return RelativeContentBundlePath.RightChop(RelativeContentBundlePath.Find(TEXT("/")));
		}
	}
	
	return FStringView();
}

FGuid ContentBundlePaths::GetContentBundleGuidFromExternalActorPackagePath(FStringView InContentBundleExternalActorPackagePath)
{
	FGuid Result;

	FStringView RelativeContentBundlePath = GetActorPathRelativeToExternalActors(InContentBundleExternalActorPackagePath);
	if (!RelativeContentBundlePath.IsEmpty())
	{
		check(RelativeContentBundlePath.Left(GetContentBundleFolder().Len()).Equals(GetContentBundleFolder()));
		RelativeContentBundlePath = RelativeContentBundlePath.RightChop(GetContentBundleFolder().Len());
		if (!RelativeContentBundlePath.IsEmpty())
		{
			RelativeContentBundlePath= RelativeContentBundlePath.LeftChop(RelativeContentBundlePath.Len() - RelativeContentBundlePath.Find(TEXT("/")));
			verify(FGuid::Parse(FString(RelativeContentBundlePath), Result));
		}
	}

	return Result;
}

FStringView ContentBundlePaths::GetActorPathRelativeToExternalActors(FStringView InContentBundleExternalActorPackagePath)
{
	uint32 ExternalActorIdx = UE::String::FindFirst(InContentBundleExternalActorPackagePath, FPackagePath::GetExternalActorsFolderName(), ESearchCase::IgnoreCase);
	if (ExternalActorIdx != INDEX_NONE)
	{
		FStringView RelativeContentBundlePath = InContentBundleExternalActorPackagePath.RightChop(ExternalActorIdx + FCString::Strlen(FPackagePath::GetExternalActorsFolderName()));
		if (RelativeContentBundlePath.Left(GetContentBundleFolder().Len()).Equals(GetContentBundleFolder()))
		{
			return RelativeContentBundlePath;
		}
	}
	return FStringView();
}

#endif