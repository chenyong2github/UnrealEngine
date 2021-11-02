// Copyright Epic Games, Inc. All Rights Reserved.

#include "stdafx.h"

#include "TextureShareD3D12Client.h"
#include "TextureShareDLL.h"

#ifdef _DEBUG
#pragma comment( lib, "TextureShareSDK-Win64-Debug.lib" )
#else
#pragma comment( lib, "TextureShareSDK.lib" )
#endif

FTextureShareD3D12Client::FTextureShareD3D12Client(const std::wstring& InShareName, ID3D12Device* InD3D12Device, ID3D12GraphicsCommandList* InCmdList, ID3D12DescriptorHeap* InD3D12HeapSRV)
	: ShareName(InShareName)
	, pD3D12Device(InD3D12Device)
	, pCmdList(InCmdList)
	, pD3D12HeapSRV(InD3D12HeapSRV)
{
}

FTextureShareD3D12Client::~FTextureShareD3D12Client()
{
}

bool FTextureShareD3D12Client::CreateShare()
{
	FTextureShareSyncPolicy DefaultSyncPolicy;
	DefaultSyncPolicy.ConnectionSync = ETextureShareSyncConnect::None;
	DefaultSyncPolicy.FrameSync = ETextureShareSyncFrame::None;
	DefaultSyncPolicy.TextureSync = ETextureShareSyncSurface::None;

	return FTextureShareInterface::CreateTextureShare(ShareName.c_str(), ETextureShareProcess::Client, DefaultSyncPolicy, ETextureShareDevice::D3D12);
}

bool FTextureShareD3D12Client::DeleteShare()
{
	return FTextureShareInterface::ReleaseTextureShare(ShareName.c_str());
}

bool FTextureShareD3D12Client::BeginSession()
{
	return FTextureShareInterface::BeginSession(ShareName.c_str());
}

bool FTextureShareD3D12Client::EndSession()
{
	return FTextureShareInterface::EndSession(ShareName.c_str());
}

bool FTextureShareD3D12Client::BeginFrame_RenderThread()
{
	return FTextureShareInterface::BeginFrame_RenderThread(ShareName.c_str());
}

bool FTextureShareD3D12Client::EndFrame_RenderThread()
{
	return FTextureShareInterface::EndFrame_RenderThread(ShareName.c_str());
}

bool FTextureShareD3D12Client::IsRemoteTextureUsed(std::wstring TextureName)
{
	return FTextureShareInterface::IsRemoteTextureUsed(ShareName.c_str(), TextureName.c_str());
}

bool FTextureShareD3D12Client::RegisterTexture(std::wstring TextureName, ETextureShareSurfaceOp TextureOp, uint32 Width, uint32 Height, DXGI_FORMAT InFormat)
{
	ETextureShareFormat ShareFormat = ETextureShareFormat::Undefined;
	uint32 ShareFormatValue = 0;

	// Use client texture format:
	if (InFormat != DXGI_FORMAT_UNKNOWN)
	{
		ShareFormat = ETextureShareFormat::Format_DXGI;
		ShareFormatValue = InFormat;
	}

	return FTextureShareInterface::RegisterTexture(ShareName.c_str(), TextureName.c_str(), Width, Height, ShareFormat, ShareFormatValue, TextureOp);
}

void ReleaseTextureAndSRV(ID3D12Resource** InOutSRVTexture)
{
	if (*InOutSRVTexture)
	{
		(*InOutSRVTexture)->Release();
		*InOutSRVTexture = nullptr;
	}
}

bool FTextureShareD3D12Client::ReadAdditionalData(FTextureShareSDKAdditionalData* OutFrameData)
{
	return FTextureShareInterface::GetRemoteAdditionalData(ShareName.c_str(), *OutFrameData);
}

bool FTextureShareD3D12Client::ReadTextureFrame_RenderThread(std::wstring TextureName, ID3D12Resource** InOutSRVTexture, int SRVIndex)
{
	bool bResult = false;

	if (FTextureShareInterface::IsValid(ShareName.c_str()))
	{
		ID3D12Resource* SharedResource;
		if (FTextureShareInterface::LockTextureD3D12_RenderThread(pD3D12Device, ShareName.c_str(), TextureName.c_str(), SharedResource))
		{
			
			if (!FTextureShareD3D12Helper::IsTexturesEqual(SharedResource, *InOutSRVTexture))
			{
				// Shared texture size changed on server side. Remove temp texture, and re-create new tempTexture
				ReleaseTextureAndSRV(InOutSRVTexture);
			}

			if (!*InOutSRVTexture)
			{
				// Create Temp texture&srv
				FTextureShareD3D12Helper::CreateSRVTexture(pD3D12Device, pD3D12HeapSRV, SharedResource, InOutSRVTexture, SRVIndex);
			}

			// Copy from shared to temp:
			if (*InOutSRVTexture)
			{
				bResult = true;
				FTextureShareD3D12Helper::CopyResource(pCmdList, SharedResource, *InOutSRVTexture);
			}

			// Unlock shared resource
			FTextureShareInterface::UnlockTexture_RenderThread(ShareName.c_str(), TextureName.c_str());
		}
		else
		{
			// Release unused texture (disconnect purpose)
			ReleaseTextureAndSRV(InOutSRVTexture);
		}
	}
	return bResult;
}

bool FTextureShareD3D12Client::WriteTextureFrame_RenderThread(std::wstring TextureName, ID3D12Resource* InTexture)
{
	if (FTextureShareInterface::IsValid(ShareName.c_str()))
	{
		ID3D12Resource* SharedResource;
		if (FTextureShareInterface::LockTextureD3D12_RenderThread(pD3D12Device, ShareName.c_str(), TextureName.c_str(), SharedResource))
		{
			FTextureShareD3D12Helper::CopyResource(pCmdList, InTexture, SharedResource);
			FTextureShareInterface::UnlockTexture_RenderThread(ShareName.c_str(), TextureName.c_str());
			return true;
		}
	}
	return false;
}

