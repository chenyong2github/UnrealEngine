// Copyright Epic Games, Inc. All Rights Reserved.

#include "MLDeformerTimelineTabSummoner.h"
#include "MLDeformerEditorToolkit.h"
#include "MLDeformerEditorStyle.h"
#include "MLDeformerEditorModel.h"
#include "MLDeformerModel.h"

#include "IDocumentation.h"
#include "SSimpleTimeSlider.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"

#define LOCTEXT_NAMESPACE "MLDeformerTimelineTabSummoner"

namespace UE::MLDeformer
{
	const FName FMLDeformerTimelineTabSummoner::TabID(TEXT("MLDeformerTimeline"));

	FMLDeformerTimelineTabSummoner::FMLDeformerTimelineTabSummoner(const TSharedRef<FMLDeformerEditorToolkit>& InEditor)
		: FWorkflowTabFactory(TabID, InEditor)
		, Editor(&InEditor.Get())
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
		TSharedRef<SSimpleTimeSlider> TimeSlider = SNew(SSimpleTimeSlider)
			.ScrubPosition_Raw(Editor, &FMLDeformerEditorToolkit::CalcTimelinePosition)
			.OnScrubPositionChanged_Raw(Editor, &FMLDeformerEditorToolkit::OnTimeSliderScrubPositionChanged);

		Editor->SetTimeSlider(TimeSlider);

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
				.Visibility_Lambda
				(
					[this]()
					{
						using namespace UE::MLDeformer;
						const FMLDeformerEditorModel* ActiveModel = Editor->GetActiveModel();
						if (ActiveModel)
						{
							return (Editor->GetActiveModel()->GetModel()->GetVizSettings()->GetVisualizationMode() == EMLDeformerVizMode::TestData) ? EVisibility::Visible : EVisibility::Collapsed; 
						}
						return EVisibility::Collapsed;
					} 
				)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "SimpleButton")
					.HAlign(EHorizontalAlignment::HAlign_Center)
					.VAlign(EVerticalAlignment::VAlign_Center)
					.ToolTipText(LOCTEXT("PlayButtonToolTip", "Play or pause the test animation sequence"))
					.ContentPadding(FMargin(0.0f))
					.OnClicked_Lambda([this]()
					{ 
							using namespace UE::MLDeformer;
							FMLDeformerEditorModel* EditorModel = Editor->GetActiveModel();
							if (EditorModel)
							{ 
								EditorModel->OnPlayButtonPressed(); 
							} 
							return FReply::Handled(); 
					})
					[
						SNew(SImage)
						.Image_Lambda
						(
							[this]()
							{
								using namespace UE::MLDeformer;
								const FMLDeformerEditorModel* EditorModel = Editor->GetActiveModel();
								return (EditorModel != nullptr && EditorModel->IsPlayingAnim())
									? FMLDeformerEditorStyle::Get().GetBrush("MLDeformer.Timeline.PauseIcon")
									: FMLDeformerEditorStyle::Get().GetBrush("MLDeformer.Timeline.PlayIcon");
							}
						)
					]
				]
			];

		return Content;
	}
}	// namespace UE::MLDeformer

#undef LOCTEXT_NAMESPACE 
