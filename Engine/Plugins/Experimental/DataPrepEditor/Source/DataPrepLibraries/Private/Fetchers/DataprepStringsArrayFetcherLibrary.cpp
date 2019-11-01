// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Fetchers/DataprepStringsArrayFetcherLibrary.h"

#include "GameFramework/Actor.h"
#include "UObject/Object.h"

#define LOCTEXT_NAMESPACE "DataprepStringsArrayFetcherLibrary"

/* UDataprepStringActorTagsFetcher methods
 *****************************************************************************/
TArray<FString> UDataprepStringActorTagsFetcher::Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const
{
	if (const AActor* Actor = Cast<const AActor>(Object))
	{
		bOutFetchSucceded = true;
		TArray<FString> Tags;
		Tags.Empty(Actor->Tags.Num());
		for (int TagIndex = 0; TagIndex < Actor->Tags.Num(); ++TagIndex)
		{
			Tags.Add(Actor->Tags[TagIndex].ToString());
		}
		return MoveTemp(Tags);
	}

	bOutFetchSucceded = false;
	return {};
}

bool UDataprepStringActorTagsFetcher::IsThreadSafe() const
{
	return true;
}

FText UDataprepStringActorTagsFetcher::GetNodeDisplayFetcherName_Implementation() const
{
	return LOCTEXT("ActorTagsFilterTitle", "Tag");
}

#undef LOCTEXT_NAMESPACE
