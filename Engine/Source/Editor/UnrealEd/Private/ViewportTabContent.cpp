// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "ViewportTabContent.h"
#include "Misc/ConfigCacheIni.h"
#include "Framework/Docking/LayoutService.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"


// FViewportTabContent ///////////////////////////

bool FViewportTabContent::BelongsToTab(TSharedRef<class SDockTab> InParentTab) const
{
	TSharedPtr<SDockTab> ParentTabPinned = ParentTab.Pin();
	return ParentTabPinned == InParentTab;
}
