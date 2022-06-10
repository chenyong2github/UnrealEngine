// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRenderPagesPageViewer.h"
#include "UI/SRenderPagesPageViewerLive.h"
#include "UI/SRenderPagesPageViewerPreview.h"
#include "UI/SRenderPagesPageViewerRendered.h"
#include "IRenderPageCollectionEditor.h"
#include "SlateOptMacros.h"
#include "Styles/RenderPagesEditorStyle.h"
#include "Widgets/Input/SButton.h"

#define LOCTEXT_NAMESPACE "SRenderPagesPageViewer"


BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPageViewer::Construct(const FArguments& InArgs, TSharedPtr<IRenderPageCollectionEditor> InBlueprintEditor)
{
	BlueprintEditorWeakPtr = InBlueprintEditor;
	ViewerMode = ERenderPagesPageViewerMode::Live;
	CachedViewerMode = ERenderPagesPageViewerMode::None;

	SAssignNew(WidgetContainer, SBorder)
		.Padding(0.0f)
		.BorderImage(new FSlateNoResource());

	Refresh();

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.HeightOverride(27.5f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					CreateViewerModeButton(FText::FromString("Live"), ERenderPagesPageViewerMode::Live)
				]
				+ SHorizontalBox::Slot()
				[
					CreateViewerModeButton(FText::FromString("Preview"), ERenderPagesPageViewerMode::Preview)
				]
				+ SHorizontalBox::Slot()
				[
					CreateViewerModeButton(FText::FromString("Rendered"), ERenderPagesPageViewerMode::Rendered)
				]
				+ SHorizontalBox::Slot()
				[
					CreateViewerModeButton(FText::FromString("None"), ERenderPagesPageViewerMode::None)
				]
			]
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			WidgetContainer.ToSharedRef()
		]
	];
}

TSharedRef<SWidget> UE::RenderPages::Private::SRenderPagesPageViewer::CreateViewerModeButton(const FText& ButtonText, const ERenderPagesPageViewerMode ButtonViewerMode)
{
	return SNew(SButton)
		.ButtonStyle(FRenderPagesEditorStyle::Get(), "TabButton")
		.Text(ButtonText)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		.OnClicked_Lambda([this, ButtonViewerMode]() -> FReply
		{
			ViewerMode = ButtonViewerMode;
			Refresh();
			return FReply::Handled();
		})
		.IsEnabled_Lambda([this, ButtonViewerMode]() -> bool
		{
			return (ViewerMode != ButtonViewerMode);
		});
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPageViewer::Refresh()
{
	if (!WidgetContainer.IsValid() || (ViewerMode == CachedViewerMode))
	{
		return;
	}

	CachedViewerMode = ViewerMode;
	WidgetContainer->ClearContent();

	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		if (ViewerMode == ERenderPagesPageViewerMode::Live)
		{
			WidgetContainer->SetContent(SNew(SRenderPagesPageViewerLive, BlueprintEditor));
		}
		else if (ViewerMode == ERenderPagesPageViewerMode::Preview)
		{
			WidgetContainer->SetContent(SNew(SRenderPagesPageViewerPreview, BlueprintEditor));
		}
		else if (ViewerMode == ERenderPagesPageViewerMode::Rendered)
		{
			WidgetContainer->SetContent(SNew(SRenderPagesPageViewerRendered, BlueprintEditor));
		}
	}
}


#undef LOCTEXT_NAMESPACE
