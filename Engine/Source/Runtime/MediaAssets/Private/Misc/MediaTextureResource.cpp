// Copyright Epic Games, Inc. All Rights Reserved.

#include "Misc/MediaTextureResource.h"
#include "MediaAssetsPrivate.h"

#include "DeviceProfiles/DeviceProfile.h"
#include "DeviceProfiles/DeviceProfileManager.h"
#include "ExternalTexture.h"
#include "IMediaPlayer.h"
#include "IMediaSamples.h"
#include "IMediaTextureSample.h"
#include "IMediaTextureSampleConverter.h"
#include "MediaDelegates.h"
#include "MediaPlayerFacade.h"
#include "MediaSampleSource.h"
#include "MediaShaders.h"
#include "PipelineStateCache.h"
#include "SceneUtils.h"
#include "Shader.h"
#include "StaticBoundShaderState.h"
#include "RenderUtils.h"
#include "RHIStaticStates.h"
#include "GenerateMips.h"
#include "ProfilingDebugging/CsvProfiler.h"

#include "MediaTexture.h"

#if PLATFORM_ANDROID
# include "Android/AndroidPlatformMisc.h"
#endif

#define MEDIATEXTURERESOURCE_TRACE_RENDER 0

/** Time spent in media player facade closing media. */
DECLARE_CYCLE_STAT(TEXT("MediaAssets MediaTextureResource Render"), STAT_MediaAssets_MediaTextureResourceRender, STATGROUP_Media);

/** Sample time of texture last rendered. */
DECLARE_FLOAT_COUNTER_STAT(TEXT("MediaAssets MediaTextureResource Sample"), STAT_MediaUtils_TextureSampleTime, STATGROUP_Media);

CSV_DECLARE_CATEGORY_MODULE_EXTERN(MEDIA_API, MediaStreaming);

DECLARE_GPU_STAT_NAMED(MediaTextureResource, TEXT("MediaTextureResource"));

/* Local helpers
 *****************************************************************************/

namespace MediaTextureResourceHelpers
{
	/**
	 * Get the pixel format for a given sample.
	 *
	 * @param Sample The sample.
	 * @return The sample's pixel format.
	 */
	EPixelFormat GetPixelFormat(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample)
	{
		switch (Sample->GetFormat())
		{
		case EMediaTextureSampleFormat::CharAYUV:
		case EMediaTextureSampleFormat::CharBGRA:
		case EMediaTextureSampleFormat::CharBMP:
		case EMediaTextureSampleFormat::CharUYVY:
		case EMediaTextureSampleFormat::CharYUY2:
		case EMediaTextureSampleFormat::CharYVYU:
			return PF_B8G8R8A8;

		case EMediaTextureSampleFormat::CharNV12:
		case EMediaTextureSampleFormat::CharNV21:
			return PF_G8;

		case EMediaTextureSampleFormat::FloatRGB:
			return PF_FloatRGB;

		case EMediaTextureSampleFormat::FloatRGBA:
			return PF_FloatRGBA;

		case EMediaTextureSampleFormat::CharBGR10A2:
			return PF_A2B10G10R10;

		case EMediaTextureSampleFormat::YUVv210:
			return PF_R32G32B32A32_UINT;

		case EMediaTextureSampleFormat::Y416:
			return PF_A16B16G16R16;

		default:
			return PF_Unknown;
		}
	}


	EPixelFormat GetConvertedPixelFormat(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample)
	{
		switch (Sample->GetFormat())
		{
		case EMediaTextureSampleFormat::CharBGR10A2:
		case EMediaTextureSampleFormat::YUVv210:
			return PF_A2B10G10R10;
		case EMediaTextureSampleFormat::Y416:
			return PF_B8G8R8A8;
		default:
			return PF_B8G8R8A8;
		}
	}


	/**
	 * Check whether the given sample requires a conversion shader.
	 *
	 * @param Sample The sample to check.
	 * @param SrgbOutput Whether the output is expected in sRGB color space.
	 * @return true if conversion is required, false otherwise.
	 * @see RequiresOutputResource
	 */
	bool RequiresConversion(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, bool SrgbOutput)
	{
		// If the output color space is expected to be sRGB, but the
		// sample is not, a color space conversion on the GPU is required.

		if (Sample->IsOutputSrgb() != SrgbOutput)
		{
			return true;
		}

		// If the output dimensions are not the same as the sample's
		// dimensions, a resizing conversion on the GPU is required.

		if (Sample->GetDim() != Sample->GetOutputDim())
		{
			return true;
		}

		// Only the following pixel formats are supported natively.
		// All other formats require a conversion on the GPU.

		const EMediaTextureSampleFormat Format = Sample->GetFormat();

		return ((Format != EMediaTextureSampleFormat::CharBGRA) &&
				(Format != EMediaTextureSampleFormat::FloatRGB) &&
				(Format != EMediaTextureSampleFormat::FloatRGBA));
	}


	/**
	 * Check whether the given sample requires an sRGB texture.
	 *
	 * @param Sample The sample to check.
	 * @return true if an sRGB texture is required, false otherwise.
	 */
	bool RequiresSrgbTexture(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample)
	{
		if (!Sample->IsOutputSrgb())
		{
			return false;
		}

		const EMediaTextureSampleFormat Format = Sample->GetFormat();

		return ((Format == EMediaTextureSampleFormat::CharBGRA) ||
				(Format == EMediaTextureSampleFormat::CharBMP) ||
				(Format == EMediaTextureSampleFormat::FloatRGB) ||
				(Format == EMediaTextureSampleFormat::FloatRGBA));
	}
}


/* FMediaTextureResource structors
 *****************************************************************************/

FMediaTextureResource::FMediaTextureResource(UMediaTexture& InOwner, FIntPoint& InOwnerDim, SIZE_T& InOwnerSize, FLinearColor InClearColor, FGuid InTextureGuid, bool InEnableGenMips, uint8 InNumMips)
	: Cleared(false)
	, CurrentClearColor(InClearColor)
	, InitialTextureGuid(InTextureGuid)
	, Owner(InOwner)
	, OwnerDim(InOwnerDim)
	, OwnerSize(InOwnerSize)
	, bEnableGenMips(InEnableGenMips)
	, CurrentNumMips(InEnableGenMips ? InNumMips : 1)
	, CurrentSamplerFilter(ESamplerFilter_Num)
{
#if PLATFORM_ANDROID
	bUsesImageExternal = (!FAndroidMisc::ShouldUseVulkan() && GSupportsImageExternal);
#else
	bUsesImageExternal = GSupportsImageExternal;
#endif
}


/* FMediaTextureResource interface
 *****************************************************************************/

void FMediaTextureResource::Render(const FRenderParams& Params)
{
	check(IsInRenderingThread());

	LLM_SCOPE(ELLMTag::MediaStreaming);
	SCOPE_CYCLE_COUNTER(STAT_MediaAssets_MediaTextureResourceRender);
	CSV_SCOPED_TIMING_STAT(MediaStreaming, FMediaTextureResource_Render);

	FLinearColor Rotation(1, 0, 0, 1);
	FLinearColor Offset(0, 0, 0, 0);

	TSharedPtr<FMediaTextureSampleSource, ESPMode::ThreadSafe> SampleSource = Params.SampleSource.Pin();

	if (SampleSource.IsValid())
	{
		// get the most current sample to be rendered
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> TestSample;
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;
		bool UseSample = false;
		
		while (SampleSource->Peek(TestSample) && TestSample.IsValid())
		{
			const FTimespan StartTime = TestSample->GetTime();
			const FTimespan EndTime = StartTime + TestSample->GetDuration();

			if ((Params.Rate >= 0.0f) && (Params.Time < StartTime))
			{
				break; // future sample (forward play)
			}

			if ((Params.Rate <= 0.0f) && (Params.Time >= EndTime))
			{
				break; // future sample (reverse play)
			}

#if UE_MEDIAUTILS_DEVELOPMENT_DELEGATE
			if (UseSample && Sample.IsValid())
			{
				FMediaDelegates::OnSampleDiscarded_RenderThread.Broadcast(&Owner, Sample);
			}
#endif

			UseSample = SampleSource->Dequeue(Sample);

#if MEDIATEXTURERESOURCE_TRACE_RENDER
			if (!UseSample && Sample.IsValid())
			{
				UE_LOG(LogMediaAssets, VeryVerbose, TEXT("TextureResource %p: Sample with time %s got flushed at time %s"),
					this,
					*Sample->GetTime().ToString(TEXT("%h:%m:%s.%t")),
					*Params.Time.ToString(TEXT("%h:%m:%s.%t"))
				);
			}
#endif
		}

#if UE_MEDIAUTILS_DEVELOPMENT_DELEGATE
		FMediaDelegates::OnPreSampleRender_RenderThread.Broadcast(&Owner, UseSample, Sample);
#endif
		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
		// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

		const uint8 NumMips = bEnableGenMips ? Params.NumMips : 1;

		// If real "external texture" support is in place and no mips are used the image will "bypass" any of this processing via the GUID-based lookup for "ExternalTextures" and will
		// reach the reading material shader without any processing here...
		if (UseSample && !(bUsesImageExternal && !bEnableGenMips))
		{
			//
			// Valid sample & Sample should be shown
			//

			if (Sample->GetOutputDim().GetMin() <= 0)
			{
				//
				// Sample dimensions are invalid
				//
				ClearTexture(FLinearColor::Red, Params.SrgbOutput); // mark corrupt sample
			}
			else if (Sample->GetMediaTextureSampleConverter())
			{
				//
				// Sample brings its own converter
				//
				CreateOutputRenderTarget(Sample->GetOutputDim(), MediaTextureResourceHelpers::GetConvertedPixelFormat(Sample), Params.SrgbOutput, Params.ClearColor, NumMips);
				Sample->GetMediaTextureSampleConverter()->Convert(RenderTargetTextureRHI);
				Cleared = false;
			}
			else if (MediaTextureResourceHelpers::RequiresConversion(Sample, Params.SrgbOutput))
			{
				//
				// Sample needs to be converted by built in converter code
				//
				ConvertSample(Sample, Params.ClearColor, Params.SrgbOutput, NumMips);
			}
			else
			{
				//
				// Sample can be used directly or is a simple copy
				//
				CopySample(Sample, Params.ClearColor, Params.SrgbOutput, NumMips, Params.CurrentGuid);
			}

			Rotation = Sample->GetScaleRotation();
			Offset = Sample->GetOffset();

			// Keep new current sample around to avoid it returning to any pool too soon
			// (we hold on to rendering resources at a lower level anyway)
			CurrentSample = TRefCountPtr<FTextureSampleKeeper>(new FTextureSampleKeeper(Sample));
			check(CurrentSample);

			// Generate mips as needed
			if (CurrentNumMips > 1 && !Cleared)
			{
				check(OutputTarget);

				FMemMark MemMark(FMemStack::Get());
				FRHICommandListImmediate & RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();
#if PLATFORM_ANDROID
				// Vulkan does not implement RWBarrier & ES does not do anything specific anyways
				RHICmdList.TransitionResource(EResourceTransitionAccess::EWritable, OutputTarget);
#else
				RHICmdList.TransitionResource(EResourceTransitionAccess::ERWBarrier, OutputTarget);
#endif
				FGenerateMips::Execute(RHICmdList, OutputTarget, CachedMipsGenParams, FGenerateMipsParams{ SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp }, true);
				RHICmdList.TransitionResource(EResourceTransitionAccess::EReadable, OutputTarget);
			}

			SET_FLOAT_STAT(STAT_MediaUtils_TextureSampleTime, Sample->GetTime().GetTotalMilliseconds());
		}
		else
		{
			//
			// Last sample is still valid
			//

			// Output is using internal buffer?
			if (OutputTarget == RenderTargetTextureRHI)
			{
				/*
					We know that the current sample is using the internal buffer to store converted data.
					This data is the data actually used by any user of this resource. Hence we can release
					the current sample as soon as the first frame using it is done processing.
					Either this will happen when a new sample arrives or we do it explicitly here.
				*/
				CurrentSample = nullptr;
			}
		}
	}
	else if (Params.CanClear)
	{
		//
		// No valid sample source & we should clear
		//

		// Need to clear the output?
		if (!Cleared || (Params.ClearColor != CurrentClearColor))
		{
			// Yes...
			ClearTexture(Params.ClearColor, Params.SrgbOutput);
			// Also get rid of any sample from previous rendering...
			CurrentSample = nullptr;
		}
	}

	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
	// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -

	// Cache next available sample time in the MediaTexture owner since we're the only one that can consume from the queue
	CacheNextAvailableSampleTime(SampleSource);

	// Update external texture registration in case we have no native support
	// (in that case there is support, the player will do this - but it is used all the time)
	if (!bUsesImageExternal)
	{
		SetupSampler();

		if (Params.CurrentGuid.IsValid())
		{
			FTextureRHIRef VideoTexture = (FTextureRHIRef)Owner.TextureReference.TextureReferenceRHI;
			FExternalTextureRegistry::Get().RegisterExternalTexture(Params.CurrentGuid, VideoTexture, SamplerStateRHI, Rotation, Offset);
		}

		if (Params.PreviousGuid.IsValid() && (Params.PreviousGuid != Params.CurrentGuid))
		{
			FExternalTextureRegistry::Get().UnregisterExternalTexture(Params.PreviousGuid);
		}
	}
	
	// Update usable Guid for the RenderThread
	Owner.SetRenderedExternalTextureGuid(Params.CurrentGuid);
}


/* FRenderTarget interface
 *****************************************************************************/

FIntPoint FMediaTextureResource::GetSizeXY() const
{
	return FIntPoint(Owner.GetWidth(), Owner.GetHeight());
}


/* FTextureResource interface
 *****************************************************************************/

FString FMediaTextureResource::GetFriendlyName() const
{
	return Owner.GetPathName();
}


uint32 FMediaTextureResource::GetSizeX() const
{
	return Owner.GetWidth();
}


uint32 FMediaTextureResource::GetSizeY() const
{
	return Owner.GetHeight();
}


void FMediaTextureResource::SetupSampler()
{
	ESamplerFilter OwnerFilter = bEnableGenMips ? (ESamplerFilter)UDeviceProfileManager::Get().GetActiveProfile()->GetTextureLODSettings()->GetSamplerFilter(&Owner) : SF_Bilinear;

	if (CurrentSamplerFilter != OwnerFilter)
	{
		CurrentSamplerFilter = OwnerFilter;

		// create the sampler state
		FSamplerStateInitializerRHI SamplerStateInitializer(
			CurrentSamplerFilter,
			(Owner.AddressX == TA_Wrap) ? AM_Wrap : ((Owner.AddressX == TA_Clamp) ? AM_Clamp : AM_Mirror),
			(Owner.AddressY == TA_Wrap) ? AM_Wrap : ((Owner.AddressY == TA_Clamp) ? AM_Clamp : AM_Mirror),
			AM_Wrap
		);

		SamplerStateRHI = RHICreateSamplerState(SamplerStateInitializer);
	}
}


void FMediaTextureResource::InitDynamicRHI()
{
	SetupSampler();
	
	// Note: set up default texture, or we can get sampler bind errors on render
	// we can't leave here without having a valid bindable resource for some RHIs.

	ClearTexture(CurrentClearColor, Owner.SRGB);

	// Make sure init has done it's job - we can't leave here without valid bindable resources for some RHI's
	check(TextureRHI.IsValid());
	check(RenderTargetTextureRHI.IsValid());
	check(OutputTarget.IsValid());

	// Register "external texture" parameters if the platform does not support them (and hence the player does not set them)
	if (!bUsesImageExternal)
	{
		FTextureRHIRef VideoTexture = (FTextureRHIRef)Owner.TextureReference.TextureReferenceRHI;
		FExternalTextureRegistry::Get().RegisterExternalTexture(InitialTextureGuid, VideoTexture, SamplerStateRHI);
	}
}


void FMediaTextureResource::ReleaseDynamicRHI()
{
	Cleared = false;

	CachedMipsGenParams.Reset();

	InputTarget.SafeRelease();
	OutputTarget.SafeRelease();
	RenderTargetTextureRHI.SafeRelease();
	TextureRHI.SafeRelease();

	UpdateTextureReference(nullptr);
}


/* FMediaTextureResource implementation
 *****************************************************************************/

void FMediaTextureResource::ClearTexture(const FLinearColor& ClearColor, bool SrgbOutput)
{
	// create output render target if we don't have one yet
	CreateOutputRenderTarget(FIntPoint(2, 2), PF_B8G8R8A8, SrgbOutput, ClearColor, 1);

	// draw the clear color
	FRHICommandListImmediate& CommandList = FRHICommandListExecutor::GetImmediateCommandList();
	{
		SCOPED_DRAW_EVENT(CommandList, FMediaTextureResource_ClearTexture);
		SCOPED_GPU_STAT(CommandList, MediaTextureResource);

		FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::Clear_Store);
		CommandList.BeginRenderPass(RPInfo, TEXT("ClearTexture"));
		CommandList.EndRenderPass();
		CommandList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTargetTextureRHI);
	}

	Cleared = true;
}


void FMediaTextureResource::ConvertSample(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FLinearColor& ClearColor, bool SrgbOutput, uint8 InNumMips)
{
	const EPixelFormat InputPixelFormat = MediaTextureResourceHelpers::GetPixelFormat(Sample);

	// get input texture
	FRHITexture2D* InputTexture = nullptr;
	{
		// If the sample already provides a texture resource, we simply use that
		// as the input texture. If the sample only provides raw data, then we
		// create our own input render target and copy the data into it.

		FRHITexture* SampleTexture = Sample->GetTexture();
		FRHITexture2D* SampleTexture2D = (SampleTexture != nullptr) ? SampleTexture->GetTexture2D() : nullptr;

		if (SampleTexture2D != nullptr)
		{
			// Use the sample as source texture...

			InputTexture = SampleTexture2D;

			InputTarget.SafeRelease();
			UpdateResourceSize();
		}
		else
		{
			// Make a source texture so we can convert from it...

			const bool SrgbTexture = MediaTextureResourceHelpers::RequiresSrgbTexture(Sample);
			const uint32 InputCreateFlags = TexCreate_Dynamic | (SrgbTexture ? TexCreate_SRGB : 0);
			const FIntPoint SampleDim = Sample->GetDim();

			// create a new temp input render target if necessary
			if (!InputTarget.IsValid() || (InputTarget->GetSizeXY() != SampleDim) || (InputTarget->GetFormat() != InputPixelFormat) || ((InputTarget->GetFlags() & InputCreateFlags) != InputCreateFlags))
			{
				TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;
				FRHIResourceCreateInfo CreateInfo;

				RHICreateTargetableShaderResource2D(
					SampleDim.X,
					SampleDim.Y,
					InputPixelFormat,
					1,
					InputCreateFlags,
					TexCreate_RenderTargetable,
					false,
					CreateInfo,
					InputTarget,
					DummyTexture2DRHI
				);

				UpdateResourceSize();
			}

			// copy sample data to input render target
			FUpdateTextureRegion2D Region(0, 0, 0, 0, SampleDim.X, SampleDim.Y);
			RHIUpdateTexture2D(InputTarget, 0, Region, Sample->GetStride(), (uint8*)Sample->GetBuffer());

			InputTexture = InputTarget;
		}
	}

	// create the output texture
	const FIntPoint OutputDim = Sample->GetOutputDim();
	CreateOutputRenderTarget(OutputDim, MediaTextureResourceHelpers::GetConvertedPixelFormat(Sample), SrgbOutput, ClearColor, InNumMips);

	// perform the conversion
	FRHICommandListImmediate& CommandList = FRHICommandListExecutor::GetImmediateCommandList();
	{
		SCOPED_DRAW_EVENT(CommandList, FMediaTextureResource_Convert);
		SCOPED_GPU_STAT(CommandList, MediaTextureResource);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		FRHITexture* RenderTarget = RenderTargetTextureRHI.GetReference();

		FRHIRenderPassInfo RPInfo(RenderTarget, ERenderTargetActions::DontLoad_Store);
		CommandList.BeginRenderPass(RPInfo, TEXT("ConvertMedia"));
		{
			CommandList.ApplyCachedRenderTargets(GraphicsPSOInit);
			CommandList.SetViewport(0, 0, 0.0f, OutputDim.X, OutputDim.Y, 1.0f);

			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

			// configure media shaders
			auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FMediaShadersVS> VertexShader(ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);

			FMatrix YUVToRGBMatrix = Sample->GetYUVToRGBMatrix();
			FVector YUVOffset(MediaShaders::YUVOffset8bits);

			if (Sample->GetFormat() == EMediaTextureSampleFormat::YUVv210)
			{
				YUVOffset = MediaShaders::YUVOffset10bits;
			}

			bool bIsSampleOutputSrgb = Sample->IsOutputSrgb();
			if (GMaxRHIFeatureLevel == ERHIFeatureLevel::ES2 && IsSimulatedPlatform(GMaxRHIShaderPlatform) )
			{
				// simulated ES2 has no HW support for sRGB, all external textures are assumed to be in sRGB form. 
				// see FHLSLMaterialTranslator::TextureSample(), SAMPLERTYPE_External case.
				// We must not convert to linear for the ES2 case.
				bIsSampleOutputSrgb = false;
			}

			switch (Sample->GetFormat())
			{
			case EMediaTextureSampleFormat::CharAYUV:
			{
				TShaderMapRef<FAYUVConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharBMP:
			{
				TShaderMapRef<FBMPConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, bIsSampleOutputSrgb && !SrgbOutput);
			}
			break;

			case EMediaTextureSampleFormat::CharNV12:
			{
				TShaderMapRef<FNV12ConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharNV21:
			{
				TShaderMapRef<FNV21ConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharUYVY:
			{
				TShaderMapRef<FUYVYConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharYUY2:
			{
				TShaderMapRef<FYUY2ConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharYVYU:
			{
				TShaderMapRef<FYVYUConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::YUVv210:
			{
				TShaderMapRef<FYUVv210ConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, YUVToRGBMatrix, YUVOffset, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharBGR10A2:
			{
				TShaderMapRef<FRGBConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, bIsSampleOutputSrgb);
			}
			break;

			case EMediaTextureSampleFormat::CharBGRA:
			case EMediaTextureSampleFormat::FloatRGB:
			case EMediaTextureSampleFormat::FloatRGBA:
			{
				TShaderMapRef<FRGBConvertPS> ConvertShader(ShaderMap);
				GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*ConvertShader);
				SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
				ConvertShader->SetParameters(CommandList, InputTexture, OutputDim, false);
			}
			break;

			default:
				return; // unsupported format
			}

			// draw full size quad into render target
			FVertexBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer();
			CommandList.SetStreamSource(0, VertexBuffer, 0);
			// set viewport to RT size
			CommandList.SetViewport(0, 0, 0.0f, OutputDim.X, OutputDim.Y, 1.0f);

			CommandList.DrawPrimitive(0, 2, 1);
		}
		CommandList.EndRenderPass();
		CommandList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTargetTextureRHI);
	}

	Cleared = false;
}


void FMediaTextureResource::CopySample(const TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FLinearColor& ClearColor, bool SrgbOutput, uint8 InNumMips, const FGuid & TextureGUID)
{
	FRHITexture* SampleTexture = Sample->GetTexture();
	FRHITexture2D* SampleTexture2D = (SampleTexture != nullptr) ? SampleTexture->GetTexture2D() : nullptr;

	// If the sample already provides a texture resource, we simply use that
	// as the output render target. If the sample only provides raw data, then
	// we create our own output render target and copy the data into it.

	if (SampleTexture2D != nullptr)
	{
		// Use sample's texture as the new render target - no copy
		if (TextureRHI != SampleTexture2D)
		{
			UpdateTextureReference(SampleTexture2D);

			CachedMipsGenParams.Reset();
			OutputTarget.SafeRelease();
		}
		else
		{
			// Texture to receive texture from sample
			CreateOutputRenderTarget(Sample->GetOutputDim(), MediaTextureResourceHelpers::GetPixelFormat(Sample), SrgbOutput, ClearColor, InNumMips);

			// Copy data into the output texture to able to add mips later on
			FRHICommandListExecutor::GetImmediateCommandList().CopyTexture(SampleTexture2D, OutputTarget, FRHICopyTextureInfo());
		}
	}
	else
	{
		// Texture to receive precisely only output pixels via CPU copy
		CreateOutputRenderTarget(Sample->GetDim(), MediaTextureResourceHelpers::GetPixelFormat(Sample), SrgbOutput, ClearColor, InNumMips);

		if (bEnableGenMips && bUsesImageExternal && !Sample->GetBuffer())
		{
			// We are using an external texture to read from
			CopyFromExternalTexture(Sample, TextureGUID);
		}
		else
		{
			// Copy sample data (from CPU mem) to output render target
			FUpdateTextureRegion2D Region(0, 0, 0, 0, Sample->GetDim().X, Sample->GetDim().Y);
			RHIUpdateTexture2D(RenderTargetTextureRHI.GetReference(), 0, Region, Sample->GetStride(), (uint8*)Sample->GetBuffer());
		}
	}

	Cleared = false;
}


void FMediaTextureResource::CopyFromExternalTexture(const TSharedPtr <IMediaTextureSample, ESPMode::ThreadSafe>& Sample, const FGuid & TextureGUID)
{
	FRHICommandListImmediate& CommandList = FRHICommandListExecutor::GetImmediateCommandList();
	{
		FTextureRHIRef SampleTexture;
		FSamplerStateRHIRef SamplerState;
		if (!FExternalTextureRegistry::Get().GetExternalTexture(nullptr, TextureGUID, SampleTexture, SamplerState))
		{
			// This should never happen: we could not find the external texture data. Still, if it does we clear the output...
			FRHIRenderPassInfo RPInfo(RenderTargetTextureRHI, ERenderTargetActions::Clear_Store);
			CommandList.BeginRenderPass(RPInfo, TEXT("ClearTexture"));
			CommandList.EndRenderPass();
			CommandList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTargetTextureRHI);
			return;
		}

		FLinearColor Offset, ScaleRotation;
		FExternalTextureRegistry::Get().GetExternalTextureCoordinateOffset(TextureGUID, Offset);
		FExternalTextureRegistry::Get().GetExternalTextureCoordinateScaleRotation(TextureGUID, ScaleRotation);

		SCOPED_DRAW_EVENT(CommandList, FMediaTextureResource_ConvertExternalTexture);
		SCOPED_GPU_STAT(CommandList, MediaTextureResource);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		FRHITexture* RenderTarget = RenderTargetTextureRHI.GetReference();

		FRHIRenderPassInfo RPInfo(RenderTarget, ERenderTargetActions::DontLoad_Store);
		CommandList.BeginRenderPass(RPInfo, TEXT("ConvertMedia_ExternalTexture"));
		{
			const FIntPoint OutputDim = Sample->GetOutputDim();

			CommandList.ApplyCachedRenderTargets(GraphicsPSOInit);
			CommandList.SetViewport(0, 0, 0.0f, OutputDim.X, OutputDim.Y, 1.0f);
			
			GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
			GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
			GraphicsPSOInit.BlendState = TStaticBlendStateWriteMask<CW_RGBA, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE, CW_NONE>::GetRHI();
			GraphicsPSOInit.PrimitiveType = PT_TriangleStrip;

			// configure media shaders
			auto ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
			TShaderMapRef<FMediaShadersVS> VertexShader(ShaderMap);

			GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GMediaVertexDeclaration.VertexDeclarationRHI;
			GraphicsPSOInit.BoundShaderState.VertexShaderRHI = GETSAFERHISHADER_VERTEX(*VertexShader);

			TShaderMapRef<FReadTextureExternalPS> CopyShader(ShaderMap);
			GraphicsPSOInit.BoundShaderState.PixelShaderRHI = GETSAFERHISHADER_PIXEL(*CopyShader);
			SetGraphicsPipelineState(CommandList, GraphicsPSOInit);
			CopyShader->SetParameters(CommandList, SampleTexture, SamplerState, ScaleRotation, Offset);

			// draw full size quad into render target
			FVertexBufferRHIRef VertexBuffer = CreateTempMediaVertexBuffer();
			CommandList.SetStreamSource(0, VertexBuffer, 0);
			// set viewport to RT size
			CommandList.SetViewport(0, 0, 0.0f, OutputDim.X, OutputDim.Y, 1.0f);

			CommandList.DrawPrimitive(0, 2, 1);
		}
		CommandList.EndRenderPass();
		CommandList.TransitionResource(EResourceTransitionAccess::EReadable, RenderTargetTextureRHI);
	}
}


void FMediaTextureResource::UpdateResourceSize()
{
	SIZE_T ResourceSize = 0;

	if (InputTarget.IsValid())
	{
		ResourceSize += CalcTextureSize(InputTarget->GetSizeX(), InputTarget->GetSizeY(), InputTarget->GetFormat(), 1);
	}

	if (OutputTarget.IsValid())
	{
		ResourceSize += CalcTextureSize(OutputTarget->GetSizeX(), OutputTarget->GetSizeY(), OutputTarget->GetFormat(), 1);
	}

	OwnerSize = ResourceSize;
}


void FMediaTextureResource::UpdateTextureReference(FRHITexture2D* NewTexture)
{
	TextureRHI = NewTexture;
	RenderTargetTextureRHI = NewTexture;

	RHIUpdateTextureReference(Owner.TextureReference.TextureReferenceRHI, NewTexture);

	if (RenderTargetTextureRHI != nullptr)
	{
		OwnerDim = FIntPoint(RenderTargetTextureRHI->GetSizeX(), RenderTargetTextureRHI->GetSizeY());
	}
	else
	{
		OwnerDim = FIntPoint::ZeroValue;
	}
}


void FMediaTextureResource::CreateOutputRenderTarget(const FIntPoint & InDim, EPixelFormat InPixelFormat, bool bInSRGB, const FLinearColor & InClearColor, uint8 InNumMips)
{
	// create output render target if necessary
	uint32 OutputCreateFlags = TexCreate_Dynamic | (bInSRGB ? TexCreate_SRGB : 0);
	if (InNumMips > 1)
	{
		// Make sure can have mips & the mip generator has what it needs to work
		OutputCreateFlags |= (TexCreate_GenerateMipCapable | TexCreate_UAV);

		// Make sure we only set a number of mips that actually makes sense, given the sample size
		uint8 MaxMips = FGenericPlatformMath::FloorToInt(FGenericPlatformMath::Log2(FGenericPlatformMath::Max(InDim.X, InDim.Y)));
		InNumMips = FMath::Min(InNumMips, MaxMips);
	}

	if ((InClearColor != CurrentClearColor) || !OutputTarget.IsValid() || (OutputTarget->GetSizeXY() != InDim) || (OutputTarget->GetFormat() != InPixelFormat) || ((OutputTarget->GetFlags() & OutputCreateFlags) != OutputCreateFlags) || CurrentNumMips != InNumMips)
	{
		TRefCountPtr<FRHITexture2D> DummyTexture2DRHI;

		CachedMipsGenParams.Reset();

		FRHIResourceCreateInfo CreateInfo = {
			FClearValueBinding(InClearColor)
		};

		RHICreateTargetableShaderResource2D(
			InDim.X,
			InDim.Y,
			InPixelFormat,
			InNumMips,
			OutputCreateFlags,
			TexCreate_RenderTargetable,
			false,
			CreateInfo,
			OutputTarget,
			DummyTexture2DRHI
		);

		CurrentClearColor = InClearColor;
		CurrentNumMips = InNumMips;
		UpdateResourceSize();

		Cleared = false;
	}

	if (RenderTargetTextureRHI != OutputTarget)
	{
		UpdateTextureReference(OutputTarget);
	}
}


void FMediaTextureResource::CacheNextAvailableSampleTime(const TSharedPtr<FMediaTextureSampleSource, ESPMode::ThreadSafe>& InSampleQueue) const
{
	FTimespan SampleTime(FTimespan::MinValue());

	if (InSampleQueue.IsValid())
	{
		TSharedPtr<IMediaTextureSample, ESPMode::ThreadSafe> Sample;
		if (InSampleQueue->Peek(Sample))
		{
			SampleTime = Sample->GetTime();
		}
	}

	Owner.CacheNextAvailableSampleTime(SampleTime);
}
