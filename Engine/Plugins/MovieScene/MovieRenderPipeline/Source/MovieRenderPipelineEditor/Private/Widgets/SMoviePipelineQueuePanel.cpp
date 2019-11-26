// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

// Movie Pipeline Includes
#include "Widgets/SMoviePipelineQueuePanel.h"
#include "Widgets/MoviePipelineWidgetConstants.h"
#include "SMoviePipelineQueueEditor.h"
#include "MovieRenderPipelineSettings.h"

// Slate Includes
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "EditorStyleSet.h"

// Misc

#define LOCTEXT_NAMESPACE "SMoviePipelineQueuePanel"

PRAGMA_DISABLE_OPTIMIZATION
void SMoviePipelineQueuePanel::Construct(const FArguments& InArgs)
{
	// Allocate a transient preset automatically so they can start editing without having to create an asset.
	// TransientPreset = AllocateTransientPreset();

	// Copy the base preset into the transient preset if it was provided.
	if (InArgs._BasePreset)
	{
		// TransientPreset->CopyFrom(InArgs._BasePreset);
	}

	// Create the child widgets that need to know about our pipeline
	PipelineQueueEditorWidget = SNew(SMoviePipelineQueueEditor);
	// .MoviePipeline(this, &SMoviePipelineConfigPanel::GetMoviePipeline);

	ChildSlot
	[
		SNew(SVerticalBox)

		// Create the toolbar for adding new items to the queue
		+ SVerticalBox::Slot()
		.Padding(FMargin(0.f, 1.0f))
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
			[
				SNew(SHorizontalBox)

				// Add a Level Sequence to the queue 
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					PipelineQueueEditorWidget->MakeAddSequenceJobButton()
				]

				// Remove a job (potentially already processed) from the the queue 
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.AutoWidth()
				[
					PipelineQueueEditorWidget->RemoveSelectedJobButton()
				]	
			]
		]
	
		// Main Queue Body
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			[
				PipelineQueueEditorWidget.ToSharedRef()
			]
		]

		// Footer Bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("DetailsView.CategoryTop"))
			.BorderBackgroundColor(FLinearColor(.6, .6, .6, 1.0f))
			.Padding(FMargin(0, 2, 0, 2))
			[
				SNew(SHorizontalBox)

				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Left)
				.FillWidth(1.f)
				[
					SNullWidget::NullWidget
				]

				// Render Local in Process
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SNew(SButton)
					.ContentPadding(MoviePipeline::ButtonPadding)
					.Text(LOCTEXT("RenderQueueLocal_Text", "Render (Local)"))
					.ToolTipText(LOCTEXT("RenderQueueLocal_Tooltip", "Renders the current queue in the current process using Play in Editor."))
					.ForegroundColor(FSlateColor::UseForeground())
					.IsEnabled(this, &SMoviePipelineQueuePanel::IsRenderLocalEnabled)
					.OnClicked(this, &SMoviePipelineQueuePanel::OnRenderLocalRequested)
				]

				// Render Remotely (Separate Process or Farm)
				+ SHorizontalBox::Slot()
				.Padding(MoviePipeline::ButtonOffset)
				.VAlign(VAlign_Fill)
				.HAlign(HAlign_Right)
				.AutoWidth()
				[
					SNew(SButton)
					.ContentPadding(MoviePipeline::ButtonPadding)
					.Text(LOCTEXT("RenderQueueRemote_Text", "Render (Remote)"))
					.ToolTipText(LOCTEXT("RenderQueueRemote_Tooltip", "Renders the current queue in a separate process."))
					.ForegroundColor(FSlateColor::UseForeground())
					.IsEnabled(this, &SMoviePipelineQueuePanel::IsRenderLocalEnabled)
					.OnClicked(this, &SMoviePipelineQueuePanel::OnRenderLocalRequested)
				]
			]
		]
	];
}

PRAGMA_ENABLE_OPTIMIZATION

FReply SMoviePipelineQueuePanel::OnRenderLocalRequested()
{
	UE_LOG(LogTemp, Log, TEXT("Render locally!"));
	return FReply::Handled();
}

bool SMoviePipelineQueuePanel::IsRenderLocalEnabled() const
{
	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	return ProjectSettings->DefaultLocalExecutor != nullptr;
}

FReply SMoviePipelineQueuePanel::OnRenderRemoteRequested()
{
	UE_LOG(LogTemp, Log, TEXT("Render remotely!"));
	return FReply::Handled();
}

bool SMoviePipelineQueuePanel::IsRenderRemoteEnabled() const
{
	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	return ProjectSettings->DefaultRemoteExecutor != nullptr;
}

/*UMoviePipelineConfigBase* SMoviePipelineConfigPanel::AllocateTransientPreset()
{
	static const TCHAR* PackageName = TEXT("/Temp/MoviePipeline/PendingPipeline");

	// Return a cached transient if it exists
	UMoviePipelineConfigBase* ExistingPreset = FindObject<UMoviePipelineConfigBase>(nullptr, TEXT("/Temp/MoviePipeline/PendingPipeline.PendingPipeline"));
	if (ExistingPreset)
	{
		return ExistingPreset;
	}

	static FName DesiredName = "PendingMoviePipeline";
	
	UPackage* NewPackage = CreatePackage(nullptr, PackageName);
	NewPackage->SetFlags(RF_Transient);
	NewPackage->AddToRoot();

	UMoviePipelineConfigBase* NewPreset = NewObject<UMoviePipelineConfigBase>(NewPackage, DesiredName, RF_Transient | RF_Transactional | RF_Standalone);

	return NewPreset;
}*/

void SMoviePipelineQueuePanel::AddReferencedObjects(FReferenceCollector& Collector)
{
	// Collector.AddReferencedObject(TransientPreset);
	// Collector.AddReferencedObject(SuppliedLevelSequence);
	// Collector.AddReferencedObject(RecordingLevelSequence);
}



#undef LOCTEXT_NAMESPACE // SMoviePipelineQueuePanel