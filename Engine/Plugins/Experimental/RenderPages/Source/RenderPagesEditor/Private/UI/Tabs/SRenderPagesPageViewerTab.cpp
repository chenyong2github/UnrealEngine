// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Tabs/SRenderPagesPageViewerTab.h"
#include "UI/SRenderPagesPageViewer.h"
#include "IRenderPageCollectionEditor.h"
#include "SlateOptMacros.h"

#define LOCTEXT_NAMESPACE "SRenderPagesPageViewerTab"


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPageViewerTab::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	ChildSlot
	[
		SNew(SRenderPagesPageViewer, InBlueprintEditor)
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION


#undef LOCTEXT_NAMESPACE
