// Copyright Epic Games, Inc. All Rights Reserved.

#include "VideoCapturerContext.h"
#include "PixelStreamingSettings.h"
#include "CudaModule.h"
#include "VulkanRHIPrivate.h"
#include "Misc/CoreDelegates.h"
#include "PixelStreamingPrivate.h"

FVideoCapturerContext::FVideoCapturerContext(int InCaptureWidth, int InCaptureHeight, bool bInFixedResolution)
    : CaptureWidth(InCaptureWidth)
    , CaptureHeight(InCaptureHeight)
    , bFixedResolution(bInFixedResolution)
    , VideoEncoderInput(CreateVideoEncoderInput(InCaptureWidth, InCaptureHeight, bInFixedResolution))
{
    this->VideoEncoderInput->SetMaxNumBuffers((uint32)PixelStreamingSettings::CVarPixelStreamingMaxNumBackBuffers.GetValueOnRenderThread());
    FCoreDelegates::OnPreExit.AddRaw(this, &FVideoCapturerContext::DeleteBackBuffers);
}

void FVideoCapturerContext::DeleteBackBuffers()
{
    BackBuffers.Empty();
}

bool FVideoCapturerContext::IsFixedResolution() const
{
    return this->bFixedResolution;
}

int FVideoCapturerContext::GetCaptureWidth() const
{
    return this->CaptureWidth;
}

int FVideoCapturerContext::GetCaptureHeight() const
{
    return this->CaptureHeight;
}

TSharedPtr<AVEncoder::FVideoEncoderInput> FVideoCapturerContext::GetVideoEncoderInput() const
{
    return this->VideoEncoderInput;
}

void FVideoCapturerContext::SetCaptureResolution(int NewCaptureWidth, int NewCaptureHeight)
{
	// Don't change resolution if we are in a fixed resolution capturer or the user has indicated they do not want this behaviour.
	if(this->bFixedResolution || PixelStreamingSettings::CVarPixelStreamingWebRTCDisableResolutionChange.GetValueOnAnyThread())
	{
		return;
	}

	// Check is requested resolution is same as current resolution, if so, do nothing.
	if(this->CaptureWidth == NewCaptureWidth && this->CaptureHeight == NewCaptureHeight)
	{
		return;
	}

	verifyf(NewCaptureWidth > 0, TEXT("Capture width must be greater than zero."));
	verifyf(NewCaptureHeight > 0, TEXT("Capture height must be greater than zero."));

	this->CaptureWidth = NewCaptureWidth;
	this->CaptureHeight = NewCaptureHeight;

	this->VideoEncoderInput->SetResolution(this->CaptureWidth, this->CaptureHeight);

    // Flushes available frames only, active frames still get a chance to go through the pipeline and be naturally removed from the backbuffers.
	this->VideoEncoderInput->Flush();
}

TSharedPtr<AVEncoder::FVideoEncoderInput> FVideoCapturerContext::CreateVideoEncoderInput(int InWidth, int InHeight, bool bInFixedResolution)
{
    if(!GDynamicRHI)
	{
        UE_LOG(PixelStreamer, Error, TEXT("GDynamicRHI not valid for some reason."));
        return nullptr;
	}

    FString RHIName = GDynamicRHI->GetName();
    bool bIsResizable = !bInFixedResolution;

    if(RHIName == TEXT("Vulkan"))
    {
        if(IsRHIDeviceAMD())
        {
            FVulkanDynamicRHI* DynamicRHI = static_cast<FVulkanDynamicRHI*>(GDynamicRHI);
            AVEncoder::FVulkanDataStruct VulkanData = {DynamicRHI->GetInstance(), DynamicRHI->GetDevice()->GetPhysicalHandle(), DynamicRHI->GetDevice()->GetInstanceHandle()};

            return AVEncoder::FVideoEncoderInput::CreateForVulkan(&VulkanData, InWidth, InHeight, bIsResizable);
        }
        else if(IsRHIDeviceNVIDIA())
        {
            if(FModuleManager::Get().IsModuleLoaded("CUDA"))
            {
                return AVEncoder::FVideoEncoderInput::CreateForCUDA(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext(), InWidth, InHeight, bIsResizable);
            }
            else
            {
                UE_LOG(PixelStreamer, Error, TEXT("CUDA module is not loaded!"));
                return nullptr;
            }
        }
    }
#if PLATFORM_WINDOWS
    else if(RHIName == TEXT("D3D11"))
    {
        return AVEncoder::FVideoEncoderInput::CreateForD3D11(GDynamicRHI->RHIGetNativeDevice(), InWidth, InHeight, bIsResizable, IsRHIDeviceAMD());
    }
    else if(RHIName == TEXT("D3D12"))
    {
        return AVEncoder::FVideoEncoderInput::CreateForD3D12(GDynamicRHI->RHIGetNativeDevice(), InWidth, InHeight, bIsResizable, IsRHIDeviceNVIDIA());
    }
#endif

    UE_LOG(PixelStreamer, Error, TEXT("Current RHI %s is not supported in Pixel Streaming"), *RHIName);
    return nullptr;

}

#if PLATFORM_WINDOWS
FTexture2DRHIRef FVideoCapturerContext::SetBackbufferTextureDX11(AVEncoder::FVideoEncoderInputFrame* InputFrame)
{
    FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));
    FTexture2DRHIRef Texture = GDynamicRHI->RHICreateTexture2D(this->CaptureWidth, this->CaptureHeight, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable, ERHIAccess::CopyDest, CreateInfo);
    InputFrame->SetTexture((ID3D11Texture2D*)Texture->GetNativeResource(), [this, InputFrame](ID3D11Texture2D* NativeTexture) { this->BackBuffers.Remove(InputFrame); });
    this->BackBuffers.Add(InputFrame, Texture);
    return Texture;
}

FTexture2DRHIRef FVideoCapturerContext::SetBackbufferTextureDX12(AVEncoder::FVideoEncoderInputFrame* InputFrame)
{
    FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));
    FTexture2DRHIRef Texture = GDynamicRHI->RHICreateTexture2D(this->CaptureWidth, this->CaptureHeight, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable, ERHIAccess::CopyDest, CreateInfo);
    InputFrame->SetTexture((ID3D12Resource*)Texture->GetNativeResource(), [this, InputFrame](ID3D12Resource* NativeTexture) { this->BackBuffers.Remove(InputFrame); });
    this->BackBuffers.Add(InputFrame, Texture);
    return Texture;
}
#endif // PLATFORM_WINDOWS

FTexture2DRHIRef FVideoCapturerContext::SetBackbufferTexturePureVulkan(AVEncoder::FVideoEncoderInputFrame* InputFrame)
{
    FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));
    FTexture2DRHIRef Texture =
        GDynamicRHI->RHICreateTexture2D(this->CaptureWidth, this->CaptureHeight, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable | TexCreate_UAV, ERHIAccess::Present, CreateInfo);

    FVulkanTexture2D* VulkanTexture = static_cast<FVulkanTexture2D*>(Texture.GetReference());
    InputFrame->SetTexture(VulkanTexture->Surface.Image, [this, InputFrame](VkImage NativeTexture) { this->BackBuffers.Remove(InputFrame); });
    this->BackBuffers.Add(InputFrame, Texture);
    return Texture;
}

FVideoCapturerContext::FCapturerInput FVideoCapturerContext::ObtainCapturerInput()
{
    if(!this->VideoEncoderInput.IsValid())
    {
        UE_LOG(PixelStreamer, Error, TEXT("VideoEncoderInput is nullptr cannot capture a frame."));
        return FVideoCapturerContext::FCapturerInput();
    }

    // Obtain a frame from video encoder input, we use this frame to store an RHI specific texture.
    // Note: obtain frame will recycle frames when they are no longer being used and become "available".
	AVEncoder::FVideoEncoderInputFrame* InputFrame = VideoEncoderInput->ObtainInputFrame();

	if(InputFrame == nullptr)
	{
		return FVideoCapturerContext::FCapturerInput();
	}

    // Back buffer already contains a texture for this particular frame, no need to go and make one.
    if(BackBuffers.Contains(InputFrame))
    {
        return FVideoCapturerContext::FCapturerInput(InputFrame, this->BackBuffers[InputFrame]);
    }

    // Got here, backbuffer does not contain this frame/texture already, so we must create a new platform specific texture.
    FString RHIName = GDynamicRHI->GetName();
    
    FTexture2DRHIRef OutTexture;

    // VULKAN
    if(RHIName == TEXT("Vulkan"))
    {
        if(IsRHIDeviceAMD())
        {
            OutTexture = this->SetBackbufferTexturePureVulkan(InputFrame);
        }
        else if(IsRHIDeviceNVIDIA())
        {
            OutTexture = this->SetBackbufferTextureCUDAVulkan(InputFrame);
        }
        else
        {
            UE_LOG(PixelStreamer, Error, TEXT("Pixel Streaming only supports AMD and NVIDIA devices, this device is neither of those."));
            return FVideoCapturerContext::FCapturerInput();
        }
    }
#if PLATFORM_WINDOWS	
    // DX11
    else if(RHIName == TEXT("D3D11"))
    {
        OutTexture = this->SetBackbufferTextureDX11(InputFrame);
    }
    // DX12
    else if(RHIName == TEXT("D3D12"))
    {
        OutTexture = this->SetBackbufferTextureDX12(InputFrame);
    }
#endif // PLATFORM_WINDOWS
    else
    {
        UE_LOG(PixelStreamer, Error, TEXT("Pixel Streaming does not support this RHI - %s"), *RHIName);
        return FVideoCapturerContext::FCapturerInput();
    }

    return FVideoCapturerContext::FCapturerInput(InputFrame, OutTexture);
}

FTexture2DRHIRef FVideoCapturerContext::SetBackbufferTextureCUDAVulkan(AVEncoder::FVideoEncoderInputFrame* InputFrame)
{
    FRHIResourceCreateInfo CreateInfo(TEXT("VideoCapturerBackBuffer"));
    FVulkanDynamicRHI* VulkanDynamicRHI = static_cast<FVulkanDynamicRHI*>(GDynamicRHI);

    // Create a texture that can be exposed to external memory
    FTexture2DRHIRef Texture =
        GDynamicRHI->RHICreateTexture2D(this->CaptureWidth, this->CaptureHeight, EPixelFormat::PF_B8G8R8A8, 1, 1, TexCreate_Shared | TexCreate_RenderTargetable | TexCreate_UAV, ERHIAccess::Present, CreateInfo);

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

    InputFrame->SetTexture(mappedArray, [this, mappedArray, mappedMipArray, mappedExternalMemory, InputFrame](CUarray NativeTexture)
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
    return Texture;
}
