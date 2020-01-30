// Copyright Epic Games, Inc. All Rights Reserved.

// Movie Pipeline Includes
#include "Widgets/SMoviePipelineQueuePanel.h"
#include "Widgets/MoviePipelineWidgetConstants.h"
#include "SMoviePipelineQueueEditor.h"
#include "SMoviePipelineConfigPanel.h"
#include "MovieRenderPipelineSettings.h"
#include "MovieRenderPipelineStyle.h"
#include "Sections/MovieSceneCinematicShotSection.h"
#include "MoviePipelineQueue.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineQueueSubsystem.h"

// Slate Includes
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/SWindow.h"
#include "EditorStyleSet.h"
#include "Framework/Application/SlateApplication.h"

// Misc
#include "Editor.h"

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
	PipelineQueueEditorWidget = SNew(SMoviePipelineQueueEditor)
		.OnEditConfigRequested(this, &SMoviePipelineQueuePanel::OnEditJobConfigRequested);
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
					.ButtonStyle(FMovieRenderPipelineStyle::Get(), "FlatButton.Success")
					.IsEnabled(this, &SMoviePipelineQueuePanel::IsRenderLocalEnabled)
					.OnClicked(this, &SMoviePipelineQueuePanel::OnRenderLocalRequested)
					.Content()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.Text(LOCTEXT("RenderQueueLocal_Text", "Render (Local)"))
						.ToolTipText(LOCTEXT("RenderQueueLocal_Tooltip", "Renders the current queue in the current process using Play in Editor."))
						.Margin(FMargin(4, 0, 4, 0))
					]
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
					.ButtonStyle(FMovieRenderPipelineStyle::Get(), "FlatButton.Success")
					.IsEnabled(this, &SMoviePipelineQueuePanel::IsRenderRemoteEnabled)
					.OnClicked(this, &SMoviePipelineQueuePanel::OnRenderRemoteRequested)
					.Content()
					[
						SNew(STextBlock)
						.TextStyle(FEditorStyle::Get(), "NormalText.Important")
						.Text(LOCTEXT("RenderQueueRemote_Text", "Render (Remote)"))
						.ToolTipText(LOCTEXT("RenderQueueRemote_Tooltip", "Renders the current queue in a separate process."))
						.Margin(FMargin(4, 0, 4, 0))
					]
				]
			]
		]
	];
}

PRAGMA_ENABLE_OPTIMIZATION

FReply SMoviePipelineQueuePanel::OnRenderLocalRequested()
{
	UMoviePipelineQueueSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	check(Subsystem);

	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	check(ProjectSettings->DefaultLocalExecutor != nullptr);

	Subsystem->RenderQueueWithExecutor(ProjectSettings->DefaultLocalExecutor);
	return FReply::Handled();
}

bool SMoviePipelineQueuePanel::IsRenderLocalEnabled() const
{
	UMoviePipelineQueueSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	check(Subsystem);

	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	return (!Subsystem->IsRendering()) && (ProjectSettings->DefaultLocalExecutor != nullptr);
}

FReply SMoviePipelineQueuePanel::OnRenderRemoteRequested()
{
	UMoviePipelineQueueSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	check(Subsystem);

	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	check(ProjectSettings->DefaultRemoteExecutor != nullptr);

	Subsystem->RenderQueueWithExecutor(ProjectSettings->DefaultRemoteExecutor);
	return FReply::Handled();
}

bool SMoviePipelineQueuePanel::IsRenderRemoteEnabled() const
{
	UMoviePipelineQueueSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMoviePipelineQueueSubsystem>();
	check(Subsystem);

	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	return (!Subsystem->IsRendering()) && (ProjectSettings->DefaultRemoteExecutor != nullptr);
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

void SMoviePipelineQueuePanel::OnEditJobConfigRequested(TWeakObjectPtr<UMoviePipelineExecutorJob> InJob, TWeakObjectPtr<UMovieSceneCinematicShotSection> InShot)
{
	TSharedRef<SWindow> EditorWindow =
		SNew(SWindow)
		.ClientSize(FVector2D(700, 600));

	TSharedRef<SMoviePipelineConfigPanel> ConfigEditorPanel =
		SNew(SMoviePipelineConfigPanel, UMoviePipelineMasterConfig::StaticClass())
		.Job(InJob)
		.OnConfigurationModified(this, &SMoviePipelineQueuePanel::OnConfigUpdatedForJob)
		.OnConfigurationSetToPreset(this, &SMoviePipelineQueuePanel::OnConfigUpdatedForJobToPreset)
		.BasePreset(InJob->GetPresetOrigin())
		.BaseConfig(InJob->GetConfiguration());

	EditorWindow->SetContent(ConfigEditorPanel);


	TSharedPtr<SWindow> ParentWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
	if (ParentWindow.IsValid())
	{
		FSlateApplication::Get().AddWindowAsNativeChild(EditorWindow, ParentWindow.ToSharedRef());
	}

	WeakEditorWindow = EditorWindow;
}

void SMoviePipelineQueuePanel::OnConfigWindowClosed()
{
	UE_LOG(LogTemp, Warning, TEXT("Config updated!"));
	if (WeakEditorWindow.IsValid())
	{
		WeakEditorWindow.Pin()->RequestDestroyWindow();
	}
}

void SMoviePipelineQueuePanel::OnConfigUpdatedForJob(TWeakObjectPtr<UMoviePipelineExecutorJob> InJob, UMoviePipelineConfigBase* InConfig)
{
	if (InJob.IsValid())
	{
		UMoviePipelineMasterConfig* MasterConfig = Cast<UMoviePipelineMasterConfig>(InConfig);
		if (MasterConfig)
		{
			InJob->SetConfiguration(MasterConfig);
		}
	}

	OnConfigWindowClosed();
}

void SMoviePipelineQueuePanel::OnConfigUpdatedForJobToPreset(TWeakObjectPtr<UMoviePipelineExecutorJob> InJob, UMoviePipelineConfigBase* InConfig)
{
	if (InJob.IsValid())
	{
		UMoviePipelineMasterConfig* MasterConfig = Cast<UMoviePipelineMasterConfig>(InConfig);
		if (MasterConfig)
		{
			InJob->SetPresetOrigin(MasterConfig);
		}
	}

	OnConfigWindowClosed();
}

#undef LOCTEXT_NAMESPACE // SMoviePipelineQueuePanel