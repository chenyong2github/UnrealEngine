// Copyright Epic Games, Inc. All Rights Reserved.

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

bool FViewportTabContent::IsViewportConfigurationSet(const FName& ConfigurationName) const
{
	if (ActiveViewportLayout.IsValid())
	{
		return ActiveViewportLayout->GetLayoutTypeName() == ConfigurationName;
	}
	return false;
}

void FViewportTabContent::PerformActionOnViewports(TFunction<void(FName Name, TSharedPtr<IEditorViewportLayoutEntity>)> &TFuncPtr)
{
	const TMap< FName, TSharedPtr<IEditorViewportLayoutEntity> >& Entities = ActiveViewportLayout->GetViewports();
	for (auto& Entity : Entities)
	{
		TFuncPtr(Entity.Key, Entity.Value);
	}
}
