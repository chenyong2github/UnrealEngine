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
#include "MoviePipelinePIEExecutorSettings.h"
#include "MoviePipelineEditorBlueprintLibrary.h"
#include "Misc/MessageDialog.h"
#include "Modules/ModuleManager.h"
#include "MessageLogModule.h"
#include "Logging/MessageLog.h"

#define LOCTEXT_NAMESPACE "MoviePipelinePIEExecutor"


const TArray<FString> UMoviePipelinePIEExecutor::FValidationMessageGatherer::Whitelist = { "LogMovieRenderPipeline", "LogMoviePipelineExecutor" };

UMoviePipelinePIEExecutor::FValidationMessageGatherer::FValidationMessageGatherer()
	: FOutputDevice()
	, ExecutorLog()
{
	FMessageLogModule& MessageLogModule = FModuleManager::LoadModuleChecked<FMessageLogModule>("MessageLog");
	FMessageLogInitializationOptions MessageLogOptions;
	MessageLogOptions.bShowPages = true;
	MessageLogOptions.bAllowClear = true;
	MessageLogOptions.MaxPageCount = 10;
	MessageLogOptions.bShowFilters = true;
	MessageLogModule.RegisterLogListing("MoviePipelinePIEExecutor", LOCTEXT("MoviePipelineExecutorLogLabel", "High Quality Media Export"));

	ExecutorLog = MakeUnique<FMessageLog>("MoviePipelinePIEExecutor");
}

void UMoviePipelinePIEExecutor::FValidationMessageGatherer::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const class FName& Category)
{
	for (const FString& WhiteCategory : Whitelist)
	{
		if (Category.ToString().Equals(WhiteCategory))
		{
			if (Verbosity == ELogVerbosity::Warning)
			{
				ExecutorLog->Warning(FText::FromString(FString(V)));
			}
			else if (Verbosity == ELogVerbosity::Error)
			{
				ExecutorLog->Error(FText::FromString(FString(V)));
			}
			return;
		}
	}
}

void UMoviePipelinePIEExecutor::Start(const UMoviePipelineExecutorJob* InJob)
{
	Super::Start(InJob);

	// Start capturing logging messages
	ValidationMessageGatherer.StartGathering();

	// Check for unsaved maps. It's pretty rare that someone actually wants to execute on an unsaved map,
	// and it catches the much more common case of adding the job to an unsaved map and then trying to render
	// from a newly loaded map, PIE startup will fail because the map is no longer valid.
	const bool bAllMapsValid = UMoviePipelineEditorBlueprintLibrary::IsMapValidForRemoteRender(Queue->GetJobs());
	if (!bAllMapsValid)
	{
		FText FailureReason = LOCTEXT("UnsavedMapFailureDialog", "One or more jobs in the queue have an unsaved map as their target map. Maps must be saved at least once before rendering.");
		FMessageDialog::Open(EAppMsgType::Ok, FailureReason);

		OnExecutorFinishedImpl();
		return;
	}

	// Create a Slate window to hold our preview.
	TSharedRef<SWindow> CustomWindow = SNew(SWindow)
		.ClientSize(FVector2D(1280, 720))
		.AutoCenter(EAutoCenter::PrimaryWorkArea)
		.UseOSWindowBorder(true)
		.FocusWhenFirstShown(false)
		.ActivationPolicy(EWindowActivationPolicy::Never)
		.HasCloseButton(true)
		.SupportsMaximize(false)
		.SupportsMinimize(true)
		.SizingRule(ESizingRule::UserSized);

	WeakCustomWindow = CustomWindow;
	FSlateApplication::Get().AddWindow(CustomWindow);

	// Initialize our own copy of the Editor Play settings which we will adjust defaults on.
	ULevelEditorPlaySettings* PlayInEditorSettings = NewObject<ULevelEditorPlaySettings>();
	PlayInEditorSettings->SetPlayNetMode(EPlayNetMode::PIE_Standalone);
	PlayInEditorSettings->SetPlayNumberOfClients(1);
	PlayInEditorSettings->bLaunchSeparateServer = false;
	PlayInEditorSettings->SetRunUnderOneProcess(true);
	PlayInEditorSettings->LastExecutedPlayModeType = EPlayModeType::PlayMode_InEditorFloating;
	PlayInEditorSettings->bUseNonRealtimeAudioDevice = true;

	FRequestPlaySessionParams Params;
	Params.EditorPlaySettings = PlayInEditorSettings;
	Params.CustomPIEWindow = CustomWindow;
	Params.GlobalMapOverride = InJob->Map.GetAssetPathString();

	// Initialize the transient settings so that they will exist in time for the GameOverrides check.
	InJob->GetConfiguration()->InitializeTransientSettings();

	TArray<UMoviePipelineSetting*> AllSettings = InJob->GetConfiguration()->GetAllSettings();
	UMoviePipelineSetting** GameOverridesPtr = AllSettings.FindByPredicate([](UMoviePipelineSetting* InSetting) { return InSetting->GetClass() == UMoviePipelineGameOverrideSetting::StaticClass(); });
	if (GameOverridesPtr)
	{	
		UMoviePipelineSetting* Setting = *GameOverridesPtr;
		if (Setting)
		{
			Params.GameModeOverride = CastChecked<UMoviePipelineGameOverrideSetting>(Setting)->GameModeOverride;
		}
	}

	bPreviousUseFixedTimeStep = FApp::UseFixedTimeStep();
	PreviousFixedTimeStepDelta = FApp::GetFixedDeltaTime();

	// Force the engine into fixed timestep mode. It's going to get overridden on the first frame by the movie pipeline,
	// and everything controlled by Sequencer will use the correct timestep for renders but non-controlled things (such
	// as pawns) use an uncontrolled DT on the first frame which lowers determinism.
	ULevelSequence* LevelSequence = CastChecked<ULevelSequence>(InJob->Sequence.TryLoad());
	if (LevelSequence)
	{
		FApp::SetUseFixedTimeStep(true);
		FApp::SetFixedDeltaTime(InJob->GetConfiguration()->GetEffectiveFrameRate(LevelSequence).AsInterval());
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
	
	if(!ExecutingWorld)
	{
		// This only happens if PIE startup fails and they've usually gotten a pop-up dialog already.
		OnExecutorFinishedImpl();
		return;
	}

	// Only mark us as rendering once we've gotten the OnPIEStartupFinished call. If something were to interrupt PIE
	// startup (such as non-compiled blueprints) the queue would get stuck thinking it's rendering when it's not.
	bIsRendering = true;

	// Allow the user to have overridden which Pipeline is actually run. This is an unlikely scenario but allows
	// the user to create their own implementation while still re-using the rest of our UI and infrastructure.
	const UMovieRenderPipelineProjectSettings* ProjectSettings = GetDefault<UMovieRenderPipelineProjectSettings>();
	TSubclassOf<UMoviePipeline> PipelineClass = ProjectSettings->DefaultPipeline;

	// This Pipeline belongs to the world being created so that they have context for things they execute.
	ActiveMoviePipeline = NewObject<UMoviePipeline>(ExecutingWorld, PipelineClass);
	ActiveMoviePipeline->DebugWidgetClass = DebugWidgetClass;
	
	// We allow users to set a multi-frame delay before we actually run the Initialization function and start thinking.
	// This solves cases where there are engine systems that need to finish loading before we do anything.
	const UMoviePipelinePIEExecutorSettings* ExecutorSettings = GetDefault<UMoviePipelinePIEExecutorSettings>();

	// We tick each frame to update the Window Title, and kick off latent pipeling initialization.
	FCoreDelegates::OnBeginFrame.AddUObject(this, &UMoviePipelinePIEExecutor::OnTick);

	// Listen for when the pipeline thinks it has finished.
	ActiveMoviePipeline->OnMoviePipelineFinished().AddUObject(this, &UMoviePipelinePIEExecutor::OnPIEMoviePipelineFinished);
	
	if (ExecutorSettings->InitialDelayFrameCount == 0)
	{
		ActiveMoviePipeline->Initialize(Queue->GetJobs()[CurrentPipelineIndex]);
		RemainingInitializationFrames = -1;
	}
	else
	{
		RemainingInitializationFrames = ExecutorSettings->InitialDelayFrameCount;
	}
	
	// Listen for PIE shutdown in case the user hits escape to close it. 
	FEditorDelegates::EndPIE.AddUObject(this, &UMoviePipelinePIEExecutor::OnPIEEnded);
}

void UMoviePipelinePIEExecutor::OnTick()
{
	if (RemainingInitializationFrames >= 0)
	{
		if (RemainingInitializationFrames == 0)
		{
			ActiveMoviePipeline->Initialize(Queue->GetJobs()[CurrentPipelineIndex]);
		}

		RemainingInitializationFrames--;
	}

	FText WindowTitle = GetWindowTitle();
	TSharedPtr<SWindow> CustomWindow = WeakCustomWindow.Pin();
	if (CustomWindow)
	{
		CustomWindow->SetTitle(WindowTitle);
	}
}

void UMoviePipelinePIEExecutor::OnPIEMoviePipelineFinished(UMoviePipeline* InMoviePipeline, bool bFatalError)
{
	if (bFatalError)
	{
		OnPipelineErrored(InMoviePipeline, true, FText());
	}

	// Unsubscribe to the EndPIE event so we don't think the user canceled it.
	FCoreDelegates::OnBeginFrame.RemoveAll(this);

	if (ActiveMoviePipeline)
	{
		// Unsubscribe in the event that it gets called twice we don't have issues.
		ActiveMoviePipeline->OnMoviePipelineFinished().RemoveAll(this);
	}

	// The End Play will happen on the next frame.
	GEditor->RequestEndPlayMap();
}

void UMoviePipelinePIEExecutor::OnPIEEnded(bool)
{
	FEditorDelegates::EndPIE.RemoveAll(this);

	// Only call Shutdown if the pipeline hasn't been finished.
	if (ActiveMoviePipeline && ActiveMoviePipeline->GetPipelineState() != EMovieRenderPipelineState::Finished)
	{
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("PIE Ended while Movie Pipeline was still active. Stalling to do full shutdown."));

		// This will flush any outstanding work on the movie pipeline (file writes) immediately
		ActiveMoviePipeline->Shutdown(true);
		
		UE_LOG(LogMovieRenderPipeline, Log, TEXT("MoviePipelinePIEExecutor: Stalling finished, pipeline has shut down."));
	}
	// ToDo: bAnyJobHadFatalError

	// Delay for one frame so that PIE can finish shut down. It's not a huge fan of us starting up on the same frame.
	GEditor->GetTimerManager()->SetTimerForNextTick(FTimerDelegate::CreateUObject(this, &UMoviePipelinePIEExecutor::DelayedFinishNotification));

	// Restore the previous settings.
	FApp::SetUseFixedTimeStep(bPreviousUseFixedTimeStep);
	FApp::SetFixedDeltaTime(PreviousFixedTimeStepDelta);

	// Stop capturing logging messages
	ValidationMessageGatherer.StopGathering();
	ValidationMessageGatherer.OpenLog();
}

void UMoviePipelinePIEExecutor::DelayedFinishNotification()
{
	OnIndividualJobFinishedImpl(Queue->GetJobs()[CurrentPipelineIndex]);

	// Now that PIE has finished
	UMoviePipeline* MoviePipeline = ActiveMoviePipeline;
	
	// Null these out now since OnIndividualPipelineFinished might invoke something that causes a GC
	// and we want them to go away with the GC.
	ActiveMoviePipeline = nullptr;
	
	// Now that another frame has passed and we should be OK to start another PIE session, notify our owner.
	OnIndividualPipelineFinished(MoviePipeline);
}
#undef LOCTEXT_NAMESPACE // "MoviePipelinePIEExecutor"