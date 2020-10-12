// Copyright Epic Games, Inc. All Rights Reserved.

#include "MoviePipelineAvidDNxOutput.h"
#include "MoviePipeline.h"
#include "MoviePipelineOutputSetting.h"
#include "MoviePipelineMasterConfig.h"
#include "ImagePixelData.h"
#include "MoviePipelineImageQuantization.h"
#include "SampleBuffer.h"
#include "MovieRenderPipelineCoreModule.h"

// For logs
#include "MovieRenderPipelineCoreModule.h"

TUniquePtr<MovieRenderPipeline::IVideoCodecWriter> UMoviePipelineAvidDNxOutput::Initialize_GameThread(const FString& InFileName, FIntPoint InResolution, EImagePixelType InPixelType, ERGBFormat InPixelFormat, uint8 InBitDepth, uint8 InNumChannels)
{
	UMoviePipelineOutputSetting* OutputSettings = GetPipeline()->GetPipelineMasterConfig()->FindSetting<UMoviePipelineOutputSetting>();
	if (!OutputSettings)
	{
		return nullptr;
	}
		
	 
	FAvidDNxEncoderOptions Options;
	Options.OutputFilename = InFileName;
	Options.Width = InResolution.X;
	Options.Height = InResolution.Y;
	Options.FrameRate = GetPipeline()->GetPipelineMasterConfig()->GetEffectiveFrameRate(GetPipeline()->GetTargetSequence());
	Options.bCompress = bUseCompression;
	Options.NumberOfEncodingThreads = NumberOfEncodingThreads;

	TUniquePtr<FAvidDNxEncoder> Encoder = MakeUnique<FAvidDNxEncoder>(Options);
	
	TUniquePtr<FAvidWriter> OutWriter = MakeUnique<FAvidWriter>();
	OutWriter->Writer = MoveTemp(Encoder);
	OutWriter->FileName = InFileName;
	
	return OutWriter;
}

void UMoviePipelineAvidDNxOutput::Initialize_EncodeThread(MovieRenderPipeline::IVideoCodecWriter* InWriter)
{
	FAvidWriter* CodecWriter = static_cast<FAvidWriter*>(InWriter);
	if(!CodecWriter->Writer->Initialize())
	{
		UE_LOG(LogMovieRenderPipeline, Error, TEXT("Failed to initialize Apple Pro Res Writer."));
	}
}

void UMoviePipelineAvidDNxOutput::WriteFrame_EncodeThread(MovieRenderPipeline::IVideoCodecWriter* InWriter, FImagePixelData* InPixelData)
{
	FAvidWriter* CodecWriter = static_cast<FAvidWriter*>(InWriter);
	
	// Quantize our 16 bit float data to 8 bit and apply sRGB
	TUniquePtr<FImagePixelData> QuantizedPixelData = UE::MoviePipeline::QuantizeImagePixelDataToBitDepth(InPixelData, 8, nullptr, InWriter->bConvertToSrgb);
	 
	const void* Data = nullptr;
	int64 DataSize;
	QuantizedPixelData->GetRawData(Data, DataSize);

	CodecWriter->Writer->WriteFrame((uint8*)Data);
}

void UMoviePipelineAvidDNxOutput::BeginFinalize_EncodeThread(MovieRenderPipeline::IVideoCodecWriter* InWriter)
{
	// The current AvidDNx SDK does not support audio encoding so we don't write audio to the container.
	return;
}

void UMoviePipelineAvidDNxOutput::Finalize_EncodeThread(MovieRenderPipeline::IVideoCodecWriter* InWriter)
{
	// Commit this to disk.
	FAvidWriter* CodecWriter = static_cast<FAvidWriter*>(InWriter);
	CodecWriter->Writer->Finalize();
}