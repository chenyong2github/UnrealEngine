// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Tabs/SRenderPagesCollectionPropertiesTab.h"
#include "UI/SRenderPagesCollection.h"
#include "IRenderPageCollectionEditor.h"
#include "SlateOptMacros.h"
#include "Widgets/Layout/SScrollBox.h"

#define LOCTEXT_NAMESPACE "SRenderPagesCollectionPropertiesTab"


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesCollectionPropertiesTab::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
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
				SNew(SRenderPagesCollection, InBlueprintEditor)
			]
		]
	];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION


#undef LOCTEXT_NAMESPACE
