// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoCapturer.h"
#include "Utils.h"

#include "HAL/PlatformTime.h"
#include "Misc/Timespan.h"

#include "PixelStreamingFrameBuffer.h"

#if PLATFORM_LINUX
#include "CudaModule.h"
#endif

extern TAutoConsoleVariable<int32> CVarPixelStreamingEncoderUseBackBufferSize;
extern TAutoConsoleVariable<FString> CVarPixelStreamingEncoderTargetSize;

FVideoCapturer::FVideoCapturer()
{
	CurrentState = webrtc::MediaSourceInterface::SourceState::kInitializing;

	LastTimestampUs = rtc::TimeMicros();

	if (GDynamicRHI)
	{
		FString RHIName = GDynamicRHI->GetName();

#if PLATFORM_WINDOWS
		if (RHIName == TEXT("D3D11"))
			VideoEncoderInput = AVEncoder::FVideoEncoderInput::CreateForD3D11(GDynamicRHI->RHIGetNativeDevice(), Width, Height, true);
		else if (RHIName == TEXT("D3D12"))
			VideoEncoderInput = AVEncoder::FVideoEncoderInput::CreateForD3D12(GDynamicRHI->RHIGetNativeDevice(), Width, Height, true);
		else
#endif
#if WITH_CUDA
			VideoEncoderInput = AVEncoder::FVideoEncoderInput::CreateForCUDA(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext(), Width, Height, true);
#else
			unimplemented();
#endif
	}
}

void FVideoCapturer::OnFrameReady(const FTexture2DRHIRef& FrameBuffer)
{
	FIntPoint Resolution = FrameBuffer->GetSizeXY();
	int64 TimestampUs = rtc::TimeMicros();

	int outWidth, outHeight, cropWidth, cropHeight, cropX, cropY;
	if (!AdaptFrame(Resolution.X, Resolution.Y, TimestampUs, &outWidth, &outHeight, &cropWidth, &cropHeight, &cropX, &cropY))
	{
		return;
	}

	if (CurrentState != webrtc::MediaSourceInterface::SourceState::kLive)
		CurrentState = webrtc::MediaSourceInterface::SourceState::kLive;
	
	if (CVarPixelStreamingEncoderUseBackBufferSize.GetValueOnRenderThread() == 0)
	{
		// set resolution based on cvars
		FString EncoderTargetSize = CVarPixelStreamingEncoderTargetSize.GetValueOnRenderThread();
		FString TargetWidth, TargetHeight;
		bool bValidSize = EncoderTargetSize.Split(TEXT("x"), &TargetWidth, &TargetHeight);
		if (bValidSize)
		{
			Resolution.X = FCString::Atoi(*TargetWidth);
			Resolution.Y = FCString::Atoi(*TargetHeight);
		}
		else
		{
			UE_LOG(PixelStreamer, Error, TEXT("CVarPixelStreamingEncoderTargetSize is not in a valid format: %s. It should be e.g: \"1920x1080\""), *EncoderTargetSize);
			CVarPixelStreamingEncoderTargetSize->Set(*FString::Printf(TEXT("%dx%d"), Resolution.X, Resolution.Y));
		}

		SetCaptureResolution(Resolution.X, Resolution.Y);
	}
	else
		SetCaptureResolution(outWidth, outHeight); // set resolution based on back buffer

	AVEncoder::FVideoEncoderInputFrame* InputFrame = VideoEncoderInput->ObtainInputFrame();
	InputFrame->PTS = FTimespan::FromSeconds(FPlatformTime::Seconds()).GetTicks();
	auto FrameId = InputFrame->GetFrameID();

	if (!BackBuffers.Contains(InputFrame))
	{
#if PLATFORM_WINDOWS
		FString RHIName = GDynamicRHI->GetName();
		if (RHIName == TEXT("D3D11"))
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));
			auto Texture = GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable | TexCreate_UAV, ERHIAccess::CopyDest, CreateInfo);
			InputFrame->SetTexture((ID3D11Texture2D*)Texture->GetNativeResource(), [&, InputFrame](ID3D11Texture2D* NativeTexture) { BackBuffers.Remove(InputFrame); });
			BackBuffers.Add(InputFrame, Texture);
		}
		else if (RHIName == TEXT("D3D12"))
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));
			auto Texture = GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable | TexCreate_UAV, ERHIAccess::CopyDest, CreateInfo);
			InputFrame->SetTexture((ID3D12Resource*)Texture->GetNativeResource(), [&, InputFrame](ID3D12Resource* NativeTexture) { BackBuffers.Remove(InputFrame); });
			BackBuffers.Add(InputFrame, Texture);
		}
#endif // PLATFORM_WINDOWS
#if WITH_CUDA
#if PLATFORM_WINDOWS
		else if(RHIName == TEXT("Vulkan"))
#endif // PLATFORM_WINDOWS
		{
			FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));

			// TODO (M84FIX) Replace with CUDA texture
			// Create a texture that can be exposed to external memory
			auto Texture = GDynamicRHI->RHICreateTexture2D(Width, Height, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable | TexCreate_UAV, ERHIAccess::CopyDest, CreateInfo);

			auto VulkanTexture = static_cast<FVulkanTexture2D*>(Texture.GetReference());

			auto device = static_cast<FVulkanDynamicRHI*>(GDynamicRHI)->GetDevice()->GetInstanceHandle();

			// Get the CUarray to that textures memory making sure the clear it when done
			int fd;

			{    // Generate VkMemoryGetFdInfoKHR
				VkMemoryGetFdInfoKHR vkMemoryGetFdInfoKHR = {};
				vkMemoryGetFdInfoKHR.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
				vkMemoryGetFdInfoKHR.pNext = NULL;
				vkMemoryGetFdInfoKHR.memory = VulkanTexture->Surface.GetAllocationHandle();
				vkMemoryGetFdInfoKHR.handleType =
					VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

				auto fpGetMemoryFdKHR = (PFN_vkGetMemoryFdKHR)VulkanRHI::vkGetDeviceProcAddr(device, "vkGetMemoryFdKHR");
				VERIFYVULKANRESULT(fpGetMemoryFdKHR(device, &vkMemoryGetFdInfoKHR, &fd));
			}

			cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

			CUexternalMemory mappedExternalMemory = nullptr;

			{
				// generate a cudaExternalMemoryHandleDesc
				CUDA_EXTERNAL_MEMORY_HANDLE_DESC cudaExtMemHandleDesc = {};
				cudaExtMemHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
				cudaExtMemHandleDesc.handle.fd = fd;
				cudaExtMemHandleDesc.size = VulkanTexture->Surface.GetAllocationOffset() + VulkanTexture->Surface.GetMemorySize();

				// import external memory
				auto result = cuImportExternalMemory(&mappedExternalMemory, &cudaExtMemHandleDesc);
				if (result != CUDA_SUCCESS)
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
				auto result = cuExternalMemoryGetMappedMipmappedArray(&mappedMipArray, mappedExternalMemory, &mipmapDesc);
				if (result != CUDA_SUCCESS)
				{
					UE_LOG(PixelStreamer, Error, TEXT("Failed to bind mipmappedArray error: %d"), result);
				}
			}

			// get the CUarray from the external memory
			CUresult mipMapArrGetLevelErr = cuMipmappedArrayGetLevel(&mappedArray, mappedMipArray, 0);
			if (mipMapArrGetLevelErr != CUDA_SUCCESS)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to bind to mip 0."));
			}

			cuCtxPopCurrent(NULL);

			InputFrame->SetTexture(mappedArray, [&, InputFrame](CUarray NativeTexture)
				{
					// free the cuda types
					cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

					if (mappedArray)
					{
						cuArrayDestroy(mappedArray);
					}
					if (mappedMipArray)
					{
						cuMipmappedArrayDestroy(mappedMipArray);
					}
					if (mappedExternalMemory)
					{
						cuDestroyExternalMemory(mappedExternalMemory);
					}

					cuCtxPopCurrent(NULL);

					// finally remove the input frame
					BackBuffers.Remove(InputFrame);
				});
			BackBuffers.Add(InputFrame, Texture);
		}
#endif // WITH_CUDA

		UE_LOG(PixelStreamer, Log, TEXT("%d backbuffers currently allocated"), BackBuffers.Num());
	}

	CopyTexture(FrameBuffer, BackBuffers[InputFrame]);

	rtc::scoped_refptr<FPixelStreamingFrameBuffer> Buffer = new rtc::RefCountedObject<FPixelStreamingFrameBuffer>(InputFrame, VideoEncoderInput);

	webrtc::VideoFrame Frame = webrtc::VideoFrame::Builder().
		set_video_frame_buffer(Buffer).
		set_timestamp_us(TimestampUs).
		set_rotation(webrtc::VideoRotation::kVideoRotation_0).
		set_id(FrameId).
		build();

	UE_LOG(PixelStreamer, VeryVerbose, TEXT("(%d) captured video %lld"), RtcTimeMs(), TimestampUs);
	OnFrame(Frame);
	InputFrame->Release();
}


void FVideoCapturer::CopyTexture(const FTexture2DRHIRef& SourceTexture, FTexture2DRHIRef& DestinationTexture)
{
	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	if (SourceTexture->GetFormat() == DestinationTexture->GetFormat() &&
		SourceTexture->GetSizeXY() == DestinationTexture->GetSizeXY())
	{
		RHICmdList.CopyToResolveTarget(SourceTexture, DestinationTexture, FResolveParams{});
	}
	else // Texture format mismatch, use a shader to do the copy.
	{
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

			if (DestinationTexture->GetSizeX() != SourceTexture->GetSizeX() || DestinationTexture->GetSizeY() != SourceTexture->GetSizeY())
			{
				PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Bilinear>::GetRHI(), SourceTexture);
			}
			else
			{
				PixelShader->SetParameters(RHICmdList, TStaticSamplerState<SF_Point>::GetRHI(), SourceTexture);
			}

			RendererModule->DrawRectangle(
				RHICmdList,
				0, 0,									// Dest X, Y
				DestinationTexture->GetSizeX(),			// Dest Width
				DestinationTexture->GetSizeY(),			// Dest Height
				0, 0,									// Source U, V
				1, 1,									// Source USize, VSize
				DestinationTexture->GetSizeXY(),		// Target buffer size
				FIntPoint(1, 1),						// Source texture size
				VertexShader,
				EDRF_Default);
		}

		RHICmdList.EndRenderPass();
	}

}

bool FVideoCapturer::SetCaptureResolution(int NewCaptureWidth, int NewCaptureHeight)
{
	// Check is requested resolution is same as current resolution, if so, do nothing.
	if(Width == NewCaptureWidth && Height == NewCaptureHeight)
		return false;

	verifyf(NewCaptureWidth > 0, TEXT("Capture width must be greater than zero."));
	verifyf(NewCaptureHeight  > 0, TEXT("Capture height must be greater than zero."));

	Width = NewCaptureWidth;
	Height = NewCaptureHeight;
	VideoEncoderInput->SetResolution(Width, Height);
	VideoEncoderInput->Flush();

	return true;
}
