// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "DetailsViewObjectFilter.h"

/**
 * The default object set is just a pass through to the details panel.  
 */
class FDetailsViewDefaultObjectFilter : public FDetailsViewObjectFilter
{
public:
	FDetailsViewDefaultObjectFilter(bool bInAllowMultipleRoots);

	/**
	 * FDetailsViewObjectFilter interface
	 */
	virtual TArray<FDetailsViewObjectRoot> FilterObjects(const TArray<UObject*>& SourceObjects);

private:
	bool bAllowMultipleRoots;
};