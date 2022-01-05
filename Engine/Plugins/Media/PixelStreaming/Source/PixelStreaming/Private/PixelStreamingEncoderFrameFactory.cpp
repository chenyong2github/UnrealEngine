// Copyright Epic Games, Inc. All Rights Reserved.

#include "PixelStreamingEncoderFrameFactory.h"
#include "PixelStreamingPrivate.h"
#include "CudaModule.h"
#include "VulkanRHIPrivate.h"
#include "PixelStreamingSettings.h"
#include "VideoEncoderInput.h"

#if PLATFORM_WINDOWS
	#include "VideoCommon.h"
	#include "D3D11State.h"
	#include "D3D11Resources.h"
	#include "D3D12RHICommon.h"
	#include "D3D12RHIPrivate.h"
	#include "D3D12Resources.h"
	#include "D3D12Texture.h"
	#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
	#include <VersionHelpers.h>
THIRD_PARTY_INCLUDES_END
	#include "Windows/HideWindowsPlatformTypes.h"
#endif

FPixelStreamingEncoderFrameFactory::FPixelStreamingEncoderFrameFactory()
{
}

FPixelStreamingEncoderFrameFactory::~FPixelStreamingEncoderFrameFactory()
{
	this->FlushFrames();
}

void FPixelStreamingEncoderFrameFactory::FlushFrames()
{
	for (auto& Entry : this->TextureToFrameMapping)
	{
		this->EncoderInput->DestroyBuffer(Entry.Value);
	}
	this->TextureToFrameMapping.Empty();
}

AVEncoder::FVideoEncoderInputFrame* FPixelStreamingEncoderFrameFactory::GetOrCreateFrame(int InWidth, int InHeight, const FTexture2DRHIRef InTexture)
{
	check(EncoderInput.IsValid());

	AVEncoder::FVideoEncoderInputFrame* OutFrame;

	if (this->TextureToFrameMapping.Contains(InTexture))
	{
		OutFrame = *(this->TextureToFrameMapping.Find(InTexture));
		checkf(
            InWidth == OutFrame->GetWidth() && InHeight == OutFrame->GetHeight(),
            TEXT("The requested resolution does not match the existing frame, we do not support a resolution change at this step."));
	}
	// A frame needs to be created
	else
	{
		OutFrame = this->EncoderInput->CreateBuffer([this](const AVEncoder::FVideoEncoderInputFrame* ReleasedFrame) { /* OnReleased */ });
		this->SetTexture(OutFrame, InTexture);
		this->TextureToFrameMapping.Add(InTexture, OutFrame);
	}

	OutFrame->SetFrameID(++this->FrameId);
	return OutFrame;
}

AVEncoder::FVideoEncoderInputFrame* FPixelStreamingEncoderFrameFactory::GetFrameAndSetTexture(int InWidth, int InHeight, FTexture2DRHIRef InTexture)
{
	check(EncoderInput.IsValid());

	AVEncoder::FVideoEncoderInputFrame* Frame = this->GetOrCreateFrame(InWidth, InHeight, InTexture);

	return Frame;
}

TSharedPtr<AVEncoder::FVideoEncoderInput> FPixelStreamingEncoderFrameFactory::GetOrCreateVideoEncoderInput(int InWidth, int InHeight)
{
	if (!this->EncoderInput.IsValid())
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
	if (!GDynamicRHI)
	{
		UE_LOG(PixelStreamer, Error, TEXT("GDynamicRHI not valid for some reason."));
		return nullptr;
	}

	FString RHIName = GDynamicRHI->GetName();

	// Consider if we want to support runtime resolution changing?
	bool bIsResizable = false;

	if (RHIName == TEXT("Vulkan"))
	{
		if (IsRHIDeviceAMD())
		{
			FVulkanDynamicRHI* DynamicRHI = static_cast<FVulkanDynamicRHI*>(GDynamicRHI);
			AVEncoder::FVulkanDataStruct VulkanData = { DynamicRHI->GetInstance(), DynamicRHI->GetDevice()->GetPhysicalHandle(), DynamicRHI->GetDevice()->GetInstanceHandle() };

			return AVEncoder::FVideoEncoderInput::CreateForVulkan(&VulkanData, InWidth, InHeight, bIsResizable);
		}
		else if (IsRHIDeviceNVIDIA())
		{
			if (FModuleManager::Get().IsModuleLoaded("CUDA"))
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
	else if (RHIName == TEXT("D3D11"))
	{
		if (IsRHIDeviceAMD())
		{
			return AVEncoder::FVideoEncoderInput::CreateForD3D11(GDynamicRHI->RHIGetNativeDevice(), InWidth, InHeight, bIsResizable, true);
		}
		else if (IsRHIDeviceNVIDIA())
		{
			return AVEncoder::FVideoEncoderInput::CreateForD3D11(GDynamicRHI->RHIGetNativeDevice(), InWidth, InHeight, bIsResizable, false);
		}
	}
	else if (RHIName == TEXT("D3D12"))
	{
		if (IsRHIDeviceAMD())
		{
			return AVEncoder::FVideoEncoderInput::CreateForD3D12(GDynamicRHI->RHIGetNativeDevice(), InWidth, InHeight, bIsResizable, false);
		}
		else if (IsRHIDeviceNVIDIA())
		{
			return AVEncoder::FVideoEncoderInput::CreateForCUDA(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext(), InWidth, InHeight, bIsResizable);
		}
	}
#endif

	UE_LOG(PixelStreamer, Error, TEXT("Current RHI %s is not supported in Pixel Streaming"), *RHIName);
	return nullptr;
}

void FPixelStreamingEncoderFrameFactory::SetTexture(AVEncoder::FVideoEncoderInputFrame* InputFrame, const FTexture2DRHIRef& Texture)
{
	FString RHIName = GDynamicRHI->GetName();

	// VULKAN
	if (RHIName == TEXT("Vulkan"))
	{
		if (IsRHIDeviceAMD())
		{
			FVulkanTexture2D* VulkanTexture = static_cast<FVulkanTexture2D*>(Texture.GetReference());
			InputFrame->SetTexture(VulkanTexture->Surface.Image, [](VkImage NativeTexture) { /* Do something with released texture if needed */ });
		}
		else if (IsRHIDeviceNVIDIA())
		{
			this->SetTextureCUDAVulkan(InputFrame, Texture);
		}
		else
		{
			UE_LOG(PixelStreamer, Error, TEXT("Pixel Streaming only supports AMD and NVIDIA devices, this device is neither of those."));
		}
	}
#if PLATFORM_WINDOWS
	// TODO: Fix CUDA DX11 (Using CUDA as a bridge between DX11 and NVENC currently produces garbled results)
	// DX11
	else if (RHIName == TEXT("D3D11"))
	{
		InputFrame->SetTexture((ID3D11Texture2D*)Texture->GetNativeResource(), [](ID3D11Texture2D* NativeTexture) { /* Do something with released texture if needed */ });
	}
	// DX12
	else if (RHIName == TEXT("D3D12"))
	{
		if (IsRHIDeviceAMD())
		{
			InputFrame->SetTexture((ID3D12Resource*)Texture->GetNativeResource(), [](ID3D12Resource* NativeTexture) { /* Do something with released texture if needed */ });
		}
		else if (IsRHIDeviceNVIDIA())
		{
			this->SetTextureCUDAD3D12(InputFrame, Texture);
		}
		else
		{
			UE_LOG(PixelStreamer, Error, TEXT("Pixel Streaming only supports AMD and NVIDIA devices, this device is neither of those."));
		}
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

#if PLATFORM_WINDOWS
	HANDLE handle;
	// It is recommended to use NT handles where available, but these are only supported from Windows 8 onward, for earliers versions of Windows
	// we need to use a Win7 style handle. NT handles require us to close them when we are done with them to prevent memory leaks.
	// Refer to remarks section of https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiresource1-createsharedhandle
	bool bUseNTHandle = IsWindows8OrGreater();

	{
		// Generate VkMemoryGetWin32HandleInfoKHR
		VkMemoryGetWin32HandleInfoKHR vkMemoryGetHandleInfoKHR = {};
		vkMemoryGetHandleInfoKHR.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
		vkMemoryGetHandleInfoKHR.pNext = NULL;
		vkMemoryGetHandleInfoKHR.memory = VulkanTexture->Surface.GetAllocationHandle();
		vkMemoryGetHandleInfoKHR.handleType = bUseNTHandle ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT;

		// While this operation is safe (and unavoidable) C4191 has been enabled and this will trigger an error with warnings as errors
	#pragma warning(push)
	#pragma warning(disable : 4191)
		PFN_vkGetMemoryWin32HandleKHR GetMemoryWin32HandleKHR = (PFN_vkGetMemoryWin32HandleKHR)VulkanRHI::vkGetDeviceProcAddr(device, "vkGetMemoryWin32HandleKHR");
		VERIFYVULKANRESULT(GetMemoryWin32HandleKHR(device, &vkMemoryGetHandleInfoKHR, &handle));
	#pragma warning(pop)
	}

	FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

	CUexternalMemory mappedExternalMemory = nullptr;

	{
		// generate a cudaExternalMemoryHandleDesc
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC cudaExtMemHandleDesc = {};
		cudaExtMemHandleDesc.type = bUseNTHandle ? CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 : CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT;
		cudaExtMemHandleDesc.handle.win32.name = NULL;
		cudaExtMemHandleDesc.handle.win32.handle = handle;
		cudaExtMemHandleDesc.size = VulkanTexture->Surface.GetAllocationOffset() + VulkanTexture->Surface.GetMemorySize();

		// import external memory
		auto result = FCUDAModule::CUDA().cuImportExternalMemory(&mappedExternalMemory, &cudaExtMemHandleDesc);
		if (result != CUDA_SUCCESS)
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to import external memory from vulkan error: %d"), result);
		}
	}

	// Only store handle to be closed on frame destruction if it is an NT handle
	handle = bUseNTHandle ? handle : NULL;
#else
	void* handle = nullptr;

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
		if (result != CUDA_SUCCESS)
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to import external memory from vulkan error: %d"), result);
		}
	}

#endif

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
		if (result != CUDA_SUCCESS)
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to bind mipmappedArray error: %d"), result);
		}
	}

	// get the CUarray from the external memory
	CUresult mipMapArrGetLevelErr = FCUDAModule::CUDA().cuMipmappedArrayGetLevel(&mappedArray, mappedMipArray, 0);
	if (mipMapArrGetLevelErr != CUDA_SUCCESS)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to bind to mip 0."));
	}

	FCUDAModule::CUDA().cuCtxPopCurrent(NULL);

	InputFrame->SetTexture(mappedArray, AVEncoder::FVideoEncoderInputFrame::EUnderlyingRHI::Vulkan, handle, [mappedArray, mappedMipArray, mappedExternalMemory](CUarray NativeTexture) {
		// free the cuda types
		FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

		if (mappedArray)
		{
			auto result = FCUDAModule::CUDA().cuArrayDestroy(mappedArray);
			if (result != CUDA_SUCCESS)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedArray: %d"), result);
			}
		}

		if (mappedMipArray)
		{
			auto result = FCUDAModule::CUDA().cuMipmappedArrayDestroy(mappedMipArray);
			if (result != CUDA_SUCCESS)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedMipArray: %d"), result);
			}
		}

		if (mappedExternalMemory)
		{
			auto result = FCUDAModule::CUDA().cuDestroyExternalMemory(mappedExternalMemory);
			if (result != CUDA_SUCCESS)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedExternalMemoryArray: %d"), result);
			}
		}

		FCUDAModule::CUDA().cuCtxPopCurrent(NULL);
	});
}

#if PLATFORM_WINDOWS
void FPixelStreamingEncoderFrameFactory::SetTextureCUDAD3D11(AVEncoder::FVideoEncoderInputFrame* InputFrame, const FTexture2DRHIRef& Texture)
{
	FD3D11TextureBase* D3D11Texture = GetD3D11TextureFromRHITexture(Texture);
	unsigned long long TextureMemorySize = D3D11Texture->GetMemorySize();

	ID3D11Texture2D* D3D11NativeTexture = static_cast<ID3D11Texture2D*>(D3D11Texture->GetResource());

	TRefCountPtr<IDXGIResource> DXGIResource;
	HRESULT Result = D3D11NativeTexture->QueryInterface(IID_PPV_ARGS(DXGIResource.GetInitReference()));
	if (Result != S_OK)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to get DX texture handle for importing memory to CUDA: %d"), Result);
	}

	HANDLE D3D11TextureHandle;
	DXGIResource->GetSharedHandle(&D3D11TextureHandle);
	DXGIResource->Release();

	FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

	CUexternalMemory mappedExternalMemory = nullptr;

	{
		// generate a cudaExternalMemoryHandleDesc
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC cudaExtMemHandleDesc = {};
		cudaExtMemHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_RESOURCE_KMT;
		cudaExtMemHandleDesc.handle.win32.name = NULL;
		cudaExtMemHandleDesc.handle.win32.handle = D3D11TextureHandle;
		cudaExtMemHandleDesc.size = TextureMemorySize;
		// Necessary for committed resources (DX11 and committed DX12 resources)
		cudaExtMemHandleDesc.flags |= CUDA_EXTERNAL_MEMORY_DEDICATED;

		// import external memory
		auto result = FCUDAModule::CUDA().cuImportExternalMemory(&mappedExternalMemory, &cudaExtMemHandleDesc);
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
		mipmapDesc.offset = 0;
		mipmapDesc.arrayDesc.Width = Texture->GetSizeX();
		mipmapDesc.arrayDesc.Height = Texture->GetSizeY();
		mipmapDesc.arrayDesc.Depth = 1;
		mipmapDesc.arrayDesc.NumChannels = 4;
		mipmapDesc.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
		mipmapDesc.arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST | CUDA_ARRAY3D_COLOR_ATTACHMENT;

		// get the CUarray from the external memory
		CUresult result = FCUDAModule::CUDA().cuExternalMemoryGetMappedMipmappedArray(&mappedMipArray, mappedExternalMemory, &mipmapDesc);
		if (result != CUDA_SUCCESS)
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to bind mipmappedArray error: %d"), result);
		}
	}

	// get the CUarray from the external memory
	CUresult mipMapArrGetLevelErr = FCUDAModule::CUDA().cuMipmappedArrayGetLevel(&mappedArray, mappedMipArray, 0);
	if (mipMapArrGetLevelErr != CUDA_SUCCESS)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to bind to mip 0."));
	}

	FCUDAModule::CUDA().cuCtxPopCurrent(NULL);

	InputFrame->SetTexture(mappedArray, AVEncoder::FVideoEncoderInputFrame::EUnderlyingRHI::D3D11, nullptr, [mappedArray, mappedMipArray, mappedExternalMemory](CUarray NativeTexture) {
		// free the cuda types
		FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

		if (mappedArray)
		{
			auto result = FCUDAModule::CUDA().cuArrayDestroy(mappedArray);
			if (result != CUDA_SUCCESS)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedArray: %d"), result);
			}
		}

		if (mappedMipArray)
		{
			auto result = FCUDAModule::CUDA().cuMipmappedArrayDestroy(mappedMipArray);
			if (result != CUDA_SUCCESS)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedMipArray: %d"), result);
			}
		}

		if (mappedExternalMemory)
		{
			auto result = FCUDAModule::CUDA().cuDestroyExternalMemory(mappedExternalMemory);
			if (result != CUDA_SUCCESS)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedExternalMemoryArray: %d"), result);
			}
		}

		FCUDAModule::CUDA().cuCtxPopCurrent(NULL);
	});
}

void FPixelStreamingEncoderFrameFactory::SetTextureCUDAD3D12(AVEncoder::FVideoEncoderInputFrame* InputFrame, const FTexture2DRHIRef& Texture)
{
	FD3D12TextureBase* D3D12Texture = GetD3D12TextureFromRHITexture(Texture);
	ID3D12Resource* NativeD3D12Resource = (ID3D12Resource*)Texture->GetNativeResource();
	unsigned long long TextureMemorySize = D3D12Texture->GetMemorySize();

	// Because we create our texture as RenderTargetable, it is created as a committed resource, which is what our current implementation here supports.
	// To prevent a mystery crash in future, check that our resource is a committed resource
	check(!D3D12Texture->GetResource()->IsPlacedResource());

	TRefCountPtr<ID3D12Device> OwnerDevice;
	HRESULT Result;
	if ((Result = NativeD3D12Resource->GetDevice(IID_PPV_ARGS(OwnerDevice.GetInitReference()))) != S_OK)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to get DX texture handle for importing memory to CUDA: %d (Get Device)"), Result);
	}

	//
	// ID3D12Device::CreateSharedHandle gives as an NT Handle, and so we need to call CloseHandle on it
	//
	HANDLE D3D12TextureHandle;
	if ((Result = OwnerDevice->CreateSharedHandle(NativeD3D12Resource, NULL, GENERIC_ALL, NULL, &D3D12TextureHandle)) != S_OK)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to get DX texture handle for importing memory to CUDA: %d"), Result);
	}

	FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

	CUexternalMemory mappedExternalMemory = nullptr;

	{
		// generate a cudaExternalMemoryHandleDesc
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC cudaExtMemHandleDesc = {};
		cudaExtMemHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
		cudaExtMemHandleDesc.handle.win32.name = NULL;
		cudaExtMemHandleDesc.handle.win32.handle = D3D12TextureHandle;
		cudaExtMemHandleDesc.size = TextureMemorySize;
		// Necessary for committed resources (DX11 and committed DX12 resources)
		cudaExtMemHandleDesc.flags |= CUDA_EXTERNAL_MEMORY_DEDICATED;

		// import external memory
		auto result = FCUDAModule::CUDA().cuImportExternalMemory(&mappedExternalMemory, &cudaExtMemHandleDesc);
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
		mipmapDesc.offset = 0;
		mipmapDesc.arrayDesc.Width = Texture->GetSizeX();
		mipmapDesc.arrayDesc.Height = Texture->GetSizeY();
		mipmapDesc.arrayDesc.Depth = 1;
		mipmapDesc.arrayDesc.NumChannels = 4;
		mipmapDesc.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
		mipmapDesc.arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST | CUDA_ARRAY3D_COLOR_ATTACHMENT;

		// get the CUarray from the external memory
		CUresult result = FCUDAModule::CUDA().cuExternalMemoryGetMappedMipmappedArray(&mappedMipArray, mappedExternalMemory, &mipmapDesc);
		if (result != CUDA_SUCCESS)
		{
			UE_LOG(PixelStreamer, Error, TEXT("Failed to bind mipmappedArray error: %d"), result);
		}
	}

	// get the CUarray from the external memory
	CUresult mipMapArrGetLevelErr = FCUDAModule::CUDA().cuMipmappedArrayGetLevel(&mappedArray, mappedMipArray, 0);
	if (mipMapArrGetLevelErr != CUDA_SUCCESS)
	{
		UE_LOG(PixelStreamer, Error, TEXT("Failed to bind to mip 0."));
	}

	FCUDAModule::CUDA().cuCtxPopCurrent(NULL);

	InputFrame->SetTexture(mappedArray, AVEncoder::FVideoEncoderInputFrame::EUnderlyingRHI::D3D12, D3D12TextureHandle, [mappedArray, mappedMipArray, mappedExternalMemory](CUarray NativeTexture) {
		// free the cuda types
		FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

		if (mappedArray)
		{
			auto result = FCUDAModule::CUDA().cuArrayDestroy(mappedArray);
			if (result != CUDA_SUCCESS)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedArray: %d"), result);
			}
		}

		if (mappedMipArray)
		{
			auto result = FCUDAModule::CUDA().cuMipmappedArrayDestroy(mappedMipArray);
			if (result != CUDA_SUCCESS)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedMipArray: %d"), result);
			}
		}

		if (mappedExternalMemory)
		{
			auto result = FCUDAModule::CUDA().cuDestroyExternalMemory(mappedExternalMemory);
			if (result != CUDA_SUCCESS)
			{
				UE_LOG(PixelStreamer, Error, TEXT("Failed to destroy mappedExternalMemoryArray: %d"), result);
			}
		}

		FCUDAModule::CUDA().cuCtxPopCurrent(NULL);
	});
}
#endif //PLATFORM_WINDOWS