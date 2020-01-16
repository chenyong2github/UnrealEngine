// Copyright Epic Games, Inc. All Rights Reserved.
#include "MoviePipelinePIEExecutor.h"
#include "MoviePipelineMasterConfig.h"
#include "MoviePipelineShotConfig.h"
#include "MoviePipeline.h"
#include "MoviePipelineGameOverrideSetting.h"
#include "MovieRenderPipelineCoreModule.h"
#include "Framework/Application/SlateApplication.h"
#include "MovieRenderPipelineDataTypes.h"
#include "MovieRenderPipelineSettings.h"
#include "Editor/EditorEngine.h"
#include "Editor.h"
#include "Settings/LevelEditorPlaySettings.h"
#include "MoviePipelineQueue.h"

#define LOCTEXT_NAMESPACE "MoviePipelinePIEExecutor"

FText UMoviePipelinePIEExecutor::GetWindowTitle(const int32 InConfigIndex, const int32 InNumConfigs) const
{
	FNumberFormattingOptions PercentFormatOptions;
	PercentFormatOptions.MinimumIntegralDigits = 2;
	PercentFormatOptions.MaximumIntegralDigits = 3;

	FText TitleFormatString = LOCTEXT("MoviePreviewWindowTitleFormat", "Movie Pipeline Render (Preview) [{CurrentCount}/{TotalCount} Total] {PercentComplete}% Completed.");
	FText WindowTitle = FText::FormatNamed(TitleFormatString, TEXT("CurrentCount"), FText::AsNumber(InConfigIndex + 1), TEXT("TotalCount"), FText::AsNumber(InNumConfigs), TEXT("PercentComplete"), FText::AsNumber(12.f, &PercentFormatOptions));
	return WindowTitle;
}

void UMoviePipelinePIEExecutor::Start(const UMoviePipelineExecutorJob* InJob)
{
	Super::Start(InJob);

	// Create a Slate window to hold our preview.
	TSharedRef<SWindow> CustomWindow = SNew(SWindow)
		.Title_UObject(this, &UMoviePipelinePIEExecutor::GetWindowTitle, CurrentPipelineIndex, Queue->GetJobs().Num())
		.ClientSize(FVector2D(1280, 720))
		.AutoCenter(EAutoCenter::PrimaryWorkArea)
		.UseOSWindowBorder(true)
		.FocusWhenFirstShown(false)
		.ActivationPolicy(EWindowActivationPolicy::Never)
		.HasCloseButton(true)
		.SupportsMaximize(false)
		.SupportsMinimize(true)
		.SizingRule(ESizingRule::UserSized);

	FSlateApplication::Get().AddWindow(CustomWindow);

	// Initialize our own copy of the Editor Play settings which we will adjust defaults on.
	ULevelEditorPlaySettings* PlayInEditorSettings = NewObject<ULevelEditorPlaySettings>();
	PlayInEditorSettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
	PlayInEditorSettings->SetPlayNumberOfClients(1);
	PlayInEditorSettings->bLaunchSeparateServer = false;

	FRequestPlaySessionParams Params;
	Params.EditorPlaySettings = PlayInEditorSettings;
	Params.CustomPIEWindow = CustomWindow;

	UMoviePipelineGameOverrideSetting* GameOverrides = InJob->GetConfiguration()->FindSetting<UMoviePipelineGameOverrideSetting>();
	if (GameOverrides)
	{
		Params.GameModeOverride = GameOverrides->GameModeOverride;
	}

	// Kick off an async request to start a play session. This won't happen until the next frame.
	GEditor->RequestPlaySession(Params);

	// Listen for PIE startup since there's no current way to pass a delegate through the request.
	FEditorDelegates::PostPIEStarted.AddUObject(this, &UMoviePipelinePIEExecutor::OnPIEStartupFinished);
}

void UMoviePipelinePIEExecutor::OnPIEStartupFinished(bool)
{
	// Immediately un-bind our delegate so that we don't catch all PIE startup requests in the future.
	FEditorDelegates::PostPIEStarted.RemoveAll(this);


	// Hack to find out the PIE world since it is not provided by the delegate.
	UWorld* ExecutingWorld = nullptr;
	
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		if (Context.WorldType == EWorldType::PIE)
		{
			ExecutingWorld = Context.World();
		}
	}
	
	check(ExecutingWorld);

	// Allow the user to have overridden which Pipeline is actually run. This is an unlikely scenario but allows
	// the user to create their own implementation while still re-using the rest of our UI and infrastructure.
	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	TSubclassOf<UMoviePipeline> PipelineClass = ProjectSettings->DefaultPipeline;

	// This Pipeline belongs to the world being created so that they have context for things they execute.
	ActiveMoviePipeline = NewObject<UMoviePipeline>(ExecutingWorld, PipelineClass);
	
	ActiveMoviePipeline->Initialize(Queue->GetJobs()[CurrentPipelineIndex]);

	// Listen for when the pipeline thinks it has finished.
	ActiveMoviePipeline->OnMoviePipelineFinished().AddUObject(this, &UMoviePipelinePIEExecutor::OnPIEMoviePipelineFinished);
	ActiveMoviePipeline->OnMoviePipelineErrored().AddUObject(this, &UMoviePipelinePIEExecutor::OnPipelineErrored);

	// Listen for PIE shutdown in case the user hits escape to close it. 
	FEditorDelegates::EndPIE.AddUObject(this, &UMoviePipelinePIEExecutor::OnPIEEnded);
}

void UMoviePipelinePIEExecutor::OnPIEMoviePipelineFinished(UMoviePipeline* InMoviePipeline)
{
	// Unsubscribe to the EndPIE event so we don't think the user canceled it.
	FEditorDelegates::EndPIE.RemoveAll(this);

	if (ActiveMoviePipeline)
	{
		// Unsubscribe in the event that it gets called twice we don't have issues.
		ActiveMoviePipeline->OnMoviePipelineFinished().RemoveAll(this);
	}

	// The End Play will happen on the next frame.
	GEditor->RequestEndPlayMap();

	// Delay for one frame so that PIE can finish shut down. It's not a huge fan of us starting up on the same frame.
	FTimerHandle Handle;
	GEditor->GetTimerManager()->SetTimer(Handle, FTimerDelegate::CreateUObject(this, &UMoviePipelinePIEExecutor::DelayedFinishNotification), 1.f, false);
}

void UMoviePipelinePIEExecutor::OnPIEEnded(bool)
{
	// If the movie pipeline finishes naturally we unsubscribe from the EndPIE event.
	// This means that if this gets called, the user has hit escape and wants to cancel.
	if (ActiveMoviePipeline)
	{
		// This will flush any outstanding work on the movie pipeline (file writes) immediately
		ActiveMoviePipeline->Shutdown();

		// Broadcast that we finished.
		// ToDo: bAnyJobHadFatalError
		DelayedFinishNotification();
	}
}

void UMoviePipelinePIEExecutor::DelayedFinishNotification()
{
	UMoviePipeline* MoviePipeline = ActiveMoviePipeline;
	
	// Null these out now since OnIndividualPipelineFinished might invoke something that causes a GC
	// and we want them to go away with the GC.
	ActiveMoviePipeline = nullptr;
	
	// Now that another frame has passed and we should be OK to start another PIE session, notify our owner.
	OnIndividualPipelineFinished(MoviePipeline);
}

#undef LOCTEXT_NAMESPACE // "MoviePipelinePIEExecutor"
