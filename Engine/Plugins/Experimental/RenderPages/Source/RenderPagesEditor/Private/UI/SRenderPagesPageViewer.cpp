// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/SRenderPagesPageViewer.h"
#include "UI/SRenderPagesPageViewerLive.h"
#include "UI/SRenderPagesPageViewerPreview.h"
#include "UI/SRenderPagesPageViewerRendered.h"
#include "IRenderPageCollectionEditor.h"
#include "SlateOptMacros.h"
#include "Widgets/Input/SCheckBox.h"

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
	InBlueprintEditor->OnRenderPagesBatchRenderingStarted().AddSP(this, &SRenderPagesPageViewer::OnBatchRenderingStarted);
	InBlueprintEditor->OnRenderPagesBatchRenderingFinished().AddSP(this, &SRenderPagesPageViewer::OnBatchRenderingFinished);

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
	return SNew(SCheckBox)
		.Style(FAppStyle::Get(), "PlacementBrowser.Tab")
		.IsChecked_Lambda([this, ButtonViewerMode]() -> ECheckBoxState
		{
			return (ViewerMode == ButtonViewerMode) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		})
		.OnCheckStateChanged_Lambda([this, ButtonViewerMode](ECheckBoxState State)
		{
			if (State != ECheckBoxState::Checked)
			{
				return;
			}
			ViewerMode = ButtonViewerMode;
			Refresh();
		})
		[
			SNew(SBox)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(ButtonText)
			]
		];
}

END_SLATE_FUNCTION_BUILD_OPTIMIZATION

void UE::RenderPages::Private::SRenderPagesPageViewer::Refresh()
{
	if (!WidgetContainer.IsValid())
	{
		return;
	}
	if (const TSharedPtr<IRenderPageCollectionEditor> BlueprintEditor = BlueprintEditorWeakPtr.Pin())
	{
		ERenderPagesPageViewerMode CurrentViewerMode = (BlueprintEditor->IsBatchRendering() ? ERenderPagesPageViewerMode::None : ViewerMode);
		if (CurrentViewerMode == CachedViewerMode)
		{
			return;
		}

		CachedViewerMode = CurrentViewerMode;
		WidgetContainer->ClearContent();

		if (CurrentViewerMode == ERenderPagesPageViewerMode::Live)
		{
			WidgetContainer->SetContent(SNew(SRenderPagesPageViewerLive, BlueprintEditor));
		}
		else if (CurrentViewerMode == ERenderPagesPageViewerMode::Preview)
		{
			WidgetContainer->SetContent(SNew(SRenderPagesPageViewerPreview, BlueprintEditor));
		}
		else if (CurrentViewerMode == ERenderPagesPageViewerMode::Rendered)
		{
			WidgetContainer->SetContent(SNew(SRenderPagesPageViewerRendered, BlueprintEditor));
		}
	}
}


#undef LOCTEXT_NAMESPACE
