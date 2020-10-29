// Copyright Epic Games, Inc. All Rights Reserved.

#include "ContentBrowserPluginFilters.h"
#include "Interfaces/IPluginManager.h"


/////////////////////////////////////////
// FContentBrowserPluginFilter_ContentOnlyPlugins
/////////////////////////////////////////
bool FContentBrowserPluginFilter_ContentOnlyPlugins::PassesFilter(FPluginFilterType InItem) const
{
	return InItem->GetDescriptor().Modules.Num() == 0;
}
