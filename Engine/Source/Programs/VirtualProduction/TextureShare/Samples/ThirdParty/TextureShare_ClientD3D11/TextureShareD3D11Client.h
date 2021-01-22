// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TextureShareD3D11Helper.h"

class FTextureShareD3D11Client
{
public:
	FTextureShareD3D11Client(ID3D11Device* D3DDevice);
	~FTextureShareD3D11Client();

	bool CreateShare(std::wstring ShareName);
	bool DeleteShare(std::wstring ShareName);

	bool BeginSession(std::wstring ShareName);
	bool EndSession(std::wstring ShareName);

	bool IsRemoteTextureUsed(std::wstring ShareName, std::wstring TextureName);
	bool RegisterTexture(std::wstring ShareName, std::wstring TextureName, ETextureShareSurfaceOp TextureOp, uint32 Width = 0, uint32 Height = 0, DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN);

	bool BeginFrame_RenderThread(std::wstring ShareName);
	bool EndFrame_RenderThread(std::wstring ShareName);

	bool ReadTextureFrame_RenderThread( std::wstring ShareName, std::wstring TextureName, ID3D11Texture2D** InOutTexture, ID3D11ShaderResourceView** InOutTextureSRV);
	bool WriteTextureFrame_RenderThread(std::wstring ShareName, std::wstring TextureName, ID3D11Texture2D* InTexture);

	bool ReadAdditionalData(std::wstring ShareName, FTextureShareSDKAdditionalData* OutFrameData);

private:
	ID3D11Device* pD3D11Device;
};

