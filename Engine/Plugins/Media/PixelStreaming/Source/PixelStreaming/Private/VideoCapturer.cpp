// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoCapturer.h"
#include "Utils.h"
#include "Engine/Engine.h"
#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"
#include "Modules/ModuleManager.h"
#include "PixelStreamingFrameBuffer.h"
#include "PixelStreamingSettings.h"
#include "PixelStreamingStats.h"
#include "ClearQuad.h"
#include "LatencyTester.h"
#include "RHIStaticStates.h"
#include "ScreenRendering.h"
#include "CudaModule.h"
#include "VulkanRHIPrivate.h"

extern TAutoConsoleVariable<FString> CVarPixelStreamingEncoderTargetSize;

FVideoCapturer::FVideoCapturer()
{
	if (GEngine && GEngine->GameViewport)
    {
		FVector2D Res;
		GEngine->GameViewport->GetViewportSize(Res);
		this->Width = Res.X;
		this->Height = Res.Y;
    }

	CurrentState = webrtc::MediaSourceInterface::SourceState::kInitializing;

	if(GDynamicRHI)
	{
		FString RHIName = GDynamicRHI->GetName();

		bool bIsRHISupported = false;

		if(RHIName == TEXT("Vulkan"))
		{
			if(IsRHIDeviceAMD())
			{
				FVulkanDynamicRHI* DynamicRHI = static_cast<FVulkanDynamicRHI*>(GDynamicRHI);
				AVEncoder::FVulkanDataStruct VulkanData = {DynamicRHI->GetInstance(), DynamicRHI->GetDevice()->GetPhysicalHandle(), DynamicRHI->GetDevice()->GetInstanceHandle()};

				VideoEncoderInput = AVEncoder::FVideoEncoderInput::CreateForVulkan(&VulkanData, Width, Height, true);
				bIsRHISupported = true;
			}
			else if(IsRHIDeviceNVIDIA())
			{
				if(FModuleManager::Get().IsModuleLoaded("CUDA"))
				{
					VideoEncoderInput = AVEncoder::FVideoEncoderInput::CreateForCUDA(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext(), Width, Height, true);
					bIsRHISupported = true;
				}
				else
				{
					UE_LOG(PixelStreamer, Error, TEXT("CUDA module is not loaded!"));
				}
			}
		}
#if PLATFORM_WINDOWS
		else if(RHIName == TEXT("D3D11"))
		{
			VideoEncoderInput = AVEncoder::FVideoEncoderInput::CreateForD3D11(GDynamicRHI->RHIGetNativeDevice(), Width, Height, true, IsRHIDeviceAMD());
			bIsRHISupported = true;
		}
		else if(RHIName == TEXT("D3D12"))
		{
			VideoEncoderInput = AVEncoder::FVideoEncoderInput::CreateForD3D12(GDynamicRHI->RHIGetNativeDevice(), Width, Height, true, IsRHIDeviceNVIDIA());
			bIsRHISupported = true;
		}
#endif

		if(!bIsRHISupported)
		{
			unimplemented();
		}

		FCoreDelegates::OnPreExit.AddRaw(this, &FVideoCapturer::DeleteBackBuffers);
	}
}

void FVideoCapturer::DeleteBackBuffers()
{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kEnded;
	BackBuffers.Empty();
}

void FVideoCapturer::OnFrameReady(const FTexture2DRHIRef& FrameBuffer)
{
	const int64 TimestampUs = rtc::TimeMicros();

	if(!AdaptCaptureFrame(TimestampUs, FrameBuffer->GetSizeXY()))
	{
		return;
	}

	if(CurrentState != webrtc::MediaSourceInterface::SourceState::kLive)
		CurrentState = webrtc::MediaSourceInterface::SourceState::kLive;

	AVEncoder::FVideoEncoderInputFrame* InputFrame = ObtainInputFrame();
	if(InputFrame == nullptr)
		return;

	const int32 FrameId = InputFrame->GetFrameID();
	InputFrame->SetTimestampUs(TimestampUs);

	// Latency test pre capture
	if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::PRE_CAPTURE)
	{
		FLatencyTester::RecordPreCaptureTime(FrameId);
	}

	// Actual texture copy (i.e the actual "capture")
	CopyTexture(FrameBuffer, BackBuffers[InputFrame]);

	// Latency test post capture
	if(FLatencyTester::IsTestRunning() && FLatencyTester::GetTestStage() == FLatencyTester::ELatencyTestStage::POST_CAPTURE)
	{
		FLatencyTester::RecordPostCaptureTime(FrameId);
	}

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("(%d) captured video %lld"), RtcTimeMs(), TimestampUs);

	// Here we pass the frame to Webrtc which will pass it to the correct encoder.
	rtc::scoped_refptr<FPixelStreamingFrameBuffer> Buffer = new rtc::RefCountedObject<FPixelStreamingFrameBuffer>(BackBuffers[InputFrame], InputFrame, VideoEncoderInput);
	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder().set_video_frame_buffer(Buffer).set_timestamp_us(TimestampUs).set_rotation(webrtc::VideoRotation::kVideoRotation_0).set_id(FrameId).build();
	OnFrame(Frame);

	InputFrame->Release();

	// If stats are enabled, records the stats during capture now.
	FPixelStreamingStats& Stats = FPixelStreamingStats::Get();
	if(Stats.GetStatsEnabled())
	{
		int64 TimestampNowUs = rtc::TimeMicros();
		int64 CaptureLatencyUs = TimestampNowUs - TimestampUs;
		double CaptureLatencyMs = (double)CaptureLatencyUs / 1000.0;
		Stats.SetCaptureLatency(CaptureLatencyMs);
		Stats.OnCaptureFinished();
	}

}

AVEncoder::FVideoEncoderInputFrame* FVideoCapturer::ObtainInputFrame()
{
	VideoEncoderInput->SetMaxNumBuffers((uint32)PixelStreamingSettings::CVarPixelStreamingMaxNumBackBuffers.GetValueOnRenderThread());
	AVEncoder::FVideoEncoderInputFrame* InputFrame = VideoEncoderInput->ObtainInputFrame();
	if(InputFrame == nullptr)
		return nullptr;

	if(!BackBuffers.Contains(InputFrame))
	{
#if PLATFORM_WINDOWS
		FString RHIName = GDynamicRHI->GetName();
		if(RHIName == TEXT("D3D11"))
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));
			FTexture2DRHIRef Texture = GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable, ERHIAccess::CopyDest, CreateInfo);
			InputFrame->SetTexture((ID3D11Texture2D*)Texture->GetNativeResource(), [this, InputFrame](ID3D11Texture2D* NativeTexture) { BackBuffers.Remove(InputFrame); });
			BackBuffers.Add(InputFrame, Texture);
		}
		else if(RHIName == TEXT("D3D12"))
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));
			FTexture2DRHIRef Texture = GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable, ERHIAccess::CopyDest, CreateInfo);
			InputFrame->SetTexture((ID3D12Resource*)Texture->GetNativeResource(), [this, InputFrame](ID3D12Resource* NativeTexture) { BackBuffers.Remove(InputFrame); });
			BackBuffers.Add(InputFrame, Texture);
		}
		else if(RHIName == TEXT("Vulkan"))
#endif // PLATFORM_WINDOWS
		{
			if(IsRHIDeviceNVIDIA())
			{
				FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));

				FVulkanDynamicRHI* VulkanDynamicRHI = static_cast<FVulkanDynamicRHI*>(GDynamicRHI);

				// Create a texture that can be exposed to external memory
				FTexture2DRHIRef Texture =
					GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable | TexCreate_UAV, ERHIAccess::Present, CreateInfo);

				FVulkanTexture2D* VulkanTexture = static_cast<FVulkanTexture2D*>(Texture.GetReference());

				VkDevice device = static_cast<FVulkanDynamicRHI*>(GDynamicRHI)->GetDevice()->GetInstanceHandle();

				// Get the CUarray to that textures memory making sure the clear it when done
				int fd;

				{ 
					// Generate VkMemoryGetFdInfoKHR
					VkMemoryGetFdInfoKHR vkMemoryGetFdInfoKHR = {};
					vkMemoryGetFdInfoKHR.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
					vkMemoryGetFdInfoKHR.pNext = NULL;
					vkMemoryGetFdInfoKHR.memory = VulkanTexture->Surface.GetAllocationHandle();
					vkMemoryGetFdInfoKHR.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

					// While this operation is safe (and unavoidable) C4191 has been enabled and this will trigger an error with warnings as errors
#pragma warning(push)
#pragma warning(disable : 4191)
					PFN_vkGetMemoryFdKHR fpGetMemoryFdKHR = (PFN_vkGetMemoryFdKHR)VulkanRHI::vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR");
					VERIFYVULKANRESULT(fpGetMemoryFdKHR(device, &vkMemoryGetFdInfoKHR, &fd));
#pragma warning(pop)
				}

				FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

				CUexternalMemory mappedExternalMemory = nullptr;

				{
					// generate a cudaExternalMemoryHandleDesc
					CUDA_EXTERNAL_MEMORY_HANDLE_DESC cudaExtMemHandleDesc = {};
					cudaExtMemHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
					cudaExtMemHandleDesc.handle.fd = fd;
					cudaExtMemHandleDesc.size = VulkanTexture->Surface.GetAllocationOffset() + VulkanTexture->Surface.GetMemorySize();

					// import external memory
					auto result = FCUDAModule::CUDA().cuImportExternalMemory(&mappedExternalMemory, &cudaExtMemHandleDesc);
					if(result != CUDA_SUCCESS)
					{
						UE_LOG(PixelStreamer, Error, TEXT("Failed to import external memory from vulkan error: %d"), result);
					}
				}

				CUmipmappedArray mappedMipArray = nullptr;
				CUarray mappedArray = nullptr;

				{
					CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC mipmapDesc = {};
					mipmapDesc.numLevels = 1;
					mipmapDesc.offset = VulkanTexture->Surface.GetAllocationOffset();
					mipmapDesc.arrayDesc.Width = Texture->GetSizeX();
					mipmapDesc.arrayDesc.Height = Texture->GetSizeY();
					mipmapDesc.arrayDesc.Depth = 0;
					mipmapDesc.arrayDesc.NumChannels = 4;
					mipmapDesc.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
					mipmapDesc.arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST | CUDA_ARRAY3D_COLOR_ATTACHMENT;

					// get the CUarray from the external memory
					auto result = FCUDAModule::CUDA().cuExternalMemoryGetMappedMipmappedArray(&mappedMipArray, mappedExternalMemory, &mipmapDesc);
					if(result != CUDA_SUCCESS)
					{
						UE_LOG(PixelStreamer, Error, TEXT("Failed to bind mipmappedArray error: %d"), result);
					}
				}

				// get the CUarray from the external memory
				CUresult mipMapArrGetLevelErr = FCUDAModule::CUDA().cuMipmappedArrayGetLevel(&mappedArray, mappedMipArray, 0);
				if(mipMapArrGetLevelErr != CUDA_SUCCESS)
				{
					UE_LOG(PixelStreamer, Error, TEXT("Failed to bind to mip 0."));
				}

				FCUDAModule::CUDA().cuCtxPopCurrent(NULL);

				InputFrame->SetTexture(mappedArray,
				                       [this, mappedArray, mappedMipArray, mappedExternalMemory, InputFrame](CUarray NativeTexture)
				                       {
										   // free the cuda types
										   FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

										   if(mappedArray)
										   {
											   auto result = FCUDAModule::CUDA().cuArrayDestroy(mappedArray);
											   if(result != CUDA_SUCCESS)
											   {
												   UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedArray: %d"), result);
											   }
										   }

										   if(mappedMipArray)
										   {
											   auto result = FCUDAModule::CUDA().cuMipmappedArrayDestroy(mappedMipArray);
											   if(result != CUDA_SUCCESS)
											   {
												   UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedMipArray: %d"), result);
											   }
										   }

										   if(mappedExternalMemory)
										   {
											   auto result = FCUDAModule::CUDA().cuDestroyExternalMemory(mappedExternalMemory);
											   if(result != CUDA_SUCCESS)
											   {
												   UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedExternalMemoryArray: %d"), result);
											   }
										   }

										   FCUDAModule::CUDA().cuCtxPopCurrent(NULL);

										   // finally remove the input frame
										   BackBuffers.Remove(InputFrame);
									   });
				BackBuffers.Add(InputFrame, Texture);
			}
			else if(IsRHIDeviceAMD())
			{
				FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));
				FTexture2DRHIRef Texture =
					GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable | TexCreate_UAV, ERHIAccess::Present, CreateInfo);
				FVulkanTexture2D* VulkanTexture = static_cast<FVulkanTexture2D*>(Texture.GetReference());

				InputFrame->SetTexture(VulkanTexture->Surface.Image, [this, InputFrame](VkImage NativeTexture) { BackBuffers.Remove(InputFrame); });

				BackBuffers.Add(InputFrame, Texture);
			}
		}
	}

	return InputFrame;
}

void FVideoCapturer::CopyTexture(const FTexture2DRHIRef& SourceTexture, FTexture2DRHIRef& DestinationTexture) const
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	IRendererModule* RendererModule = &FModuleManager::GetModuleChecked<IRendererModule>("Renderer");

	// #todo-renderpasses there's no explicit resolve here? Do we need one?
	FRHIRenderPassInfo RPInfo(DestinationTexture, ERenderTargetActions::Load_Store);

	RHICmdList.BeginRenderPass(RPInfo, TEXT("CopyBackbuffer"));

	{
		RHICmdList.SetViewport(0, 0, 0.0f, DestinationTexture->GetSizeX(), DestinationTexture->GetSizeY(), 1.0f);

		FGraphicsPipelineStateInitializer GraphicsPSOInit;
		RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
		GraphicsPSOInit.BlendState = TStaticBlendState<>::GetRHI();
		GraphicsPSOInit.RasterizerState = TStaticRasterizerState<>::GetRHI();
		GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();

		// New engine version...
		FGlobalShaderMap* ShaderMap = GetGlobalShaderMap(GMaxRHIFeatureLevel);
		TShaderMapRef<FScreenVS> VertexShader(ShaderMap);
		TShaderMapRef<FScreenPS> PixelShader(ShaderMap);

		GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GFilterVertexDeclaration.VertexDeclarationRHI;
		GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
		GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();

		GraphicsPSOInit.PrimitiveType = PT_TriangleList;

		SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit);

		if(DestinationTexture->GetSizeX() != SourceTexture->GetSizeX() || DestinationTexture->GetSizeY() != SourceTexture->GetSizeY())
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SourceTexture);
		}
		else
		{
			PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SourceTexture);
		}

		RendererModule->DrawRectangle(RHICmdList, 0, 0,                // Dest X, Y
		                              DestinationTexture->GetSizeX(),  // Dest Width
		                              DestinationTexture->GetSizeY(),  // Dest Height
		                              0, 0,                            // Source U, V
		                              1, 1,                            // Source USize, VSize
		                              DestinationTexture->GetSizeXY(), // Target buffer size
		                              FIntPoint(1, 1),                 // Source texture size
		                              VertexShader, EDRF_Default);
	}

	RHICmdList.EndRenderPass();
}

bool FVideoCapturer::AdaptCaptureFrame(const int64 TimestampUs, FIntPoint Resolution)
{
	int outWidth, outHeight, cropWidth, cropHeight, cropX, cropY;
	if(!AdaptFrame(Resolution.X, Resolution.Y, TimestampUs, &outWidth, &outHeight, &cropWidth, &cropHeight, &cropX, &cropY))
	{
		return false;
	}

	// Set resolution of encoder using user-defined params (i.e. not the back buffer).
	if(!PixelStreamingSettings::CVarPixelStreamingUseBackBufferCaptureSize.GetValueOnRenderThread())
	{
		// set resolution based on cvars
		FString CaptureSize = PixelStreamingSettings::CVarPixelStreamingCaptureSize.GetValueOnRenderThread();
		FString TargetWidth, TargetHeight;
		bool bValidSize = CaptureSize.Split(TEXT("x"), &TargetWidth, &TargetHeight);
		if(bValidSize)
		{
			Resolution.X = FCString::Atoi(*TargetWidth);
			Resolution.Y = FCString::Atoi(*TargetHeight);
		}
		else
		{
			UE_LOG(PixelStreamer, Error, TEXT("CVarPixelStreamingCaptureSize is not in a valid format: %s. It should be e.g: \"1920x1080\""), *CaptureSize);
			PixelStreamingSettings::CVarPixelStreamingCaptureSize->Set(*FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y));
		}
	}
	else
	{
		Resolution.X = outWidth;
		Resolution.Y = outHeight;
	}

	SetCaptureResolution(Resolution.X, Resolution.Y);

	return true;
}

void FVideoCapturer::SetCaptureResolution(int NewCaptureWidth, int NewCaptureHeight)
{
	// Don't change resolution if user has indicated they do not want this behaviour.
	if(PixelStreamingSettings::CVarPixelStreamingWebRTCDisableResolutionChange.GetValueOnAnyThread())
	{
		return;
	}

	// Check is requested resolution is same as current resolution, if so, do nothing.
	if(Width == NewCaptureWidth && Height == NewCaptureHeight)
		return;

	verifyf(NewCaptureWidth > 0, TEXT("Capture width must be greater than zero."));
	verifyf(NewCaptureHeight > 0, TEXT("Capture height must be greater than zero."));

	Width = NewCaptureWidth;
	Height = NewCaptureHeight;
	VideoEncoderInput->SetResolution(Width, Height);
	VideoEncoderInput->Flush();
}
