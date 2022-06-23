// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderPage/RenderPageMoviePipelineJob.h"

#include "IRenderPagesModule.h"
#include "RenderPage/RenderPageCollection.h"
#include "RenderPage/RenderPageManager.h"
#include "RenderPagesUtils.h"

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
	RenderJob->PageCollection = Args.PageCollection;
	RenderJob->RenderQueue = NewObject<UMoviePipelineQueue>(RenderJob);
	RenderJob->ActiveExecutor = NewObject<UMoviePipelineExecutorBase>(RenderJob, PipelineExecutor);

	if (Args.bHeadless)
	{
		if (UMoviePipelinePIEExecutor* ActiveExecutorPIE = Cast<UMoviePipelinePIEExecutor>(RenderJob->ActiveExecutor))
		{
			ActiveExecutorPIE->SetIsRenderingOffscreen(true);
		}
	}


	for (const URenderPage* Page : Args.Pages)
	{
		if (!IsValid(Page))
		{
			continue;
		}

		ULevelSequence* PageSequence = Page->GetSequence();
		if (!IsValid(PageSequence) || !Page->GetSequenceStartFrame().IsSet() || !Page->GetSequenceEndFrame().IsSet() || (Page->GetSequenceStartFrame().Get(0) >= Page->GetSequenceEndFrame().Get(0)))
		{
			RenderJob->PageStatuses.Add(Page, TEXT("Skipped"));
			continue;
		}

		UMoviePipelineExecutorJob* NewJob = UMoviePipelineEditorBlueprintLibrary::CreateJobFromSequence(RenderJob->RenderQueue, PageSequence);
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
			RenderJob->PageStatuses.Add(Page, TEXT("Skipped"));
		}

		RenderJob->PageExecutorJobs.Add(Page, NewJob);
	}

	if (RenderJob->PageExecutorJobs.Num() <= 0)
	{
		return nullptr;
	}
	return RenderJob;
}


void URenderPagesMoviePipelineRenderJob::Execute()
{
	if (ActiveExecutor->IsRendering())
	{
		return;
	}
	OnExecuteStartedDelegate.Broadcast(this);
	AddToRoot();
	if (ILevelSequenceEditorModule* LevelSequenceEditorModule = FModuleManager::GetModulePtr<ILevelSequenceEditorModule>("LevelSequenceEditor"); LevelSequenceEditorModule)
	{
		LevelSequenceEditorModule->OnComputePlaybackContext().AddUObject(this, &URenderPagesMoviePipelineRenderJob::ComputePlaybackContext);
	}
	if (UMoviePipelinePIEExecutor* ActiveExecutorPIE = Cast<UMoviePipelinePIEExecutor>(ActiveExecutor))
	{
		ActiveExecutorPIE->OnIndividualJobStarted().AddUObject(this, &URenderPagesMoviePipelineRenderJob::ExecutePageStarted);
		ActiveExecutorPIE->OnIndividualJobWorkFinished().AddUObject(this, &URenderPagesMoviePipelineRenderJob::ExecutePageFinished);
	}
	ActiveExecutor->OnExecutorFinished().AddUObject(this, &URenderPagesMoviePipelineRenderJob::ExecuteFinished);
	ActiveExecutor->Execute(RenderQueue);
}

void URenderPagesMoviePipelineRenderJob::Cancel()
{
	if (ActiveExecutor->IsRendering())
	{
		ActiveExecutor->CancelAllJobs();
	}
}

FString URenderPagesMoviePipelineRenderJob::GetPageStatus(URenderPage* Page) const
{
	if (!IsValid(Page))
	{
		return TEXT("");
	}

	const FString* StatusPtr = PageStatuses.Find(Page);
	if (StatusPtr)
	{
		// got the result from the Page Statuses map
		return *StatusPtr;
	}

	const TObjectPtr<UMoviePipelineExecutorJob>* JobPtr = PageExecutorJobs.Find(Page);
	if (!JobPtr)
	{
		return TEXT("");
	}
	TObjectPtr<UMoviePipelineExecutorJob> Job = *JobPtr;
	if (!Job)
	{
		return TEXT("");
	}

	if (Job->GetStatusProgress() > 0)
	{
		return TEXT("Rendering");
	}
	return Job->GetStatusMessage();
}

void URenderPagesMoviePipelineRenderJob::ComputePlaybackContext(bool& bOutAllowBinding)
{
	bOutAllowBinding = false;
}

void URenderPagesMoviePipelineRenderJob::ExecutePageStarted(UMoviePipelineExecutorJob* JobToStart)
{
	const TObjectPtr<const URenderPage>* PagePtr = PageExecutorJobs.FindKey(JobToStart);
	if (!PagePtr)
	{
		return;
	}
	TObjectPtr<const URenderPage> Page = *PagePtr;
	PageStatuses.Add(Page, TEXT("Rendering..."));
	PreviousPageProps = UE::RenderPages::IRenderPagesModule::Get().GetManager().ApplyPagePropValues(PageCollection, Page.Get());
}

void URenderPagesMoviePipelineRenderJob::ExecutePageFinished(FMoviePipelineOutputData PipelineOutputData)
{
	const TObjectPtr<const URenderPage>* PagePtr = PageExecutorJobs.FindKey(PipelineOutputData.Job);
	if (!PagePtr)
	{
		return;
	}
	TObjectPtr<const URenderPage> Page = *PagePtr;
	PageStatuses.Add(Page, TEXT("Done"));
	UE::RenderPages::IRenderPagesModule::Get().GetManager().RestorePagePropValues(PreviousPageProps);
	PreviousPageProps = FRenderPageManagerPreviousPagePropValues();
}

void URenderPagesMoviePipelineRenderJob::ExecuteFinished(UMoviePipelineExecutorBase* PipelineExecutor, const bool bSuccess)
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
	RemoveFromRoot();
	OnExecuteFinishedDelegate.Broadcast(this, bSuccess);
}
