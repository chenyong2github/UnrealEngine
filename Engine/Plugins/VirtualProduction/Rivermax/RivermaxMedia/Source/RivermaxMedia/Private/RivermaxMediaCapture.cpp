// Copyright Epic Games, Inc. All Rights Reserved.

#include "RivermaxMediaCapture.h"

#include "IRivermaxCoreModule.h"
#include "IRivermaxManager.h"
#include "IRivermaxOutputStream.h"
#include "RenderGraphUtils.h"
#include "RivermaxMediaLog.h"
#include "RivermaxMediaOutput.h"
#include "RivermaxMediaUtils.h"
#include "RivermaxShaders.h"
#include "RivermaxTypes.h"
#include "Slate/SceneViewport.h"
#include "Widgets/SViewport.h"

#if WITH_EDITOR
#include "AnalyticsEventAttribute.h"
#include "EngineAnalytics.h"
#endif



/* namespace RivermaxMediaCaptureDevice
*****************************************************************************/

#if WITH_EDITOR
namespace RivermaxMediaCaptureAnalytics
{
	/**
	 * @EventName MediaFramework.RivermaxCaptureStarted
	 * @Trigger Triggered when a Rivermax capture of the viewport or render target is started.
	 * @Type Client
	 * @Owner MediaIO Team
	 */
	void SendCaptureEvent(const FIntPoint& Resolution, const FFrameRate FrameRate, const FString& CaptureType)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			TArray<FAnalyticsEventAttribute> EventAttributes;
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("CaptureType"), CaptureType));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("ResolutionWidth"), FString::Printf(TEXT("%d"), Resolution.X)));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("ResolutionHeight"), FString::Printf(TEXT("%d"), Resolution.Y)));
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("FrameRate"), FrameRate.ToPrettyText().ToString()));
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("MediaFramework.RivermaxCaptureStarted"), EventAttributes);
		}
	}
}
#endif

namespace UE::RivermaxMediaCaptureUtil
{
	FIntPoint GetAlignedResolution(const UE::RivermaxCore::FVideoFormatInfo& InFormatInfo, const FIntPoint& InSize)
	{
		// We need to adjust horizontal resolution if it's not aligned to pgroup pixel coverage
		// RTP header has to describe a full pgroup even if it's outside of effective resolution.
		const uint32 PixelAlignment = InFormatInfo.PixelGroupCoverage;
		const uint32 AlignedHorizontalResolution = InSize.X % PixelAlignment ? InSize.X + (PixelAlignment - InSize.X % PixelAlignment) : InSize.X;
		return FIntPoint(AlignedHorizontalResolution, InSize.Y);
	}

	void GetOutputEncodingInfo(ERivermaxMediaOutputPixelFormat InPixelFormat, const FIntPoint& InSize, uint32& OutBytesPerElement, uint32& ElementsPerRow)
	{
		using namespace UE::RivermaxCore;

		const ESamplingType SamplingType = UE::RivermaxMediaUtils::Private::MediaOutputPixelFormatToRivermaxSamplingType(InPixelFormat);
		const FVideoFormatInfo Info = FStandardVideoFormat::GetVideoFormatInfo(SamplingType);

		// Compute horizontal byte count (stride) of aligned resolution
		const uint32 PixelAlignment = Info.PixelGroupCoverage;
		const FIntPoint AlignedResolution = GetAlignedResolution(Info, InSize);
		const uint32 PixelGroupCount = AlignedResolution.X / PixelAlignment;
		const uint32 HorizontalByteCount = PixelGroupCount * Info.PixelGroupSize;

		switch (InPixelFormat)
		{
		case ERivermaxMediaOutputPixelFormat::PF_10BIT_YUV422:
		{
			OutBytesPerElement = sizeof(UE::RivermaxShaders::FRGBToYUV10Bit422LittleEndianCS::FYUV10Bit422LEBuffer);
			break;
		}
		case ERivermaxMediaOutputPixelFormat::PF_8BIT_RGB:
		{
			OutBytesPerElement = sizeof(UE::RivermaxShaders::FRGBToRGB8BitCS::FRGB8BitBuffer);
			break;
		}
		case ERivermaxMediaOutputPixelFormat::PF_10BIT_RGB:
		{
			OutBytesPerElement = sizeof(UE::RivermaxShaders::FRGBToRGB10BitCS::FRGB10BitBuffer);
			break;
		}
		case ERivermaxMediaOutputPixelFormat::PF_FLOAT16_RGB:
		{
			OutBytesPerElement = sizeof(UE::RivermaxShaders::FRGBToRGB16fCS::FRGB16fBuffer);
			break;
		}
		}

		// Shader encoding might not align with pixel group size so we need to have enough elements to represent the last pixel group
		ElementsPerRow = FMath::CeilToInt32((float)HorizontalByteCount / OutBytesPerElement);
	}
}


///* URivermaxMediaCapture implementation
//*****************************************************************************/

bool URivermaxMediaCapture::ValidateMediaOutput() const
{
	URivermaxMediaOutput* RivermaxMediaOutput = Cast<URivermaxMediaOutput>(MediaOutput);
	if (!RivermaxMediaOutput)
	{
		UE_LOG(LogRivermaxMedia, Error, TEXT("Can not start the capture. MediaOutput's class is not supported."));
		return false;
	}

	return true;
}

bool URivermaxMediaCapture::InitializeCapture()
{
	URivermaxMediaOutput* RivermaxOutput = CastChecked<URivermaxMediaOutput>(MediaOutput);
	bool bResult = Initialize(RivermaxOutput);
#if WITH_EDITOR
	if (bResult)
	{
		RivermaxMediaCaptureAnalytics::SendCaptureEvent(GetDesiredSize(), RivermaxOutput->FrameRate, GetCaptureSourceType());
	}
#endif
	return bResult;
}

bool URivermaxMediaCapture::UpdateSceneViewportImpl(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	return true;
}

bool URivermaxMediaCapture::UpdateRenderTargetImpl(UTextureRenderTarget2D* InRenderTarget)
{
	return true;
}

void URivermaxMediaCapture::StopCaptureImpl(bool bAllowPendingFrameToBeProcess)
{
	if (RivermaxStream)
	{
		RivermaxStream->Uninitialize();
		RivermaxStream.Reset();
	}
}

bool URivermaxMediaCapture::ShouldCaptureRHIResource() const
{
	//todo for gpudirect support
	return false;  
}

void URivermaxMediaCapture::BeforeFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, FTextureRHIRef InTexture)
{
	if (ShouldCaptureRHIResource())
	{
	
	}
}

bool URivermaxMediaCapture::HasFinishedProcessing() const
{
	return Super::HasFinishedProcessing();
}

bool URivermaxMediaCapture::Initialize(URivermaxMediaOutput* InMediaOutput)
{
	using namespace UE::RivermaxCore;

	check(InMediaOutput);

	IRivermaxCoreModule* Module = FModuleManager::GetModulePtr<IRivermaxCoreModule>("RivermaxCore");
	if (Module)
	{
		FRivermaxStreamOptions Options;
		if (ConfigureStream(InMediaOutput, Options))
		{
			RivermaxStream = Module->CreateOutputStream();
			if (RivermaxStream)
			{
				return RivermaxStream->Initialize(Options, *this);
			}
		}
	}

	return false;
}

bool URivermaxMediaCapture::ConfigureStream(URivermaxMediaOutput* InMediaOutput, UE::RivermaxCore::FRivermaxStreamOptions& OutOptions) const
{
	using namespace UE::RivermaxCore;

	// Resolve interface address
	IRivermaxCoreModule* Module = FModuleManager::GetModulePtr<IRivermaxCoreModule>("RivermaxCore");
	if (Module == nullptr)
	{
		return false;
	}

	const bool bFoundDevice = Module->GetRivermaxManager()->GetMatchingDevice(InMediaOutput->InterfaceAddress, OutOptions.InterfaceAddress);
	if (bFoundDevice == false)
	{
		UE_LOG(LogRivermaxMedia, Error, TEXT("Could not find a matching interface for IP '%s'"), *InMediaOutput->InterfaceAddress);
		return false;
	}

	OutOptions.StreamAddress = InMediaOutput->StreamAddress;
	OutOptions.Port = InMediaOutput->Port;
	OutOptions.Resolution = InMediaOutput->Resolution;
	OutOptions.FrameRate = InMediaOutput->FrameRate;
	OutOptions.NumberOfBuffers = InMediaOutput->NumberOfTextureBuffers;

	OutOptions.PixelFormat = UE::RivermaxMediaUtils::Private::MediaOutputPixelFormatToRivermaxSamplingType(InMediaOutput->PixelFormat);
	const FVideoFormatInfo Info = FStandardVideoFormat::GetVideoFormatInfo(OutOptions.PixelFormat);
	OutOptions.AlignedResolution = UE::RivermaxMediaCaptureUtil::GetAlignedResolution(Info, InMediaOutput->Resolution);

	return true;
}

void URivermaxMediaCapture::OnFrameCaptured_RenderingThread(const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, void* InBuffer, int32 Width, int32 Height, int32 BytesPerRow)
{
	using namespace UE::RivermaxCore;

	TRACE_CPUPROFILER_EVENT_SCOPE(URivermaxMediaCapture::OnFrameCaptured_RenderingThread);
	
	FRivermaxOutputVideoFrameInfo NewFrame;
	NewFrame.Height = Height;
	NewFrame.Width = Width;
	NewFrame.Stride = BytesPerRow;
	NewFrame.VideoBuffer = InBuffer;
	NewFrame.FrameIdentifier = InBaseData.SourceFrameNumber;
	if (RivermaxStream->PushVideoFrame(NewFrame) == false)
	{
		UE_LOG(LogRivermaxMedia, VeryVerbose, TEXT("Failed to pushed captured frame"));
	}
}

void URivermaxMediaCapture::OnRHIResourceCaptured_RenderingThread(const FCaptureBaseData& InBaseData,	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, FTextureRHIRef InTexture)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(URivermaxMediaCapture::OnFrameCaptured_RenderingThread);
}

FIntPoint URivermaxMediaCapture::GetCustomOutputSize(const FIntPoint& InSize) const
{
	URivermaxMediaOutput* RivermaxOutput = CastChecked<URivermaxMediaOutput>(MediaOutput);

	uint32 BytesPerElement = 0;
	uint32 ElementsPerRow = 0;
	UE::RivermaxMediaCaptureUtil::GetOutputEncodingInfo(RivermaxOutput->PixelFormat, InSize, BytesPerElement, ElementsPerRow);
	return FIntPoint(ElementsPerRow, InSize.Y);
}

EMediaCaptureResourceType URivermaxMediaCapture::GetCustomOutputResourceType() const
{
	URivermaxMediaOutput* RivermaxOutput = CastChecked<URivermaxMediaOutput>(MediaOutput);

	switch (RivermaxOutput->PixelFormat)
	{
	case ERivermaxMediaOutputPixelFormat::PF_10BIT_YUV422:
	case ERivermaxMediaOutputPixelFormat::PF_8BIT_RGB:
	case ERivermaxMediaOutputPixelFormat::PF_10BIT_RGB:
	case ERivermaxMediaOutputPixelFormat::PF_FLOAT16_RGB:
	{
		return EMediaCaptureResourceType::Buffer; //we use compute shader for all since output format doesn't match texture formats
	}
	default:
		return EMediaCaptureResourceType::Texture;
	}
}

FRDGBufferDesc URivermaxMediaCapture::GetCustomBufferDescription(const FIntPoint& InDesiredSize) const
{
	URivermaxMediaOutput* RivermaxOutput = CastChecked<URivermaxMediaOutput>(MediaOutput);

	uint32 BytesPerElement = 0;
	uint32 ElementsPerRow = 0;
	UE::RivermaxMediaCaptureUtil::GetOutputEncodingInfo(RivermaxOutput->PixelFormat, InDesiredSize, BytesPerElement, ElementsPerRow);
	return FRDGBufferDesc::CreateStructuredDesc(BytesPerElement, ElementsPerRow * InDesiredSize.Y);;
}

void URivermaxMediaCapture::OnCustomCapture_RenderingThread(FRDGBuilder& GraphBuilder, const FCaptureBaseData& InBaseData, TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData, FRDGTextureRef InSourceTexture, FRDGBufferRef OutputBuffer, const FRHICopyTextureInfo& CopyInfo, FVector2D CropU, FVector2D CropV)
{
	using namespace UE::RivermaxShaders;
	URivermaxMediaOutput* RivermaxOutput = CastChecked<URivermaxMediaOutput>(MediaOutput);

	FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);

	// Rectangle area to use from source. This is used when source render target is bigger than output resolution
	const FIntRect ViewRect(CopyInfo.GetSourceRect());
	constexpr bool bDoLinearToSRGB = false;
	const FIntPoint SourceSize = { InSourceTexture->Desc.Extent.X, InSourceTexture->Desc.Extent.Y };
	const FIntVector GroupCount = FComputeShaderUtils::GetGroupCount(DesiredOutputSize, FComputeShaderUtils::kGolden2DGroupSize);
	
	switch (RivermaxOutput->PixelFormat)
	{
	case ERivermaxMediaOutputPixelFormat::PF_10BIT_YUV422:
	{
		TShaderMapRef<FRGBToYUV10Bit422LittleEndianCS> ComputeShader(GlobalShaderMap);
		FRGBToYUV10Bit422LittleEndianCS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InSourceTexture, SourceSize, ViewRect, DesiredOutputSize, MediaShaders::RgbToYuvRec709Scaled, MediaShaders::YUVOffset10bits, bDoLinearToSRGB, OutputBuffer);

		FComputeShaderUtils::AddPass(
			GraphBuilder
			, RDG_EVENT_NAME("RGBAToYUV10Bit422LE")
			, ComputeShader
			, Parameters
			, GroupCount);
	
		break;
	}
	case ERivermaxMediaOutputPixelFormat::PF_8BIT_RGB:
	{
		TShaderMapRef<FRGBToRGB8BitCS> ComputeShader(GlobalShaderMap);
		FRGBToRGB8BitCS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InSourceTexture, SourceSize, ViewRect, DesiredOutputSize, OutputBuffer);

		FComputeShaderUtils::AddPass(
			GraphBuilder
			, RDG_EVENT_NAME("RGBAToRGB8Bit")
			, ComputeShader
			, Parameters
			, GroupCount);
		
		break;
	}
	case ERivermaxMediaOutputPixelFormat::PF_10BIT_RGB:
	{
		TShaderMapRef<FRGBToRGB10BitCS> ComputeShader(GlobalShaderMap);
		FRGBToRGB10BitCS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InSourceTexture, SourceSize, ViewRect, DesiredOutputSize, OutputBuffer);

		FComputeShaderUtils::AddPass(
			GraphBuilder
			, RDG_EVENT_NAME("RGBAToRGB10Bit")
			, ComputeShader
			, Parameters
			, GroupCount);

		break;
	}
	case ERivermaxMediaOutputPixelFormat::PF_FLOAT16_RGB:
	{
		TShaderMapRef<FRGBToRGB16fCS> ComputeShader(GlobalShaderMap);
		FRGBToRGB16fCS::FParameters* Parameters = ComputeShader->AllocateAndSetParameters(GraphBuilder, InSourceTexture, SourceSize, ViewRect, DesiredOutputSize, OutputBuffer);

		FComputeShaderUtils::AddPass(
			GraphBuilder
			, RDG_EVENT_NAME("RGBAToRGB16f")
			, ComputeShader
			, Parameters
			, GroupCount);

		break;
	}
	}
}

void URivermaxMediaCapture::OnInitializationCompleted(bool bHasSucceed)
{
	if (GetState() != EMediaCaptureState::Stopped)
	{
		SetState(bHasSucceed ? EMediaCaptureState::Capturing : EMediaCaptureState::Error);
	}
}

void URivermaxMediaCapture::OnStreamError()
{
	UE_LOG(LogRivermaxMedia, Error, TEXT("Outputstream has caught an error. Stopping capture."));
	if (GetState() != EMediaCaptureState::Stopped)
	{
		SetState(EMediaCaptureState::Error);
	}
}
