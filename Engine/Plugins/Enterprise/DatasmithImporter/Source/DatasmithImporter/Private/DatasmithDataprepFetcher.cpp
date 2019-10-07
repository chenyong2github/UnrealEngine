// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "DatasmithDataprepFetcher.h"

#include "DatasmithContentBlueprintLibrary.h"

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

bool UDatasmithStringMetadataValueFetcher::IsThreadSafe() const
{
	return true;
}