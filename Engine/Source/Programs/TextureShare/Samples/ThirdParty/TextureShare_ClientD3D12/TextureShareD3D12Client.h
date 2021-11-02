// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "TextureShareD3D12Helper.h"

class FTextureShareD3D12Client
{
public:
	FTextureShareD3D12Client(const std::wstring& InShareName, ID3D12Device* pD3D12Device, ID3D12GraphicsCommandList* pCmdList, ID3D12DescriptorHeap* pD3D12HeapSRV);
	~FTextureShareD3D12Client();

	bool CreateShare();
	bool DeleteShare();

	bool BeginSession();
	bool EndSession();

	bool IsRemoteTextureUsed(std::wstring TextureName);
	bool RegisterTexture(std::wstring TextureName, ETextureShareSurfaceOp TextureOp, uint32 Width = 0, uint32 Height = 0, DXGI_FORMAT Format = DXGI_FORMAT_UNKNOWN);

	bool BeginFrame_RenderThread();
	bool EndFrame_RenderThread();

	bool ReadTextureFrame_RenderThread(std::wstring TextureName, ID3D12Resource** InOutSRVTexture, int SRVIndex);
	bool WriteTextureFrame_RenderThread(std::wstring TextureName, ID3D12Resource* InTexture);

	bool ReadAdditionalData(FTextureShareSDKAdditionalData* OutFrameData);

private:
	std::wstring ShareName;
	ID3D12Device* pD3D12Device;
	ID3D12GraphicsCommandList* pCmdList;
	ID3D12DescriptorHeap* pD3D12HeapSRV;
};

