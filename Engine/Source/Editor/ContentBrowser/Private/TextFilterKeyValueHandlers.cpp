// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextFilterKeyValueHandlers.h"

#include "TextFilterKeyValueHandler.h"

bool UTextFilterKeyValueHandlers::HandleTextFilterKeyValue(const FContentBrowserItem& InContentBrowserItem, const FName& InKey, const FTextFilterString& InValue, const ETextFilterComparisonOperation InComparisonOperation, const ETextFilterTextComparisonMode InTextComparisonMode)
{
	for (const FTextFilterKeyValueHandlerEntry& Handler : GetDefault<UTextFilterKeyValueHandlers>()->TextFilterKeyValueHandlers)
	{
		if (InKey.IsEqual(Handler.Key))
		{
			if (UClass* TextFilterKeyValueHandlerClass = Handler.HandlerClass.LoadSynchronous())
			{
				return GetDefault<UTextFilterKeyValueHandler>(TextFilterKeyValueHandlerClass)->HandleTextFilterKeyValue(InContentBrowserItem, InKey, InValue, InComparisonOperation, InTextComparisonMode);
			}
		}
	}

	return false;
}