// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserDataFilter.h"
#include "Containers/StringFwd.h"

namespace ContentBrowserDataUtils
{
	/** Returns number of folders in forward slashed path (Eg, 1 for "/Path", 2 for "/Path/Name") */
	CONTENTBROWSERDATA_API int32 CalculateFolderDepthOfPath(const FStringView InPath);

	/**
	 * Tests internal path against attribute filter
	 * 
	 * @param InPath Invariant path to test
	 * @param InAlreadyCheckedDepth Number of folders deep that have already been tested to avoid re-testing during recursion. Pass 0 if portion of path not already tested.
	 * @param InItemAttributeFilter Filter to test against
	 * 
	 * @return True if passes filter
	 */
	CONTENTBROWSERDATA_API bool PathPassesAttributeFilter(const FStringView InPath, const int32 InAlreadyCheckedDepth, const EContentBrowserItemAttributeFilter InAttributeFilter);
}
