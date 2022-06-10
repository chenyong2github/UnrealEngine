// Copyright Epic Games, Inc. All Rights Reserved.

#include "TabFactory/CollectionPropertiesTabSummoner.h"
#include "UI/Tabs/SRenderPagesCollectionPropertiesTab.h"
#include "Styling/AppStyle.h"
#include "IRenderPageCollectionEditor.h"

#define LOCTEXT_NAMESPACE "RenderPages"


const FName UE::RenderPages::Private::FCollectionPropertiesTabSummoner::TabID(TEXT("CollectionProperties"));


UE::RenderPages::Private::FCollectionPropertiesTabSummoner::FCollectionPropertiesTabSummoner(TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
	: FWorkflowTabFactory(TabID, InBlueprintEditor)
	, BlueprintEditorWeakPtr(InBlueprintEditor)
{
	TabLabel = LOCTEXT("CollectionProperties_TabLabel", "Collection");
	TabIcon = FSlateIcon(FAppStyle::GetAppStyleSetName(), "ContentBrowser.ShowSourcesView");
	bIsSingleton = true;
	ViewMenuDescription = LOCTEXT("CollectionProperties_ViewMenu_Desc", "Collection");
	ViewMenuTooltip = LOCTEXT("CollectionProperties_ViewMenu_ToolTip", "Show the page collection properties.");
}

TSharedRef<SWidget> UE::RenderPages::Private::FCollectionPropertiesTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	return SNew(SRenderPagesCollectionPropertiesTab, BlueprintEditorWeakPtr.Pin())
		.AddMetaData<FTagMetaData>(FTagMetaData(TEXT("Collection")));
}


#undef LOCTEXT_NAMESPACE
