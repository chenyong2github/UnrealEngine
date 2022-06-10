// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Tabs/SRenderPagesPageListTab.h"
#include "UI/SRenderPagesPageList.h"
#include "IRenderPageCollectionEditor.h"
#include "SlateOptMacros.h"

#define LOCTEXT_NAMESPACE "SRenderPagesPageListTab"


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPageListTab::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	ChildSlot
	[
		SNew(SRenderPagesPageList, InBlueprintEditor)
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION


#undef LOCTEXT_NAMESPACE
