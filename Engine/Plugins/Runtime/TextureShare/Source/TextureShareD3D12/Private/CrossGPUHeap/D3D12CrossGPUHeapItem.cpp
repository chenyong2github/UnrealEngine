// Copyright Epic Games, Inc. All Rights Reserved.

#include "D3D12CrossGPUHeapItem.h"

#if TEXTURESHARE_CROSSGPUHEAP
// DX12 Cross GPU heap resource API (experimental)
#include "TextureShareD3D12Log.h"
#include "D3D12RHIPrivate.h"
#include "D3D12Util.h"

// macro to deal with COM calls inside a function that returns `{}` on error
#define CHECK_HR_DEFAULT(COM_call)\
	{\
		HRESULT Res = COM_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(LogD3D12CrossGPUHeap, Error, TEXT("`" #COM_call "` failed: 0x%X - %s"), Res, *GetComErrorDescription(Res)); \
			return {};\
		}\
	}

FD3D12CrossGPUItem::FD3D12CrossGPUItem(const FString& InResourceID, const FTextureShareD3D12CrossGPUSecurity& InSecurity)
	: ResourceID(InResourceID)
	, Security(InSecurity)
{}

FString FD3D12CrossGPUItem::GetSharedHeapName()
{
	return FString::Printf(TEXT("Global\\CrossGPU_%s"), *ResourceID);
}

bool FD3D12CrossGPUItem::CreateSharingHeapResource(ID3D12Resource* SrcResource, FIntPoint& Size, EPixelFormat Format, HANDLE& OutResourceHandle)
{
	auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());

	// Describe cross-adapter shared resources on primaryDevice adapter
	D3D12_RESOURCE_DESC crossAdapterDesc = SrcResource->GetDesc();
	crossAdapterDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_CROSS_ADAPTER | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
	crossAdapterDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	crossAdapterDesc.Width = Size.X;
	crossAdapterDesc.Height = Size.Y;

	CD3DX12_HEAP_PROPERTIES HeapProperty(D3D12_HEAP_TYPE_DEFAULT);


	CHECK_HR_DEFAULT(UE4D3DDevice->CreateCommittedResource(
		&HeapProperty,
		D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_SHARED_CROSS_ADAPTER,
		&crossAdapterDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(D3D12Resource.GetInitReference())));

	OutResourceHandle = nullptr;
#if TE_USE_NAMEDSHARE
	CHECK_HR_DEFAULT(UE4D3DDevice->CreateSharedHandle(D3D12Resource, *Security, GENERIC_ALL, *GetSharedHeapName(), &OutResourceHandle));
#else
	CHECK_HR_DEFAULT(UE4D3DDevice->CreateSharedHandle(D3D12Resource, *Security, GENERIC_ALL, nullptr, &OutResourceHandle));
#endif


	FD3D12DynamicRHI* DynamicRHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);
	Resource = DynamicRHI->RHICreateTexture2DFromResource(Format, TexCreate_None, FClearValueBinding::None, D3D12Resource).GetReference();

	return Resource.IsValid();
}

void FD3D12CrossGPUItem::Release()
{
	Resource.SafeRelease();
}

bool FD3D12CrossGPUItem::OpenSharingHeapResource(HANDLE ResourceHandle, EPixelFormat Format)
{
	auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());

	FD3D12DynamicRHI* DynamicRHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);

	// Open shared handle on secondaryDevice device
#if TE_USE_NAMEDSHARE
	HANDLE NamedResourceHandle;
	CHECK_HR_DEFAULT(UE4D3DDevice->OpenSharedHandleByName(*GetSharedHeapName(), GENERIC_ALL, &NamedResourceHandle));
	CHECK_HR_DEFAULT(UE4D3DDevice->OpenSharedHandle(NamedResourceHandle, IID_PPV_ARGS(D3D12Resource.GetInitReference())));
	CloseHandle(NamedResourceHandle);
#else
	CHECK_HR_DEFAULT(UE4D3DDevice->OpenSharedHandle(ResourceHandle, IID_PPV_ARGS(D3D12Resource.GetInitReference())));
	CloseHandle(ResourceHandle);
#endif

	Resource = DynamicRHI->RHICreateTexture2DFromResource(Format, TexCreate_None, FClearValueBinding::None, D3D12Resource).GetReference();

	return true;
}

#if TE_CUDA_TEXTURECOPY
#define CheckCudaErrors(val) check((val), #val, __FUNCTION__, __FILE__, __LINE__)

#include "cuda_runtime_api.h"


void FD3D12CrossGPUItem::InitCuda()
{
	int num_cuda_devices = 0;
	CheckCudaErrors(cudaGetDeviceCount(&num_cuda_devices));

	if (!num_cuda_devices)
		throw std::exception("No CUDA Devices found");

	for (UINT devId = 0; devId < num_cuda_devices; devId++)
	{
		cudaDeviceProp devProp{};
		CheckCudaErrors(cudaGetDeviceProperties(&devProp, devId));
		const auto cmp1 = memcmp(&m_dx12deviceluid.LowPart, devProp.luid, sizeof(m_dx12deviceluid.LowPart)) == 0;
		const auto cmp2 = memcmp(&m_dx12deviceluid.HighPart, devProp.luid + sizeof(m_dx12deviceluid.LowPart), sizeof(m_dx12deviceluid.HighPart)) == 0;
		if (cmp1 && cmp2)
		{
			CheckCudaErrors(cudaSetDevice(devId));
			m_cudaDeviceID = devId;
			m_nodeMask = devProp.luidDeviceNodeMask;
			CheckCudaErrors(cudaStreamCreate(&m_streamToRun));
			printf("CUDA Device Used [%d] %s\n", devId, devProp.name);
			break;
		}
	}
}

bool FD3D12CrossGPUItem::CreateCudaResource(ID3D12Resource* SrcResource, FIntPoint& Size, EPixelFormat Format, HANDLE& OutResourceHandle)
{
	//!
	InitCuda();

	auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());

	// Describe cross-adapter shared resources on primaryDevice adapter
	D3D12_RESOURCE_DESC texDesc = SrcResource->GetDesc();
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
	//cudaDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	texDesc.Width = Size.X;
	texDesc.Height = Size.Y;

	CD3DX12_HEAP_PROPERTIES HeapProperty(D3D12_HEAP_TYPE_DEFAULT);

	// Create texture for share
	CHECK_HR_DEFAULT(UE4D3DDevice->CreateCommittedResource(
		&HeapProperty,
		D3D12_HEAP_FLAG_SHARED,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(D3D12Resource.GetInitReference())));

	// Setup SRV
	FD3D12DynamicRHI* DynamicRHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);
	Resource = DynamicRHI->RHICreateTexture2DFromResource(Format, TexCreate_None, FClearValueBinding::None, D3D12Resource).GetReference();

	if (!Resource.IsValid())
	{
		D3D12Resource.SafeRelease();
		return false;
	}

	/*
	// importation of the D3D12 texture into a cuda surface
	{
		HANDLE sharedHandle{};
		FD3D12CrossGPUSecurity secAttr{};
		
		CHECK_HR_DEFAULT(UE4D3DDevice->CreateSharedHandle(D3D12Resource.GetReference(), &secAttr, GENERIC_ALL, 0, &sharedHandle));
		
		const auto texAllocInfo = UE4D3DDevice->GetResourceAllocationInfo(m_nodeMask, 1, &texDesc);

		cudaExternalMemoryHandleDesc cuExtmemHandleDesc{};
		cuExtmemHandleDesc.type = cudaExternalMemoryHandleTypeD3D12Heap;
		cuExtmemHandleDesc.handle.win32.handle = sharedHandle;
		cuExtmemHandleDesc.size = texAllocInfo.SizeInBytes;
		cuExtmemHandleDesc.flags = cudaExternalMemoryDedicated;
		CheckCudaErrors(cudaImportExternalMemory(&m_externalMemory, &cuExtmemHandleDesc));

		cudaExternalMemoryMipmappedArrayDesc cuExtmemMipDesc{};
		cuExtmemMipDesc.extent = make_cudaExtent(texDesc.Width, texDesc.Height, 0);
		cuExtmemMipDesc.formatDesc = cudaCreateChannelDesc<float4>();
		cuExtmemMipDesc.numLevels = 1;
		cuExtmemMipDesc.flags = cudaArraySurfaceLoadStore;

		cudaMipmappedArray_t cuMipArray{};
		CheckCudaErrors(cudaExternalMemoryGetMappedMipmappedArray(&cuMipArray, m_externalMemory, &cuExtmemMipDesc));

		cudaArray_t cuArray{};
		CheckCudaErrors(cudaGetMipmappedArrayLevel(&cuArray, cuMipArray, 0));

		cudaResourceDesc cuResDesc{};
		cuResDesc.resType = cudaResourceTypeArray;
		cuResDesc.res.array.array = cuArray;
		checkCudaErrors(cudaCreateSurfaceObject(&cuSurface, &cuResDesc));
	}
	*/
	return false;
}
#endif

#endif
