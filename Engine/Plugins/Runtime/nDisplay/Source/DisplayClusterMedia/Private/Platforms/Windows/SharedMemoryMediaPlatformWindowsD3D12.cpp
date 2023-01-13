// Copyright Epic Games, Inc. All Rights Reserved.

#include "SharedMemoryMediaPlatformWindowsD3D12.h"

#include "ID3D12DynamicRHI.h"

#include "DisplayClusterMediaLog.h"


// This should run when the module starts and register the creation function for this rhi platform.
bool FSharedMemoryMediaPlatformWindowsD3D12::bRegistered = FSharedMemoryMediaPlatform::RegisterPlatformForRhi(
	ERHIInterfaceType::D3D12, &FSharedMemoryMediaPlatformWindowsD3D12::CreateInstance);

TSharedPtr<FSharedMemoryMediaPlatform, ESPMode::ThreadSafe> FSharedMemoryMediaPlatformWindowsD3D12::CreateInstance()
{
	return MakeShared<FSharedMemoryMediaPlatformWindowsD3D12, ESPMode::ThreadSafe>();
}

FSharedMemoryMediaPlatformWindowsD3D12::~FSharedMemoryMediaPlatformWindowsD3D12()
{
	// Close shared handles
	for (int32 BufferIdx = 0; BufferIdx < UE::SharedMemoryMedia::SenderNumBuffers; ++BufferIdx)
	{
		::CloseHandle(SharedHandle[BufferIdx]);
	}
}

FTextureRHIRef FSharedMemoryMediaPlatformWindowsD3D12::CreateSharedCrossGpuTexture(EPixelFormat Format, int32 Width, int32 Height, const FGuid& Guid, uint32 BufferIdx)
{
	check(BufferIdx < UE::SharedMemoryMedia::SenderNumBuffers);
	check(!SharedHandle[BufferIdx]);

	ID3D12Device* D3D12Device = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
	check(D3D12Device);

	D3D12_RESOURCE_DESC SharedCrossGpuTextureDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT(GPixelFormats[Format].PlatformFormat),
		Width,
		Height,
		1, // arraySize
		1, // mipLevels
		1, // sampleCount
		0, // sampleQuality
		D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS, // flags
		D3D12_TEXTURE_LAYOUT_ROW_MAJOR // layout
	);

	CD3DX12_HEAP_PROPERTIES HeapProperties(D3D12_HEAP_TYPE_DEFAULT);

	ID3D12Resource* SharedGpuResource = nullptr;

	HRESULT HResult = D3D12Device->CreateCommittedResource(
		&HeapProperties,
		D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER,
		&SharedCrossGpuTextureDesc,
		D3D12_RESOURCE_STATE_COPY_DEST,
		nullptr, // pOptimizedClearValue
		IID_PPV_ARGS(&SharedGpuResource)
	);

	check(HResult == S_OK);
	check(SharedGpuResource);

	const FString SharedGpuTextureName = FString::Printf(TEXT("Global\\%s"),
		*Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));

	HResult = D3D12Device->CreateSharedHandle(
		SharedGpuResource,                    // pObject
		nullptr,                              // pAttributes
		GENERIC_ALL,                          // Access
		*SharedGpuTextureName,                // Name
		&SharedHandle[BufferIdx]              // pHandle
	);

	check(HResult == S_OK);
	check(SharedHandle[BufferIdx]);

	return GetID3D12DynamicRHI()->RHICreateTexture2DFromResource(
		Format,
		TexCreate_Dynamic | TexCreate_DisableSRVCreation,
		FClearValueBinding::None,
		SharedGpuResource
	);
}

void FSharedMemoryMediaPlatformWindowsD3D12::ReleaseSharedCrossGpuTexture(uint32 BufferIdx)
{
	check(BufferIdx < UE::SharedMemoryMedia::SenderNumBuffers);

	::CloseHandle(SharedHandle[BufferIdx]);
}

FTextureRHIRef FSharedMemoryMediaPlatformWindowsD3D12::OpenSharedCrossGpuTextureByGuid(const FGuid& Guid, FSharedMemoryMediaTextureDescription& OutTextureDescription)
{
	ID3D12Device* D3D12Device = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
	check(D3D12Device);

	const FString SharedGpuTextureName = FString::Printf(TEXT("Global\\%s"), *Guid.ToString(EGuidFormats::DigitsWithHyphensInBraces));

	ID3D12Resource* SharedCrossGpuTexture = nullptr;

	HANDLE NamedSharedGpuTextureHandle = nullptr;
	HRESULT HResult = D3D12Device->OpenSharedHandleByName(*SharedGpuTextureName, GENERIC_ALL, &NamedSharedGpuTextureHandle);

	if (FAILED(HResult))
	{
		UE_LOG(LogDisplayClusterMedia, Error, TEXT("D3D12Device->OpenSharedHandleByName(%s) failed: 0x%X - %s"),
			*SharedGpuTextureName, HResult, *GetD3D12ComErrorDescription(HResult));

		return nullptr;
	}

	HResult = D3D12Device->OpenSharedHandle(NamedSharedGpuTextureHandle, IID_PPV_ARGS(&SharedCrossGpuTexture));

	::CloseHandle(NamedSharedGpuTextureHandle);

	if (FAILED(HResult))
	{
		UE_LOG(LogDisplayClusterMedia, Error, TEXT("D3D12Device->OpenSharedHandle(0x%X) failed: 0x%X - %s"),
			NamedSharedGpuTextureHandle, HResult, *GetD3D12ComErrorDescription(HResult));

		return nullptr;
	}

	check(SharedCrossGpuTexture);

	// Query texture dimensions and use them to describe downstream resources
	{
		D3D12_RESOURCE_DESC SharedGpuTextureDesc = SharedCrossGpuTexture->GetDesc();

		OutTextureDescription.Height = SharedGpuTextureDesc.Height;
		OutTextureDescription.Width = SharedGpuTextureDesc.Width;

		// Find EPixelFormat from platform format
		for (int32 FormatIdx = 0; FormatIdx < PF_MAX; FormatIdx++)
		{
			if (GPixelFormats[FormatIdx].PlatformFormat == SharedGpuTextureDesc.Format)
			{
				OutTextureDescription.Format = EPixelFormat(FormatIdx);
				OutTextureDescription.BytesPerPixel = GPixelFormats[FormatIdx].BlockBytes;
				OutTextureDescription.Stride = OutTextureDescription.Width * OutTextureDescription.BytesPerPixel;
				break;
			}
		}

		if (OutTextureDescription.Format == EPixelFormat::PF_Unknown)
		{
			UE_LOG(LogDisplayClusterMedia, Error, TEXT("Could not find a known pixel format for SharedCrossGpuTexture DXGI_FORMAT %d."),
				SharedGpuTextureDesc.Format);

			::CloseHandle(NamedSharedGpuTextureHandle);

			return nullptr;
		}
	}

	return GetID3D12DynamicRHI()->RHICreateTexture2DFromResource(
		OutTextureDescription.Format,
		TexCreate_Dynamic | TexCreate_DisableSRVCreation,
		FClearValueBinding::None,
		SharedCrossGpuTexture
	);
}


const FString FSharedMemoryMediaPlatformWindowsD3D12::GetD3D12ComErrorDescription(HRESULT Hresult)
{
	constexpr uint32 BufSize = 1024;
	WIDECHAR Buffer[BufSize];

	if (::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr,
		Hresult,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
		Buffer,
		sizeof(Buffer) / sizeof(*Buffer),
		nullptr))
	{
		return Buffer;
	}
	else
	{
		return FString::Printf(TEXT("[Could not find a d3d12 error description for HRESULT %d]"), Hresult);
	}
}
