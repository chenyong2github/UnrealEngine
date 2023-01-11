// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaTextureSampleConverter.h"

#include "RenderGraphBuilder.h"
#include "RivermaxMediaLog.h"
#include "RivermaxMediaTextureSample.h"
#include "RivermaxMediaUtils.h"
#include "RivermaxShaders.h"

DECLARE_GPU_STAT(RivermaxSource_SampleConversion);


void FRivermaxMediaTextureSampleConverter::Setup(ERivermaxMediaSourcePixelFormat InPixelFormat, TWeakPtr<FRivermaxMediaTextureSample> InSample, bool bInDoSRGBToLinear)
{
	InputPixelFormat = InPixelFormat;
	Sample = InSample;
	bDoSRGBToLinear = bInDoSRGBToLinear;
}

bool FRivermaxMediaTextureSampleConverter::Convert(FTexture2DRHIRef& InDestinationTexture, const FConversionHints& Hints)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::Convert);

	using namespace UE::RivermaxShaders;
	using namespace UE::RivermaxMediaUtils::Private;

	TSharedPtr<FRivermaxMediaTextureSample> SamplePtr = Sample.Pin();
	if (SamplePtr.IsValid() == false)
	{
		return false;
	}

	const FSourceBufferDesc SourceBufferDesc = GetBufferDescription(SamplePtr->GetDim(), InputPixelFormat);
	
	FRDGBuilder GraphBuilder(FRHICommandListExecutor::GetImmediateCommandList());
	{
		RDG_GPU_STAT_SCOPE(GraphBuilder, RivermaxSource_SampleConversion)
		SCOPED_DRAW_EVENT(GraphBuilder.RHICmdList, Rivermax_SampleConverter);

		FRDGTextureRef OutputResource = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(InDestinationTexture, TEXT("RivermaxMediaTextureOutputResource")));

		// If we have a valid GPUBuffer, i.e GPUDirect is involved, use that one. Otherwise, take the system buffer and upload it in a new structured buffer.
		FRDGBufferRef InputBuffer;
		if (SamplePtr->GetGPUBuffer().IsValid())
		{
			InputBuffer = GraphBuilder.RegisterExternalBuffer(SamplePtr->GetGPUBuffer(), TEXT("RMaxGPUBuffer"));
		}
		else
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::CreateStructuredBuffer);
			InputBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("RivermaxInputBuffer"), SourceBufferDesc.BytesPerElement, SourceBufferDesc.NumberOfElements, SamplePtr->GetBuffer(), SourceBufferDesc.BytesPerElement * SourceBufferDesc.NumberOfElements, ERDGInitialDataFlags::NoCopy);
		}
		
		const FIntPoint ProcessedOutputDimension = { (int32)SourceBufferDesc.ElementsPerRow, InDestinationTexture->GetDesc().Extent.Y };
		const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(ProcessedOutputDimension, FComputeShaderUtils::kGolden2DGroupSize);
		FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

		//Configure shader and add conversion pass based on desired pixel format
		switch (InputPixelFormat)
		{
		case ERivermaxMediaSourcePixelFormat::YUV422_8bit:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::YUV8ShaderSetup);

			FYUV8Bit422ToRGBACS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FYUV8Bit422ToRGBACS::FSRGBToLinear>(bDoSRGBToLinear);

			const FMatrix YUVToRGBMatrix = SamplePtr->GetYUVToRGBMatrix();
			const FVector YUVOffset(MediaShaders::YUVOffset8bits);
			TShaderMapRef<FYUV8Bit422ToRGBACS> ComputeShader(GlobalShaderMap, PermutationVector);
			FYUV8Bit422ToRGBACS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, YUVToRGBMatrix, YUVOffset, SourceBufferDesc.ElementsPerRow, ProcessedOutputDimension.Y);


			FComputeShaderUtils::AddPass(
				GraphBuilder
				, RDG_EVENT_NAME("YUV8Bit422ToRGBA")
				, ComputeShader
				, Parameters
				, GroupCount);
			break;
		}
		case ERivermaxMediaSourcePixelFormat::YUV422_10bit:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::YUV10ShaderSetup);
			
			FYUV10Bit422ToRGBACS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FYUV10Bit422ToRGBACS::FSRGBToLinear>(bDoSRGBToLinear);

			const FMatrix YUVToRGBMatrix = SamplePtr->GetYUVToRGBMatrix();
			const FVector YUVOffset(MediaShaders::YUVOffset10bits);
			TShaderMapRef<FYUV10Bit422ToRGBACS> ComputeShader(GlobalShaderMap, PermutationVector);
			FYUV10Bit422ToRGBACS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, YUVToRGBMatrix, YUVOffset, SourceBufferDesc.ElementsPerRow, ProcessedOutputDimension.Y);

			FComputeShaderUtils::AddPass(
				GraphBuilder
				, RDG_EVENT_NAME("YUV10Bit422ToRGBA")
				, ComputeShader
				, Parameters
				, GroupCount);
			break;
		}
		case ERivermaxMediaSourcePixelFormat::RGB_8bit:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::RGB8ShaderSetup);

			FRGB8BitToRGBA8CS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRGB8BitToRGBA8CS::FSRGBToLinear>(bDoSRGBToLinear);

			TShaderMapRef<FRGB8BitToRGBA8CS> ComputeShader(GlobalShaderMap, PermutationVector);
			FRGB8BitToRGBA8CS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, SourceBufferDesc.ElementsPerRow, ProcessedOutputDimension.Y);


			FComputeShaderUtils::AddPass(
				GraphBuilder
				, RDG_EVENT_NAME("RGB8BitToRGBA8")
				, ComputeShader
				, Parameters
				, GroupCount);
			break;
		}
		case ERivermaxMediaSourcePixelFormat::RGB_10bit:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::RGB10ShaderSetup);

			FRGB10BitToRGBA10CS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRGB10BitToRGBA10CS::FSRGBToLinear>(bDoSRGBToLinear);

			TShaderMapRef<FRGB10BitToRGBA10CS> ComputeShader(GlobalShaderMap, PermutationVector);
			FRGB10BitToRGBA10CS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, SourceBufferDesc.ElementsPerRow, ProcessedOutputDimension.Y);

			FComputeShaderUtils::AddPass(
				GraphBuilder
				, RDG_EVENT_NAME("RGB10BitToRGBA")
				, ComputeShader
				, Parameters
				, GroupCount);
			break;
		}
		case ERivermaxMediaSourcePixelFormat::RGB_12bit:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::RGB12ShaderSetup);

			FRGB12BitToRGBA12CS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRGB12BitToRGBA12CS::FSRGBToLinear>(bDoSRGBToLinear);

			TShaderMapRef<FRGB12BitToRGBA12CS> ComputeShader(GlobalShaderMap, PermutationVector);
			FRGB12BitToRGBA12CS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, SourceBufferDesc.ElementsPerRow, ProcessedOutputDimension.Y);

			FComputeShaderUtils::AddPass(
				GraphBuilder
				, RDG_EVENT_NAME("RGB12BitToRGBA")
				, ComputeShader
				, Parameters
				, GroupCount);
			break;
		}
		case ERivermaxMediaSourcePixelFormat::RGB_16bit_Float:
		{
			TRACE_CPUPROFILER_EVENT_SCOPE(RivermaxSampleConverter::RGB16FloatShaderSetup);

			FRGB16fBitToRGBA16fCS::FPermutationDomain PermutationVector;
			PermutationVector.Set<FRGB16fBitToRGBA16fCS::FSRGBToLinear>(bDoSRGBToLinear);

			TShaderMapRef<FRGB16fBitToRGBA16fCS> ComputeShader(GlobalShaderMap, PermutationVector);
			FRGB16fBitToRGBA16fCS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InputBuffer, OutputResource, SourceBufferDesc.ElementsPerRow, ProcessedOutputDimension.Y);

			FComputeShaderUtils::AddPass(
				GraphBuilder
				, RDG_EVENT_NAME("RGB16fBitToRGBA")
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
	}

	GraphBuilder.Execute();

	return true;
}

uint32 FRivermaxMediaTextureSampleConverter::GetConverterInfoFlags() const
{
	return IMediaTextureSampleConverter::ConverterInfoFlags_NeedUAVOutputTexture;
}
