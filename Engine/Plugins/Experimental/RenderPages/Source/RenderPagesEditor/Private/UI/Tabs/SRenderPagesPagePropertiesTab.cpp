// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Tabs/SRenderPagesPagePropertiesTab.h"
#include "UI/SRenderPagesPage.h"
#include "UI/SRenderPagesProps.h"
#include "IRenderPageCollectionEditor.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SRenderPagesPagePropertiesTab"


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPagePropertiesTab::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	ChildSlot
	[
		SNew(SScrollBox)
		.Style(FAppStyle::Get(), "ScrollBox")

		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f)
			[
				SNew(SRenderPagesPage, InBlueprintEditor)
			]

			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f)
			[
				SNew(SRenderPagesProps, InBlueprintEditor)
			]
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION


#undef LOCTEXT_NAMESPACE
