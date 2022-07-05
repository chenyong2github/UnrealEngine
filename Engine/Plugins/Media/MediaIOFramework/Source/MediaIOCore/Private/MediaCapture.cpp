// Copyright Epic Games, Inc. All Rights Reserved.

#include "MediaCapture.h"


#include "Application/ThrottleManager.h"
#include "Async/Async.h"
#include "Engine/GameEngine.h"
#include "Engine/RendererSettings.h"
#include "Engine/TextureRenderTarget2D.h"
#include "EngineModule.h"
#include "MediaIOCoreModule.h"
#include "MediaOutput.h"
#include "MediaShaders.h"
#include "Misc/App.h"
#include "Misc/ScopeLock.h"
#include "PipelineStateCache.h"
#include "RendererInterface.h"
#include "RenderGraphResources.h"
#include "RenderGraphUtils.h"
#include "RenderUtils.h"
#include "RenderTargetPool.h"
#include "RHIStaticStates.h"
#include "SceneUtils.h"
#include "ScreenPass.h"
#include "Slate/SceneViewport.h"
#include "UObject/WeakObjectPtrTemplates.h"

#if WITH_EDITOR
#include "AnalyticsEventAttribute.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "EngineAnalytics.h"
#include "Framework/Notifications/NotificationManager.h"
#include "IAssetViewport.h"
#include "LevelEditor.h"
#include "Widgets/Notifications/SNotificationList.h"
#endif //WITH_EDITOR

#define LOCTEXT_NAMESPACE "MediaCapture"


/** Time spent in media capture sending a frame. */
DECLARE_CYCLE_STAT(TEXT("MediaCapture RenderThread FrameCapture"), STAT_MediaCapture_RenderThread_FrameCapture, STATGROUP_Media);
DECLARE_CYCLE_STAT(TEXT("MediaCapture RenderThread LockResource"), STAT_MediaCapture_RenderThread_LockResource, STATGROUP_Media);
DECLARE_CYCLE_STAT(TEXT("MediaCapture RenderThread RHI Capture Callback"), STAT_MediaCapture_RenderThread_CaptureCallback, STATGROUP_Media);

/** These pixel formats do not require additional conversion except for swizzling and normalized sampling. */
static TSet<EPixelFormat> SupportedRgbaSwizzleFormats =
{
	PF_A32B32G32R32F,
	PF_B8G8R8A8,
	PF_G8,
	PF_G16,
	PF_FloatRGB,
	PF_FloatRGBA,
	PF_R32_FLOAT,
	PF_G16R16,
	PF_G16R16F,
	PF_G32R32F,
	PF_A2B10G10R10,
	PF_A16B16G16R16,
	PF_R16F,
	PF_FloatR11G11B10,
	PF_A8,
	PF_R32_UINT,
	PF_R32_SINT,
	PF_R16_UINT,
	PF_R16_SINT,
	PF_R16G16B16A16_UINT,
	PF_R16G16B16A16_SINT,
	PF_R5G6B5_UNORM,
	PF_R8G8B8A8,
	PF_A8R8G8B8,
	PF_R8G8,
	PF_R32G32B32A32_UINT,
	PF_R16G16_UINT,
	PF_R8_UINT,
	PF_R8G8B8A8_UINT,
	PF_R8G8B8A8_SNORM,
	PF_R16G16B16A16_UNORM,
	PF_R16G16B16A16_SNORM,
	PF_R32G32_UINT,
	PF_R8,
};

/* namespace MediaCaptureDetails definition
*****************************************************************************/

namespace MediaCaptureDetails
{
	bool FindSceneViewportAndLevel(TSharedPtr<FSceneViewport>& OutSceneViewport);

	//Validation for the source of a capture
	bool ValidateSceneViewport(const TSharedPtr<FSceneViewport>& SceneViewport, const FMediaCaptureOptions& CaptureOption, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing);
	bool ValidateTextureRenderTarget2D(const UTextureRenderTarget2D* RenderTarget, const FMediaCaptureOptions& CaptureOption, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing);

	//Validation that there is a capture
	bool ValidateIsCapturing(const UMediaCapture& CaptureToBeValidated);

	void ShowSlateNotification();

	/** Returns bytes per pixel based on pixel format */
	int32 GetBytesPerPixel(EPixelFormat InPixelFormat);

	static const FName LevelEditorName(TEXT("LevelEditor"));
}

#if WITH_EDITOR
namespace MediaCaptureAnalytics
{
	/**
	 * @EventName MediaFramework.CaptureStarted
	 * @Trigger Triggered when a capture of the viewport or render target is started.
	 * @Type Client
	 * @Owner MediaIO Team
	 */
	void SendCaptureEvent(const FString& CaptureType)
	{
		if (FEngineAnalytics::IsAvailable())
		{
			TArray<FAnalyticsEventAttribute> EventAttributes;
			EventAttributes.Add(FAnalyticsEventAttribute(TEXT("CaptureType"), CaptureType));
			FEngineAnalytics::GetProvider().RecordEvent(TEXT("MediaFramework.CaptureStarted"), EventAttributes);
		}
	}
}
#endif

namespace UE::MediaCaptureData
{
	class FCaptureFrame
	{
	public:
		FCaptureFrame()
			: bReadbackRequested(false)
		{

		}
		virtual ~FCaptureFrame() {};

		/** Returns true if its output resource is valid */
		virtual bool HasValidResource() const = 0;

		/** Simple way to validate the resource type and cast safely */
		virtual bool IsTextureResource() const = 0;
		virtual bool IsBufferResource() const = 0;

		/** Locks the readback resource and returns a pointer to access data from system memory */
		virtual void* Lock(FRHICommandListImmediate& RHICmdList, FRHIGPUMask GPUMask, int32& OutRowStride) = 0;

		/** Unlocks the readback resource */
		virtual void Unlock() = 0;

		/** Returns true if the readback is ready to be used */
		virtual bool IsReadbackReady(FRHIGPUMask GPUMask) = 0;

		UMediaCapture::FCaptureBaseData CaptureBaseData;
		TAtomic<bool> bReadbackRequested;
		TSharedPtr<FMediaCaptureUserData> UserData;
	};

	class FTextureCaptureFrame : public FCaptureFrame
	{
	public:
		/** Type alias for the output resource type used during capture frame */
		using FOutputResourceType = FRDGTextureRef;

		//~ Begin FCaptureFrame interface
		virtual bool HasValidResource() const override
		{
			return RenderTarget != nullptr;
		}

		virtual bool IsTextureResource() const override
		{
			return true;
		}

		virtual bool IsBufferResource() const override
		{
			return false;
		}

		virtual void* Lock(FRHICommandListImmediate& RHICmdList, FRHIGPUMask GPUMask, int32& OutRowStride) override
		{
			if (ReadbackTexture->IsReady(GPUMask) == false)
			{
				UE_LOG(LogMediaIOCore, Verbose, TEXT("Fenced for texture readback was not ready"));
			}

			int32 ReadbackWidth;
			void* ReadbackPointer = ReadbackTexture->Lock(ReadbackWidth);
			OutRowStride = ReadbackWidth * MediaCaptureDetails::GetBytesPerPixel(RenderTarget->GetDesc().Format);
			return ReadbackPointer;
		}

		virtual void Unlock() override
		{
			ReadbackTexture->Unlock();
		}

		virtual bool IsReadbackReady(FRHIGPUMask GPUMask) override
		{
			return ReadbackTexture->IsReady(GPUMask);
		}

		//~ End FCaptureFrame interface

		/** Registers an external texture to be tracked by the graph and returns a pointer to the tracked resource */
		FRDGTextureRef RegisterResource(FRDGBuilder& RDGBuilder)
		{
			return RDGBuilder.RegisterExternalTexture(RenderTarget, TEXT("OutputTexture"));
		}

		/** Adds a readback pass to the graph */
		void EnqueueCopy(FRDGBuilder& RDGBuilder, FRDGTextureRef ResourceToReadback)
		{
			AddEnqueueCopyPass(RDGBuilder, ReadbackTexture.Get(), ResourceToReadback);
		}

		/** Returns RHI resource of the allocated pooled resource */
		FRHITexture* GetRHIResource()
		{
			return RenderTarget->GetRHI();
		}
		
	public:
		TRefCountPtr<IPooledRenderTarget> RenderTarget;
		TUniquePtr<FRHIGPUTextureReadback> ReadbackTexture;
	};

	class FBufferCaptureFrame : public FCaptureFrame
	{
	public:
		/** Type alias for the output resource type used during capture frame */
		using FOutputResourceType = FRDGBufferRef;

		//~ Begin FCaptureFrame interface
		virtual bool HasValidResource() const override
		{
			return Buffer != nullptr;
		}

		virtual bool IsTextureResource() const override
		{
			return false;
		}

		virtual bool IsBufferResource() const override
		{
			return true;
		}

		virtual void* Lock(FRHICommandListImmediate& RHICmdList, FRHIGPUMask GPUMask, int32& OutRowStride) override
		{
			if (ReadbackBuffer->IsReady(GPUMask) == false)
			{
				UE_LOG(LogMediaIOCore, Verbose, TEXT("Fenced for buffer readback was not ready, blocking."));
				RHICmdList.BlockUntilGPUIdle();
			}

			OutRowStride = Buffer->GetRHI()->GetStride();
			return ReadbackBuffer->Lock(Buffer->GetRHI()->GetSize());
		}

		virtual void Unlock() override
		{
			ReadbackBuffer->Unlock();
		}

		virtual bool IsReadbackReady(FRHIGPUMask GPUMask) override
		{
			return ReadbackBuffer->IsReady(GPUMask);
		}
		//~ End FCaptureFrame interface

		/** Registers an external texture to be tracked by the graph and returns a pointer to the tracked resource */
		FRDGBufferRef RegisterResource(FRDGBuilder& RDGBuilder)
		{
			return RDGBuilder.RegisterExternalBuffer(Buffer);
		}

		/** Adds a readback pass to the graph */
		void EnqueueCopy(FRDGBuilder& RDGBuilder, FRDGBufferRef ResourceToReadback)
		{
			AddEnqueueCopyPass(RDGBuilder, ReadbackBuffer.Get(), ResourceToReadback, Buffer->GetRHI()->GetSize());
		}

		/** Returns RHI resource of the allocated pooled resource */
		FRHIBuffer* GetRHIResource()
		{
			return Buffer->GetRHI();
		}

	public:
		TRefCountPtr<FRDGPooledBuffer> Buffer;
		TUniquePtr<FRHIGPUBufferReadback> ReadbackBuffer;
	};

	/** Helper struct to contain arguments for CaptureFrame */
	struct FCaptureFrameArgs
	{
		FRHICommandListImmediate& RHICmdList;
		TObjectPtr<UMediaCapture> MediaCapture = nullptr;
		FSceneViewport* CapturingSceneViewport = nullptr;
		FTextureRenderTargetResource* TextureRenderTargetResource = nullptr;
		FIntPoint DesiredSize = FIntPoint::ZeroValue;
	};

	/** Helper struct to contain arguments for AddConversionPass */
	struct FConversionPassArgs
	{
		FRDGBuilder& GraphBuilder;
		const FRDGTextureRef& SourceRGBTexture;
		bool bRequiresFormatConversion = false;
		FRHICopyTextureInfo CopyInfo;
		FVector2D SizeU = FVector2D::ZeroVector;
		FVector2D SizeV = FVector2D::ZeroVector;
	};

	/** Helper class to be able to friend it and call methods on input media capture */
	class FMediaCaptureHelper
	{
	public:

		static void BeforeFrameCapture(const FCaptureFrameArgs& Args, FTextureCaptureFrame* CapturingFrame)
		{
			UE_LOG(LogMediaIOCore, VeryVerbose, TEXT("Before Locking texture %p"), reinterpret_cast<uintptr_t>(CapturingFrame->RenderTarget->GetRHI()->GetNativeResource()));
			Args.MediaCapture->BeforeFrameCaptured_RenderingThread(CapturingFrame->CaptureBaseData, CapturingFrame->UserData, CapturingFrame->RenderTarget->GetRHI());
			UE_LOG(LogMediaIOCore, VeryVerbose, TEXT("After Locking texture %p"), reinterpret_cast<uintptr_t>(CapturingFrame->RenderTarget->GetRHI()->GetNativeResource()));
		}

		static void BeforeFrameCapture(const FCaptureFrameArgs& Args, FBufferCaptureFrame* CapturingFrame)
		{
			FBufferCaptureFrame* BufferFrame = static_cast<FBufferCaptureFrame*>(CapturingFrame);
			Args.MediaCapture->BeforeFrameCaptured_RenderingThread(CapturingFrame->CaptureBaseData, CapturingFrame->UserData, BufferFrame->Buffer->GetRHI());
		}

		static FTexture2DRHIRef GetSourceTextureForInput(const FCaptureFrameArgs& Args)
		{
			FTexture2DRHIRef SourceTexture;
			if (Args.CapturingSceneViewport)
			{
#if WITH_EDITOR
				if (!IsRunningGame())
				{
					// PIE, PIE in windows, editor viewport
					SourceTexture = Args.CapturingSceneViewport->GetRenderTargetTexture();
					if (!SourceTexture.IsValid() && Args.CapturingSceneViewport->GetViewportRHI())
					{
						SourceTexture = Args.RHICmdList.GetViewportBackBuffer(Args.CapturingSceneViewport->GetViewportRHI());
					}
				}
				else
#endif
					if (Args.CapturingSceneViewport->GetViewportRHI())
					{
						// Standalone and packaged
						SourceTexture = Args.RHICmdList.GetViewportBackBuffer(Args.CapturingSceneViewport->GetViewportRHI());
					}
			}
			else if (Args.TextureRenderTargetResource && Args.TextureRenderTargetResource->GetTextureRenderTarget2DResource())
			{
				SourceTexture = Args.TextureRenderTargetResource->GetTextureRenderTarget2DResource()->GetTextureRHI();
			}

			return SourceTexture;
		}

		static bool AreInputsValid(const FCaptureFrameArgs& Args, const FTexture2DRHIRef& SourceTexture)
		{
			// If it is a simple rgba swizzle we can handle the conversion. Supported formats
			// contained in SupportedRgbaSwizzleFormats. Warning would've been displayed on start of capture.
			if (Args.MediaCapture->DesiredPixelFormat != SourceTexture->GetFormat() &&
				(!SupportedRgbaSwizzleFormats.Contains(SourceTexture->GetFormat()) || !Args.MediaCapture->DesiredCaptureOptions.bConvertToDesiredPixelFormat))
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. The Source pixel format doesn't match with the user requested pixel format. %sRequested: %s Source: %s")
					, *Args.MediaCapture->MediaOutputName
					, (SupportedRgbaSwizzleFormats.Contains(SourceTexture->GetFormat()) && !Args.MediaCapture->DesiredCaptureOptions.bConvertToDesiredPixelFormat) ? TEXT("Please enable \"Convert To Desired Pixel Format\" option in Media Capture settings. ") : TEXT("")
					, GetPixelFormatString(Args.MediaCapture->DesiredPixelFormat)
					, GetPixelFormatString(SourceTexture->GetFormat()));

				return false;
			}

			if (Args.MediaCapture->DesiredCaptureOptions.Crop == EMediaCaptureCroppingType::None)
			{
				if (Args.DesiredSize.X != SourceTexture->GetSizeX() || Args.DesiredSize.Y != SourceTexture->GetSizeY())
				{
					UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. The Source size doesn't match with the user requested size. Requested: %d,%d  Source: %d,%d")
						, *Args.MediaCapture->MediaOutputName
						, Args.DesiredSize.X, Args.DesiredSize.Y
						, SourceTexture->GetSizeX(), SourceTexture->GetSizeY());

					return false;
				}
			}
			else
			{
				FIntPoint StartCapturePoint = FIntPoint::ZeroValue;
				if (Args.MediaCapture->DesiredCaptureOptions.Crop == EMediaCaptureCroppingType::Custom)
				{
					StartCapturePoint = Args.MediaCapture->DesiredCaptureOptions.CustomCapturePoint;
				}

				if ((uint32)(Args.DesiredSize.X + StartCapturePoint.X) > SourceTexture->GetSizeX() || (uint32)(Args.DesiredSize.Y + StartCapturePoint.Y) > SourceTexture->GetSizeY())
				{
					UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. The Source size doesn't match with the user requested size. Requested: %d,%d  Source: %d,%d")
						, *Args.MediaCapture->MediaOutputName
						, Args.DesiredSize.X, Args.DesiredSize.Y
						, SourceTexture->GetSizeX(), SourceTexture->GetSizeY());

					return false;
				}
			}

			return true;
		}

		static void GetCopyInfo(const FCaptureFrameArgs& Args, const FTexture2DRHIRef& SourceTexture, FRHICopyTextureInfo& OutCopyInfo, FVector2D& OutSizeU, FVector2D& OutSizeV)
		{
			// Default to no crop
			OutSizeU = { 0.0f, 1.0f };
			OutSizeV = { 0.0f, 1.0f };
			OutCopyInfo.Size = FIntVector(SourceTexture->GetSizeX(), SourceTexture->GetSizeY(), 1);
			if (Args.MediaCapture->DesiredCaptureOptions.Crop != EMediaCaptureCroppingType::None)
			{
				switch (Args.MediaCapture->DesiredCaptureOptions.Crop)
				{
				case EMediaCaptureCroppingType::Center:
					OutCopyInfo.SourcePosition = FIntVector((SourceTexture->GetSizeX() - Args.DesiredSize.X) / 2, (SourceTexture->GetSizeY() - Args.DesiredSize.Y) / 2, 0);
					break;
				case EMediaCaptureCroppingType::TopLeft:
					break;
				case EMediaCaptureCroppingType::Custom:
					OutCopyInfo.SourcePosition = FIntVector(Args.MediaCapture->DesiredCaptureOptions.CustomCapturePoint.X, Args.MediaCapture->DesiredCaptureOptions.CustomCapturePoint.Y, 0);
					break;
				}

				OutSizeU.X = (float)(OutCopyInfo.SourcePosition.X)                      / (float)SourceTexture->GetSizeX();
				OutSizeU.Y = (float)(OutCopyInfo.SourcePosition.X + OutCopyInfo.Size.X) / (float)SourceTexture->GetSizeX();
				OutSizeV.X = (float)(OutCopyInfo.SourcePosition.Y)                      / (float)SourceTexture->GetSizeY();
				OutSizeV.Y = (float)(OutCopyInfo.SourcePosition.Y + OutCopyInfo.Size.Y) / (float)SourceTexture->GetSizeY();
			}
		}

		static void AddConversionPass(const FCaptureFrameArgs& Args, const FConversionPassArgs& ConversionPassArgs, FRDGTextureRef OutputResource)
		{
			//Based on conversion type, this might be changed
			bool bRequiresFormatConversion = ConversionPassArgs.bRequiresFormatConversion;

			// Rectangle area to use from source
			const FIntRect ViewRect(ConversionPassArgs.CopyInfo.GetSourceRect());

			//Dummy ViewFamily/ViewInfo created to use built in Draw Screen/Texture Pass
			FSceneViewFamily ViewFamily(FSceneViewFamily::ConstructionValues(nullptr, nullptr, FEngineShowFlags(ESFIM_Game))
				.SetTime(FGameTime())
				.SetGammaCorrection(1.0f));
			FSceneViewInitOptions ViewInitOptions;
			ViewInitOptions.ViewFamily = &ViewFamily;
			ViewInitOptions.SetViewRectangle(ViewRect);
			ViewInitOptions.ViewOrigin = FVector::ZeroVector;
			ViewInitOptions.ViewRotationMatrix = FMatrix::Identity;
			ViewInitOptions.ProjectionMatrix = FMatrix::Identity;
			FViewInfo ViewInfo = FViewInfo(ViewInitOptions);

			// If no conversion was required, go through a simple copy
			if (Args.MediaCapture->ConversionOperation == EMediaCaptureConversionOperation::NONE && !bRequiresFormatConversion)
			{
				AddCopyTexturePass(ConversionPassArgs.GraphBuilder, ConversionPassArgs.SourceRGBTexture, OutputResource, ConversionPassArgs.CopyInfo);
			}
			else
			{
				//At some point we should support color conversion (ocio) but for now we push incoming texture as is
				constexpr bool bDoLinearToSRGB = false;

				FGlobalShaderMap* GlobalShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
				TShaderMapRef<FScreenPassVS> VertexShader(GlobalShaderMap);

				switch (Args.MediaCapture->ConversionOperation)
				{
				case EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT:
				{
					//Configure source/output viewport to get the right UV scaling from source texture to output texture
					FScreenPassTextureViewport InputViewport(ConversionPassArgs.SourceRGBTexture, ViewRect);
					FScreenPassTextureViewport OutputViewport(OutputResource);
					TShaderMapRef<FRGB8toUYVY8ConvertPS> PixelShader(GlobalShaderMap);
					FRGB8toUYVY8ConvertPS::FParameters* Parameters = PixelShader->AllocateAndSetParameters(ConversionPassArgs.GraphBuilder, ConversionPassArgs.SourceRGBTexture, MediaShaders::RgbToYuvRec709Scaled, MediaShaders::YUVOffset8bits, bDoLinearToSRGB, OutputResource);
					AddDrawScreenPass(ConversionPassArgs.GraphBuilder, RDG_EVENT_NAME("RGBToUYVY 8 bit"), ViewInfo, OutputViewport, InputViewport, VertexShader, PixelShader, Parameters);
				}
				break;
				case EMediaCaptureConversionOperation::RGB10_TO_YUVv210_10BIT:
				{
					//Configure source/output viewport to get the right UV scaling from source texture to output texture
					const FIntPoint InExtent = FIntPoint((((ConversionPassArgs.SourceRGBTexture->Desc.Extent.X + 47) / 48) * 48), ConversionPassArgs.SourceRGBTexture->Desc.Extent.Y);;
					FScreenPassTextureViewport InputViewport(ConversionPassArgs.SourceRGBTexture, ViewRect);
					FScreenPassTextureViewport OutputViewport(OutputResource);
					TShaderMapRef<FRGB10toYUVv210ConvertPS> PixelShader(GlobalShaderMap);
					FRGB10toYUVv210ConvertPS::FParameters* Parameters = PixelShader->AllocateAndSetParameters(ConversionPassArgs.GraphBuilder, ConversionPassArgs.SourceRGBTexture, MediaShaders::RgbToYuvRec709Scaled, MediaShaders::YUVOffset10bits, bDoLinearToSRGB, OutputResource);
					AddDrawScreenPass(ConversionPassArgs.GraphBuilder, RDG_EVENT_NAME("RGBToYUVv210"), ViewInfo, OutputViewport, InputViewport, VertexShader, PixelShader, Parameters);
				}
				break;
				case EMediaCaptureConversionOperation::INVERT_ALPHA:
					// fall through
				case EMediaCaptureConversionOperation::SET_ALPHA_ONE:
					// fall through
				case EMediaCaptureConversionOperation::NONE:
					bRequiresFormatConversion = true;
				default:
					if (bRequiresFormatConversion)
					{
						//Configure source/output viewport to get the right UV scaling from source texture to output texture
						FScreenPassTextureViewport InputViewport(ConversionPassArgs.SourceRGBTexture, ViewRect);
						FScreenPassTextureViewport OutputViewport(OutputResource);

						// In cases where texture is converted from a format that doesn't have A channel, we want to force set it to 1.
						EMediaCaptureConversionOperation MediaConversionOperation = Args.MediaCapture->DesiredCaptureOptions.bForceAlphaToOneOnConversion ? EMediaCaptureConversionOperation::SET_ALPHA_ONE : Args.MediaCapture->ConversionOperation;
						FModifyAlphaSwizzleRgbaPS::FPermutationDomain PermutationVector;
						PermutationVector.Set<FModifyAlphaSwizzleRgbaPS::FConversionOp>(static_cast<int32>(MediaConversionOperation));

						TShaderMapRef<FModifyAlphaSwizzleRgbaPS> PixelShader(GlobalShaderMap, PermutationVector);
						FModifyAlphaSwizzleRgbaPS::FParameters* Parameters = PixelShader->AllocateAndSetParameters(ConversionPassArgs.GraphBuilder, ConversionPassArgs.SourceRGBTexture, OutputResource);
						AddDrawScreenPass(ConversionPassArgs.GraphBuilder, RDG_EVENT_NAME("MediaCaptureSwizzle"), ViewInfo, OutputViewport, InputViewport, VertexShader, PixelShader, Parameters);
					}
					break;
				}
			}
		}

		static void AddConversionPass(const FCaptureFrameArgs& Args, const FConversionPassArgs& ConversionPassArgs, FRDGBufferRef OutputResource)
		{
			/** Used at some point to deal with native buffer output resource type but there to validate compiled template */
		}

		template<typename TFrameType>
		static bool CaptureFrame(const FCaptureFrameArgs& Args, TFrameType* CapturingFrame)
		{
			// Validate if we have a resources used to capture source texture
			if (!CapturingFrame->HasValidResource())
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. A capture frame had an invalid render resource."), *Args.MediaCapture->MediaOutputName);
				return false;
			}

			// Get source texture that is going to be captured
			const FTexture2DRHIRef SourceTexture = GetSourceTextureForInput(Args);

			// If true, we will need to go through our different shader to convert from source format to out format (i.e RGB to YUV)
			const bool bRequiresFormatConversion = Args.MediaCapture->DesiredPixelFormat != SourceTexture->GetFormat();

			if (!SourceTexture.IsValid())
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("Can't grab the Texture to capture for '%s'."), *Args.MediaCapture->MediaOutputName);
				return false;
			}

			// Validate pixel formats and sizes before pursuing
			if (AreInputsValid(Args, SourceTexture) == false)
			{
				return false;
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(UMediaCapture::BeforeFrameCaptured);
				BeforeFrameCapture(Args, CapturingFrame);
			}

			{
				TRACE_CPUPROFILER_EVENT_SCOPE(UMediaCapture::FrameCapture);
				SCOPE_CYCLE_COUNTER(STAT_MediaCapture_RenderThread_FrameCapture);

				FRHICopyTextureInfo CopyInfo;
				FVector2D SizeU;
				FVector2D SizeV;
				GetCopyInfo(Args, SourceTexture, CopyInfo, SizeU, SizeV);

				FRDGBuilder GraphBuilder(Args.RHICmdList);

				// Register the source texture that we want to capture
				const FRDGTextureRef SourceRGBTexture = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(SourceTexture, TEXT("SourceTexture")));

				// Register output resource used by the current capture method (texture or buffer)
				typename TFrameType::FOutputResourceType OutputResource = CapturingFrame->RegisterResource(GraphBuilder);
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(UMediaCapture::GraphSetup);
					SCOPED_DRAW_EVENTF(Args.RHICmdList, MediaCapture, TEXT("MediaCapture"));

					// If custom conversion was requested from implementation, give it useful information to apply 
					if (Args.MediaCapture->ConversionOperation == EMediaCaptureConversionOperation::CUSTOM)
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(UMediaCapture::CustomCapture);

						Args.MediaCapture->OnCustomCapture_RenderingThread(GraphBuilder, CapturingFrame->CaptureBaseData, CapturingFrame->UserData
							, SourceRGBTexture, OutputResource, CopyInfo, SizeU, SizeV);
					}
					else
					{
						TRACE_CPUPROFILER_EVENT_SCOPE(UMediaCapture::FormatConversion);
						AddConversionPass(Args, { GraphBuilder, SourceRGBTexture, bRequiresFormatConversion, CopyInfo, SizeU, SizeV }, OutputResource);
					}
				}

				// If Capture implementation is not grabbing GPU resource directly, push a readback pass to access it from CPU
				if (Args.MediaCapture->bShouldCaptureRHIResource == false)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(UMediaCapture::EnqueueReadback);

					CapturingFrame->EnqueueCopy(GraphBuilder, OutputResource);

					CapturingFrame->bReadbackRequested = true;
					const int32 FrameIndex = Args.MediaCapture->CaptureFrames.IndexOfByPredicate([CapturingFrame](const TUniquePtr<FCaptureFrame>& InCaptureFrame) { return InCaptureFrame.Get() == CapturingFrame; });
					UE_LOG(LogMediaIOCore, VeryVerbose, TEXT("Requested copy for capture frame %d"), FrameIndex);
					++Args.MediaCapture->NumberOfResourcesToReadback;
				}

				GraphBuilder.Execute();

				if (Args.MediaCapture->bShouldCaptureRHIResource)
				{
					TRACE_CPUPROFILER_EVENT_SCOPE(UMediaCapture::RHIResourceCaptured);
					SCOPE_CYCLE_COUNTER(STAT_MediaCapture_RenderThread_CaptureCallback)
					Args.MediaCapture->OnRHIResourceCaptured_RenderingThread(CapturingFrame->CaptureBaseData, CapturingFrame->UserData, CapturingFrame->GetRHIResource());
					CapturingFrame->bReadbackRequested = false;
				}
			}

			return true;
		}
	};
}


/* UMediaCapture::FCaptureBaseData
*****************************************************************************/
UMediaCapture::FCaptureBaseData::FCaptureBaseData()
	: SourceFrameNumberRenderThread(0)
{

}

/* FMediaCaptureOptions
*****************************************************************************/
FMediaCaptureOptions::FMediaCaptureOptions()
	: Crop(EMediaCaptureCroppingType::None)
	, CustomCapturePoint(FIntPoint::ZeroValue)
	, bResizeSourceBuffer(false)
	, bSkipFrameWhenRunningExpensiveTasks(true)
	, bConvertToDesiredPixelFormat(true)
	, bForceAlphaToOneOnConversion(false)
	, bAutostopOnCapture(false)
	, NumberOfFramesToCapture(-1)
{

}


/* UMediaCapture
*****************************************************************************/

UMediaCapture::UMediaCapture(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, ValidSourceGPUMask(FRHIGPUMask::All())
	, bOutputResourcesInitialized(false)
	, bShouldCaptureRHIResource(false)
	, WaitingForRenderCommandExecutionCounter(0)
	, NumberOfResourcesToReadback(0)
{
}

UMediaCapture::~UMediaCapture() = default;

UMediaCapture::UMediaCapture(FVTableHelper&)
{

}


void UMediaCapture::BeginDestroy()
{
	if (GetState() == EMediaCaptureState::Capturing || GetState() == EMediaCaptureState::Preparing)
	{
		UE_LOG(LogMediaIOCore, Warning, TEXT("%s will be destroyed and the capture was not stopped."), *GetName());
	}
	StopCapture(false);

	Super::BeginDestroy();
}

FString UMediaCapture::GetDesc()
{
	if (MediaOutput)
	{
		return FString::Printf(TEXT("%s [%s]"), *Super::GetDesc(), *MediaOutput->GetDesc());
	}
	return FString::Printf(TEXT("%s [none]"), *Super::GetDesc());
}

bool UMediaCapture::CaptureActiveSceneViewport(FMediaCaptureOptions CaptureOptions)
{
	StopCapture(false);

	check(IsInGameThread());

	TSharedPtr<FSceneViewport> FoundSceneViewport;
	if (!MediaCaptureDetails::FindSceneViewportAndLevel(FoundSceneViewport) || !FoundSceneViewport.IsValid())
	{
		UE_LOG(LogMediaIOCore, Warning, TEXT("Can not start the capture. No viewport could be found."));
		return false;
	}

	return CaptureSceneViewport(FoundSceneViewport, CaptureOptions);
}

bool UMediaCapture::CaptureSceneViewport(TSharedPtr<FSceneViewport>& InSceneViewport, FMediaCaptureOptions InCaptureOptions)
{
	StopCapture(false);

	check(IsInGameThread());

	DesiredCaptureOptions = InCaptureOptions;

	if (!ValidateMediaOutput())
	{
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	CacheMediaOutput(EMediaCaptureSourceType::SCENE_VIEWPORT);

	if (bUseRequestedTargetSize)
	{
		DesiredSize = InSceneViewport->GetSize();
	}
	else if (DesiredCaptureOptions.bResizeSourceBuffer)
	{
		SetFixedViewportSize(InSceneViewport);
	}

	CacheOutputOptions();

	const bool bCurrentlyCapturing = false;
	if (!MediaCaptureDetails::ValidateSceneViewport(InSceneViewport, DesiredCaptureOptions, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		ResetFixedViewportSize(InSceneViewport, false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	SetState(EMediaCaptureState::Preparing);
	bool bInitialized = CaptureSceneViewportImpl(InSceneViewport);

	// This could have been updated by the call to CaptureSceneViewportImpl
	bShouldCaptureRHIResource = ShouldCaptureRHIResource(); 
	if (bShouldCaptureRHIResource && DesiredOutputResourceType != EMediaCaptureResourceType::Texture)
	{
		UE_LOG(LogMediaIOCore, Warning, TEXT("Can't capture RHI resource when not using texture output resource"));
		ResetFixedViewportSize(InSceneViewport, false);
		SetState(EMediaCaptureState::Stopped);
		MediaCaptureDetails::ShowSlateNotification();
	}

	if (bInitialized)
	{
		InitializeOutputResources(MediaOutput->NumberOfTextureBuffers);
		bInitialized = GetState() != EMediaCaptureState::Stopped;
	}

	if (bInitialized)
	{
		//no lock required, the command on the render thread is not active
		CapturingSceneViewport = InSceneViewport;

		CurrentReadbackIndex = 0;
		FCoreDelegates::OnEndFrame.AddUObject(this, &UMediaCapture::OnEndFrame_GameThread);
	}
	else
	{
		ResetFixedViewportSize(InSceneViewport, false);
		SetState(EMediaCaptureState::Stopped);
		MediaCaptureDetails::ShowSlateNotification();
	}

#if WITH_EDITOR
	MediaCaptureAnalytics::SendCaptureEvent(TEXT("SceneViewport"));
#endif
	
	return bInitialized;
}

bool UMediaCapture::CaptureTextureRenderTarget2D(UTextureRenderTarget2D* InRenderTarget2D, FMediaCaptureOptions CaptureOptions)
{
	StopCapture(false);

	check(IsInGameThread());

	DesiredCaptureOptions = CaptureOptions; 
	
	if (!ValidateMediaOutput())
	{
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	CacheMediaOutput(EMediaCaptureSourceType::RENDER_TARGET);

	if (bUseRequestedTargetSize)
	{
		DesiredSize = FIntPoint(InRenderTarget2D->SizeX, InRenderTarget2D->SizeY);
	}
	else if (DesiredCaptureOptions.bResizeSourceBuffer)
	{
		InRenderTarget2D->ResizeTarget(DesiredSize.X, DesiredSize.Y);
	}

	CacheOutputOptions();

	const bool bCurrentlyCapturing = false;
	if (!MediaCaptureDetails::ValidateTextureRenderTarget2D(InRenderTarget2D, DesiredCaptureOptions, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	SetState(EMediaCaptureState::Preparing);
	bool bInitialized = CaptureRenderTargetImpl(InRenderTarget2D);

	// This could have been updated by the call to CaptureSceneViewportImpl
	bShouldCaptureRHIResource = ShouldCaptureRHIResource();

	if (bInitialized)
	{
		InitializeOutputResources(MediaOutput->NumberOfTextureBuffers);
		bInitialized = GetState() != EMediaCaptureState::Stopped;
	}

	if (bInitialized)
	{
		//no lock required the command on the render thread is not active yet
		CapturingRenderTarget = InRenderTarget2D;

		CurrentReadbackIndex = 0;
		FCoreDelegates::OnEndFrame.AddUObject(this, &UMediaCapture::OnEndFrame_GameThread);
	}
	else
	{
		SetState(EMediaCaptureState::Stopped);
		MediaCaptureDetails::ShowSlateNotification();
	}

#if WITH_EDITOR
	MediaCaptureAnalytics::SendCaptureEvent(TEXT("RenderTarget2D"));
#endif
	
	return bInitialized;
}

void UMediaCapture::CacheMediaOutput(EMediaCaptureSourceType InSourceType)
{
	check(MediaOutput);
	DesiredSize = MediaOutput->GetRequestedSize();
	bUseRequestedTargetSize = DesiredSize == UMediaOutput::RequestCaptureSourceSize;
	DesiredPixelFormat = MediaOutput->GetRequestedPixelFormat();
	ConversionOperation = MediaOutput->GetConversionOperation(InSourceType);
}

void UMediaCapture::CacheOutputOptions()
{
	DesiredOutputSize = GetOutputSize(DesiredSize, ConversionOperation);
	DesiredOutputResourceType = GetOutputResourceType(ConversionOperation);
	DesiredOutputPixelFormat = GetOutputPixelFormat(DesiredPixelFormat, ConversionOperation);
	DesiredOutputBufferDescription = GetOutputBufferDescription(ConversionOperation);
	MediaOutputName = *MediaOutput->GetName();
	bShouldCaptureRHIResource = ShouldCaptureRHIResource();
}

FIntPoint UMediaCapture::GetOutputSize(const FIntPoint & InSize, const EMediaCaptureConversionOperation & InConversionOperation) const
{
	switch (InConversionOperation)
	{
	case EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT:
		return FIntPoint(InSize.X / 2, InSize.Y);
	case EMediaCaptureConversionOperation::RGB10_TO_YUVv210_10BIT:
		// Padding aligned on 48 (16 and 6 at the same time)
		return FIntPoint((((InSize.X + 47) / 48) * 48) / 6, InSize.Y);
	case EMediaCaptureConversionOperation::CUSTOM:
		return GetCustomOutputSize(InSize);
	case EMediaCaptureConversionOperation::NONE:
	default:
		return InSize;
	}
}

EPixelFormat UMediaCapture::GetOutputPixelFormat(const EPixelFormat & InPixelFormat, const EMediaCaptureConversionOperation & InConversionOperation) const
{
	switch (InConversionOperation)
	{
	case EMediaCaptureConversionOperation::RGBA8_TO_YUV_8BIT:
		return EPixelFormat::PF_B8G8R8A8;
	case EMediaCaptureConversionOperation::RGB10_TO_YUVv210_10BIT:
		return EPixelFormat::PF_R32G32B32A32_UINT;
	case EMediaCaptureConversionOperation::CUSTOM:
		return GetCustomOutputPixelFormat(InPixelFormat);
	case EMediaCaptureConversionOperation::NONE:
	default:
		return InPixelFormat;
	}
}

EMediaCaptureResourceType UMediaCapture::GetOutputResourceType(const EMediaCaptureConversionOperation& InConversionOperation) const
{
	switch (InConversionOperation)
	{
	case EMediaCaptureConversionOperation::CUSTOM:
		return GetCustomOutputResourceType();
	default:
		return EMediaCaptureResourceType::Texture;
	}
}

FRDGBufferDesc UMediaCapture::GetOutputBufferDescription(EMediaCaptureConversionOperation InConversionOperation) const
{
	switch (InConversionOperation)
	{
	case EMediaCaptureConversionOperation::CUSTOM:
		return GetCustomBufferDescription(DesiredSize);
	default:
		return FRDGBufferDesc();
	}
}

bool UMediaCapture::UpdateSceneViewport(TSharedPtr<FSceneViewport>& InSceneViewport)
{
	if (!MediaCaptureDetails::ValidateIsCapturing(*this))
	{
		StopCapture(false);
		return false;
	}

	check(IsInGameThread());

	if (!bUseRequestedTargetSize && DesiredCaptureOptions.bResizeSourceBuffer)
	{
		SetFixedViewportSize(InSceneViewport);
	}

	const bool bCurrentlyCapturing = true;
	if (!MediaCaptureDetails::ValidateSceneViewport(InSceneViewport, DesiredCaptureOptions, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		ResetFixedViewportSize(InSceneViewport, false);
		StopCapture(false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	if (!UpdateSceneViewportImpl(InSceneViewport))
	{
		ResetFixedViewportSize(InSceneViewport, false);
		StopCapture(false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	{
		FScopeLock Lock(&AccessingCapturingSource);
		while(WaitingForRenderCommandExecutionCounter.Load() > 0)
		{
			FlushRenderingCommands();
		}
		ResetFixedViewportSize(CapturingSceneViewport.Pin(), true);
		CapturingSceneViewport = InSceneViewport;
		CapturingRenderTarget = nullptr;
	}

	return true;
}

bool UMediaCapture::UpdateTextureRenderTarget2D(UTextureRenderTarget2D * InRenderTarget2D)
{
	if (!MediaCaptureDetails::ValidateIsCapturing(*this))
	{
		StopCapture(false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	check(IsInGameThread());

	if (!bUseRequestedTargetSize && DesiredCaptureOptions.bResizeSourceBuffer)
	{
		InRenderTarget2D->ResizeTarget(DesiredSize.X, DesiredSize.Y);
	}

	const bool bCurrentlyCapturing = true;
	if (!MediaCaptureDetails::ValidateTextureRenderTarget2D(InRenderTarget2D, DesiredCaptureOptions, DesiredSize, DesiredPixelFormat, bCurrentlyCapturing))
	{
		StopCapture(false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	if (!UpdateRenderTargetImpl(InRenderTarget2D))
	{
		StopCapture(false);
		MediaCaptureDetails::ShowSlateNotification();
		return false;
	}

	{
		FScopeLock Lock(&AccessingCapturingSource);
		while (WaitingForRenderCommandExecutionCounter.Load() > 0)
		{
			FlushRenderingCommands();
		}
		ResetFixedViewportSize(CapturingSceneViewport.Pin(), true);
		CapturingRenderTarget = InRenderTarget2D;
		CapturingSceneViewport.Reset();
	}

	return true;
}

void UMediaCapture::StopCapture(bool bAllowPendingFrameToBeProcess)
{
	check(IsInGameThread());

	if (GetState() != EMediaCaptureState::StopRequested && GetState() != EMediaCaptureState::Capturing)
	{
		bAllowPendingFrameToBeProcess = false;
	}

	if (bAllowPendingFrameToBeProcess)
	{
		if (GetState() != EMediaCaptureState::Stopped && GetState() != EMediaCaptureState::StopRequested)
		{
			SetState(EMediaCaptureState::StopRequested);

			//Do not flush when auto stopping to avoid hitches.
			if(DesiredCaptureOptions.bAutostopOnCapture != true)
			{
			while (WaitingForRenderCommandExecutionCounter.Load() > 0)
			{
				FlushRenderingCommands();
			}
		}
	}
	}
	else
	{
		if (GetState() != EMediaCaptureState::Stopped)
		{
			SetState(EMediaCaptureState::Stopped);

			FCoreDelegates::OnEndFrame.RemoveAll(this);

			while (WaitingForRenderCommandExecutionCounter.Load() > 0 || !bOutputResourcesInitialized)
			{
				FlushRenderingCommands();
			}
			StopCaptureImpl(bAllowPendingFrameToBeProcess);
			ResetFixedViewportSize(CapturingSceneViewport.Pin(), false);

			CapturingRenderTarget = nullptr;
			CapturingSceneViewport.Reset();
			DesiredSize = FIntPoint(1280, 720);
			DesiredPixelFormat = EPixelFormat::PF_A2B10G10R10;
			DesiredOutputSize = FIntPoint(1280, 720);
			DesiredOutputPixelFormat = EPixelFormat::PF_A2B10G10R10;
			DesiredCaptureOptions = FMediaCaptureOptions();
			ConversionOperation = EMediaCaptureConversionOperation::NONE;
			MediaOutputName.Reset();

			// CaptureFrames contains FTexture2DRHIRef, therefore should be released on Render Tread thread.
			// Keep references frames to be released in a temporary array and clear CaptureFrames on Game Thread.
			ENQUEUE_RENDER_COMMAND(MediaOutputReleaseCaptureFrames)(
				[TempArrayToBeReleasedOnRenderThread = MoveTemp(CaptureFrames)](FRHICommandListImmediate& RHICmdList) mutable
				{
					TempArrayToBeReleasedOnRenderThread.Reset();
				});
		}
	}
}

void UMediaCapture::SetMediaOutput(UMediaOutput* InMediaOutput)
{
	if (GetState() == EMediaCaptureState::Stopped)
	{
		MediaOutput = InMediaOutput;
	}
}

void UMediaCapture::SetState(EMediaCaptureState InNewState)
{
	if (MediaState != InNewState)
	{
		MediaState = InNewState;
		if (IsInGameThread())
		{
			BroadcastStateChanged();
		}
		else
		{
			TWeakObjectPtr<UMediaCapture> Self = this;
			AsyncTask(ENamedThreads::GameThread, [Self]
			{
				UMediaCapture* MediaCapture = Self.Get();
				if (UObjectInitialized() && MediaCapture)
				{
					MediaCapture->BroadcastStateChanged();
				}
			});
		}
	}
}

void UMediaCapture::BroadcastStateChanged()
{
	OnStateChanged.Broadcast();
	OnStateChangedNative.Broadcast();
}

void UMediaCapture::SetFixedViewportSize(TSharedPtr<FSceneViewport> InSceneViewport)
{
	InSceneViewport->SetFixedViewportSize(DesiredSize.X, DesiredSize.Y);
	bViewportHasFixedViewportSize = true;
}

void UMediaCapture::ResetFixedViewportSize(TSharedPtr<FSceneViewport> InViewport, bool bInFlushRenderingCommands)
{
	if (bViewportHasFixedViewportSize && InViewport.IsValid())
	{
		if (bInFlushRenderingCommands && WaitingForRenderCommandExecutionCounter.Load() > 0)
		{
			FlushRenderingCommands();
		}
		InViewport->SetFixedViewportSize(0, 0);
		bViewportHasFixedViewportSize = false;
	}
}

bool UMediaCapture::HasFinishedProcessing() const
{
	return WaitingForRenderCommandExecutionCounter.Load() == 0
		|| GetState() == EMediaCaptureState::Error
		|| GetState() == EMediaCaptureState::Stopped;
}

void UMediaCapture::SetValidSourceGPUMask(FRHIGPUMask GPUMask)
{
	ValidSourceGPUMask = GPUMask;
}

void UMediaCapture::InitializeOutputResources(int32 InNumberOfBuffers)
{
	using namespace UE::MediaCaptureData;

	if (DesiredOutputSize.X <= 0 || DesiredOutputSize.Y <= 0)
	{
		UE_LOG(LogMediaIOCore, Error, TEXT("Can't start the capture. The size requested is negative or zero."));
		SetState(EMediaCaptureState::Stopped);
		return;
	}

	NumberOfCaptureFrame = InNumberOfBuffers;
	check(CaptureFrames.Num() == 0);

	UMediaCapture* This = this;
	ENQUEUE_RENDER_COMMAND(MediaOutputCaptureFrameCreateResources)(
		[This](FRHICommandListImmediate& RHICmdList)
		{
			for (int32 Index = 0; Index < This->NumberOfCaptureFrame; ++Index)
			{
				if (This->DesiredOutputResourceType == EMediaCaptureResourceType::Texture)
				{
					TUniquePtr<FTextureCaptureFrame> NewFrame = MakeUnique<FTextureCaptureFrame>();
					FRDGTextureDesc OutputDesc = FRDGTextureDesc::Create2D(
						This->DesiredOutputSize,
						This->DesiredOutputPixelFormat,
						FClearValueBinding::None,
						TexCreate_Shared | TexCreate_RenderTargetable | TexCreate_UAV);


					NewFrame->RenderTarget = AllocatePooledTexture(OutputDesc, *FString::Format(TEXT("MediaCapture RenderTarget {0}"), { Index }));

					// Only create CPU readback resource when we are using the CPU callback
					if (!This->bShouldCaptureRHIResource)
					{
						NewFrame->ReadbackTexture = MakeUnique<FRHIGPUTextureReadback>(*FString::Printf(TEXT("MediaCaptureTextureReadback_%d"), Index));
					}

					This->CaptureFrames.Emplace(MoveTemp(NewFrame));
				}
				else
				{
					TUniquePtr<FBufferCaptureFrame> NewFrame = MakeUnique<FBufferCaptureFrame>();

					if (This->DesiredOutputBufferDescription.NumElements > 0)
					{
						NewFrame->Buffer = AllocatePooledBuffer(This->DesiredOutputBufferDescription, *FString::Format(TEXT("MediaCapture BufferResource {0}"), { Index }));

						// Only create CPU readback resource when we are using the CPU callback
						if (!This->bShouldCaptureRHIResource)
						{
							NewFrame->ReadbackBuffer = MakeUnique<FRHIGPUBufferReadback>(*FString::Printf(TEXT("MediaCaptureBufferReadback_%d"), Index));
						}

						This->CaptureFrames.Emplace(MoveTemp(NewFrame));
					}
					else
					{
						UE_LOG(LogMediaIOCore, Error, TEXT("Can't start the capture. Trying to allocate buffer resource but number of elements to allocate was 0."));
						This->SetState(EMediaCaptureState::Error);
					}
				}
			}
			This->bOutputResourcesInitialized = true;
		});
}

bool UMediaCapture::ValidateMediaOutput() const
{
	if (MediaOutput == nullptr)
	{
		UE_LOG(LogMediaIOCore, Error, TEXT("Can not start the capture. The Media Output is invalid."));
		return false;
	}

	FString FailureReason;
	if (!MediaOutput->Validate(FailureReason))
	{
		UE_LOG(LogMediaIOCore, Error, TEXT("Can not start the capture. %s."), *FailureReason);
		return false;
	}

	if(DesiredCaptureOptions.bAutostopOnCapture && DesiredCaptureOptions.NumberOfFramesToCapture < 1)
	{
		UE_LOG(LogMediaIOCore, Error, TEXT("Can not start the capture. Please set the Number Of Frames To Capture when using Autostop On Capture in the Media Capture Options"));
		return false;
	}

	return true;
}

void UMediaCapture::OnEndFrame_GameThread()
{
	using namespace UE::MediaCaptureData;

	const FString EndFrameTraceName = FString::Format(TEXT("MediaCapture::OnEndFrame_GameThread (Index {0})"), { CurrentReadbackIndex });
	TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*EndFrameTraceName);

	if (!bOutputResourcesInitialized)
	{
		FlushRenderingCommands();
	}

	if (!MediaOutput)
	{
		return;
	}

	if (GetState() == EMediaCaptureState::Error)
	{
		StopCapture(false);
	}

	if (GetState() != EMediaCaptureState::Capturing && GetState() != EMediaCaptureState::StopRequested)
	{
		return;
	}

	if (DesiredCaptureOptions.bSkipFrameWhenRunningExpensiveTasks && !FSlateThrottleManager::Get().IsAllowingExpensiveTasks())
	{
		return;
	}

	CurrentReadbackIndex = (CurrentReadbackIndex + 1) % NumberOfCaptureFrame;
	int32 ReadyFrameIndex = (CurrentReadbackIndex - 1); // Previous one in the buffer queue
	if (ReadyFrameIndex < 0)
	{
		ReadyFrameIndex = NumberOfCaptureFrame - 1;
	}

	// Frame that should be on the system ram and we want to send to the user
	FCaptureFrame* ReadyFrame = CaptureFrames[ReadyFrameIndex].Get();
	// Frame that we want to transfer to system ram
	FCaptureFrame* CapturingFrame = (GetState() != EMediaCaptureState::StopRequested) ? CaptureFrames[CurrentReadbackIndex].Get() : nullptr;

	if (!ShouldCaptureRHIResource())
	{
		TStringBuilder<256> ReadbackInfoBuilder;
		ReadbackInfoBuilder << "\n";
		for (int32 Index = 0; Index < NumberOfCaptureFrame; Index++)
		{
			ReadbackInfoBuilder << FString::Format(TEXT("Frame {0} readback requested: {1}\n"), { Index, CaptureFrames[Index]->bReadbackRequested ? TEXT("true") : TEXT("false") });
		}
		UE_LOG(LogMediaIOCore, VeryVerbose, TEXT("%s"), ReadbackInfoBuilder.GetData());
	}


	UE_LOG(LogMediaIOCore, VeryVerbose, TEXT("MediaOutput: '%s'. ReadyFrameIndex: '%d' '%s'. CurrentReadbackIndex: '%d'.")
		, *MediaOutputName, ReadyFrameIndex, (CaptureFrames[ReadyFrameIndex]->bReadbackRequested) ? TEXT("Y"): TEXT("N"), CurrentReadbackIndex);
	if (GetState() == EMediaCaptureState::StopRequested && NumberOfResourcesToReadback.Load() <= 0)
	{
		// All the requested frames have been captured.
		StopCapture(false);
		return;
	}

	if (CapturingFrame)
	{
		//Verify if game thread is overrunning the render thread.
		if (CapturingFrame->bReadbackRequested)
		{
			FString TraceName = FString::Format(TEXT("MediaCapture::FlushRenderingCommands (Index {0}/{1})"), { CurrentReadbackIndex, NumberOfCaptureFrame });
			TRACE_CPUPROFILER_EVENT_SCOPE_TEXT(*TraceName);
			UE_LOG(LogMediaIOCore, VeryVerbose, TEXT("MediaOutput: '%s'. Flushing commands.")
				, *MediaOutputName);
			FlushRenderingCommands();
		}

		CapturingFrame->CaptureBaseData.SourceFrameTimecode = FApp::GetTimecode();
		CapturingFrame->CaptureBaseData.SourceFrameTimecodeFramerate = FApp::GetTimecodeFrameRate();
		CapturingFrame->CaptureBaseData.SourceFrameNumberRenderThread = GFrameNumber;
		CapturingFrame->CaptureBaseData.SourceFrameNumber = ++CaptureRequestCount;
		CapturingFrame->UserData = GetCaptureFrameUserData_GameThread();
	}

	// Init variables for ENQUEUE_RENDER_COMMAND.
	//The Lock only synchronize while we are copying the value to the enqueue. The viewport and the rendertarget may change while we are in the enqueue command.
	{
		FScopeLock Lock(&AccessingCapturingSource);

		TSharedPtr<FSceneViewport> CapturingSceneViewportPin = CapturingSceneViewport.Pin();
		FSceneViewport* InCapturingSceneViewport = CapturingSceneViewportPin.Get();
		FTextureRenderTargetResource* InTextureRenderTargetResource = CapturingRenderTarget ? CapturingRenderTarget->GameThread_GetRenderTargetResource() : nullptr;
		FIntPoint InDesiredSize = DesiredSize;
		FMediaCaptureStateChangedSignature InOnStateChanged = OnStateChanged;
		UMediaCapture* InMediaCapture = this;

		if (InCapturingSceneViewport != nullptr || InTextureRenderTargetResource != nullptr)
		{
			++WaitingForRenderCommandExecutionCounter;

			const FRHIGPUMask SourceGPUMask = ValidSourceGPUMask;

			// RenderCommand to be executed on the RenderThread
			ENQUEUE_RENDER_COMMAND(FMediaOutputCaptureFrameCreateTexture)(
				[InMediaCapture, CapturingFrame, ReadyFrame, InCapturingSceneViewport, InTextureRenderTargetResource, InDesiredSize, InOnStateChanged, SourceGPUMask](FRHICommandListImmediate& RHICmdList)
			{
				SCOPED_GPU_MASK(RHICmdList, SourceGPUMask);
				InMediaCapture->Capture_RenderThread(RHICmdList, InMediaCapture, CapturingFrame, ReadyFrame, InCapturingSceneViewport, InTextureRenderTargetResource, InDesiredSize, InOnStateChanged);
			});

			//If auto-stopping, count the number of frame captures requested and stop when reaching 0.
			if(DesiredCaptureOptions.bAutostopOnCapture && GetState() == EMediaCaptureState::Capturing && --DesiredCaptureOptions.NumberOfFramesToCapture <= 0)
			{
				StopCapture(true);
			}
		}
	}
}

void UMediaCapture::Capture_RenderThread(FRHICommandListImmediate& RHICmdList,
	UMediaCapture* InMediaCapture,
	UE::MediaCaptureData::FCaptureFrame* CapturingFrame,
	UE::MediaCaptureData::FCaptureFrame* ReadyFrame,
	FSceneViewport* InCapturingSceneViewport,
	FTextureRenderTargetResource* InTextureRenderTargetResource,
	FIntPoint InDesiredSize,
	FMediaCaptureStateChangedSignature InOnStateChanged)
{
	using namespace UE::MediaCaptureData;
	
	TRACE_CPUPROFILER_EVENT_SCOPE(UMediaCapture::Capture_RenderThread);

	// Whatever happens, we want to decrement our counter to track enqueued commands
	ON_SCOPE_EXIT
	{
		--InMediaCapture->WaitingForRenderCommandExecutionCounter;
	};

	if (CapturingFrame)
	{
		// Call the capture frame algo based on the specific type of resource we are using
		bool bHasCaptureSuceeded = false;
		FCaptureFrameArgs CaptureArgs = { RHICmdList, InMediaCapture, InCapturingSceneViewport, InTextureRenderTargetResource, InDesiredSize };
		if (DesiredOutputResourceType == EMediaCaptureResourceType::Texture)
		{
			if (ensure(CapturingFrame->IsTextureResource()))
			{
				FTextureCaptureFrame* TextureFrame = static_cast<FTextureCaptureFrame*>(CapturingFrame);
				bHasCaptureSuceeded = FMediaCaptureHelper::CaptureFrame(CaptureArgs, TextureFrame);
			}
			else
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. Capture frame was expected to use Texture resource but wasn't."), *InMediaCapture->MediaOutputName);
			}
		}
		else
		{
			if (ensure(CapturingFrame->IsBufferResource()))
			{
				FBufferCaptureFrame* BufferFrame = static_cast<FBufferCaptureFrame*>(CapturingFrame);
				bHasCaptureSuceeded = FMediaCaptureHelper::CaptureFrame(CaptureArgs, BufferFrame);
			}
			else
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("The capture will stop for '%s'. Capture frame was expected to use Buffer resource but wasn't."), *InMediaCapture->MediaOutputName);
			}
		}
		
		if (bHasCaptureSuceeded == false)
		{
			InMediaCapture->SetState(EMediaCaptureState::Error);
		}
	}

	if (!InMediaCapture->bShouldCaptureRHIResource && InMediaCapture->GetState() != EMediaCaptureState::Error)
	{
		if (ReadyFrame->bReadbackRequested)
		{
			FRHIGPUMask GPUMask;
#if WITH_MGPU
			GPUMask = RHICmdList.GetGPUMask();

			// If GPUMask is not set to a specific GPU we and since we are reading back the texture, it shouldn't matter which GPU we do this on.
			if (!GPUMask.HasSingleIndex())
			{
				GPUMask = FRHIGPUMask::FromIndex(GPUMask.GetFirstIndex());
			}

			SCOPED_GPU_MASK(RHICmdList, GPUMask);
#endif
			// Lock & read
			void* ColorDataBuffer = nullptr;
			int32 RowStride = 0;

			// Texture readback does no verification if it's ready. It is assumed it will stall if it's not
			// Buffer readback will verify and block GPU until idle in case it's not ready otherwise it ensures
			{
				SCOPE_CYCLE_COUNTER(STAT_MediaCapture_RenderThread_LockResource);
				ColorDataBuffer = ReadyFrame->Lock(RHICmdList, GPUMask, RowStride);
			}

			{
				SCOPE_CYCLE_COUNTER(STAT_MediaCapture_RenderThread_CaptureCallback)

				// The Width/Height of the surface may be different then the DesiredOutputSize : Some underlying implementations enforce a specific stride, therefore
				// there may be padding at the end of each row.
				InMediaCapture->OnFrameCaptured_RenderingThread(ReadyFrame->CaptureBaseData, ReadyFrame->UserData, ColorDataBuffer, InMediaCapture->DesiredOutputSize.X, InMediaCapture->DesiredOutputSize.Y, RowStride);
			}
			ReadyFrame->bReadbackRequested = false;

			const int32 FrameIndex = CaptureFrames.IndexOfByPredicate([&CapturingFrame](const TUniquePtr<FCaptureFrame>& InCaptureFrame) { return InCaptureFrame.Get() == CapturingFrame; });
			UE_LOG(LogMediaIOCore, VeryVerbose, TEXT("Copy completed for capture frame %d."), FrameIndex);
			--NumberOfResourcesToReadback;

			ReadyFrame->Unlock();
		}
	}
}

/* namespace MediaCaptureDetails implementation
*****************************************************************************/
namespace MediaCaptureDetails
{
	bool FindSceneViewportAndLevel(TSharedPtr<FSceneViewport>& OutSceneViewport)
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				if (Context.WorldType == EWorldType::PIE)
				{
					UEditorEngine* EditorEngine = CastChecked<UEditorEngine>(GEngine);
					FSlatePlayInEditorInfo& Info = EditorEngine->SlatePlayInEditorMap.FindChecked(Context.ContextHandle);

					// The PIE window has priority over the regular editor window, so we need to break out of the loop if either of these are found
					if (TSharedPtr<IAssetViewport> DestinationLevelViewport = Info.DestinationSlateViewport.Pin())
					{
						OutSceneViewport = DestinationLevelViewport->GetSharedActiveViewport();
						break;
					}
					else if (Info.SlatePlayInEditorWindowViewport.IsValid())
					{
						OutSceneViewport = Info.SlatePlayInEditorWindowViewport;
						break;
					}
				}
				else if (Context.WorldType == EWorldType::Editor)
				{
					if (FLevelEditorModule* LevelEditorModule = FModuleManager::GetModulePtr<FLevelEditorModule>(LevelEditorName))
					{
						TSharedPtr<IAssetViewport> ActiveLevelViewport = LevelEditorModule->GetFirstActiveViewport();
						if (ActiveLevelViewport.IsValid())
						{
							OutSceneViewport = ActiveLevelViewport->GetSharedActiveViewport();
						}
					}
				}
			}
		}
		else
#endif
		{
			UGameEngine* GameEngine = CastChecked<UGameEngine>(GEngine);
			OutSceneViewport = GameEngine->SceneViewport;
		}

		return (OutSceneViewport.IsValid());
	}

	bool ValidateSize(const FIntPoint TargetSize, const FIntPoint& DesiredSize, const FMediaCaptureOptions& CaptureOptions, const bool bCurrentlyCapturing)
	{
		if (CaptureOptions.Crop == EMediaCaptureCroppingType::None)
		{
			if (DesiredSize.X != TargetSize.X || DesiredSize.Y != TargetSize.Y)
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target size doesn't match with the requested size. SceneViewport: %d,%d  MediaOutput: %d,%d")
					, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
					, TargetSize.X, TargetSize.Y
					, DesiredSize.X, DesiredSize.Y);
				return false;
			}
		}
		else
		{
			FIntPoint StartCapturePoint = FIntPoint::ZeroValue;
			if (CaptureOptions.Crop == EMediaCaptureCroppingType::Custom)
			{
				if (CaptureOptions.CustomCapturePoint.X < 0 || CaptureOptions.CustomCapturePoint.Y < 0)
				{
					UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The start capture point is negatif. Start Point: %d,%d")
						, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
						, StartCapturePoint.X, StartCapturePoint.Y);
					return false;
				}
				StartCapturePoint = CaptureOptions.CustomCapturePoint;
			}

			if (DesiredSize.X + StartCapturePoint.X > TargetSize.X || DesiredSize.Y + StartCapturePoint.Y > TargetSize.Y)
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target size is too small for the requested cropping options. SceneViewport: %d,%d  MediaOutput: %d,%d Start Point: %d,%d")
					, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
					, TargetSize.X, TargetSize.Y
					, DesiredSize.X, DesiredSize.Y
					, StartCapturePoint.X, StartCapturePoint.Y);
				return false;
			}
		}

		return true;
	}

	bool ValidateSceneViewport(const TSharedPtr<FSceneViewport>& SceneViewport, const FMediaCaptureOptions& CaptureOptions, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing)
	{
		if (!SceneViewport.IsValid())
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Scene Viewport is invalid.")
				, bCurrentlyCapturing ? TEXT("continue") : TEXT("start"));
			return false;
		}

		const FIntPoint SceneViewportSize = SceneViewport->GetRenderTargetTextureSizeXY();
		if (!ValidateSize(SceneViewportSize, DesiredSize, CaptureOptions, bCurrentlyCapturing))
		{
			return false;
		}

		static const auto CVarDefaultBackBufferPixelFormat = IConsoleManager::Get().FindTConsoleVariableDataInt(TEXT("r.DefaultBackBufferPixelFormat"));
		EPixelFormat SceneTargetFormat = EDefaultBackBufferPixelFormat::Convert2PixelFormat(EDefaultBackBufferPixelFormat::FromInt(CVarDefaultBackBufferPixelFormat->GetValueOnGameThread()));
		if (DesiredPixelFormat != SceneTargetFormat)
		{
			if (!SupportedRgbaSwizzleFormats.Contains(SceneTargetFormat) || !CaptureOptions.bConvertToDesiredPixelFormat)
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target pixel format doesn't match with the requested pixel format. %sRenderTarget: %s MediaOutput: %s")
					, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
					, (SupportedRgbaSwizzleFormats.Contains(SceneTargetFormat) && !CaptureOptions.bConvertToDesiredPixelFormat) ? TEXT("Please enable \"Convert To Desired Pixel Format\" option in Media Capture settings") : TEXT("")
					, GetPixelFormatString(SceneTargetFormat)
					, GetPixelFormatString(DesiredPixelFormat));
				return false;
			}
			else
			{
				UE_LOG(LogMediaIOCore, Warning, TEXT("The Render Target pixel format doesn't match with the requested pixel format. Render target will be automatically converted. This could have a slight performance impact. RenderTarget: %s MediaOutput: %s")
					, GetPixelFormatString(SceneTargetFormat)
					, GetPixelFormatString(DesiredPixelFormat));
			}
		}

		return true;
	}

	bool ValidateTextureRenderTarget2D(const UTextureRenderTarget2D* InRenderTarget2D, const FMediaCaptureOptions& CaptureOptions, const FIntPoint& DesiredSize, const EPixelFormat DesiredPixelFormat, const bool bCurrentlyCapturing)
	{
		if (InRenderTarget2D == nullptr)
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Couldn't %s the capture. The Render Target is invalid.")
				, bCurrentlyCapturing ? TEXT("continue") : TEXT("start"));
			return false;
		}

		if (!ValidateSize(FIntPoint(InRenderTarget2D->SizeX, InRenderTarget2D->SizeY), DesiredSize, CaptureOptions, bCurrentlyCapturing))
		{
			return false;
		}

		if (DesiredPixelFormat != InRenderTarget2D->GetFormat())
		{
			if (!SupportedRgbaSwizzleFormats.Contains(InRenderTarget2D->GetFormat()) || !CaptureOptions.bConvertToDesiredPixelFormat)
			{
				UE_LOG(LogMediaIOCore, Error, TEXT("Can not %s the capture. The Render Target pixel format doesn't match with the requested pixel format. %sRenderTarget: %s MediaOutput: %s")
					, bCurrentlyCapturing ? TEXT("continue") : TEXT("start")
					, (SupportedRgbaSwizzleFormats.Contains(InRenderTarget2D->GetFormat()) && !CaptureOptions.bConvertToDesiredPixelFormat) ? TEXT("Please enable \"Convert To Desired Pixel Format\" option in Media Capture settings. ") : TEXT("")
					, GetPixelFormatString(InRenderTarget2D->GetFormat())
					, GetPixelFormatString(DesiredPixelFormat));
				return false;
			}
			else
			{
				UE_LOG(LogMediaIOCore, Warning, TEXT("The Render Target pixel format doesn't match with the requested pixel format. Render target will be automatically converted. This could have a slight performance impact. RenderTarget: %s MediaOutput: %s")
					, GetPixelFormatString(InRenderTarget2D->GetFormat())
					, GetPixelFormatString(DesiredPixelFormat));
			}
		}

		return true;
	}

	bool ValidateIsCapturing(const UMediaCapture& CaptureToBeValidated)
	{
		if (CaptureToBeValidated.GetState() != EMediaCaptureState::Capturing && CaptureToBeValidated.GetState() != EMediaCaptureState::Preparing)
		{
			UE_LOG(LogMediaIOCore, Error, TEXT("Can not update the capture. There is no capture currently.\
			Only use UpdateSceneViewport or UpdateTextureRenderTarget2D when the state is Capturing or Preparing"));
			return false;
		}

		return true;
	}

	void ShowSlateNotification()
	{
#if WITH_EDITOR
		if (GIsEditor)
		{
			static double PreviousWarningTime = 0.0;
			const double TimeNow = FPlatformTime::Seconds();
			const double TimeBetweenWarningsInSeconds = 3.0f;

			if (TimeNow - PreviousWarningTime > TimeBetweenWarningsInSeconds)
			{
				FNotificationInfo NotificationInfo(LOCTEXT("MediaCaptureFailedError", "The media failed to capture. Check Output Log for details!"));
				NotificationInfo.ExpireDuration = 2.0f;
				FSlateNotificationManager::Get().AddNotification(NotificationInfo);

				PreviousWarningTime = TimeNow;
			}
		}
#endif // WITH_EDITOR
	}

	int32 GetBytesPerPixel(EPixelFormat InPixelFormat)
	{
		//We can capture viewports and render targets. Possible pixel format is limited by that
		switch (InPixelFormat)
		{

		case PF_A8:
		case PF_R8_UINT:
		case PF_R8_SINT:
		case PF_G8:
		{
			return 1;
		}
		case PF_R16_UINT:
		case PF_R16_SINT:
		case PF_R5G6B5_UNORM:
		case PF_R8G8:
		case PF_R16F:
		case PF_R16F_FILTER:
		case PF_V8U8:
		case PF_R8G8_UINT:
		case PF_B5G5R5A1_UNORM:
		{
			return 2;
		}
		case PF_R32_UINT:
		case PF_R32_SINT:
		case PF_R8G8B8A8:
		case PF_A8R8G8B8:
		case PF_FloatR11G11B10:
		case PF_A2B10G10R10:
		case PF_G16R16:
		case PF_G16R16F:
		case PF_G16R16F_FILTER:
		case PF_R32_FLOAT:
		case PF_R16G16_UINT:
		case PF_R8G8B8A8_UINT:
		case PF_R8G8B8A8_SNORM:
		case PF_B8G8R8A8:
		case PF_G16R16_SNORM:
		case PF_FloatRGB: //Equivalent to R11G11B10
		{
			return 4;
		}
		case PF_R16G16B16A16_UINT:
		case PF_R16G16B16A16_SINT:
		case PF_A16B16G16R16:
		case PF_G32R32F:
		case PF_R16G16B16A16_UNORM:
		case PF_R16G16B16A16_SNORM:
		case PF_R32G32_UINT:
		case PF_R64_UINT:
		case PF_FloatRGBA: //Equivalent to R16G16B16A16
		{
			return 8;
		}
		case PF_A32B32G32R32F:
		case PF_R32G32B32A32_UINT:
		{
			return 16;
		}
		default:
		{
			ensureMsgf(false, TEXT("MediaCapture - Pixel format (%d) not handled. Invalid bytes per pixel returned."), InPixelFormat);
			return 0;
		}
		}
	}
}

#undef LOCTEXT_NAMESPACE
