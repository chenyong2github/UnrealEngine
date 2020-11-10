// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineVideoOutputBase.h"
#include "MoviePipeline.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineMasterConfig.h"
#include "MovieRenderPipelineCoreModule.h"

void UMoviePipelineVideoOutputBase::OnReceiveImageDataImpl(FMoviePipelineMergerOutputFrame* InMergedOutputFrame)
{
	UMoviePipelineOutputSetting* OutputSettings = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	check(OutputSettings);

	FString OutputDirectory = OutputSettings->OutputDirectory.Path;

	for (TPair<FMoviePipelinePassIdentifier, TUniquePtr<FImagePixelData>>& RenderPassData : InMergedOutputFrame->ImageOutputData)
	{
		FImagePixelDataPayload* Payload = RenderPassData.Value->GetPayload<FImagePixelDataPayload>();

		// We need to resolve the filename format string. We combine the folder and file name into one long string first
		FMoviePipelineFormatArgs FinalFormatArgs;
		FString FinalFilePath;
		FString FinalVideoFileName;
		FString ClipName;
		{
			FString FileNameFormatString = OutputSettings->FileNameFormat;

			// If we're writing more than one render pass out, we need to ensure the file name has the format string in it so we don't
			// overwrite the same file multiple times. Burn In overlays don't count because they get composited on top of an existing file.
			const bool bIncludeRenderPass = InMergedOutputFrame->ImageOutputData.Num() > 1;
			const bool bTestFrameNumber = false;

			UE::MoviePipeline::ValidateOutputFormatString(FileNameFormatString, bIncludeRenderPass, bTestFrameNumber);

			// Strip any frame number tags so we don't get one video file per frame.
			UE::MoviePipeline::RemoveFrameNumberFormatStrings(FileNameFormatString, true);

			// Create specific data that needs to override 
			FStringFormatNamedArguments FormatOverrides;
			FormatOverrides.Add(TEXT("render_pass"), RenderPassData.Key.Name);
			FormatOverrides.Add(TEXT("ext"), GetFilenameExtension());

			GetPipeline()->ResolveFilenameFormatArguments(FileNameFormatString, FormatOverrides, FinalVideoFileName, FinalFormatArgs, &InMergedOutputFrame->FrameOutputState);

			FinalFilePath = OutputDirectory / FinalVideoFileName;

			// Create a deterministic clipname by file extension, and any trailing .'s
			FMoviePipelineFormatArgs TempFormatArgs;
			GetPipeline()->ResolveFilenameFormatArguments(FileNameFormatString, FormatOverrides, ClipName, TempFormatArgs, &InMergedOutputFrame->FrameOutputState);
			ClipName.RemoveFromEnd(GetFilenameExtension());
			ClipName.RemoveFromEnd(".");
		}


		MovieRenderPipeline::IVideoCodecWriter* OutputWriter = nullptr;
		for (const TUniquePtr<MovieRenderPipeline::IVideoCodecWriter>& Writer : AllWriters)
		{
			if (Writer->FileName == FinalFilePath)
			{
				OutputWriter = Writer.Get();
				break;
			}
		}
		
		if(!OutputWriter)
		{
			// Create a new writer for this file name (and output format settings)
			TUniquePtr<MovieRenderPipeline::IVideoCodecWriter> NewWriter = Initialize_GameThread(FinalFilePath,
				RenderPassData.Value->GetSize(), RenderPassData.Value->GetType(), RenderPassData.Value->GetPixelLayout(),
				RenderPassData.Value->GetBitDepth(), RenderPassData.Value->GetNumChannels());

			if (NewWriter)
			{
				AllWriters.Add(MoveTemp(NewWriter));
				OutputWriter = AllWriters.Last().Get();
				OutputWriter->FormatArgs = FinalFormatArgs;

				Initialize_EncodeThread(OutputWriter);
			}
		}

		if (!OutputWriter)
		{
			UE_LOG(LogMovieRenderPipeline, Error, TEXT("Failed to generate writer for FileName: %s"), *FinalFilePath);
			continue;
		}

		FMoviePipelineBackgroundMediaTasks Task;
		FImagePixelData* RawRenderPassData = RenderPassData.Value.Get();

		// Making sure that if OCIO is enabled the Quantization won't do additional color conversion.
		UMoviePipelineColorSetting* ColorSetting = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineColorSetting>();
		OutputWriter->bConvertToSrgb = !(ColorSetting && ColorSetting->OCIOConfiguration.bIsEnabled);

		//FGraphEventRef Event = Task.Execute([this, OutputWriter, RawRenderPassData]
		//	{
				// Enqueue a encode for this frame onto our worker thread.
				this->WriteFrame_EncodeThread(OutputWriter, RawRenderPassData);
		//	});
		//OutstandingTasks.Add(Event);
		
#if WITH_EDITOR
		GetPipeline()->AddFrameToOutputMetadata(ClipName, FinalVideoFileName, InMergedOutputFrame->FrameOutputState, GetFilenameExtension(), Payload->bRequireTransparentOutput);
#endif
	}
}

bool UMoviePipelineVideoOutputBase::HasFinishedProcessingImpl()
{
	// for (int32 Index = OutstandingTasks.Num() - 1; Index >= 0; Index--)
	// {
	// 	if (OutstandingTasks[Index].())
	// 	{
	// 		OutstandingTasks.RemoveAt(Index);
	// 	}
	// }

	return OutstandingTasks.Num() == 0;
}

void UMoviePipelineVideoOutputBase::BeginFinalizeImpl()
{
	for (const TUniquePtr<MovieRenderPipeline::IVideoCodecWriter>& Writer : AllWriters)
	{
		MovieRenderPipeline::IVideoCodecWriter* RawWriter = Writer.Get();
		FMoviePipelineBackgroundMediaTasks Task;
	
		//OutstandingTasks.Add(Task.Execute([this, RawWriter] {
			this->BeginFinalize_EncodeThread(RawWriter);
		//	}));
	}
}

void UMoviePipelineVideoOutputBase::FinalizeImpl()
{
	FGraphEventRef* LastEvent = nullptr;
	for (const TUniquePtr<MovieRenderPipeline::IVideoCodecWriter>& Writer : AllWriters)
	{
		MovieRenderPipeline::IVideoCodecWriter* RawWriter = Writer.Get();
		FMoviePipelineBackgroundMediaTasks Task;
	
		//*LastEvent = Task.Execute([this, RawWriter] {
			this->Finalize_EncodeThread(RawWriter);
		//	});
	}
	
	// Stall until all of the events are handled so that they still exist when the Task Graph goes to execute them.
	if (LastEvent)
	{
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(*LastEvent);
	}

	AllWriters.Empty();
}

#if WITH_EDITOR
FText UMoviePipelineVideoOutputBase::GetFooterText(UMoviePipelineExecutorJob* InJob) const
{
	if (!IsAudioSupported())
	{
		return NSLOCTEXT("MovieRenderPipeline", "VideoOutputAudioUnsupported", "Audio output is not supported for this video encoder. Please consider using the .wav writer and combining in post.");
	}

	return FText();
}
#endif