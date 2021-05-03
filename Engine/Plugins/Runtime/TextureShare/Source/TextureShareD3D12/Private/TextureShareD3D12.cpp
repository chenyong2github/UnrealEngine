// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareD3D12.h"
#include "TextureShareD3D12Log.h"

#include "D3D12RHIPrivate.h"
#include "D3D12Util.h"

#if TEXTURESHARE_CROSSGPUHEAP
#include "CrossGPUHeap/D3D12CrossGPUHeap.h"
#endif

// macro to deal with COM calls inside a function that returns `{}` on error
#define CHECK_HR_DEFAULT(COM_call)\
	{\
		HRESULT Res = COM_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(LogTextureShareD3D12, Error, TEXT("`" #COM_call "` failed: 0x%X - %s"), Res, *GetComErrorDescription(Res)); \
			return {};\
		}\
	}

inline bool CreateSharedHandle(ID3D12Device* pD3D12Device, ID3D12Resource* pD3D12Resource, const FString& UniqueHandleId, const SECURITY_ATTRIBUTES* pAttributes, HANDLE& OutSharedHandle)
{
	HANDLE SharedHandle;
	CHECK_HR_DEFAULT(pD3D12Device->CreateSharedHandle(pD3D12Resource, pAttributes, GENERIC_ALL, *UniqueHandleId, &SharedHandle));
	OutSharedHandle = SharedHandle;
	return true;
}

bool FTextureShareD3D12::CreateRHITexture(ID3D12Resource* OpenedSharedResource, EPixelFormat Format, FTexture2DRHIRef& DstTexture)
{
	FD3D12DynamicRHI* DynamicRHI = static_cast<FD3D12DynamicRHI*>(GDynamicRHI);

	ETextureCreateFlags TexCreateFlags = TexCreate_Shared;
	DstTexture = DynamicRHI->RHICreateTexture2DFromResource(Format, TexCreateFlags, FClearValueBinding::None, (ID3D12Resource*)OpenedSharedResource).GetReference();
	return DstTexture.IsValid();
}

bool FTextureShareD3D12::CreateSharedTexture(FIntPoint& Size, EPixelFormat Format, FTexture2DRHIRef& OutRHITexture, void*& OutHandle, FGuid& OutSharedHandleGuid)
{
	// Create RHI resource
	FRHIResourceCreateInfo CreateInfo;
	OutRHITexture = RHICreateTexture2D(Size.X, Size.Y, Format, 1, 1, TexCreate_Shared | TexCreate_ResolveTargetable, CreateInfo);
	if (OutRHITexture.IsValid() && OutRHITexture->IsValid())
	{
		auto UE4D3DDevice = static_cast<ID3D12Device*>(GDynamicRHI->RHIGetNativeDevice());
		ID3D12Resource* ResolvedTexture = (ID3D12Resource*)OutRHITexture->GetTexture2D()->GetNativeResource();

		// Create unique handle name
		OutSharedHandleGuid = FGuid::NewGuid();
		FString UniqueHandleId = FString::Printf(TEXT("Global\\%s"), *OutSharedHandleGuid.ToString(EGuidFormats::DigitsWithHyphensInBraces));

		if (CreateSharedHandle(UE4D3DDevice, ResolvedTexture, UniqueHandleId, *SharedResourceSecurityAttributes, OutHandle))
		{
			return true;
		}

		// Handle not opened
		UE_LOG(LogTextureShareD3D12, Error, TEXT("Can't open shared texture handle '%s'"), *UniqueHandleId);
		OutRHITexture.SafeRelease();
		return false;
	}

	// Texture not created
	UE_LOG(LogTextureShareD3D12, Error, TEXT("Can't create shared texture"));
	return false;
}

/*
 * DX12 Cross GPU heap resource
 */
void FTextureShareD3D12::StartupModule()
{
#if TEXTURESHARE_CROSSGPUHEAP
	CrossGPUHeap = MakeShareable(new FD3D12CrossGPUHeap());
	CrossGPUHeap->StartupCrossGPU();
#endif
}

void FTextureShareD3D12::ShutdownModule()
{
#if TEXTURESHARE_CROSSGPUHEAP
	CrossGPUHeap->ShutdownCrossGPU();
	CrossGPUHeap.SafeRelease();
#endif
}

bool FTextureShareD3D12::GetCrossGPUHeap(TSharedPtr<ID3D12CrossGPUHeap>& OutCrossGPUHeap)
{
	if (CrossGPUHeap.IsValid())
	{
		OutCrossGPUHeap = CrossGPUHeap;
		return true;
	}

	UE_LOG(LogTextureShareD3D12, Error, TEXT("CrossGPUHeap API disabled now"));
	return false;
}

IMPLEMENT_MODULE(FTextureShareD3D12, TextureShareD3D12);
