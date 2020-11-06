// Copyright Epic Games, Inc. All Rights Reserved.

#include "TextureShareD3D11Client.h"
#include "TextureShareDLL.h"

#ifdef _DEBUG
#pragma comment( lib, "TextureShareSDK-Win64-Debug.lib" )
#else
#pragma comment( lib, "TextureShareSDK.lib" )
#endif

FTextureShareD3D11Client::FTextureShareD3D11Client(ID3D11Device* InD3DDevice)
	: pD3D11Device(InD3DDevice)
{
}

FTextureShareD3D11Client::~FTextureShareD3D11Client()
{
}

bool FTextureShareD3D11Client::CreateShare(std::wstring ShareName)
{
	FTextureShareSyncPolicy DefaultSyncPolicy;
	DefaultSyncPolicy.ConnectionSync = ETextureShareSyncConnect::None;
	DefaultSyncPolicy.FrameSync = ETextureShareSyncFrame::None;
	DefaultSyncPolicy.TextureSync = ETextureShareSyncSurface::None;
	return FTextureShareInterface::CreateTextureShare(ShareName.c_str(), ETextureShareProcess::Client, DefaultSyncPolicy, ETextureShareDevice::D3D11);
}

bool FTextureShareD3D11Client::DeleteShare(std::wstring ShareName)
{
	return FTextureShareInterface::ReleaseTextureShare(ShareName.c_str());
}

bool FTextureShareD3D11Client::BeginSession(std::wstring ShareName)
{
	return FTextureShareInterface::BeginSession(ShareName.c_str());
}

bool FTextureShareD3D11Client::EndSession(std::wstring ShareName)
{
	return FTextureShareInterface::EndSession(ShareName.c_str());
}

bool FTextureShareD3D11Client::BeginFrame_RenderThread(std::wstring ShareName)
{
	return FTextureShareInterface::BeginFrame_RenderThread(ShareName.c_str());
}

bool FTextureShareD3D11Client::EndFrame_RenderThread(std::wstring ShareName)
{
	return FTextureShareInterface::EndFrame_RenderThread(ShareName.c_str());
}

bool FTextureShareD3D11Client::IsRemoteTextureUsed(std::wstring ShareName, std::wstring TextureName)
{
	return FTextureShareInterface::IsRemoteTextureUsed(ShareName.c_str(), TextureName.c_str());
}

bool FTextureShareD3D11Client::RegisterTexture(std::wstring ShareName, std::wstring TextureName, ETextureShareSurfaceOp TextureOp, uint32 Width, uint32 Height, DXGI_FORMAT InFormat)
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

void ReleaseTextureAndSRV(ID3D11Texture2D** OutTexture, ID3D11ShaderResourceView** TextureSRV)
{
	if (*TextureSRV)
	{
		(*TextureSRV)->Release();
		*TextureSRV = nullptr;
	}
	if (*OutTexture)
	{
		(*OutTexture)->Release();
		*OutTexture = nullptr;
	}
}

bool FTextureShareD3D11Client::ReadAdditionalData(std::wstring ShareName, FTextureShareSDKAdditionalData* OutFrameData)
{
	return FTextureShareInterface::GetRemoteAdditionalData(ShareName.c_str(), *OutFrameData);
}

bool FTextureShareD3D11Client::ReadTextureFrame_RenderThread(std::wstring ShareName, std::wstring TextureName, ID3D11Texture2D** InOutTexture, ID3D11ShaderResourceView** InOutTextureSRV)
{
	bool bResult = false;

	if (FTextureShareInterface::IsValid(ShareName.c_str()))
	{
		ID3D11Texture2D* SharedResource;
		if (FTextureShareInterface::LockTextureD3D11_RenderThread(pD3D11Device, ShareName.c_str(), TextureName.c_str(), SharedResource))
		{
			
			if (!FTextureShareD3D11Helper::IsTexturesEqual(SharedResource, *InOutTexture))
			{
				// Shared texture size changed on server side,
				// Remove temp texture, and re-create new tempTexture
				ReleaseTextureAndSRV(InOutTexture, InOutTextureSRV);
			}

			if (!*InOutTexture)
			{
				// Create new temp texture&srv
				if (!FTextureShareD3D11Helper::CreateSRVTexture(pD3D11Device, SharedResource, InOutTexture, InOutTextureSRV))
				{
					ReleaseTextureAndSRV(InOutTexture, InOutTextureSRV);
				}
			}

			// Copy from shared to temp:
			if (*InOutTexture)
			{
				bResult = true;
				FTextureShareD3D11Helper::CopyResource(pD3D11Device, SharedResource, *InOutTexture);
			}

			// Unlock shared resource
			FTextureShareInterface::UnlockTexture_RenderThread(ShareName.c_str(), TextureName.c_str());
		}
		else
		{
			// Release unused texture (disconnect purpose)
			ReleaseTextureAndSRV(InOutTexture, InOutTextureSRV);
		}
	}
	return bResult;
}

bool FTextureShareD3D11Client::WriteTextureFrame_RenderThread(std::wstring ShareName, std::wstring TextureName, ID3D11Texture2D* InTexture)
{
	if (FTextureShareInterface::IsValid(ShareName.c_str()))
	{
		ID3D11Texture2D* SharedResource;
		if (FTextureShareInterface::LockTextureD3D11_RenderThread(pD3D11Device, ShareName.c_str(), TextureName.c_str(), SharedResource))
		{
			// Copy backbuffer to shared texture
			FTextureShareD3D11Helper::CopyResource(pD3D11Device, InTexture, SharedResource);
			FTextureShareInterface::UnlockTexture_RenderThread(ShareName.c_str(), TextureName.c_str());
			return true;
		}
	}
	return false;
}

