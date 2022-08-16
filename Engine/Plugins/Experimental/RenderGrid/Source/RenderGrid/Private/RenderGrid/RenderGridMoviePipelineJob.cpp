// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderGrid/RenderGridMoviePipelineJob.h"

#include "IRenderGridModule.h"
#include "RenderGrid/RenderGrid.h"
#include "RenderGrid/RenderGridManager.h"
#include "RenderGridUtils.h"
#include "Utils/RenderGridQueue.h"

#include "LevelSequence.h"
#include "LevelSequenceEditorModule.h"
#include "LevelSequenceEditorModule.h"
#include "Modules/ModuleManager.h"
#include "MoviePipelineAntiAliasingSetting.h"
#include "MoviePipelineDeferredPasses.h"
#include "MoviePipelineEditorBlueprintLibrary.h"
#include "MoviePipelineExecutor.h"
#include "MoviePipelineImageSequenceOutput.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelinePIEExecutor.h"
#include "MoviePipelineQueue.h"


URenderGridMoviePipelineRenderJobEntry* URenderGridMoviePipelineRenderJobEntry::Create(URenderGridMoviePipelineRenderJob* RenderJob, URenderGridJob* Job, const UE::RenderGrid::FRenderGridMoviePipelineRenderJobCreateArgs& Args)
{
	if (!IsValid(RenderJob) || !IsValid(Job) || !IsValid(Args.RenderGrid) || (Args.RenderGridJobs.Num() <= 0))
	{
		return nullptr;
	}

	const UClass* PipelineExecutor = (IsValid(*Args.PipelineExecutorClass) ? *Args.PipelineExecutorClass : UMoviePipelinePIEExecutor::StaticClass());
	if (!PipelineExecutor)
	{
		return nullptr;
	}

	URenderGridMoviePipelineRenderJobEntry* RenderJobEntry = NewObject<URenderGridMoviePipelineRenderJobEntry>(RenderJob);
	RenderJobEntry->RenderGridJob = Job;
	RenderJobEntry->RenderGrid = Args.RenderGrid;
	RenderJobEntry->RenderQueue = NewObject<UMoviePipelineQueue>(RenderJobEntry);
	RenderJobEntry->Executor = NewObject<UMoviePipelineExecutorBase>(RenderJobEntry, PipelineExecutor);
	RenderJobEntry->ExecutorJob = nullptr;
	RenderJobEntry->Status = TEXT("Skipped");
	RenderJobEntry->bCanExecute = false;
	RenderJobEntry->bCanceled = false;
	RenderJobEntry->Promise = MakeShared<TPromise<void>>();
	RenderJobEntry->Promise->SetValue();
	RenderJobEntry->PromiseFuture = RenderJobEntry->Promise->GetFuture().Share();
	RenderJobEntry->Promise.Reset();

	if (Args.bHeadless)
	{
		if (UMoviePipelinePIEExecutor* ActiveExecutorPIE = Cast<UMoviePipelinePIEExecutor>(RenderJobEntry->Executor))
		{
			ActiveExecutorPIE->SetIsRenderingOffscreen(true);
		}
	}


	ULevelSequence* JobSequence = Job->GetSequence();
	if (!IsValid(JobSequence) || !Job->GetSequenceStartFrame().IsSet() || !Job->GetSequenceEndFrame().IsSet() || (Job->GetSequenceStartFrame().Get(0) >= Job->GetSequenceEndFrame().Get(0)))
	{
		return RenderJobEntry;
	}

	UMoviePipelineExecutorJob* NewJob = UMoviePipelineEditorBlueprintLibrary::CreateJobFromSequence(RenderJobEntry->RenderQueue, JobSequence);
	RenderJobEntry->ExecutorJob = NewJob;

	UMoviePipelineMasterConfig* JobRenderPreset = Job->GetRenderPreset();
	if (IsValid(JobRenderPreset))
	{
		NewJob->SetConfiguration(JobRenderPreset);
	}
	else
	{
		UMoviePipelineEditorBlueprintLibrary::EnsureJobHasDefaultSettings(NewJob);
	}

	if (Args.DisableSettingsClasses.Num() > 0)
	{
		for (UMoviePipelineSetting* Setting : NewJob->GetConfiguration()->FindSettings<UMoviePipelineSetting>())
		{
			if (!IsValid(Setting))
			{
				continue;
			}
			for (TSubclassOf<UMoviePipelineSetting> DisableSettingsClass : Args.DisableSettingsClasses)
			{
				if (Setting->IsA(DisableSettingsClass))
				{
					Setting->SetIsEnabled(false);
					break;
				}
			}
		}
	}

	if (Args.bForceOutputImage || Args.bForceOnlySingleOutput)
	{
		bool bFound = false;
		const bool bContainsPreferredType = IsValid(NewJob->GetConfiguration()->FindSetting<UMoviePipelineImageSequenceOutput_PNG>());
		for (UMoviePipelineOutputBase* Setting : NewJob->GetConfiguration()->FindSettings<UMoviePipelineOutputBase>())
		{
			if (!IsValid(Setting))
			{
				continue;
			}
			if (Cast<UMoviePipelineImageSequenceOutput_PNG>(Setting) || Cast<UMoviePipelineImageSequenceOutput_JPG>(Setting) || Cast<UMoviePipelineImageSequenceOutput_BMP>(Setting))
			{
				if (!Args.bForceOnlySingleOutput || (!bFound && (!bContainsPreferredType || Cast<UMoviePipelineImageSequenceOutput_PNG>(Setting))))
				{
					bFound = true;
					continue;
				}
			}
			Setting->SetIsEnabled(false);
		}
		if (Args.bForceOutputImage && !bFound)
		{
			if (UMoviePipelineImageSequenceOutput_PNG* NewSetting = Cast<UMoviePipelineImageSequenceOutput_PNG>(NewJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineImageSequenceOutput_PNG::StaticClass())))
			{
				NewSetting->bWriteAlpha = false;
			}
		}
	}

	if (UMoviePipelineAntiAliasingSetting* ExistingAntiAliasingSettings = NewJob->GetConfiguration()->FindSetting<UMoviePipelineAntiAliasingSetting>())
	{
		// anti-aliasing settings already present (and enabled)
		if ((UE::MovieRenderPipeline::GetEffectiveAntiAliasingMethod(ExistingAntiAliasingSettings) == AAM_FXAA) && (ExistingAntiAliasingSettings->SpatialSampleCount <= 1) && (ExistingAntiAliasingSettings->TemporalSampleCount <= 1))
		{
			ExistingAntiAliasingSettings->TemporalSampleCount = 2;// FXAA transparency fix
		}
	}
	else if (UMoviePipelineAntiAliasingSetting* AntiAliasingSettings = Cast<UMoviePipelineAntiAliasingSetting>(NewJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass())))
	{
		// anti-aliasing settings not yet present (or enabled), created a new one
		AntiAliasingSettings->EngineWarmUpCount = 0;
		AntiAliasingSettings->RenderWarmUpCount = 0;
		AntiAliasingSettings->SpatialSampleCount = 1;
		AntiAliasingSettings->TemporalSampleCount = 2;
		AntiAliasingSettings->bOverrideAntiAliasing = true;
		AntiAliasingSettings->AntiAliasingMethod = AAM_FXAA;
	}

	bool bHasShot = false;
	for (const UMoviePipelineExecutorShot* Shot : NewJob->ShotInfo)
	{
		if (!Shot)
		{
			continue;
		}
		bHasShot = true;

		UMoviePipelineOutputSetting* Setting = Cast<UMoviePipelineOutputSetting>(UMoviePipelineBlueprintLibrary::FindOrGetDefaultSettingForShot(UMoviePipelineOutputSetting::StaticClass(), NewJob->GetConfiguration(), Shot));

		Setting->bUseCustomPlaybackRange = true;
		Setting->CustomStartFrame = Job->GetSequenceStartFrame().Get(0);
		Setting->CustomEndFrame = Job->GetSequenceEndFrame().Get(0);

		if (Args.bForceUseSequenceFrameRate)
		{
			Setting->bUseCustomFrameRate = false;
		}

		if (Job->GetIsUsingCustomResolution())
		{
			Setting->OutputResolution = Job->GetCustomResolution();
		}

		const FString JobOutputRootDirectory = Job->GetOutputDirectory();
		const FString JobId = Job->GetJobId();
		if (!JobOutputRootDirectory.IsEmpty() && !JobId.IsEmpty())
		{
			const FString JobOutputDirectory = JobOutputRootDirectory / JobId;
			UE::RenderGrid::Private::FRenderGridUtils::DeleteDirectory(JobOutputDirectory);
			Setting->OutputDirectory.Path = JobOutputDirectory;
		}

		if (Args.bEnsureSequentialFilenames || !IsValid(JobRenderPreset))
		{
			Setting->FileNameFormat = TEXT("{frame_number}");
			Setting->ZeroPadFrameNumbers = 10;
			Setting->FrameNumberOffset = 1000000000;
		}
	}
	if (!bHasShot)
	{
		return RenderJobEntry;
	}

	RenderJobEntry->Status = TEXT("");
	RenderJobEntry->bCanExecute = true;
	return RenderJobEntry;
}

void URenderGridMoviePipelineRenderJobEntry::BeginDestroy()
{
	if (Promise.IsValid())
	{
		Promise->SetValue();
		Promise.Reset();
	}
	Super::BeginDestroy();
}

TSharedFuture<void> URenderGridMoviePipelineRenderJobEntry::Execute()
{
	if (Executor->IsRendering())
	{
		return PromiseFuture;
	}

	Promise = MakeShared<TPromise<void>>();
	PromiseFuture = Promise->GetFuture().Share();

	if (!bCanExecute)
	{
		Status = TEXT("Skipped");
		Promise->SetValue();
		Promise.Reset();
		return PromiseFuture;
	}
	if (bCanceled)
	{
		Status = TEXT("Canceled");
		Promise->SetValue();
		Promise.Reset();
		return PromiseFuture;
	}

	AddToRoot();
	if (ILevelSequenceEditorModule* LevelSequenceEditorModule = FModuleManager::GetModulePtr<ILevelSequenceEditorModule>("LevelSequenceEditor"); LevelSequenceEditorModule)
	{
		LevelSequenceEditorModule->OnComputePlaybackContext().AddUObject(this, &URenderGridMoviePipelineRenderJobEntry::ComputePlaybackContext);
	}
	if (UMoviePipelinePIEExecutor* ActiveExecutorPIE = Cast<UMoviePipelinePIEExecutor>(Executor))
	{
		ActiveExecutorPIE->OnIndividualJobStarted().AddUObject(this, &URenderGridMoviePipelineRenderJobEntry::ExecuteJobStarted);
		ActiveExecutorPIE->OnIndividualJobWorkFinished().AddUObject(this, &URenderGridMoviePipelineRenderJobEntry::ExecuteJobFinished);
	}
	Status = TEXT("Rendering...");
	Executor->OnExecutorFinished().AddUObject(this, &URenderGridMoviePipelineRenderJobEntry::ExecuteFinished);
	Executor->Execute(RenderQueue);
	return PromiseFuture;
}

void URenderGridMoviePipelineRenderJobEntry::Cancel()
{
	bCanceled = true;
	if (Executor->IsRendering())
	{
		Executor->CancelAllJobs();
	}
}

FString URenderGridMoviePipelineRenderJobEntry::GetStatus() const
{
	if (UMoviePipelineExecutorJob* RenderExecutorJob = ExecutorJob.Get(); IsValid(RenderExecutorJob))
	{
		if (FString JobStatus = RenderExecutorJob->GetStatusMessage().TrimStartAndEnd(); !JobStatus.IsEmpty())
		{
			return JobStatus;
		}
	}
	return Status;
}

int32 URenderGridMoviePipelineRenderJobEntry::GetEngineWarmUpCount() const
{
	if (UMoviePipelineExecutorJob* RenderExecutorJob = ExecutorJob.Get(); IsValid(RenderExecutorJob))
	{
		if (UMoviePipelineAntiAliasingSetting* AntiAliasingSettings = Cast<UMoviePipelineAntiAliasingSetting>(RenderExecutorJob->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass())))
		{
			return FMath::Max<int32>(0, AntiAliasingSettings->EngineWarmUpCount);
		}
	}
	return 0;
}

void URenderGridMoviePipelineRenderJobEntry::ComputePlaybackContext(bool& bOutAllowBinding)
{
	bOutAllowBinding = false;
}

void URenderGridMoviePipelineRenderJobEntry::ExecuteJobStarted(UMoviePipelineExecutorJob* StartingExecutorJob)
{
	RenderGrid->PreRender(RenderGridJob);
}

void URenderGridMoviePipelineRenderJobEntry::ExecuteJobFinished(FMoviePipelineOutputData PipelineOutputData)
{
	RenderGrid->PostRender(RenderGridJob);
}

void URenderGridMoviePipelineRenderJobEntry::ExecuteFinished(UMoviePipelineExecutorBase* PipelineExecutor, const bool bSuccess)
{
	if (ILevelSequenceEditorModule* LevelSequenceEditorModule = FModuleManager::GetModulePtr<ILevelSequenceEditorModule>("LevelSequenceEditorModule"))
	{
		LevelSequenceEditorModule->OnComputePlaybackContext().RemoveAll(this);
	}
	if (UMoviePipelinePIEExecutor* PipelineExecutorPIE = Cast<UMoviePipelinePIEExecutor>(PipelineExecutor))
	{
		PipelineExecutorPIE->OnIndividualJobStarted().RemoveAll(this);
		PipelineExecutorPIE->OnIndividualJobWorkFinished().RemoveAll(this);
	}
	bCanceled = (bCanceled || !bSuccess);
	Status = (bCanceled ? TEXT("Canceled") : TEXT("Done"));
	if (Promise.IsValid())
	{
		Promise->SetValue();
		Promise.Reset();
	}
	RemoveFromRoot();
}


URenderGridMoviePipelineRenderJob* URenderGridMoviePipelineRenderJob::Create(const UE::RenderGrid::FRenderGridMoviePipelineRenderJobCreateArgs& Args)
{
	if (!IsValid(Args.RenderGrid) || (Args.RenderGridJobs.Num() <= 0))
	{
		return nullptr;
	}

	const UClass* PipelineExecutor = (IsValid(*Args.PipelineExecutorClass) ? *Args.PipelineExecutorClass : UMoviePipelinePIEExecutor::StaticClass());
	if (!PipelineExecutor)
	{
		return nullptr;
	}

	URenderGridMoviePipelineRenderJob* RenderJob = NewObject<URenderGridMoviePipelineRenderJob>(GetTransientPackage());
	RenderJob->Queue = MakeShareable(new UE::RenderGrid::Private::FRenderGridQueue);
	RenderJob->RenderGrid = Args.RenderGrid;
	RenderJob->bCanceled = false;


	RenderJob->Queue->Add(UE::RenderGrid::Private::FRenderGridQueueAction::CreateLambda([RenderJob]()
	{
		RenderJob->PreviousFrameLimitSettings = UE::RenderGrid::Private::FRenderGridUtils::DisableFpsLimit();
	}));
	RenderJob->Queue->DelayFrames(1);

	for (const TObjectPtr<URenderGridJob> Job : Args.RenderGridJobs)
	{
		if (URenderGridMoviePipelineRenderJobEntry* Entry = URenderGridMoviePipelineRenderJobEntry::Create(RenderJob, Job, Args); IsValid(Entry))
		{
			RenderJob->Entries.Add(Job, Entry);

			RenderJob->Queue->Add(UE::RenderGrid::Private::FRenderGridQueueActionReturningDelay::CreateLambda([RenderJob, Job]() -> UE::RenderGrid::Private::FRenderGridQueueDelay
			{
				if (!RenderJob->IsCanceled())
				{
					RenderJob->PreviousProps = UE::RenderGrid::IRenderGridModule::Get().GetManager().ApplyJobPropValues(RenderJob->RenderGrid, Job.Get());
					return UE::RenderGrid::Private::FRenderGridQueueDelay::Frames(2 + Job->GetWaitFramesBeforeRendering());
				}
				return nullptr;
			}));

			RenderJob->Queue->Add(UE::RenderGrid::Private::FRenderGridQueueActionReturningDelayFuture::CreateLambda([Entry]()-> TSharedFuture<void>
			{
				return Entry->Execute();
			}));

			RenderJob->Queue->Add(UE::RenderGrid::Private::FRenderGridQueueActionReturningDelay::CreateLambda([RenderJob]() -> UE::RenderGrid::Private::FRenderGridQueueDelay
			{
				if (!RenderJob->PreviousProps.IsEmpty())
				{
					UE::RenderGrid::IRenderGridModule::Get().GetManager().RestorePropValues(RenderJob->PreviousProps);
					RenderJob->PreviousProps = FRenderGridManagerPreviousPropValues();
					return UE::RenderGrid::Private::FRenderGridQueueDelay::Frames(2);
				}
				return nullptr;
			}));

			RenderJob->Queue->Add(UE::RenderGrid::Private::FRenderGridQueueAction::CreateLambda([RenderJob, Entry]()
			{
				if (!RenderJob->IsCanceled() && Entry->IsCanceled())
				{
					RenderJob->Cancel();
				}
			}));
		}
	}

	RenderJob->Queue->Add(UE::RenderGrid::Private::FRenderGridQueueAction::CreateLambda([RenderJob]()
	{
		UE::RenderGrid::Private::FRenderGridUtils::RestoreFpsLimit(RenderJob->PreviousFrameLimitSettings);
		RenderJob->PreviousFrameLimitSettings = FRenderGridPreviousEngineFpsSettings();
	}));


	if (RenderJob->Entries.Num() <= 0)
	{
		return nullptr;
	}
	return RenderJob;
}

void URenderGridMoviePipelineRenderJob::Execute()
{
	if (Queue->IsRunning())
	{
		return;
	}

	{// start >>
		OnExecuteStartedDelegate.Broadcast(this);
		AddToRoot();
	}// start <<

	Queue->Add(UE::RenderGrid::Private::FRenderGridQueueAction::CreateLambda([this]()
	{
		{// end >>
			RemoveFromRoot();
			OnExecuteFinishedDelegate.Broadcast(this, !bCanceled);
		}// end <<
	}));

	Queue->Start();
}

void URenderGridMoviePipelineRenderJob::Cancel()
{
	if (bCanceled)
	{
		return;
	}
	bCanceled = true;

	TArray<TObjectPtr<URenderGridMoviePipelineRenderJobEntry>> EntryValues;
	Entries.GenerateValueArray(EntryValues);
	for (int64 i = EntryValues.Num() - 1; i >= 0; i--)
	{
		if (IsValid(EntryValues[i]))
		{
			EntryValues[i]->Cancel();
		}
	}
}

FString URenderGridMoviePipelineRenderJob::GetRenderGridJobStatus(URenderGridJob* Job) const
{
	if (!IsValid(Job))
	{
		return TEXT("");
	}

	if (const TObjectPtr<URenderGridMoviePipelineRenderJobEntry>* EntryPtr = Entries.Find(Job))
	{
		if (TObjectPtr<URenderGridMoviePipelineRenderJobEntry> Entry = *EntryPtr; IsValid(Entry))
		{
			return Entry->GetStatus();
		}
	}
	return TEXT("");
}
