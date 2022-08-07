// Copyright Epic Games, Inc. All Rights Reserved.

#include "RenderPage/RenderPageManager.h"
#include "RenderPage/RenderPageMoviePipelineJob.h"
#include "RenderPagesUtils.h"
#include "MoviePipelineHighResSetting.h"
#include "MoviePipelinePIEExecutor.h"


URenderPagesMoviePipelineRenderJob* UE::RenderPages::FRenderPageManager::CreateBatchRenderJob(URenderPageCollection* PageCollection)
{
	FRenderPagesMoviePipelineRenderJobCreateArgs JobArgs;
	JobArgs.PageCollection = PageCollection;
	JobArgs.Pages.Append(PageCollection->GetEnabledRenderPages());
	URenderPagesMoviePipelineRenderJob* NewRenderJob = URenderPagesMoviePipelineRenderJob::Create(JobArgs);
	if (!IsValid(NewRenderJob))
	{
		return nullptr;
	}
	return NewRenderJob;
}


URenderPagesMoviePipelineRenderJob* UE::RenderPages::FRenderPageManager::RenderPreviewFrame(const FRenderPageManagerRenderPreviewFrameArgs& Args)
{
	const FRenderPageManagerRenderPreviewFrameArgsCallback Callback = Args.Callback;

	if (!IsValid(Args.Page))
	{
		Callback.ExecuteIfBound(false);
		return nullptr;
	}

	URenderPage* PageCopy = DuplicateObject(Args.Page.Get(), Args.Page->GetOuter());
	if (!IsValid(PageCopy))
	{
		Callback.ExecuteIfBound(false);
		return nullptr;
	}

	PageCopy->SetPageId(PageCopy->GetId().ToString(EGuidFormats::Base36Encoded));

	if (Args.Frame.IsSet())
	{
		constexpr int32 RenderFramesCount = 1;// can be more than 1 to prevent rendering issues, will always take the last frame that's rendered

		PageCopy->SetIsUsingCustomStartFrame(true);
		PageCopy->SetCustomStartFrame(Args.Frame.Get(0));

		PageCopy->SetIsUsingCustomEndFrame(true);
		PageCopy->SetCustomEndFrame(Args.Frame.Get(0));

		if (!PageCopy->SetSequenceEndFrame(PageCopy->GetSequenceStartFrame().Get(0) + 1) ||
			!PageCopy->SetSequenceStartFrame(PageCopy->GetSequenceEndFrame().Get(0) - RenderFramesCount))
		{
			Callback.ExecuteIfBound(false);
			return nullptr;
		}
	}

	PageCopy->SetIsUsingCustomResolution(true);
	PageCopy->SetCustomResolution(Args.Resolution);

	PageCopy->SetOutputDirectory(TmpRenderedFramesPath / (Args.Frame.IsSet() ? TEXT("PreviewFrame") : TEXT("PreviewFrames")));

	FRenderPagesMoviePipelineRenderJobCreateArgs JobArgs;
	JobArgs.PageCollection = Args.PageCollection;
	JobArgs.Pages.Add(PageCopy);
	JobArgs.bHeadless = Args.bHeadless;
	JobArgs.bForceOutputImage = true;
	JobArgs.bForceOnlySingleOutput = true;
	JobArgs.bForceUseSequenceFrameRate = Args.Frame.IsSet();
	JobArgs.bEnsureSequentialFilenames = true;
	JobArgs.DisableSettingsClasses.Add(UMoviePipelineAntiAliasingSetting::StaticClass());
	JobArgs.DisableSettingsClasses.Add(UMoviePipelineHighResSetting::StaticClass());

	URenderPagesMoviePipelineRenderJob* NewRenderJob = URenderPagesMoviePipelineRenderJob::Create(JobArgs);
	if (!NewRenderJob)
	{
		Callback.ExecuteIfBound(false);
		return nullptr;
	}

	const FGuid PageId = PageCopy->GetId();
	const TOptional<int32> StartFrameOfRender = (Args.Frame.IsSet() ? TOptional<int32>() : PageCopy->GetStartFrame());
	NewRenderJob->OnExecuteFinished().AddLambda([this, Callback, PageId, StartFrameOfRender](URenderPagesMoviePipelineRenderJob* RenderJob, const bool bSuccess)
	{
		if (StartFrameOfRender.IsSet())
		{
			if (bSuccess)
			{
				StartFrameOfRenders.Add(PageId, StartFrameOfRender.Get(0));
			}
			else
			{
				StartFrameOfRenders.Remove(PageId);
			}
		}
		Callback.ExecuteIfBound(bSuccess);
	});

	NewRenderJob->Execute();
	return NewRenderJob;
}

UTexture2D* UE::RenderPages::FRenderPageManager::GetSingleRenderedPreviewFrame(URenderPage* Page, UTexture2D* ReusingTexture2D, bool& bOutReusedGivenTexture2D)
{
	static const FString PreviewFramesDir = TmpRenderedFramesPath / TEXT("PreviewFrame");
	if (!IsValid(Page))
	{
		bOutReusedGivenTexture2D = (ReusingTexture2D == nullptr);
		return nullptr;
	}
	const FString PreviewFramesSubDir = Page->GetId().ToString(EGuidFormats::Base36Encoded);

	TArray<FString> ImagePaths = Private::FRenderPagesUtils::GetFiles(PreviewFramesDir / PreviewFramesSubDir, true);
	ImagePaths.Sort();
	Algo::Reverse(ImagePaths);
	for (const FString& ImagePath : ImagePaths)
	{
		if (UTexture2D* Texture = Private::FRenderPagesUtils::GetImage(ImagePath, ReusingTexture2D, bOutReusedGivenTexture2D))
		{
			return Texture;
		}
	}
	bOutReusedGivenTexture2D = (ReusingTexture2D == nullptr);
	return nullptr;
}

UTexture2D* UE::RenderPages::FRenderPageManager::GetSingleRenderedPreviewFrame(URenderPage* Page)
{
	bool bOutReusedGivenTexture2D;
	return GetSingleRenderedPreviewFrame(Page, nullptr, bOutReusedGivenTexture2D);
}

UTexture2D* UE::RenderPages::FRenderPageManager::GetRenderedPreviewFrame(URenderPage* Page, const int32 Frame, UTexture2D* ReusingTexture2D, bool& bOutReusedGivenTexture2D)
{
	static const FString PreviewFramesDir = TmpRenderedFramesPath / TEXT("PreviewFrames");
	if (!IsValid(Page))
	{
		bOutReusedGivenTexture2D = (ReusingTexture2D == nullptr);
		return nullptr;
	}
	const FString PreviewFramesSubDir = Page->GetId().ToString(EGuidFormats::Base36Encoded);

	int32 CurrentFrame = 0;
	if (int32* StartFrameOfRenderPtr = StartFrameOfRenders.Find(Page->GetId()))
	{
		CurrentFrame = *StartFrameOfRenderPtr;
	}

	TArray<FString> ImagePaths = Private::FRenderPagesUtils::GetFiles(PreviewFramesDir / PreviewFramesSubDir, true);
	for (const FString& ImagePath : ImagePaths)
	{
		if (!Private::FRenderPagesUtils::IsImage(ImagePath))
		{
			continue;
		}
		if (CurrentFrame == Frame)
		{
			return Private::FRenderPagesUtils::GetImage(ImagePath, ReusingTexture2D, bOutReusedGivenTexture2D);
		}
		CurrentFrame++;
	}

	bOutReusedGivenTexture2D = (ReusingTexture2D == nullptr);
	return nullptr;
}

UTexture2D* UE::RenderPages::FRenderPageManager::GetRenderedPreviewFrame(URenderPage* Page, const int32 Frame)
{
	bool bOutReusedGivenTexture2D;
	return GetRenderedPreviewFrame(Page, Frame, nullptr, bOutReusedGivenTexture2D);
}


void UE::RenderPages::FRenderPageManager::UpdatePagesPropValues(URenderPageCollection* PageCollection)
{
	if (!IsValid(PageCollection))
	{
		return;
	}

	URenderPagePropsSourceRemoteControl* PropsSource = PageCollection->GetPropsSource<URenderPagePropsSourceRemoteControl>();
	if (!IsValid(PropsSource))
	{
		return;
	}

	TArray<URenderPage*> Pages = PageCollection->GetRenderPages();
	TArray<uint8> BinaryArray;
	for (URenderPagePropRemoteControl* Field : PropsSource->GetProps()->GetAllCasted())
	{
		if (!Field->GetValue(BinaryArray))
		{
			continue;
		}
		const TSharedPtr<FRemoteControlEntity> Entity = Field->GetRemoteControlEntity();
		for (URenderPage* Page : Pages)
		{
			if (!Page->HasRemoteControlValue(Entity))
			{
				Page->SetRemoteControlValue(Entity, BinaryArray);
			}
		}
	}
}

FRenderPageManagerPreviousPagePropValues UE::RenderPages::FRenderPageManager::ApplyPagePropValues(const URenderPageCollection* PageCollection, const URenderPage* Page)
{
	FRenderPageManagerPreviousPagePropValues PreviousPropValues;

	if (!IsValid(PageCollection) || !IsValid(Page))
	{
		return PreviousPropValues;
	}

	URenderPagePropsSourceBase* PropsSource = PageCollection->GetPropsSource();
	if (URenderPagePropsSourceRemoteControl* PropsSourceRC = Cast<URenderPagePropsSourceRemoteControl>(PropsSource))
	{
		for (URenderPagePropRemoteControl* Prop : PropsSourceRC->GetProps()->GetAllCasted())
		{
			TArray<uint8> PreviousPropData;
			if (!Prop->GetValue(PreviousPropData))
			{
				continue;
			}

			TArray<uint8> PropData;
			if (!Page->ConstGetRemoteControlValue(Prop->GetRemoteControlEntity(), PropData))
			{
				continue;
			}

			PreviousPropValues.RemoteControlData.Add(Prop, PreviousPropData);
			Prop->SetValue(PropData);
		}
		return PreviousPropValues;
	}
	return PreviousPropValues;
}

void UE::RenderPages::FRenderPageManager::RestorePagePropValues(const FRenderPageManagerPreviousPagePropValues& PreviousPropValues)
{
	for (const auto& PropValuePair : PreviousPropValues.RemoteControlData)
	{
		if (IsValid(PropValuePair.Key))
		{
			PropValuePair.Key->SetValue(PropValuePair.Value.Bytes);
		}
	}
}
