// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaTextureSampleConverter.h"

#include "RenderGraphBuilder.h"
#include "RivermaxShaders.h"
#include "RivermaxMediaTextureSample.h"



void FRivermaxMediaTextureSampleConverter::Setup(ERivermaxMediaSourePixelFormat InPixelFormat, TWeakPtr<FRivermaxMediaTextureSample> InSample, bool bInDoSRGBToLinear)
{
	InputPixelFormat = InPixelFormat;
	Sample = InSample;
	bDoSRGBToLinear = bInDoSRGBToLinear;
}

bool FRivermaxMediaTextureSampleConverter::Convert(FTexture2DRHIRef& InDestinationTexture, const FConversionHints& Hints)
{
	using namespace UE::RivermaxShaders;

	TSharedPtr<FRivermaxMediaTextureSample> SamplePtr = Sample.Pin();
	if (SamplePtr.IsValid() == false)
	{
		return false;
	}

	FIntVector GroupCount;
	
	FRDGBuilder GraphBuilder(FRHICommandListExecutor::GetImmediateCommandList());
	FRDGTextureRef OutputResource = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(InDestinationTexture, TEXT("RivermaxMediaTextureOutputResource")));

	//Configure shader and add conversion pass based on desired pixel format
	switch (InputPixelFormat)
	{
	case ERivermaxMediaSourePixelFormat::YUV422_8bit:
	{
		break;
	}
	case ERivermaxMediaSourePixelFormat::YUV422_10bit:
	{
		constexpr uint32 PixelPerGroup = 2;
		constexpr uint32 BytesPerGroup = 5;
		const uint32 HorizontalByteCount = (InDestinationTexture->GetDesc().Extent.X / PixelPerGroup) * BytesPerGroup;
		const int32 BytesPerElement = sizeof(FYUV10Bit422ToRGBACS::FYUV10Bit422LEBuffer);
		const int32 ElementsPerRow = HorizontalByteCount / BytesPerElement;
		const int32 ElementCount = ElementsPerRow * InDestinationTexture->GetDesc().Extent.Y;

		FYUV10Bit422ToRGBACS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FYUV10Bit422ToRGBACS::FSRGBToLinear>(bDoSRGBToLinear);

		FRDGBufferRef InputYUVBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("RivermaxInputBuffer"), BytesPerElement, ElementCount, SamplePtr->GetBuffer(), BytesPerElement * ElementCount);
		constexpr int32 PixelsPerInput = 8;
		const FIntPoint ProcessedOutputDimension = { InDestinationTexture->GetDesc().Extent.X / PixelsPerInput,InDestinationTexture->GetDesc().Extent.Y };
		GroupCount = FComputeShaderUtils::GetGroupCount(ProcessedOutputDimension, FComputeShaderUtils::kGolden2DGroupSize);
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FYUV10Bit422ToRGBACS> ComputeShader(GlobalShaderMap, PermutationVector);
		FMatrix YUVToRGBMatrix = SamplePtr->GetYUVToRGBMatrix();
		FVector YUVOffset(MediaShaders::YUVOffset10bits);
		FYUV10Bit422ToRGBACS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputYUVBuffer, OutputResource, YUVToRGBMatrix, YUVOffset, ElementsPerRow);


		FComputeShaderUtils::AddPass(
			GraphBuilder
			, RDG_EVENT_NAME("YUV10Bit422ToRGBA")
			, ComputeShader
			, Parameters
			, GroupCount);
		break;
	}
	case ERivermaxMediaSourePixelFormat::RGB_8bit:
	{
		constexpr uint32 PixelPerGroup = 1;
		constexpr uint32 BytesPerGroup = (PixelPerGroup * 3 * 8) / 8;
		const uint32 HorizontalByteCount = (InDestinationTexture->GetDesc().Extent.X / PixelPerGroup) * BytesPerGroup;
		const int32 BytesPerElement = sizeof(FRGB8BitToRGBA8CS::FRGB8BitBuffer);
		const int32 ElementsPerRow = HorizontalByteCount / BytesPerElement;
		const int32 ElementCount = ElementsPerRow * InDestinationTexture->GetDesc().Extent.Y;

		FRGB8BitToRGBA8CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FRGB8BitToRGBA8CS::FSRGBToLinear>(bDoSRGBToLinear);

		FRDGBufferRef InputRGGBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("RivermaxInputBuffer"), BytesPerElement, ElementCount, SamplePtr->GetBuffer(), BytesPerElement * ElementCount);
		constexpr int32 PixelsPerInput = 4;
		const FIntPoint ProcessedOutputDimension = { InDestinationTexture->GetDesc().Extent.X / PixelsPerInput,InDestinationTexture->GetDesc().Extent.Y };
		GroupCount = FComputeShaderUtils::GetGroupCount(ProcessedOutputDimension, FComputeShaderUtils::kGolden2DGroupSize);
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FRGB8BitToRGBA8CS> ComputeShader(GlobalShaderMap, PermutationVector);
		FRGB8BitToRGBA8CS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputRGGBuffer, OutputResource, ElementsPerRow);


		FComputeShaderUtils::AddPass(
			GraphBuilder
			, RDG_EVENT_NAME("RGB8BitToRGBA8")
			, ComputeShader
			, Parameters
			, GroupCount);
		break;
	}
	case ERivermaxMediaSourePixelFormat::RGB_10bit:
	{
		constexpr uint32 PixelPerGroup = 4;
		constexpr uint32 BytesPerGroup = (PixelPerGroup * 3 * 10) / 8;
		const uint32 HorizontalByteCount = (InDestinationTexture->GetDesc().Extent.X / PixelPerGroup) * BytesPerGroup;
		const int32 BytesPerElement = sizeof(FRGBToRGB10BitCS::FRGB10BitBuffer);
		const int32 ElementsPerRow = HorizontalByteCount / BytesPerElement;
		const int32 ElementCount = ElementsPerRow * InDestinationTexture->GetDesc().Extent.Y;
		
		FRGB10BitToRGBA10CS::FPermutationDomain PermutationVector;
		PermutationVector.Set<FRGB10BitToRGBA10CS::FSRGBToLinear>(bDoSRGBToLinear);
		
		FRDGBufferRef InputRGGBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("RivermaxInputBuffer"), BytesPerElement, ElementCount, SamplePtr->GetBuffer(), BytesPerElement * ElementCount);
		constexpr int32 PixelsPerInput = 16;
		const FIntPoint ProcessedOutputDimension = { InDestinationTexture->GetDesc().Extent.X / PixelsPerInput,InDestinationTexture->GetDesc().Extent.Y};
		GroupCount = FComputeShaderUtils::GetGroupCount(ProcessedOutputDimension, FComputeShaderUtils::kGolden2DGroupSize);
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FRGB10BitToRGBA10CS> ComputeShader(GlobalShaderMap, PermutationVector);
		FRGB10BitToRGBA10CS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputRGGBuffer, OutputResource, ElementsPerRow);


		FComputeShaderUtils::AddPass(
			GraphBuilder
			, RDG_EVENT_NAME("RGB10BitToRGBA")
			, ComputeShader
			, Parameters
			, GroupCount);
		break;
	}
	default:
	{
		ensureMsgf(false, TEXT("Unhandled pixel format (%d) given to Rivermax MediaSample converter"), InputPixelFormat);
		return false;
	}
	}

	GraphBuilder.Execute();

	return true;
}

uint32 FRivermaxMediaTextureSampleConverter::GetConverterInfoFlags() const
{
	return IMediaTextureSampleConverter::ConverterInfoFlags_NeedUAVOutputTexture;
}
