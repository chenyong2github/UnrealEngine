// Copyright Epic Games, Inc. All Rights Reserved.

#include "TabFactory/PageViewerTabSummoner.h"
#include "UI/Tabs/SRenderPagesPageViewerTab.h"
#include "Styling/AppStyle.h"
#include "IRenderPageCollectionEditor.h"

#define LOCTEXT_NAMESPACE "RenderPages"


const FName UE::RenderPages::Private::FPageViewerTabSummoner::TabID(TEXT("PageViewer"));


UE::RenderPages::Private::FPageViewerTabSummoner::FPageViewerTabSummoner(TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
	: FWorkflowTabFactory(TabID, InBlueprintEditor)
	, BlueprintEditorWeakPtr(InBlueprintEditor)
{
	TabLabel = LOCTEXT("PageViewer_TabLabel", "Viewer");
	TabIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details");
	bIsSingleton = true;
	ViewMenuDescription = LOCTEXT("PageViewer_ViewMenu_Desc", "Viewer");
	ViewMenuTooltip = LOCTEXT("PageViewer_ViewMenu_ToolTip", "Show the page viewer.");
}

TSharedRef<SWidget> UE::RenderPages::Private::FPageViewerTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	return SNew(SRenderPagesPageViewerTab, BlueprintEditorWeakPtr.Pin())
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Viewer")));
}


#undef LOCTEXT_NAMESPACE
