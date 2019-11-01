// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithDataprepFetcher.h"

#include "DatasmithContentBlueprintLibrary.h"

#define LOCTEXT_NAMESPACE "DatasmithDataprepFetcher"

/* UDataprepStringMetadataValueFetcher methods
 *****************************************************************************/
FString UDatasmithStringMetadataValueFetcher::Fetch_Implementation(const UObject* Object, bool& bOutFetchSucceded) const
{
	if ( Object ) 
	{
		bOutFetchSucceded = true;
		return UDatasmithContentBlueprintLibrary::GetDatasmithUserDataValueForKey( const_cast< UObject* >( Object ), Key );
	}

	bOutFetchSucceded = false;
	return {};
}

FText UDatasmithStringMetadataValueFetcher::GetNodeDisplayFetcherName_Implementation() const
{
	return LOCTEXT("NodeDisplayFetcher_MetadataValue", "Metadata");
}

bool UDatasmithStringMetadataValueFetcher::IsThreadSafe() const
{
	return true;
}

#undef LOCTEXT_NAMESPACE
