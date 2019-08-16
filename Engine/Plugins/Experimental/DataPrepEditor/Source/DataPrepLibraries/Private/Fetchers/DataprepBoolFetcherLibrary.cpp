// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "Fetchers/DataprepBoolFetcherLibrary.h"

#include "GameFramework/Actor.h"

/* UDataprepIsOfClassFetcher methods
 *****************************************************************************/
FText UDataprepIsClassOfFetcher::AdditionalKeyword = NSLOCTEXT("DataprepIsClassOfFetcher","AdditionalKeyword","Is Child Of");

bool UDataprepIsClassOfFetcher::Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const
{
	if ( Object )
	{
		bOutFetchSucceded = true;
		if ( bShouldIncludeChildClass )
		{
			return Object->GetClass()->IsChildOf( Class );
		}
		else
		{
			return Object->GetClass() == Class.Get();
		}
	}

	bOutFetchSucceded = false;
	return false;
}

bool UDataprepIsClassOfFetcher::IsThreadSafe() const
{
	return true;
}

FText UDataprepIsClassOfFetcher::GetAdditionalKeyword_Implementation() const
{
	return AdditionalKeyword;
}


/* UDataprepHasActorTagFetcher methods
 *****************************************************************************/
bool UDataprepHasActorTagFetcher::Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const
{
	if ( const AActor* Actor = Cast< const AActor >( Object ) )
	{
		bOutFetchSucceded = true;
		return Actor->Tags.Contains( Tag );
	}
	 bOutFetchSucceded = false;
	return false;
}

bool UDataprepHasActorTagFetcher::IsThreadSafe() const
{
	return true;
}
