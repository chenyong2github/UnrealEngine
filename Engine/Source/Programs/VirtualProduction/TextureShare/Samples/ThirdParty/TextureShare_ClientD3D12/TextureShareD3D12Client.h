// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TextureShareD3D12Helper.h"

class FTextureShareD3D12Client
{
public:
	FTextureShareD3D12Client(ID3D12Device* pD3D12Device, ID3D12GraphicsCommandList* pCmdList, ID3D12DescriptorHeap* pD3D12HeapSRV);
	~FTextureShareD3D12Client();

	bool CreateShare(std::wstring ShareName);
	bool DeleteShare(std::wstring ShareName);

	bool BeginSession(std::wstring ShareName);
	bool EndSession(std::wstring ShareName);

	bool IsRemoteTextureUsed(std::wstring ShareName, std::wstring TextureName);
	bool RegisterTexture(std::wstring ShareName, std::wstring TextureName, ETextureShareSurfaceOp TextureOp, uint32 Width = 0, uint32 Height = 0, DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN);

	bool BeginFrame_RenderThread(std::wstring ShareName);
	bool EndFrame_RenderThread(std::wstring ShareName);

	bool ReadTextureFrame_RenderThread( std::wstring ShareName, std::wstring TextureName, ID3D12Resource** InOutSRVTexture, int SRVIndex);
	bool WriteTextureFrame_RenderThread(std::wstring ShareName, std::wstring TextureName, ID3D12Resource* InTexture);

	bool ReadAdditionalData(std::wstring ShareName, FTextureShareSDKAdditionalData* OutFrameData);

private:
	ID3D12Device* pD3D12Device;
	ID3D12GraphicsCommandList* pCmdList;
	ID3D12DescriptorHeap* pD3D12HeapSRV;
};

