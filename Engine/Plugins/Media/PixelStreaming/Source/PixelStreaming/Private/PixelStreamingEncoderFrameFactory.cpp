// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingEncoderFrameFactory.h"
#include "PixelStreamingPrivate.h"
#include "CudaModule.h"
#include "VulkanRHIPrivate.h"
#include "PixelStreamingSettings.h"

FPixelStreamingEncoderFrameFactory::FPixelStreamingEncoderFrameFactory()
{

}

FPixelStreamingEncoderFrameFactory::~FPixelStreamingEncoderFrameFactory()
{
    this->FlushFrames();
}

void FPixelStreamingEncoderFrameFactory::FlushFrames()
{
    for(auto& Entry : this->TextureToFrameMapping)
    {
        this->EncoderInput->DestroyBuffer(Entry.Value);
    }
    this->TextureToFrameMapping.Empty();
}

AVEncoder::FVideoEncoderInputFrame* FPixelStreamingEncoderFrameFactory::GetOrCreateFrame(int InWidth, int InHeight, const FTexture2DRHIRef InTexture)
{
    check(EncoderInput.IsValid());

    AVEncoder::FVideoEncoderInputFrame* OutFrame;

    if(this->TextureToFrameMapping.Contains(InTexture))
    {
        OutFrame = *(this->TextureToFrameMapping.Find(InTexture));
        checkf(InWidth == OutFrame->GetWidth() && InHeight == OutFrame->GetHeight(), TEXT("The requested resolution does not match the existing frame, we do not support a resolution change at this step."));
    }
    // A frame needs to be created
    else
    {
        OutFrame = this->EncoderInput->CreateBuffer([this](const AVEncoder::FVideoEncoderInputFrame* ReleasedFrame){ /* OnReleased */ });
        this->SetTexture(OutFrame, InTexture);
        this->TextureToFrameMapping.Add(InTexture, OutFrame);
    }

    OutFrame->SetFrameID(++this->FrameId);
    return OutFrame;
}

AVEncoder::FVideoEncoderInputFrame* FPixelStreamingEncoderFrameFactory::GetFrameAndSetTexture(int InWidth, int InHeight, FTextureObtainer TextureObtainer)
{
    check(EncoderInput.IsValid());

    // Copy texture here into our one encoder frame
    const FTexture2DRHIRef SourceTexture = TextureObtainer();

    AVEncoder::FVideoEncoderInputFrame* Frame = this->GetOrCreateFrame(InWidth, InHeight, SourceTexture);

    return Frame;
}

TSharedPtr<AVEncoder::FVideoEncoderInput> FPixelStreamingEncoderFrameFactory::GetOrCreateVideoEncoderInput(int InWidth, int InHeight)
{
    if(!this->EncoderInput.IsValid())
    {
        this->EncoderInput = CreateVideoEncoderInput(InWidth, InHeight);
        this->EncoderInput->SetMaxNumBuffers((uint32)PixelStreamingSettings::CVarPixelStreamingMaxNumBackBuffers.GetValueOnAnyThread());
    }

    return this->EncoderInput;
}

void FPixelStreamingEncoderFrameFactory::SetResolution(int InWidth, int InHeight)
{
    TSharedPtr<AVEncoder::FVideoEncoderInput> NewEncoderInput = GetOrCreateVideoEncoderInput(InWidth, InHeight);
    NewEncoderInput->SetResolution(InWidth, InHeight);
    NewEncoderInput->Flush();
}

TSharedPtr<AVEncoder::FVideoEncoderInput> FPixelStreamingEncoderFrameFactory::CreateVideoEncoderInput(int InWidth, int InHeight) const
{
    if(!GDynamicRHI)
	{
        UE_LOG(PixelStreamer, Error, TEXT("GDynamicRHI not valid for some reason."));
        return nullptr;
	}

    FString RHIName = GDynamicRHI->GetName();

    // Consider if we want to support runtime resolution changing?
    bool bIsResizable = false;

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

void FPixelStreamingEncoderFrameFactory::SetTexture(AVEncoder::FVideoEncoderInputFrame* InputFrame, const FTexture2DRHIRef& Texture)
{
    FString RHIName = GDynamicRHI->GetName();

    // VULKAN
    if(RHIName == TEXT("Vulkan"))
    {
        if(IsRHIDeviceAMD())
        {
            FVulkanTexture2D* VulkanTexture = static_cast<FVulkanTexture2D*>(Texture.GetReference());
            InputFrame->SetTexture(VulkanTexture->Surface.Image, [this, InputFrame](VkImage NativeTexture) { /* Do something with released texture if needed */ });
        }
        else if(IsRHIDeviceNVIDIA())
        {
            this->SetTextureCUDAVulkan(InputFrame, Texture);
        }
        else
        {
            UE_LOG(PixelStreamer, Error, TEXT("Pixel Streaming only supports AMD and NVIDIA devices, this device is neither of those."));
        }
    }
#if PLATFORM_WINDOWS	
    // DX11
    else if(RHIName == TEXT("D3D11"))
    {
        InputFrame->SetTexture((ID3D11Texture2D*)Texture->GetNativeResource(), [this, InputFrame](ID3D11Texture2D* NativeTexture) { /* Do something with released texture if needed */ });
    }
    // DX12
    else if(RHIName == TEXT("D3D12"))
    {
        InputFrame->SetTexture((ID3D12Resource*)Texture->GetNativeResource(), [this, InputFrame](ID3D12Resource* NativeTexture) { /* Do something with released texture if needed */ });
    }
#endif // PLATFORM_WINDOWS
    else
    {
        UE_LOG(PixelStreamer, Error, TEXT("Pixel Streaming does not support this RHI - %s"), *RHIName);
    }
}

void FPixelStreamingEncoderFrameFactory::SetTextureCUDAVulkan(AVEncoder::FVideoEncoderInputFrame* InputFrame, const FTexture2DRHIRef& Texture)
{
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
    });
}