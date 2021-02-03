// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include <tchar.h>
#include <string>


#include <d3d12.h>
#include <D3dx12.h>

#include "TextureShareInterface.h"


class FTextureShareD3D12Helper
{
public:
	// Compare two textures size+format
	static bool IsTexturesEqual(ID3D12Resource* Texture1, ID3D12Resource* Texture2);

	// Create texture and create SRV inside SRVIndex
	static bool CreateSRVTexture(ID3D12Device* pD3D12Device, ID3D12DescriptorHeap* pD3D12HeapSRV, ID3D12Resource* InSharedTexture, ID3D12Resource** OutSRVTexture, int SRVIndex);

	// Copy image between two textures
	static void CopyResource(ID3D12GraphicsCommandList* pCmdList, ID3D12Resource* SourceTexture2D, ID3D12Resource* DestTexture2D);
};
