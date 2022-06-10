// Copyright Epic Games, Inc. All Rights Reserved.

#include "TabFactory/PagePropertiesTabSummoner.h"
#include "UI/Tabs/SRenderPagesPagePropertiesTab.h"
#include "Styling/AppStyle.h"
#include "IRenderPageCollectionEditor.h"

#define LOCTEXT_NAMESPACE "RenderPages"


const FName UE::RenderPages::Private::FPagePropertiesTabSummoner::TabID(TEXT("PageProperties"));


UE::RenderPages::Private::FPagePropertiesTabSummoner::FPagePropertiesTabSummoner(TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
	: FWorkflowTabFactory(TabID, InBlueprintEditor)
	, BlueprintEditorWeakPtr(InBlueprintEditor)
{
	TabLabel = LOCTEXT("PageProperties_TabLabel", "Page");
	TabIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "EditorPreferences.TabIcon");
	bIsSingleton = true;
	ViewMenuDescription = LOCTEXT("PageProperties_ViewMenu_Desc", "Page");
	ViewMenuTooltip = LOCTEXT("PageProperties_ViewMenu_ToolTip", "Show the page properties.");
}

TSharedRef<SWidget> UE::RenderPages::Private::FPagePropertiesTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	return SNew(SRenderPagesPagePropertiesTab, BlueprintEditorWeakPtr.Pin())
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Page")));
}


#undef LOCTEXT_NAMESPACE
