// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DataprepFetcher.h"
#include "DataprepParameterizableObject.h"

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "DataprepFilter.generated.h"

/**
 * The Dataprep Filter a base class for the Dataprep selection system
 * It's main responsibility is to filter a array of object and to return the selected objects
 */
UCLASS(Abstract)
class DATAPREPCORE_API UDataprepFilter : public UDataprepParameterizableObject
{
	GENERATED_BODY()

public:
	/**
	 * Take a array of object and return the objects that pass the filter
	 * @param Objects The object to filter
	 * @return The object that passed the filtering
	 */
	virtual TArray<UObject*> FilterObjects(const TArray<UObject*>& Objects) const{ return {}; }

	/**
	 * Is this filter safe to use in a multi thread execution?
	 */
	virtual bool IsThreadSafe() const { return false; } 

	/**
	 * Return the selector category for this filter
	 * Imagine the category as the following: Select by|Your filter category| data fetched by the fetcher
	 * Here a full example: Select by|String with|Object Name
	 */
	virtual FText GetFilterCategoryText() const { return {}; }

	/** 
	 * Return the type of fetcher associated with this filter
	 */
	virtual TSubclassOf<UDataprepFetcher> GetAcceptedFetcherClass() const 
	{
		// must be override
		unimplemented();
		return {};
	}

	/**
	 * Set a new fetcher for this filter
	 * Note: This should only set a new fetcher if the fetcher is a subclass of the result of GetAcceptedFetcherClass and if it's not the same class as the current one
	 */
	virtual void SetFetcher(const TSubclassOf<UDataprepFetcher>& FetcherClass) { unimplemented(); /** must be override */ }

	virtual UDataprepFetcher* GetFetcher() const
	{
		// must be override
		unimplemented();
		return {};
	}

	/**
	 * Allow the filter to exclude only the element that would normally pass the filter
	 * @param bIsExcludingResult Should the filter be a excluding filter
	 */
	void SetIsExcludingResult(bool bInIsExcludingResult)
	{
		Modify();
		bIsExcludingResult = bInIsExcludingResult;
	}

	/**
	 * Is this filter a excluding filter.
	 */
	bool IsExcludingResult() const
	{
		return bIsExcludingResult;
	}

private:
	UPROPERTY()
	bool bIsExcludingResult = false;
};
