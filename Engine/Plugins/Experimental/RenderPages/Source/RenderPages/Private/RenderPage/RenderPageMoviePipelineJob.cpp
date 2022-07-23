// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderPage/RenderPageMoviePipelineJob.h"

#include "IRenderPagesModule.h"
#include "RenderPage/RenderPageCollection.h"
#include "RenderPage/RenderPageManager.h"
#include "RenderPagesUtils.h"
#include "Utils/RenderPageQueue.h"

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


URenderPagesMoviePipelineRenderJobEntry* URenderPagesMoviePipelineRenderJobEntry::Create(URenderPagesMoviePipelineRenderJob* Job, URenderPage* Page, const UE::RenderPages::FRenderPagesMoviePipelineRenderJobCreateArgs& Args)
{
	if (!IsValid(Job) || !IsValid(Page) || !IsValid(Args.PageCollection) || (Args.Pages.Num() <= 0))
	{
		return nullptr;
	}

	const UClass* PipelineExecutor = (IsValid(*Args.PipelineExecutorClass) ? *Args.PipelineExecutorClass : UMoviePipelinePIEExecutor::StaticClass());
	if (!PipelineExecutor)
	{
		return nullptr;
	}

	URenderPagesMoviePipelineRenderJobEntry* RenderJobEntry = NewObject<URenderPagesMoviePipelineRenderJobEntry>(Job);
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


	ULevelSequence* PageSequence = Page->GetSequence();
	if (!IsValid(PageSequence) || !Page->GetSequenceStartFrame().IsSet() || !Page->GetSequenceEndFrame().IsSet() || (Page->GetSequenceStartFrame().Get(0) >= Page->GetSequenceEndFrame().Get(0)))
	{
		return RenderJobEntry;
	}

	UMoviePipelineExecutorJob* NewJob = UMoviePipelineEditorBlueprintLibrary::CreateJobFromSequence(RenderJobEntry->RenderQueue, PageSequence);
	RenderJobEntry->ExecutorJob = NewJob;

	UMoviePipelineMasterConfig* PageRenderPreset = Page->GetRenderPreset();
	if (IsValid(PageRenderPreset))
	{
		NewJob->SetConfiguration(PageRenderPreset);
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
		Setting->CustomStartFrame = Page->GetSequenceStartFrame().Get(0);
		Setting->CustomEndFrame = Page->GetSequenceEndFrame().Get(0);

		if (Args.bForceUseSequenceFrameRate)
		{
			Setting->bUseCustomFrameRate = false;
		}

		if (Page->GetIsCustomResolution())
		{
			Setting->OutputResolution = Page->GetCustomResolution();
		}

		const FString PageOutputRootDirectory = Page->GetOutputDirectory();
		const FString PageId = Page->GetPageId();
		if (!PageOutputRootDirectory.IsEmpty() && !PageId.IsEmpty())
		{
			const FString PageOutputDirectory = PageOutputRootDirectory / PageId;
			UE::RenderPages::Private::FRenderPagesUtils::DeleteDirectory(PageOutputDirectory);
			Setting->OutputDirectory.Path = PageOutputDirectory;
		}

		if (Args.bEnsureSequentialFilenames || !IsValid(PageRenderPreset))
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

void URenderPagesMoviePipelineRenderJobEntry::BeginDestroy()
{
	if (Promise.IsValid())
	{
		Promise->SetValue();
		Promise.Reset();
	}
	Super::BeginDestroy();
}

TSharedFuture<void> URenderPagesMoviePipelineRenderJobEntry::Execute()
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
		LevelSequenceEditorModule->OnComputePlaybackContext().AddUObject(this, &URenderPagesMoviePipelineRenderJobEntry::ComputePlaybackContext);
	}
	Status = TEXT("Rendering...");
	Executor->OnExecutorFinished().AddUObject(this, &URenderPagesMoviePipelineRenderJobEntry::ExecuteFinished);
	Executor->Execute(RenderQueue);
	return PromiseFuture;
}

void URenderPagesMoviePipelineRenderJobEntry::Cancel()
{
	bCanceled = true;
	if (Executor->IsRendering())
	{
		Executor->CancelAllJobs();
	}
}

FString URenderPagesMoviePipelineRenderJobEntry::GetStatus() const
{
	if (UMoviePipelineExecutorJob* Job = ExecutorJob.Get(); IsValid(Job))
	{
		if (FString JobStatus = Job->GetStatusMessage().TrimStartAndEnd(); !JobStatus.IsEmpty())
		{
			return JobStatus;
		}
	}
	return Status;
}

int32 URenderPagesMoviePipelineRenderJobEntry::GetEngineWarmUpCount() const
{
	if (UMoviePipelineExecutorJob* Job = ExecutorJob.Get(); IsValid(Job))
	{
		if (UMoviePipelineAntiAliasingSetting* AntiAliasingSettings = Cast<UMoviePipelineAntiAliasingSetting>(Job->GetConfiguration()->FindOrAddSettingByClass(UMoviePipelineAntiAliasingSetting::StaticClass())))
		{
			return FMath::Max<int32>(0, AntiAliasingSettings->EngineWarmUpCount);
		}
	}
	return 0;
}

void URenderPagesMoviePipelineRenderJobEntry::ComputePlaybackContext(bool& bOutAllowBinding)
{
	bOutAllowBinding = false;
}

void URenderPagesMoviePipelineRenderJobEntry::ExecuteFinished(UMoviePipelineExecutorBase* PipelineExecutor, const bool bSuccess)
{
	if (ILevelSequenceEditorModule* LevelSequenceEditorModule = FModuleManager::GetModulePtr<ILevelSequenceEditorModule>("LevelSequenceEditorModule"))
	{
		LevelSequenceEditorModule->OnComputePlaybackContext().RemoveAll(this);
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


URenderPagesMoviePipelineRenderJob* URenderPagesMoviePipelineRenderJob::Create(const UE::RenderPages::FRenderPagesMoviePipelineRenderJobCreateArgs& Args)
{
	if (!IsValid(Args.PageCollection) || (Args.Pages.Num() <= 0))
	{
		return nullptr;
	}

	const UClass* PipelineExecutor = (IsValid(*Args.PipelineExecutorClass) ? *Args.PipelineExecutorClass : UMoviePipelinePIEExecutor::StaticClass());
	if (!PipelineExecutor)
	{
		return nullptr;
	}

	URenderPagesMoviePipelineRenderJob* RenderJob = NewObject<URenderPagesMoviePipelineRenderJob>(GetTransientPackage());
	RenderJob->Queue = MakeShareable(new UE::RenderPages::Private::FRenderPageQueue);
	RenderJob->PageCollection = Args.PageCollection;
	RenderJob->bCanceled = false;
	RenderJob->bRanPreRender = false;


	RenderJob->Queue->Add(UE::RenderPages::Private::FRenderPageQueueAction::CreateLambda([RenderJob]()
	{
		RenderJob->PreviousFrameLimitSettings = UE::RenderPages::Private::FRenderPagesUtils::DisableFpsLimit();
	}));
	RenderJob->Queue->DelayFrames(1);

	for (const TObjectPtr<URenderPage> Page : Args.Pages)
	{
		if (URenderPagesMoviePipelineRenderJobEntry* Entry = URenderPagesMoviePipelineRenderJobEntry::Create(RenderJob, Page, Args); IsValid(Entry))
		{
			RenderJob->Entries.Add(Page, Entry);

			RenderJob->Queue->Add(UE::RenderPages::Private::FRenderPageQueueActionReturningDelay::CreateLambda([RenderJob, Page]() -> UE::RenderPages::Private::FRenderPageQueueDelay
			{
				if (!RenderJob->IsCanceled())
				{
					RenderJob->bRanPreRender = true;
					RenderJob->PageCollection->PreRender(Page);
					return UE::RenderPages::Private::FRenderPageQueueDelay::Frames(1);
				}
				return nullptr;
			}));

			RenderJob->Queue->Add(UE::RenderPages::Private::FRenderPageQueueActionReturningDelay::CreateLambda([RenderJob, Page]() -> UE::RenderPages::Private::FRenderPageQueueDelay
			{
				if (!RenderJob->IsCanceled())
				{
					RenderJob->PreviousPageProps = UE::RenderPages::IRenderPagesModule::Get().GetManager().ApplyPagePropValues(RenderJob->PageCollection, Page.Get());
					return UE::RenderPages::Private::FRenderPageQueueDelay::Frames(1 + Page->GetWaitFramesBeforeRendering());
				}
				return nullptr;
			}));

			RenderJob->Queue->Add(UE::RenderPages::Private::FRenderPageQueueActionReturningDelayFuture::CreateLambda([Entry]()-> TSharedFuture<void>
			{
				return Entry->Execute();
			}));

			RenderJob->Queue->Add(UE::RenderPages::Private::FRenderPageQueueActionReturningDelay::CreateLambda([RenderJob]() -> UE::RenderPages::Private::FRenderPageQueueDelay
			{
				if (!RenderJob->PreviousPageProps.IsEmpty())
				{
					UE::RenderPages::IRenderPagesModule::Get().GetManager().RestorePagePropValues(RenderJob->PreviousPageProps);
					RenderJob->PreviousPageProps = FRenderPageManagerPreviousPagePropValues();
					return UE::RenderPages::Private::FRenderPageQueueDelay::Frames(1);
				}
				return nullptr;
			}));

			RenderJob->Queue->Add(UE::RenderPages::Private::FRenderPageQueueActionReturningDelay::CreateLambda([RenderJob, Page]() -> UE::RenderPages::Private::FRenderPageQueueDelay
			{
				if (RenderJob->bRanPreRender)
				{
					RenderJob->bRanPreRender = false;
					RenderJob->PageCollection->PostRender(Page);
					return UE::RenderPages::Private::FRenderPageQueueDelay::Frames(1);
				}
				return nullptr;
			}));

			RenderJob->Queue->Add(UE::RenderPages::Private::FRenderPageQueueAction::CreateLambda([RenderJob, Entry]()
			{
				if (!RenderJob->IsCanceled() && Entry->IsCanceled())
				{
					RenderJob->Cancel();
				}
			}));
		}
	}

	RenderJob->Queue->Add(UE::RenderPages::Private::FRenderPageQueueAction::CreateLambda([RenderJob]()
	{
		UE::RenderPages::Private::FRenderPagesUtils::RestoreFpsLimit(RenderJob->PreviousFrameLimitSettings);
		RenderJob->PreviousFrameLimitSettings = FRenderPagePreviousEngineFpsSettings();
	}));


	if (RenderJob->Entries.Num() <= 0)
	{
		return nullptr;
	}
	return RenderJob;
}

void URenderPagesMoviePipelineRenderJob::Execute()
{
	if (Queue->IsRunning())
	{
		return;
	}

	{// start >>
		OnExecuteStartedDelegate.Broadcast(this);
		AddToRoot();
	}// start <<

	Queue->Add(UE::RenderPages::Private::FRenderPageQueueAction::CreateLambda([this]()
	{
		{// end >>
			RemoveFromRoot();
			OnExecuteFinishedDelegate.Broadcast(this, !bCanceled);
		}// end <<
	}));

	Queue->Start();
}

void URenderPagesMoviePipelineRenderJob::Cancel()
{
	if (bCanceled)
	{
		return;
	}
	bCanceled = true;

	TArray<TObjectPtr<URenderPagesMoviePipelineRenderJobEntry>> EntryValues;
	Entries.GenerateValueArray(EntryValues);
	for (int64 i = EntryValues.Num() - 1; i >= 0; i--)
	{
		if (IsValid(EntryValues[i]))
		{
			EntryValues[i]->Cancel();
		}
	}
}

FString URenderPagesMoviePipelineRenderJob::GetPageStatus(URenderPage* Page) const
{
	if (!IsValid(Page))
	{
		return TEXT("");
	}

	if (const TObjectPtr<URenderPagesMoviePipelineRenderJobEntry>* EntryPtr = Entries.Find(Page))
	{
		if (TObjectPtr<URenderPagesMoviePipelineRenderJobEntry> Entry = *EntryPtr; IsValid(Entry))
		{
			return Entry->GetStatus();
		}
	}
	return TEXT("");
}
