// Copyright Epic Games, Inc. All Rights Reserved.

#include "TabFactory/PageListTabSummoner.h"
#include "UI/Tabs/SRenderPagesPageListTab.h"
#include "Styling/AppStyle.h"
#include "IRenderPageCollectionEditor.h"

#define LOCTEXT_NAMESPACE "RenderPages"


const FName UE::RenderPages::Private::FPageListTabSummoner::TabID(TEXT("PageList"));


UE::RenderPages::Private::FPageListTabSummoner::FPageListTabSummoner(TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
	: FWorkflowTabFactory(TabID, InBlueprintEditor)
	, BlueprintEditorWeakPtr(InBlueprintEditor)
{
	TabLabel = LOCTEXT("PageList_TabLabel", "Pages");
	TabIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "PlacementBrowser.Icons.All");
	bIsSingleton = true;
	ViewMenuDescription = LOCTEXT("PageList_ViewMenu_Desc", "Pages");
	ViewMenuTooltip = LOCTEXT("PageList_ViewMenu_ToolTip", "Show the page list.");
}

TSharedRef<SWidget> UE::RenderPages::Private::FPageListTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	return SNew(SRenderPagesPageListTab, BlueprintEditorWeakPtr.Pin())
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Page List")));
}


#undef LOCTEXT_NAMESPACE
