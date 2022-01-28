// Copyright Epic Games, Inc. All Rights Reserved.

#include "EncoderFrameFactory.h"
#include "PixelStreamingPrivate.h"
#include "CudaModule.h"
#include "VulkanRHIPrivate.h"
#include "Settings.h"
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

UE::PixelStreaming::FEncoderFrameFactory::FEncoderFrameFactory()
{
}

UE::PixelStreaming::FEncoderFrameFactory::~FEncoderFrameFactory()
{
	FlushFrames();
}

void UE::PixelStreaming::FEncoderFrameFactory::FlushFrames()
{
	for (auto& Entry : TextureToFrameMapping)
	{
		EncoderInput->DestroyBuffer(Entry.Value);
	}
	TextureToFrameMapping.Empty();
}

void UE::PixelStreaming::FEncoderFrameFactory::RemoveStaleTextures()
{
	// Remove any textures whose only reference is the one held by this class

	TMap<FTexture2DRHIRef, AVEncoder::FVideoEncoderInputFrame*>::TIterator Iter = TextureToFrameMapping.CreateIterator();
	for(; Iter; ++Iter)
	{
		FTexture2DRHIRef& Tex = Iter.Key();
		AVEncoder::FVideoEncoderInputFrame* Frame = Iter.Value();

		if(Tex.GetRefCount() == 1)
		{
			EncoderInput->DestroyBuffer(Frame);
			Iter.RemoveCurrent();
		}
	}
}

AVEncoder::FVideoEncoderInputFrame* UE::PixelStreaming::FEncoderFrameFactory::GetOrCreateFrame(int InWidth, int InHeight, const FTexture2DRHIRef InTexture)
{
	check(EncoderInput.IsValid());

	RemoveStaleTextures();

	AVEncoder::FVideoEncoderInputFrame* OutFrame;

	if (TextureToFrameMapping.Contains(InTexture))
	{
		OutFrame = *(TextureToFrameMapping.Find(InTexture));
		checkf(
            InWidth == OutFrame->GetWidth() && InHeight == OutFrame->GetHeight(),
            TEXT("The requested resolution does not match the existing frame, we do not support a resolution change at this step."));
	}
	// A frame needs to be created
	else
	{
		OutFrame = EncoderInput->CreateBuffer([this](const AVEncoder::FVideoEncoderInputFrame* ReleasedFrame) { /* OnReleased */ });
		SetTexture(OutFrame, InTexture);
		TextureToFrameMapping.Add(InTexture, OutFrame);
	}

	OutFrame->SetFrameID(++FrameId);
	return OutFrame;
}

AVEncoder::FVideoEncoderInputFrame* UE::PixelStreaming::FEncoderFrameFactory::GetFrameAndSetTexture(int InWidth, int InHeight, FTexture2DRHIRef InTexture)
{
	check(EncoderInput.IsValid());

	AVEncoder::FVideoEncoderInputFrame* Frame = GetOrCreateFrame(InWidth, InHeight, InTexture);

	return Frame;
}

TSharedPtr<AVEncoder::FVideoEncoderInput> UE::PixelStreaming::FEncoderFrameFactory::GetOrCreateVideoEncoderInput(int InWidth, int InHeight)
{
	if (!EncoderInput.IsValid())
	{
		EncoderInput = CreateVideoEncoderInput(InWidth, InHeight);
	}

	return EncoderInput;
}

void UE::PixelStreaming::FEncoderFrameFactory::SetResolution(int InWidth, int InHeight)
{
	TSharedPtr<AVEncoder::FVideoEncoderInput> NewEncoderInput = GetOrCreateVideoEncoderInput(InWidth, InHeight);
	NewEncoderInput->SetResolution(InWidth, InHeight);
	NewEncoderInput->Flush();
}

TSharedPtr<AVEncoder::FVideoEncoderInput> UE::PixelStreaming::FEncoderFrameFactory::CreateVideoEncoderInput(int InWidth, int InHeight) const
{
	if (!GDynamicRHI)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("GDynamicRHI not valid for some reason."));
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
				UE_LOG(LogPixelStreaming, Error, TEXT("CUDA module is not loaded!"));
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

	UE_LOG(LogPixelStreaming, Error, TEXT("Current RHI %s is not supported in Pixel Streaming"), *RHIName);
	return nullptr;
}

void UE::PixelStreaming::FEncoderFrameFactory::SetTexture(AVEncoder::FVideoEncoderInputFrame* InputFrame, const FTexture2DRHIRef& Texture)
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
			SetTextureCUDAVulkan(InputFrame, Texture);
		}
		else
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Pixel Streaming only supports AMD and NVIDIA devices, this device is neither of those."));
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
			SetTextureCUDAD3D12(InputFrame, Texture);
		}
		else
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Pixel Streaming only supports AMD and NVIDIA devices, this device is neither of those."));
		}
	}
#endif // PLATFORM_WINDOWS
	else
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Pixel Streaming does not support this RHI - %s"), *RHIName);
	}
}

void UE::PixelStreaming::FEncoderFrameFactory::SetTextureCUDAVulkan(AVEncoder::FVideoEncoderInputFrame* InputFrame, const FTexture2DRHIRef& Texture)
{
	FVulkanTexture2D* VulkanTexture = static_cast<FVulkanTexture2D*>(Texture.GetReference());
	VkDevice Device = static_cast<FVulkanDynamicRHI*>(GDynamicRHI)->GetDevice()->GetInstanceHandle();

#if PLATFORM_WINDOWS
	HANDLE Handle;
	// It is recommended to use NT handles where available, but these are only supported from Windows 8 onward, for earliers versions of Windows
	// we need to use a Win7 style handle. NT handles require us to close them when we are done with them to prevent memory leaks.
	// Refer to remarks section of https://docs.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgiresource1-createsharedhandle
	bool bUseNTHandle = IsWindows8OrGreater();

	{
		// Generate VkMemoryGetWin32HandleInfoKHR
		VkMemoryGetWin32HandleInfoKHR MemoryGetHandleInfoKHR = {};
		MemoryGetHandleInfoKHR.sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
		MemoryGetHandleInfoKHR.pNext = NULL;
		MemoryGetHandleInfoKHR.memory = VulkanTexture->Surface.GetAllocationHandle();
		MemoryGetHandleInfoKHR.handleType = bUseNTHandle ? VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT : VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT;

		// While this operation is safe (and unavoidable) C4191 has been enabled and this will trigger an error with warnings as errors
	#pragma warning(push)
	#pragma warning(disable : 4191)
		PFN_vkGetMemoryWin32HandleKHR GetMemoryWin32HandleKHR = (PFN_vkGetMemoryWin32HandleKHR)VulkanRHI::vkGetDeviceProcAddr(Device, "vkGetMemoryWin32HandleKHR");
		VERIFYVULKANRESULT(GetMemoryWin32HandleKHR(Device, &MemoryGetHandleInfoKHR, &Handle));
	#pragma warning(pop)
	}

	FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

	CUexternalMemory MappedExternalMemory = nullptr;

	{
		// generate a cudaExternalMemoryHandleDesc
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC CudaExtMemHandleDesc = {};
		CudaExtMemHandleDesc.type = bUseNTHandle ? CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32 : CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT;
		CudaExtMemHandleDesc.handle.win32.name = NULL;
		CudaExtMemHandleDesc.handle.win32.handle = Handle;
		CudaExtMemHandleDesc.size = VulkanTexture->Surface.GetAllocationOffset() + VulkanTexture->Surface.GetMemorySize();

		// import external memory
		CUresult Result = FCUDAModule::CUDA().cuImportExternalMemory(&MappedExternalMemory, &CudaExtMemHandleDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to import external memory from vulkan error: %d"), Result);
		}
	}

	// Only store handle to be closed on frame destruction if it is an NT handle
	Handle = bUseNTHandle ? Handle : NULL;
#else
	void* Handle = nullptr;

	// Get the CUarray to that textures memory making sure the clear it when done
	int Fd;

	{
		// Generate VkMemoryGetFdInfoKHR
		VkMemoryGetFdInfoKHR MemoryGetFdInfoKHR = {};
		MemoryGetFdInfoKHR.sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
		MemoryGetFdInfoKHR.pNext = NULL;
		MemoryGetFdInfoKHR.memory = VulkanTexture->Surface.GetAllocationHandle();
		MemoryGetFdInfoKHR.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT_KHR;

		// While this operation is safe (and unavoidable) C4191 has been enabled and this will trigger an error with warnings as errors
	#pragma warning(push)
	#pragma warning(disable : 4191)
		PFN_vkGetMemoryFdKHR FPGetMemoryFdKHR = (PFN_vkGetMemoryFdKHR)VulkanRHI::vkGetDeviceProcAddr(Device, "vkGetMemoryFdKHR");
		VERIFYVULKANRESULT(FPGetMemoryFdKHR(Device, &MemoryGetFdInfoKHR, &Fd));
	#pragma warning(pop)
	}

	FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

	CUexternalMemory MappedExternalMemory = nullptr;

	{
		// generate a cudaExternalMemoryHandleDesc
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC CudaExtMemHandleDesc = {};
		CudaExtMemHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD;
		CudaExtMemHandleDesc.handle.fd = Fd;
		CudaExtMemHandleDesc.size = VulkanTexture->Surface.GetAllocationOffset() + VulkanTexture->Surface.GetMemorySize();

		// import external memory
		CUresult Result = FCUDAModule::CUDA().cuImportExternalMemory(&MappedExternalMemory, &CudaExtMemHandleDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to import external memory from vulkan error: %d"), Result);
		}
	}

#endif

	CUmipmappedArray MappedMipArray = nullptr;
	CUarray MappedArray = nullptr;

	{
		CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC MipmapDesc = {};
		MipmapDesc.numLevels = 1;
		MipmapDesc.offset = VulkanTexture->Surface.GetAllocationOffset();
		MipmapDesc.arrayDesc.Width = Texture->GetSizeX();
		MipmapDesc.arrayDesc.Height = Texture->GetSizeY();
		MipmapDesc.arrayDesc.Depth = 0;
		MipmapDesc.arrayDesc.NumChannels = 4;
		MipmapDesc.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
		MipmapDesc.arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST | CUDA_ARRAY3D_COLOR_ATTACHMENT;

		// get the CUarray from the external memory
		CUresult Result = FCUDAModule::CUDA().cuExternalMemoryGetMappedMipmappedArray(&MappedMipArray, MappedExternalMemory, &MipmapDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to bind mipmappedArray error: %d"), Result);
		}
	}

	// get the CUarray from the external memory
	CUresult MipMapArrGetLevelErr = FCUDAModule::CUDA().cuMipmappedArrayGetLevel(&MappedArray, MappedMipArray, 0);
	if (MipMapArrGetLevelErr != CUDA_SUCCESS)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Failed to bind to mip 0."));
	}

	FCUDAModule::CUDA().cuCtxPopCurrent(NULL);

	InputFrame->SetTexture(MappedArray, AVEncoder::FVideoEncoderInputFrame::EUnderlyingRHI::Vulkan, Handle, [MappedArray, MappedMipArray, MappedExternalMemory](CUarray NativeTexture) {
		// free the cuda types
		FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

		if (MappedArray)
		{
			CUresult Result = FCUDAModule::CUDA().cuArrayDestroy(MappedArray);
			if (Result != CUDA_SUCCESS)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to destroy MappedArray: %d"), Result);
			}
		}

		if (MappedMipArray)
		{
			CUresult Result = FCUDAModule::CUDA().cuMipmappedArrayDestroy(MappedMipArray);
			if (Result != CUDA_SUCCESS)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to destroy MappedMipArray: %d"), Result);
			}
		}

		if (MappedExternalMemory)
		{
			CUresult Result = FCUDAModule::CUDA().cuDestroyExternalMemory(MappedExternalMemory);
			if (Result != CUDA_SUCCESS)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to destroy MappedExternalMemoryArray: %d"), Result);
			}
		}

		FCUDAModule::CUDA().cuCtxPopCurrent(NULL);
	});
}

#if PLATFORM_WINDOWS
void UE::PixelStreaming::FEncoderFrameFactory::SetTextureCUDAD3D11(AVEncoder::FVideoEncoderInputFrame* InputFrame, const FTexture2DRHIRef& Texture)
{
	FD3D11TextureBase* D3D11Texture = GetD3D11TextureFromRHITexture(Texture);
	unsigned long long TextureMemorySize = D3D11Texture->GetMemorySize();

	ID3D11Texture2D* D3D11NativeTexture = static_cast<ID3D11Texture2D*>(D3D11Texture->GetResource());

	TRefCountPtr<IDXGIResource> DXGIResource;
	HRESULT QueryResult = D3D11NativeTexture->QueryInterface(IID_PPV_ARGS(DXGIResource.GetInitReference()));
	if (QueryResult != S_OK)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Failed to get DX texture handle for importing memory to CUDA: %d"), QueryResult);
	}

	HANDLE D3D11TextureHandle;
	DXGIResource->GetSharedHandle(&D3D11TextureHandle);
	DXGIResource->Release();

	FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

	CUexternalMemory MappedExternalMemory = nullptr;

	{
		// generate a cudaExternalMemoryHandleDesc
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC CudaExtMemHandleDesc = {};
		CudaExtMemHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_RESOURCE_KMT;
		CudaExtMemHandleDesc.handle.win32.name = NULL;
		CudaExtMemHandleDesc.handle.win32.handle = D3D11TextureHandle;
		CudaExtMemHandleDesc.size = TextureMemorySize;
		// Necessary for committed resources (DX11 and committed DX12 resources)
		CudaExtMemHandleDesc.flags |= CUDA_EXTERNAL_MEMORY_DEDICATED;

		// import external memory
		CUresult Result = FCUDAModule::CUDA().cuImportExternalMemory(&MappedExternalMemory, &CudaExtMemHandleDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to import external memory from vulkan error: %d"), Result);
		}
	}

	CUmipmappedArray MappedMipArray = nullptr;
	CUarray MappedArray = nullptr;

	{
		CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC MipmapDesc = {};
		MipmapDesc.numLevels = 1;
		MipmapDesc.offset = 0;
		MipmapDesc.arrayDesc.Width = Texture->GetSizeX();
		MipmapDesc.arrayDesc.Height = Texture->GetSizeY();
		MipmapDesc.arrayDesc.Depth = 1;
		MipmapDesc.arrayDesc.NumChannels = 4;
		MipmapDesc.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
		MipmapDesc.arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST | CUDA_ARRAY3D_COLOR_ATTACHMENT;

		// get the CUarray from the external memory
		CUresult Result = FCUDAModule::CUDA().cuExternalMemoryGetMappedMipmappedArray(&MappedMipArray, MappedExternalMemory, &MipmapDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to bind mipmappedArray error: %d"), Result);
		}
	}

	// get the CUarray from the external memory
	CUresult MipMapArrGetLevelErr = FCUDAModule::CUDA().cuMipmappedArrayGetLevel(&MappedArray, MappedMipArray, 0);
	if (MipMapArrGetLevelErr != CUDA_SUCCESS)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Failed to bind to mip 0."));
	}

	FCUDAModule::CUDA().cuCtxPopCurrent(NULL);

	InputFrame->SetTexture(MappedArray, AVEncoder::FVideoEncoderInputFrame::EUnderlyingRHI::D3D11, nullptr, [MappedArray, MappedMipArray, MappedExternalMemory](CUarray NativeTexture) {
		// free the cuda types
		FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

		if (MappedArray)
		{
			CUresult Result = FCUDAModule::CUDA().cuArrayDestroy(MappedArray);
			if (Result != CUDA_SUCCESS)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to destroy MappedArray: %d"), Result);
			}
		}

		if (MappedMipArray)
		{
			CUresult Result = FCUDAModule::CUDA().cuMipmappedArrayDestroy(MappedMipArray);
			if (Result != CUDA_SUCCESS)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to destroy MappedMipArray: %d"), Result);
			}
		}

		if (MappedExternalMemory)
		{
			CUresult Result = FCUDAModule::CUDA().cuDestroyExternalMemory(MappedExternalMemory);
			if (Result != CUDA_SUCCESS)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to destroy MappedExternalMemoryArray: %d"), Result);
			}
		}

		FCUDAModule::CUDA().cuCtxPopCurrent(NULL);
	});
}

void UE::PixelStreaming::FEncoderFrameFactory::SetTextureCUDAD3D12(AVEncoder::FVideoEncoderInputFrame* InputFrame, const FTexture2DRHIRef& Texture)
{
	FD3D12TextureBase* D3D12Texture = GetD3D12TextureFromRHITexture(Texture);
	ID3D12Resource* NativeD3D12Resource = (ID3D12Resource*)Texture->GetNativeResource();
	unsigned long long TextureMemorySize = D3D12Texture->GetMemorySize();

	// Because we create our texture as RenderTargetable, it is created as a committed resource, which is what our current implementation here supports.
	// To prevent a mystery crash in future, check that our resource is a committed resource
	check(!D3D12Texture->GetResource()->IsPlacedResource());

	TRefCountPtr<ID3D12Device> OwnerDevice;
	HRESULT QueryResult;
	if ((QueryResult = NativeD3D12Resource->GetDevice(IID_PPV_ARGS(OwnerDevice.GetInitReference()))) != S_OK)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Failed to get DX texture handle for importing memory to CUDA: %d (Get Device)"), QueryResult);
	}

	//
	// ID3D12Device::CreateSharedHandle gives as an NT Handle, and so we need to call CloseHandle on it
	//
	HANDLE D3D12TextureHandle;
	if ((QueryResult = OwnerDevice->CreateSharedHandle(NativeD3D12Resource, NULL, GENERIC_ALL, NULL, &D3D12TextureHandle)) != S_OK)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Failed to get DX texture handle for importing memory to CUDA: %d"), QueryResult);
	}

	FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

	CUexternalMemory MappedExternalMemory = nullptr;

	{
		// generate a cudaExternalMemoryHandleDesc
		CUDA_EXTERNAL_MEMORY_HANDLE_DESC CudaExtMemHandleDesc = {};
		CudaExtMemHandleDesc.type = CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE;
		CudaExtMemHandleDesc.handle.win32.name = NULL;
		CudaExtMemHandleDesc.handle.win32.handle = D3D12TextureHandle;
		CudaExtMemHandleDesc.size = TextureMemorySize;
		// Necessary for committed resources (DX11 and committed DX12 resources)
		CudaExtMemHandleDesc.flags |= CUDA_EXTERNAL_MEMORY_DEDICATED;

		// import external memory
		CUresult Result = FCUDAModule::CUDA().cuImportExternalMemory(&MappedExternalMemory, &CudaExtMemHandleDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to import external memory from vulkan error: %d"), Result);
		}
	}

	CUmipmappedArray MappedMipArray = nullptr;
	CUarray MappedArray = nullptr;

	{
		CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC MipmapDesc = {};
		MipmapDesc.numLevels = 1;
		MipmapDesc.offset = 0;
		MipmapDesc.arrayDesc.Width = Texture->GetSizeX();
		MipmapDesc.arrayDesc.Height = Texture->GetSizeY();
		MipmapDesc.arrayDesc.Depth = 1;
		MipmapDesc.arrayDesc.NumChannels = 4;
		MipmapDesc.arrayDesc.Format = CU_AD_FORMAT_UNSIGNED_INT8;
		MipmapDesc.arrayDesc.Flags = CUDA_ARRAY3D_SURFACE_LDST | CUDA_ARRAY3D_COLOR_ATTACHMENT;

		// get the CUarray from the external memory
		CUresult Result = FCUDAModule::CUDA().cuExternalMemoryGetMappedMipmappedArray(&MappedMipArray, MappedExternalMemory, &MipmapDesc);
		if (Result != CUDA_SUCCESS)
		{
			UE_LOG(LogPixelStreaming, Error, TEXT("Failed to bind mipmappedArray error: %d"), Result);
		}
	}

	// get the CUarray from the external memory
	CUresult MipMapArrGetLevelErr = FCUDAModule::CUDA().cuMipmappedArrayGetLevel(&MappedArray, MappedMipArray, 0);
	if (MipMapArrGetLevelErr != CUDA_SUCCESS)
	{
		UE_LOG(LogPixelStreaming, Error, TEXT("Failed to bind to mip 0."));
	}

	FCUDAModule::CUDA().cuCtxPopCurrent(NULL);

	InputFrame->SetTexture(MappedArray, AVEncoder::FVideoEncoderInputFrame::EUnderlyingRHI::D3D12, D3D12TextureHandle, [MappedArray, MappedMipArray, MappedExternalMemory](CUarray NativeTexture) {
		// free the cuda types
		FCUDAModule::CUDA().cuCtxPushCurrent(FModuleManager::GetModuleChecked<FCUDAModule>("CUDA").GetCudaContext());

		if (MappedArray)
		{
			CUresult Result = FCUDAModule::CUDA().cuArrayDestroy(MappedArray);
			if (Result != CUDA_SUCCESS)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to destroy MappedArray: %d"), Result);
			}
		}

		if (MappedMipArray)
		{
			CUresult Result = FCUDAModule::CUDA().cuMipmappedArrayDestroy(MappedMipArray);
			if (Result != CUDA_SUCCESS)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to destroy MappedMipArray: %d"), Result);
			}
		}

		if (MappedExternalMemory)
		{
			CUresult Result = FCUDAModule::CUDA().cuDestroyExternalMemory(MappedExternalMemory);
			if (Result != CUDA_SUCCESS)
			{
				UE_LOG(LogPixelStreaming, Error, TEXT("Failed to destroy MappedExternalMemoryArray: %d"), Result);
			}
		}

		FCUDAModule::CUDA().cuCtxPopCurrent(NULL);
	});
}
#endif //PLATFORM_WINDOWS