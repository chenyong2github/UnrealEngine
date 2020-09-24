// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareD3D11.h"
#include "TextureShareD3D11Log.h"

#include "D3D11RHIPrivate.h"

inline const FString GetComErrorDescription(HRESULT Res)
{
	const uint32 BufSize = 4096;
	WIDECHAR buffer[4096];
	if (::FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM,
		nullptr,
		Res,
		MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL),
		buffer,
		sizeof(buffer) / sizeof(*buffer),
		nullptr))
	{
		return buffer;
	}
	else
	{
		return TEXT("[cannot find error description]");
	}
}

// macro to deal with COM calls inside a function that returns `{}` on error
#define CHECK_HR_DEFAULT(COM_call)\
	{\
		HRESULT Res = COM_call;\
		if (FAILED(Res))\
		{\
			UE_LOG(LogTextureShareD3D11, Error, TEXT("`" #COM_call "` failed: 0x%X - %s"), Res, *GetComErrorDescription(Res)); \
			return {};\
		}\
	}

inline bool CreateSharedHandle(ID3D11Device* pD3D11Device, ID3D11Texture2D* pD3D11Texture2D, HANDLE& OutSharedHandle)
{
	TRefCountPtr<IDXGIResource> DXGIResource;
	CHECK_HR_DEFAULT(pD3D11Texture2D->QueryInterface(IID_PPV_ARGS(DXGIResource.GetInitReference())));

	//
	// NOTE : The HANDLE IDXGIResource::GetSharedHandle gives us is NOT an NT Handle, and therefre we should not call CloseHandle on it
	//
	HANDLE SharedHandle;
	CHECK_HR_DEFAULT(DXGIResource->GetSharedHandle(&SharedHandle));

	OutSharedHandle = SharedHandle;
	return true;
}

bool FTextureShareD3D11::CreateRHITexture(ID3D11Texture2D* OpenedSharedResource, EPixelFormat Format, FTexture2DRHIRef& DstTexture)
{
	FD3D11DynamicRHI* DynamicRHI = static_cast<FD3D11DynamicRHI*>(GDynamicRHI);

	// Create RHI texture from D3D11 resource
	ETextureCreateFlags TexCreateFlags = TexCreate_Shared;
	DstTexture = DynamicRHI->RHICreateTexture2DFromResource(Format, TexCreateFlags, FClearValueBinding::None, OpenedSharedResource).GetReference();
	return DstTexture.IsValid();
}

bool FTextureShareD3D11::CreateSharedTexture(FIntPoint& Size, EPixelFormat Format, FTexture2DRHIRef& OutRHITexture, void*& OutHandle)
{
	// Create RHI resource
	FRHIResourceCreateInfo CreateInfo;
	OutRHITexture = RHICreateTexture2D(Size.X, Size.Y, Format, 1, 1, TexCreate_Shared | TexCreate_ResolveTargetable, CreateInfo);
	if (OutRHITexture.IsValid() && OutRHITexture->IsValid())
	{
		auto UE4D3DDevice = static_cast<ID3D11Device*>(GDynamicRHI->RHIGetNativeDevice());
		ID3D11Texture2D* ResolvedTexture = (ID3D11Texture2D*)OutRHITexture->GetTexture2D()->GetNativeResource();

		if (CreateSharedHandle(UE4D3DDevice, ResolvedTexture, OutHandle))
		{
			return true;
		}

		// Handle not opened
		UE_LOG(LogTextureShareD3D11, Error, TEXT("Can't open shared texture handle"));
		OutRHITexture.SafeRelease();
		return false;
	}

	// Texture not created
	UE_LOG(LogTextureShareD3D11, Error, TEXT("Can't create shared texture"));
	return false;
}

IMPLEMENT_MODULE(FTextureShareD3D11, TextureShareD3D11);
