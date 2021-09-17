// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerTimelineTabSummoner.h"
#include "MLDeformerEditorToolkit.h"
#include "MLDeformerEditorData.h"
#include "MLDeformerEditorStyle.h"

#include "IDocumentation.h"
#include "SSimpleTimeSlider.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "MLDeformerTimelineTabSummoner"

const FName FMLDeformerTimelineTabSummoner::TabID(TEXT("MLDeformerTimeline"));

FMLDeformerTimelineTabSummoner::FMLDeformerTimelineTabSummoner(const TSharedRef<FMLDeformerEditorToolkit>& InEditor)
	: FWorkflowTabFactory(TabID, InEditor)
	, Editor(InEditor)
{
	bIsSingleton = true; // only allow a single instance of this tab

	TabLabel = LOCTEXT("TimelineTabLabel", "Timeline");
	TabIcon = FSlateIcon(FMLDeformerEditorStyle::Get().GetStyleSetName(), "MLDeformer.Timeline.TabIcon");
	ViewMenuDescription = LOCTEXT("ViewMenu_Desc", "Timeline");
	ViewMenuTooltip = LOCTEXT("ViewMenu_ToolTip", "Show the ML Deformer timeline.");
}

TSharedPtr<SToolTip> FMLDeformerTimelineTabSummoner::CreateTabToolTipWidget(const FWorkflowTabSpawnInfo& Info) const
{
	return IDocumentation::Get()->CreateToolTip(
		LOCTEXT("TimelineTooltip", "The timeline widget that controls the offset in the training or test anim sequence."), 
		NULL, 
		TEXT("Shared/Editors/Persona"), 
		TEXT("MLDeformerTimeline_Window"));
}

TSharedRef<SWidget> FMLDeformerTimelineTabSummoner::CreateTabBody(const FWorkflowTabSpawnInfo& Info) const
{
	// Create and setup the time slider widget.
	FMLDeformerEditorData* EditorData = Editor.Pin()->GetEditorData();
	TSharedRef<SSimpleTimeSlider> TimeSlider = SNew(SSimpleTimeSlider)
		.ScrubPosition_Raw(EditorData, &FMLDeformerEditorData::CalcTimelinePosition)
		.OnScrubPositionChanged_Raw(EditorData, &FMLDeformerEditorData::OnTimeSliderScrubPositionChanged);
	EditorData->SetTimeSlider(TimeSlider);

	// Add the time slider.
	TSharedRef<SHorizontalBox> Content = SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.VAlign(EVerticalAlignment::VAlign_Top)
		[
			TimeSlider
		]
		+SHorizontalBox::Slot()
		.AutoWidth()
		.HAlign(EHorizontalAlignment::HAlign_Right)
		.VAlign(EVerticalAlignment::VAlign_Top)
		[
			SNew(SBox)
			.WidthOverride(25)
			.HeightOverride(25)
			.Padding(FMargin(0.0f))
			.Visibility_Lambda([EditorData](){ return (EditorData->GetDeformerAsset()->GetVizSettings()->GetVisualizationMode() == EMLDeformerVizMode::TestData) ? EVisibility::Visible : EVisibility::Collapsed; } )
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.HAlign(EHorizontalAlignment::HAlign_Center)
				.VAlign(EVerticalAlignment::VAlign_Center)
				.ToolTipText(LOCTEXT("PlayButtonToolTip", "Play or pause the test animation sequence"))
				.ContentPadding(FMargin(0.0f))
				.OnClicked_Lambda([EditorData]() { EditorData->OnPlayButtonPressed(); return FReply::Handled(); })
				[
					SNew(SImage)
					.Image_Lambda
					(
						[EditorData]()
						{
							return EditorData->IsPlayingAnim() 
								? FMLDeformerEditorStyle::Get().GetBrush("MLDeformer.Timeline.PauseIcon")
								: FMLDeformerEditorStyle::Get().GetBrush("MLDeformer.Timeline.PlayIcon");
						}
					)
				]
			]
		];

	return Content;
}

#undef LOCTEXT_NAMESPACE 
